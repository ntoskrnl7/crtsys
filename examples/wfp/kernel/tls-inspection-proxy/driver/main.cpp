#include <ntddk.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <ntl/device_endpoint>
#include <ntl/driver>
#include <ntl/except>
#include <ntl/ioctl>
#include <ntl/net/http/transform>
#include <ntl/net/inspection/content_decoder>
#include <ntl/net/inspection/content_encoder>
#include <ntl/net/kernel/schannel>
#include <ntl/net/kernel/redirected_tls_inspection>
#include <ntl/net/kernel/redirected_tls_session>
#include <ntl/net/kernel/waitable_task>
#include <ntl/net/kernel/wsk_redirect>
#include <ntl/net/kernel/wsk_transport>
#include <ntl/system_thread>
#include <ntl/wfp/all>

#include "inspection_policy.hpp"
#include "tls_inspection_proxy_contract.hpp"

namespace {

namespace contract = wfp_kernel_tls_inspection_proxy;
using ntl::net::kernel::waitable_status_task;
using configure_certificate =
    ntl::ioctl_from_contract<contract::configure_certificate_contract>;
using query_proxy = ntl::ioctl_from_contract<contract::query_proxy_contract>;
using query_last_inspection =
    ntl::ioctl_from_contract<contract::query_last_inspection_contract>;
using read_inspection =
    ntl::ioctl_from_contract<contract::read_inspection_contract>;
using read_identity_request =
    ntl::ioctl_from_contract<contract::read_identity_request_contract>;

class fast_mutex_guard {
public:
  explicit fast_mutex_guard(FAST_MUTEX &mutex) noexcept : mutex_(&mutex) {
    NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    KeEnterCriticalRegion();
    ExAcquireFastMutexUnsafe(mutex_);
  }
  fast_mutex_guard(const fast_mutex_guard &) = delete;
  fast_mutex_guard &operator=(const fast_mutex_guard &) = delete;
  ~fast_mutex_guard() {
    ExReleaseFastMutexUnsafe(mutex_);
    KeLeaveCriticalRegion();
  }

private:
  FAST_MUTEX *mutex_;
};

class tls_proxy_state
    : public std::enable_shared_from_this<tls_proxy_state> {
public:
  tls_proxy_state() noexcept {
    ExInitializeFastMutex(&identity_lock_);
    ExInitializeFastMutex(&inspection_lock_);
    ExInitializeFastMutex(&identity_request_lock_);
    ExInitializeFastMutex(&session_lock_);
  }

  ~tls_proxy_state() { shutdown(); }

  ntl::status start() noexcept {
    process_id_ = HandleToULong(PsGetCurrentProcessId());
    if (process_id_ == 0)
      return STATUS_INVALID_CID;
    auto client = schannel_.try_client();
    if (!client)
      return client.status();
    try {
      client_credentials_ =
          std::make_shared<ntl::net::kernel::schannel_credentials>(
              std::move(*client));
      origin_security_provider_ =
          ntl::net::kernel::make_fixed_redirected_tls_origin_security_provider(
              client_credentials_);
      identities_.reserve(contract::identity_cache_capacity);
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
    try {
      provider_ = std::make_shared<ntl::net::kernel::wsk_provider>();
      runtime_observer_ = std::make_shared<runtime_observer>(weak_from_this());
      identity_provider_ = std::make_shared<identity_provider>(weak_from_this());
      auto policy = std::make_shared<ntl::net::http::inspection_policy>(
          crtsys::wfp_kernel_tls::make_inspection_policy());
      auto decoders = std::make_shared<
          ntl::net::inspection::content_decoder_registry>();
      auto encoders = std::make_shared<
          ntl::net::inspection::content_encoder_registry>();
      policy->use_content_codecs(decoders, encoders);
      ntl::net::kernel::redirected_tls_inspection_options options;
      options.resources.http1.maximum_wire_message_size =
          contract::maximum_http_message_size;
      options.resources.http1.framing.maximum_body_size =
          contract::maximum_http_body_size;
      options.resources.http2.maximum_retained_request_bytes =
          contract::maximum_http_body_size;
      options.resources.maximum_concurrent_http1_sessions = 2;
      options.resources.maximum_concurrent_http2_sessions = 2;
      options.make_observer =
          [observer = runtime_observer_](const auto &) {
            return observer;
          };
      auto inspection =
          ntl::net::kernel::standard_redirected_tls_inspection::create(
              std::move(policy), options);
      if (!inspection)
        return inspection.status();
      inspection_ = std::move(*inspection);
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
    const ntl::status opened = provider_->open({5'000});
    if (!opened.is_ok()) {
      inspection_.reset();
      identity_provider_.reset();
      runtime_observer_.reset();
      provider_.reset();
      origin_security_provider_.reset();
      client_credentials_.reset();
      return opened;
    }
    auto v4 = ntl::net::kernel::wsk_tcp_listener::try_listen(
        *provider_, ntl::net::kernel::ip_endpoint::any_ipv4(), limits());
    if (!v4) {
      provider_->close();
      provider_.reset();
      origin_security_provider_.reset();
      client_credentials_.reset();
      return v4.status();
    }
    listener_v4_ = std::move(*v4);
    auto v6 = ntl::net::kernel::wsk_tcp_listener::try_listen(
        *provider_, ntl::net::kernel::ip_endpoint::any_ipv6(), limits());
    if (!v6) {
      listener_v4_.reset();
      provider_->close();
      provider_.reset();
      origin_security_provider_.reset();
      client_credentials_.reset();
      return v6.status();
    }
    listener_v6_ = std::move(*v6);
    worker_v4_ = {this, listener_v4_.get()};
    worker_v6_ = {this, listener_v6_.get()};
    auto v4_thread = ntl::system_thread::create(&worker_entry, &worker_v4_);
    if (!v4_thread) {
      shutdown();
      return v4_thread.status();
    }
    thread_v4_ = std::move(*v4_thread);
    auto v6_thread = ntl::system_thread::create(&worker_entry, &worker_v6_);
    if (!v6_thread) {
      shutdown();
      return v6_thread.status();
    }
    thread_v6_ = std::move(*v6_thread);
    return ntl::status::ok();
  }

  ntl::status
  configure(const contract::certificate_config &configuration) noexcept {
    if (configuration.server_name_size == 0 ||
        configuration.server_name_size > contract::maximum_server_name_size ||
        configuration.server_name[configuration.server_name_size] != '\0')
      return STATUS_INVALID_PARAMETER;
    std::string server_name;
    try {
      server_name.assign(configuration.server_name.data(),
                         configuration.server_name_size);
      for (char &value : server_name) {
        if (value >= 'A' && value <= 'Z')
          value = static_cast<char>(value - 'A' + 'a');
        const bool valid = (value >= 'a' && value <= 'z') ||
                           (value >= '0' && value <= '9') || value == '-' ||
                           value == '_' || value == '.';
        if (!valid)
          return STATUS_INVALID_PARAMETER;
      }
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
    auto reference = ntl::net::kernel::schannel_certificate_store_ref::make(
        configuration.sha1_thumbprint, L"MY");
    if (!reference)
      return reference.status();
    auto acquired = schannel_.try_server(*reference);
    if (!acquired)
      return acquired.status();
    try {
      auto shared = std::make_shared<ntl::net::kernel::schannel_credentials>(
          std::move(*acquired));
      std::shared_ptr<ntl::net::kernel::schannel_credentials> retired;
      bool replaced = false;
      {
        fast_mutex_guard guard(identity_lock_);
        for (auto &identity : identities_) {
          if (identity.server_name == server_name) {
            retired = std::move(identity.credentials);
            identity.credentials = std::move(shared);
            replaced = true;
            break;
          }
        }
        if (!replaced) {
          if (identities_.size() == contract::identity_cache_capacity)
            return STATUS_QUOTA_EXCEEDED;
          identities_.push_back({std::move(server_name), std::move(shared)});
        }
      }
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
    return ntl::status::ok();
  }

  contract::proxy_info snapshot() noexcept {
    std::uint32_t identity_count = 0;
    {
      fast_mutex_guard guard(identity_lock_);
      identity_count = static_cast<std::uint32_t>(identities_.size());
    }
    return {
        process_id_,
        listener_v4_ ? listener_v4_->local_endpoint().port() : std::uint16_t{0},
        listener_v6_ ? listener_v6_->local_endpoint().port() : std::uint16_t{0},
        identity_count != 0 ? 1u : 0u,
        accepted_.load(std::memory_order_relaxed),
        handshaken_.load(std::memory_order_relaxed),
        permitted_.load(std::memory_order_relaxed),
        blocked_.load(std::memory_order_relaxed),
        failed_.load(std::memory_order_relaxed),
        origin_connected_.load(std::memory_order_relaxed),
        origin_completed_.load(std::memory_order_relaxed),
        identity_requests_.load(std::memory_order_relaxed),
        identity_timeouts_.load(std::memory_order_relaxed),
        capture_overwritten_.load(std::memory_order_relaxed),
        identity_count,
        contract::identity_cache_capacity,
    };
  }

  void last_inspection(contract::inspection_record &result) noexcept {
    fast_mutex_guard guard(inspection_lock_);
    std::memset(&result, 0, sizeof(result));
    if (inspection_sequence_ != 0)
      result = inspections_[(inspection_sequence_ - 1) % inspections_.size()];
  }

  void
  read_inspection_after(std::uint64_t after,
                        contract::inspection_read_result &result) noexcept {
    fast_mutex_guard guard(inspection_lock_);
    std::memset(&result, 0, sizeof(result));
    result.current_sequence = inspection_sequence_;
    if (inspection_sequence_ == 0)
      return;
    result.oldest_sequence =
        inspection_sequence_ > inspections_.size()
            ? inspection_sequence_ - inspections_.size() + 1
            : 1;
    std::uint64_t next = after == (std::numeric_limits<std::uint64_t>::max)()
                             ? after
                             : after + 1;
    if (next < result.oldest_sequence) {
      const std::uint64_t lost = result.oldest_sequence - next;
      result.dropped = static_cast<std::uint32_t>(
          (std::min)(lost, static_cast<std::uint64_t>(
                               (std::numeric_limits<std::uint32_t>::max)())));
      next = result.oldest_sequence;
    }
    if (next <= inspection_sequence_) {
      result.available = 1;
      result.record = inspections_[(next - 1) % inspections_.size()];
    }
  }

  void read_identity_request_after(
      std::uint64_t after,
      contract::identity_request_read_result &result) noexcept {
    fast_mutex_guard guard(identity_request_lock_);
    std::memset(&result, 0, sizeof(result));
    result.current_sequence = identity_request_sequence_;
    if (identity_request_sequence_ == 0)
      return;
    result.oldest_sequence =
        identity_request_sequence_ > identity_request_queue_.size()
            ? identity_request_sequence_ - identity_request_queue_.size() + 1
            : 1;
    std::uint64_t next = after == (std::numeric_limits<std::uint64_t>::max)()
                             ? after
                             : after + 1;
    if (next < result.oldest_sequence) {
      const std::uint64_t lost = result.oldest_sequence - next;
      result.dropped = static_cast<std::uint32_t>(
          (std::min)(lost, static_cast<std::uint64_t>(
                               (std::numeric_limits<std::uint32_t>::max)())));
      next = result.oldest_sequence;
    }
    if (next <= identity_request_sequence_) {
      result.available = 1;
      result.request =
          identity_request_queue_[(next - 1) % identity_request_queue_.size()];
    }
  }

  void shutdown() noexcept {
    if (stopping_.exchange(true, std::memory_order_acq_rel))
      return;
    if (listener_v4_)
      listener_v4_->stop();
    if (listener_v6_)
      listener_v6_->stop();
    if (thread_v4_)
      (void)thread_v4_.join();
    if (thread_v6_)
      (void)thread_v6_.join();
    listener_v4_.reset();
    listener_v6_.reset();
    if (inspection_)
      inspection_->close();
    std::vector<identity_entry> identities;
    {
      fast_mutex_guard guard(identity_lock_);
      identities.swap(identities_);
    }
    identities.clear();
    origin_security_provider_.reset();
    client_credentials_.reset();
    const ntl::status credentials_drained = schannel_.close();
    NT_ASSERT(credentials_drained.is_ok());
    if (provider_)
      provider_->close();
    inspection_.reset();
    identity_provider_.reset();
    runtime_observer_.reset();
    provider_.reset();
  }

private:
  struct session_record {
    ntl::net::kernel::ip_endpoint original_destination;
    std::string server_name;
    contract::inspected_protocol protocol =
        contract::inspected_protocol::none;
    std::vector<std::byte> request;
    std::vector<std::byte> response;
  };

  class identity_provider final
      : public ntl::net::kernel::redirected_tls_server_identity_provider {
  public:
    explicit identity_provider(std::weak_ptr<tls_proxy_state> owner) noexcept
        : owner_(std::move(owner)) {}

    ntl::result<std::shared_ptr<ntl::net::kernel::schannel_credentials>>
    select(const ntl::net::kernel::redirected_tls_identity_request_view
               &request) noexcept override {
      auto owner = owner_.lock();
      if (!owner)
        return ntl::unexpected(STATUS_DELETE_PENDING);
      auto selected = owner->wait_for_identity(request.server_name,
                                               request.session_id);
      return selected ? ntl::ok(std::move(selected))
                      : ntl::result<std::shared_ptr<
                            ntl::net::kernel::schannel_credentials>>(
                            ntl::unexpected(STATUS_NOT_FOUND));
    }

  private:
    std::weak_ptr<tls_proxy_state> owner_;
  };

  class runtime_observer final
      : public ntl::net::kernel::redirected_tls_session_observer,
        public ntl::net::kernel::redirected_http_inspection_observer {
  public:
    explicit runtime_observer(std::weak_ptr<tls_proxy_state> owner) noexcept
        : owner_(std::move(owner)) {}

    void on_downstream_handshake(
        std::uint64_t session_id,
        ntl::net::kernel::inspected_http_protocol protocol,
        std::string_view server_name) noexcept override {
      if (auto owner = owner_.lock())
        owner->record_handshake(session_id, protocol, server_name);
    }

    void on_origin_handshake(
        std::uint64_t,
        ntl::net::kernel::inspected_http_protocol) noexcept override {
      if (auto owner = owner_.lock())
        owner->origin_connected_.fetch_add(1, std::memory_order_relaxed);
    }

    void on_session_complete(std::uint64_t session_id,
                             ntl::status status) noexcept override {
      if (!status.is_ok()) {
        if (auto owner = owner_.lock()) {
          owner->failed_.fetch_add(1, std::memory_order_relaxed);
          owner->finish_failed_session(session_id, status);
        }
      }
    }

    void on_inspection(
        const ntl::net::http::inspection_context_view &context)
        noexcept override {
      if (auto owner = owner_.lock())
        owner->record_http(context);
    }

    void on_session_complete(
        ntl::net::kernel::inspected_http_protocol protocol,
        const ntl::net::http::inspection_session_metadata &metadata,
        const ntl::net::kernel::inspected_http_session_summary &summary)
        noexcept override {
      if (auto owner = owner_.lock())
        owner->finish_http_session(protocol, metadata, summary);
    }

  private:
    std::weak_ptr<tls_proxy_state> owner_;
  };

  static waitable_status_task wait_for_session(
      std::shared_ptr<ntl::net::kernel::redirected_tls_session> session) {
    if (!session)
      co_return ntl::status{STATUS_INVALID_PARAMETER};
    co_return co_await session->run();
  }

  bool remember_session(
      std::uint64_t session_id,
      const ntl::net::kernel::ip_endpoint &destination) noexcept {
    try {
      fast_mutex_guard guard(session_lock_);
      return sessions_.emplace(
          session_id, session_record{.original_destination = destination})
          .second;
    } catch (...) {
      return false;
    }
  }

  void record_handshake(
      std::uint64_t session_id,
      ntl::net::kernel::inspected_http_protocol protocol,
      std::string_view server_name) noexcept {
    handshaken_.fetch_add(1, std::memory_order_relaxed);
    try {
      fast_mutex_guard guard(session_lock_);
      const auto found = sessions_.find(session_id);
      if (found == sessions_.end())
        return;
      found->second.server_name.assign(server_name);
      found->second.protocol =
          protocol == ntl::net::kernel::inspected_http_protocol::http2
              ? contract::inspected_protocol::http2
              : contract::inspected_protocol::http1;
    } catch (...) {
    }
  }

  void record_http(
      const ntl::net::http::inspection_context_view &context) noexcept {
    if (context.stage() !=
        ntl::net::http::inspection_stage::message_complete)
      return;
    const auto id = context.connection().connection_id;
    if (!id)
      return;
    try {
      fast_mutex_guard guard(session_lock_);
      const auto found = sessions_.find(*id);
      if (found == sessions_.end())
        return;
      const auto copy_bounded = [](std::span<const std::byte> source,
                                   std::vector<std::byte> &destination) {
        const std::size_t size =
            (std::min)(source.size(), contract::maximum_capture_size);
        destination.assign(source.begin(), source.begin() + size);
      };
      copy_bounded(context.request().body, found->second.request);
      if (context.response())
        copy_bounded(context.response()->body, found->second.response);
    } catch (...) {
    }
  }

  void finish_http_session(
      ntl::net::kernel::inspected_http_protocol,
      const ntl::net::http::inspection_session_metadata &metadata,
      const ntl::net::kernel::inspected_http_session_summary &summary)
      noexcept {
    if (!metadata.connection.connection_id)
      return;
    session_record record;
    {
      fast_mutex_guard guard(session_lock_);
      const auto found = sessions_.find(*metadata.connection.connection_id);
      if (found == sessions_.end())
        return;
      record = std::move(found->second);
      sessions_.erase(found);
    }
    const auto action = summary.blocked || summary.dropped
                            ? contract::inspection_action::blocked
                            : contract::inspection_action::permitted;
    if (action == contract::inspection_action::blocked)
      blocked_.fetch_add(1, std::memory_order_relaxed);
    else {
      permitted_.fetch_add(1, std::memory_order_relaxed);
      origin_completed_.fetch_add(1, std::memory_order_relaxed);
    }
    publish_inspection(
        *metadata.connection.connection_id, record.original_destination,
        record.server_name, record.protocol, action, summary.last_status,
        STATUS_SUCCESS,
        contract::request_transformed | contract::response_transformed,
        record.request, record.response);
  }

  void finish_failed_session(std::uint64_t session_id,
                             ntl::status status) noexcept {
    session_record record;
    {
      fast_mutex_guard guard(session_lock_);
      const auto found = sessions_.find(session_id);
      if (found == sessions_.end())
        return;
      record = std::move(found->second);
      sessions_.erase(found);
    }
    publish_inspection(session_id, record.original_destination,
                       record.server_name, record.protocol,
                       contract::inspection_action::failed, 0,
                       static_cast<NTSTATUS>(status), 0, record.request,
                       record.response);
  }

  struct worker_context {
    tls_proxy_state *owner = nullptr;
    ntl::net::kernel::wsk_tcp_listener *listener = nullptr;
  };

  static ntl::net::kernel::wsk_listener_limits limits() noexcept {
    return {
        .connection = {.maximum_write_bytes = 256 * 1024,
                       .receive_buffer_bytes = 64 * 1024,
                       .connect_timeout = std::chrono::seconds(5)},
        .accept_timeout = (std::chrono::milliseconds::max)(),
    };
  }

  struct identity_entry {
    std::string server_name;
    std::shared_ptr<ntl::net::kernel::schannel_credentials> credentials;
  };

  std::shared_ptr<ntl::net::kernel::schannel_credentials>
  identity(std::string_view server_name) noexcept {
    fast_mutex_guard guard(identity_lock_);
    for (const auto &entry : identities_) {
      if (entry.server_name == server_name)
        return entry.credentials;
    }
    return {};
  }

  void publish_identity_request(std::uint64_t session_id,
                                std::string_view server_name) noexcept {
    fast_mutex_guard guard(identity_request_lock_);
    contract::identity_request request{};
    request.sequence = ++identity_request_sequence_;
    request.session_id = session_id;
    request.server_name_size = static_cast<std::uint32_t>(
        (std::min)(server_name.size(), contract::maximum_server_name_size));
    std::memcpy(request.server_name.data(), server_name.data(),
                request.server_name_size);
    request.server_name[request.server_name_size] = '\0';
    identity_request_queue_[(request.sequence - 1) %
                            identity_request_queue_.size()] = request;
    identity_requests_.fetch_add(1, std::memory_order_relaxed);
  }

  std::shared_ptr<ntl::net::kernel::schannel_credentials>
  wait_for_identity(std::string_view server_name,
                    std::uint64_t session_id) noexcept {
    if (auto selected = identity(server_name))
      return selected;
    publish_identity_request(session_id, server_name);
    for (unsigned attempt = 0; attempt != 100; ++attempt) {
      if (stopping_.load(std::memory_order_acquire))
        return {};
      LARGE_INTEGER interval{};
      interval.QuadPart = -100 * 10'000;
      (void)KeDelayExecutionThread(KernelMode, FALSE, &interval);
      if (auto selected = identity(server_name))
        return selected;
    }
    identity_timeouts_.fetch_add(1, std::memory_order_relaxed);
    return {};
  }

  static std::shared_ptr<ntl::net::kernel::schannel_credentials>
  select_identity_entry(void *context, std::string_view server_name,
                        std::uint64_t session_id) noexcept {
    return context ? static_cast<tls_proxy_state *>(context)->wait_for_identity(
                         server_name, session_id)
                   : std::shared_ptr<ntl::net::kernel::schannel_credentials>{};
  }

  static void worker_entry(void *context) noexcept {
    auto *worker = static_cast<worker_context *>(context);
    if (worker && worker->owner && worker->listener)
      worker->owner->run(*worker->listener);
    PsTerminateSystemThread(STATUS_SUCCESS);
  }

  void run(ntl::net::kernel::wsk_tcp_listener &listener) noexcept {
    while (!stopping_.load(std::memory_order_acquire)) {
      auto accepted = listener.accept();
      if (!accepted) {
        if (static_cast<NTSTATUS>(accepted.status()) == STATUS_IO_TIMEOUT)
          continue;
        if (!stopping_.load(std::memory_order_acquire))
          failed_.fetch_add(1, std::memory_order_relaxed);
        break;
      }
      accepted_.fetch_add(1, std::memory_order_relaxed);
      const std::uint64_t session_id =
          next_session_id_.fetch_add(1, std::memory_order_relaxed);
      auto handoff =
          ntl::net::kernel::wsk_redirected_connection::capture(**accepted);
      if (!handoff) {
        failed_.fetch_add(1, std::memory_order_relaxed);
        publish_inspection(session_id, {}, {},
                           contract::inspected_protocol::none,
                           contract::inspection_action::failed, 0,
                           static_cast<NTSTATUS>(handoff.status()), 0, {}, {});
        continue;
      }
      if (!remember_session(session_id, handoff->original_destination())) {
        failed_.fetch_add(1, std::memory_order_relaxed);
        publish_inspection(session_id, handoff->original_destination(), {},
                           contract::inspected_protocol::none,
                           contract::inspection_action::failed, 0,
                           STATUS_INSUFFICIENT_RESOURCES, 0, {}, {});
        continue;
      }
      std::shared_ptr<ntl::net::io::transport_backend> backend = *accepted;
      ntl::net::kernel::redirected_tls_session_limits session_limits;
      session_limits.downstream_tls.receive_timeout =
          std::chrono::seconds(3);
      session_limits.upstream_tls.receive_timeout =
          std::chrono::seconds(3);
      session_limits.upstream_transport.connect_timeout =
          std::chrono::seconds(5);
      session_limits.transport_receive_capacity = 256 * 1024;
      auto session = ntl::net::kernel::redirected_tls_session::create(
          backend, std::move(*handoff), provider_, origin_security_provider_,
          identity_provider_, inspection_, session_id, runtime_observer_,
          session_limits);
      if (!session) {
        failed_.fetch_add(1, std::memory_order_relaxed);
        finish_failed_session(session_id, session.status());
        continue;
      }
      auto operation = wait_for_session(std::move(*session));
      ntl::status result = operation.wait(std::chrono::seconds(20));
      if (!result.is_ok()) {
        backend->stop();
        result = operation.wait();
      }
      if (!result.is_ok()) {
        finish_failed_session(session_id, result);
      }
    }
  }

  static void publish_inspection_entry(
      void *context, std::uint64_t session_id,
      const ntl::net::kernel::ip_endpoint &destination,
      std::string_view server_name, contract::inspected_protocol protocol,
      contract::inspection_action action, std::uint32_t status,
      NTSTATUS failure_status, std::uint32_t flags,
      std::span<const std::byte> request,
      std::span<const std::byte> response) noexcept {
    if (context)
      static_cast<tls_proxy_state *>(context)->publish_inspection(
          session_id, destination, server_name, protocol, action, status,
          failure_status, flags, request, response);
  }

  void publish_inspection(std::uint64_t session_id,
                          const ntl::net::kernel::ip_endpoint &destination,
                          std::string_view server_name,
                          contract::inspected_protocol protocol,
                          contract::inspection_action action,
                          std::uint32_t status, NTSTATUS failure_status,
                          std::uint32_t flags,
                          std::span<const std::byte> request,
                           std::span<const std::byte> response) noexcept {
    fast_mutex_guard guard(inspection_lock_);
    const std::uint64_t sequence = ++inspection_sequence_;
    if (sequence > inspections_.size())
      capture_overwritten_.fetch_add(1, std::memory_order_relaxed);
    auto &record = inspections_[(sequence - 1) % inspections_.size()];
    std::memset(&record, 0, sizeof(record));
    record.sequence = sequence;
    record.session_id = session_id;
    record.original_family = destination.family();
    record.original_port = destination.port();
    if (destination.family() == AF_INET) {
      const auto *address =
          reinterpret_cast<const SOCKADDR_IN *>(
              destination.borrowed_native_address());
      std::memcpy(record.original_address.data(), &address->sin_addr,
                  sizeof(address->sin_addr));
    } else if (destination.family() == AF_INET6) {
      const auto *address =
          reinterpret_cast<const SOCKADDR_IN6 *>(
              destination.borrowed_native_address());
      std::memcpy(record.original_address.data(), &address->sin6_addr,
                  sizeof(address->sin6_addr));
    }
    record.server_name_size = static_cast<std::uint32_t>(
        (std::min)(server_name.size(), contract::maximum_server_name_size));
    if (record.server_name_size != 0)
      std::memcpy(record.server_name.data(), server_name.data(),
                  record.server_name_size);
    record.server_name[record.server_name_size] = '\0';
    record.protocol = protocol;
    record.action = action;
    record.flags = flags;
    record.status = status;
    record.failure_status = failure_status;
    const std::size_t request_size =
        (std::min)(request.size(), contract::maximum_capture_size);
    const std::size_t response_size =
        (std::min)(response.size(), contract::maximum_capture_size);
    record.request_size = static_cast<std::uint32_t>(request_size);
    record.response_size = static_cast<std::uint32_t>(response_size);
    if (request_size != 0)
      std::memcpy(record.request.data(), request.data(), request_size);
    if (response_size != 0)
      std::memcpy(record.response.data(), response.data(), response_size);
  }

  ntl::net::kernel::schannel schannel_{};
  std::shared_ptr<ntl::net::kernel::wsk_provider> provider_{};
  std::shared_ptr<ntl::net::kernel::standard_redirected_tls_inspection>
      inspection_{};
  std::shared_ptr<
      ntl::net::kernel::redirected_tls_origin_security_provider>
      origin_security_provider_{};
  std::shared_ptr<identity_provider> identity_provider_{};
  std::shared_ptr<runtime_observer> runtime_observer_{};
  ntl::net::kernel::wsk_tcp_listener::pointer listener_v4_{};
  ntl::net::kernel::wsk_tcp_listener::pointer listener_v6_{};
  worker_context worker_v4_{};
  worker_context worker_v6_{};
  ntl::system_thread thread_v4_{};
  ntl::system_thread thread_v6_{};
  FAST_MUTEX identity_lock_{};
  FAST_MUTEX inspection_lock_{};
  FAST_MUTEX identity_request_lock_{};
  FAST_MUTEX session_lock_{};
  std::vector<identity_entry> identities_{};
  std::unordered_map<std::uint64_t, session_record> sessions_{};
  std::shared_ptr<ntl::net::kernel::schannel_credentials> client_credentials_{};
  std::array<contract::inspection_record, 16> inspections_{};
  std::array<contract::identity_request, 16> identity_request_queue_{};
  std::uint64_t inspection_sequence_ = 0;
  std::uint64_t identity_request_sequence_ = 0;
  std::uint32_t process_id_ = 0;
  std::atomic<std::uint64_t> next_session_id_{1};
  std::atomic<std::uint64_t> accepted_{0};
  std::atomic<std::uint64_t> handshaken_{0};
  std::atomic<std::uint64_t> permitted_{0};
  std::atomic<std::uint64_t> blocked_{0};
  std::atomic<std::uint64_t> failed_{0};
  std::atomic<std::uint64_t> origin_connected_{0};
  std::atomic<std::uint64_t> origin_completed_{0};
  std::atomic<std::uint64_t> identity_requests_{0};
  std::atomic<std::uint64_t> identity_timeouts_{0};
  std::atomic<std::uint64_t> capture_overwritten_{0};
  std::atomic<bool> stopping_{false};
};

template <class Layer>
ntl::wfp::terminating_decision
redirect_connection(ntl::wfp::connect_redirector &redirector,
                    const ntl::wfp::classify_event<Layer> &event) noexcept {
  const auto protocol = event.value(Layer::field::protocol).uint8();
  if (!protocol || *protocol != IPPROTO_TCP)
    return ntl::wfp::terminating_decision::block;
  return redirector.redirect(
      event, ntl::wfp::local_proxy_target::from_filter_context(
                 event.filter().context()));
}

} // namespace

ntl::status ntl::main(ntl::driver &driver, const std::wstring &) {
  auto state = std::make_shared<tls_proxy_state>();
  const ntl::status started = state->start();
  if (!started.is_ok())
    return started;
  auto redirect =
      ntl::wfp::connect_redirector::try_create(contract::provider_key);
  if (!redirect) {
    state->shutdown();
    return redirect.status();
  }
  auto redirector =
      std::make_shared<ntl::wfp::connect_redirector>(std::move(*redirect));

  auto options = ntl::device_options()
                     .name(contract::device_name)
                     .type(FILE_DEVICE_UNKNOWN)
                     .exclusive(false)
                     .security_descriptor(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",
                                          contract::device_class_guid);
  auto endpoint_result = ntl::try_create_device_endpoint<void>(driver, options);
  if (!endpoint_result) {
    state->shutdown();
    return endpoint_result.status();
  }
  auto endpoint = std::move(*endpoint_result);
  ntl::status route_status = ntl::status::ok();
  const auto add_route = [&](ntl::status route) {
    route_status = route;
    return route.is_ok();
  };
  if (!add_route(endpoint.on_ioctl<
          contract::configure_certificate_contract>(
          [state](const contract::certificate_config &configuration) noexcept {
            return state->configure(configuration);
          })) ||
      !add_route(endpoint.on_ioctl<contract::query_proxy_contract>(
          [state](contract::proxy_info &output) noexcept {
            output = state->snapshot();
            return ntl::status::ok();
          })) ||
      !add_route(endpoint.on_ioctl<
          contract::query_last_inspection_contract>(
          [state](contract::inspection_record &output) noexcept {
            state->last_inspection(output);
            return ntl::status::ok();
          })) ||
      !add_route(endpoint.on_ioctl<contract::read_inspection_contract>(
          [state](const contract::inspection_cursor &cursor,
                  contract::inspection_read_result &output) noexcept {
            state->read_inspection_after(cursor.after_sequence, output);
            return ntl::status::ok();
          })) ||
      !add_route(endpoint.on_ioctl<
          contract::read_identity_request_contract>(
          [state](const contract::inspection_cursor &cursor,
                  contract::identity_request_read_result &output) noexcept {
            state->read_identity_request_after(cursor.after_sequence, output);
            return ntl::status::ok();
          }))) {
    state->shutdown();
    return route_status;
  }

  ntl::wfp::callout_driver<> callouts(driver);
  const ntl::status v4 = callouts.add_terminating(
      contract::callout_key_v4, redirector,
      [](ntl::wfp::connect_redirector &owned_redirector,
         const ntl::wfp::classify_event<contract::layer_v4> &event) noexcept {
        return redirect_connection(owned_redirector, event);
      });
  if (!v4.is_ok()) {
    state->shutdown();
    return v4;
  }
  const ntl::status v6 = callouts.add_terminating(
      contract::callout_key_v6, redirector,
      [](ntl::wfp::connect_redirector &owned_redirector,
         const ntl::wfp::classify_event<contract::layer_v6> &event) noexcept {
        return redirect_connection(owned_redirector, event);
      });
  if (!v6.is_ok()) {
    state->shutdown();
    return v6;
  }

  driver.on_unload([state, endpoint, callouts]() mutable {
    const ntl::status closed = endpoint.close();
    NT_ASSERT(closed.is_ok());
    const ntl::status reset = callouts.close();
    NT_ASSERT(reset.is_ok());
    state->shutdown();
  });
  return ntl::status::ok();
}
