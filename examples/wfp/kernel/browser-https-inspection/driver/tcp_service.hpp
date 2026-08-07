#pragma once

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
#include <vector>

#include <ntl/net/io/async_transport_stream>
#include <ntl/net/kernel/schannel>
#include <ntl/net/kernel/wsk_redirect>
#include <ntl/net/kernel/wsk_transport>
#include <ntl/system_thread>

#include "browser_https_inspection_contract.hpp"
#include "tcp_session.hpp"

namespace crtsys::wfp_kernel_browser_https::driver {

struct workspace_lifetime_probe {
  workspace_lifetime_probe(KEVENT *completed_value,
                           std::atomic<bool> *passive_value) noexcept
      : completed(completed_value), passive(passive_value) {}

  ~workspace_lifetime_probe() {
    passive->store(ntl::current_irql() == ntl::irql::passive,
                   std::memory_order_release);
    KeSetEvent(completed, IO_NO_INCREMENT, FALSE);
  }

  KEVENT *completed = nullptr;
  std::atomic<bool> *passive = nullptr;
};

using workspace_lifetime_probe_pool =
    ntl::net::kernel::workspace_pool<workspace_lifetime_probe,
                                     ntl::pool_tag("bWkN")>;

inline ntl::status verify_workspace_lifetime_contract() noexcept {
  KEVENT completed{};
  KeInitializeEvent(&completed, NotificationEvent, FALSE);
  std::atomic<bool> passive{false};
  try {
    workspace_lifetime_probe_pool pool(0, 1);
    auto acquired = pool.try_acquire(&completed, &passive);
    if (!acquired)
      return acquired.status();
    auto exhausted = pool.try_acquire(&completed, &passive);
    if (exhausted || exhausted.status() != STATUS_QUOTA_EXCEEDED)
      return STATUS_INTERNAL_ERROR;

    auto lease = std::move(*acquired);
    pool.close();
    pool.close();
    auto after_close = pool.try_acquire(&completed, &passive);
    if (after_close || after_close.status() != STATUS_DELETE_PENDING)
      return STATUS_INTERNAL_ERROR;

    KIRQL previous = PASSIVE_LEVEL;
    KeRaiseIrql(DISPATCH_LEVEL, &previous);
    lease.reset();
    KeLowerIrql(previous);

    LARGE_INTEGER timeout{};
    timeout.QuadPart = -5LL * 10'000'000LL;
    const NTSTATUS waited = KeWaitForSingleObject(
        &completed, Executive, KernelMode, FALSE, &timeout);
    if (!NT_SUCCESS(waited))
      return waited;
    return passive.load(std::memory_order_acquire)
               ? ntl::status::ok()
               : ntl::status{STATUS_INVALID_DEVICE_STATE};
  } catch (const ntl::exception &error) {
    return error.get_status();
  } catch (...) {
    return STATUS_UNHANDLED_EXCEPTION;
  }
}

class fast_mutex_guard {
public:
  explicit fast_mutex_guard(FAST_MUTEX &value) noexcept : value_(&value) {
    NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    KeEnterCriticalRegion();
    ExAcquireFastMutexUnsafe(value_);
  }
  fast_mutex_guard(const fast_mutex_guard &) = delete;
  fast_mutex_guard &operator=(const fast_mutex_guard &) = delete;
  ~fast_mutex_guard() {
    ExReleaseFastMutexUnsafe(value_);
    KeLeaveCriticalRegion();
  }

private:
  FAST_MUTEX *value_;
};

class tcp_service : public std::enable_shared_from_this<tcp_service> {
  struct identity_entry {
    std::string server_name;
    std::shared_ptr<ntl::net::kernel::schannel_credentials> credentials;
  };
  struct origin_security_entry {
    std::string server_name;
    std::shared_ptr<ntl::net::kernel::schannel_credentials> credentials;
    std::shared_ptr<ntl::net::kernel::schannel_peer_certificate_policy> policy;
  };

public:
  class identity_update {
  public:
    identity_update() noexcept = default;
    identity_update(identity_update &&) noexcept = default;
    identity_update &operator=(identity_update &&) noexcept = default;
    identity_update(const identity_update &) = delete;
    identity_update &operator=(const identity_update &) = delete;

  private:
    friend class tcp_service;
    std::string server_name_{};
    identity_entry previous_{};
    bool had_previous_ = false;
    bool applied_ = false;
  };

  class origin_security_update {
  public:
    origin_security_update() noexcept = default;
    origin_security_update(origin_security_update &&) noexcept = default;
    origin_security_update &
    operator=(origin_security_update &&) noexcept = default;
    origin_security_update(const origin_security_update &) = delete;
    origin_security_update &operator=(const origin_security_update &) = delete;

  private:
    friend class tcp_service;
    std::string server_name_{};
    origin_security_entry previous_{};
    bool had_previous_ = false;
    bool applied_ = false;
  };

  class session_events final
      : public ntl::net::kernel::redirected_tls_session_observer {
  public:
    explicit session_events(std::weak_ptr<tcp_service> owner) noexcept
        : owner_(std::move(owner)) {}

    void on_downstream_handshake(
        std::uint64_t, ntl::net::kernel::inspected_http_protocol,
        std::string_view) noexcept override {
      if (auto owner = owner_.lock())
        owner->handshaken_.fetch_add(1, std::memory_order_relaxed);
    }

    void on_origin_handshake(
        std::uint64_t,
        ntl::net::kernel::inspected_http_protocol) noexcept override {
      if (auto owner = owner_.lock())
        owner->origin_connected_.fetch_add(1, std::memory_order_relaxed);
    }

    void on_origin_certificate_validated(
        std::uint64_t,
        ntl::net::kernel::inspected_http_protocol) noexcept override {
      if (auto owner = owner_.lock())
        owner->origin_peer_validated_.fetch_add(1,
                                                std::memory_order_relaxed);
    }

  private:
    std::weak_ptr<tcp_service> owner_;
  };

  tcp_service() noexcept {
    ExInitializeFastMutex(&configuration_transaction_lock_);
    ExInitializeFastMutex(&identity_lock_);
    ExInitializeFastMutex(&inspection_lock_);
    ExInitializeFastMutex(&identity_request_lock_);
    ExInitializeFastMutex(&session_lock_);
    ExInitializeFastMutex(&origin_security_lock_);
  }

  ~tcp_service() { shutdown(); }

  FAST_MUTEX &configuration_transaction_lock() noexcept {
    return configuration_transaction_lock_;
  }

  ntl::status start() noexcept {
    const ntl::status workspace_status =
        verify_workspace_lifetime_contract();
    if (!workspace_status.is_ok())
      return workspace_status;
    workspace_lifetime_passed_.store(true, std::memory_order_release);
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
      provider_ = std::make_shared<ntl::net::kernel::wsk_provider>();
      identities_.reserve(contract::identity_cache_capacity);
      origin_security_.reserve(contract::identity_cache_capacity);
      sessions_.reserve(maximum_sessions);

      const std::weak_ptr<tcp_service> weak = weak_from_this();
      identity_provider_ =
          ntl::net::kernel::make_redirected_tls_server_identity_provider(
              [weak](const ntl::net::kernel::
                         redirected_tls_identity_request_view &request)
                  -> ntl::result<std::shared_ptr<
                      ntl::net::kernel::schannel_credentials>> {
                auto owner = weak.lock();
                if (!owner)
                  return ntl::unexpected(STATUS_DELETE_PENDING);
                auto selected = owner->wait_for_identity(
                    request.server_name, request.session_id);
                return selected ? ntl::ok(std::move(selected))
                                : ntl::unexpected(STATUS_NOT_FOUND);
              });
      origin_security_provider_ =
          ntl::net::kernel::make_redirected_tls_origin_security_provider(
              [weak](const ntl::net::kernel::
                         redirected_tls_identity_request_view &request)
                  -> ntl::result<ntl::net::kernel::
                      redirected_tls_origin_security> {
                auto owner = weak.lock();
                return owner
                           ? owner->origin_security(request.server_name)
                           : ntl::result<ntl::net::kernel::
                                 redirected_tls_origin_security>(
                                 ntl::unexpected(STATUS_DELETE_PENDING));
              });
      session_events_ = std::make_shared<session_events>(weak);

      auto grpc =
          std::make_shared<ntl::net::grpc::message_transform_pipeline>();
      crtsys::wfp_browser_http_policy::configure_grpc_transforms(*grpc);
      auto policy = crtsys::wfp_browser_http_policy::
          make_browser_inspection_policy(grpc);
      auto dispatcher = make_browser_tcp_dispatcher(
          std::move(policy),
          [weak](const ntl::net::http::inspection_session_metadata &metadata) {
            auto owner = weak.lock();
            return owner ? owner->make_session_observer(metadata)
                         : tcp_session_observer{};
          });
      if (!dispatcher)
        return dispatcher.status();
      dispatcher_ = std::move(*dispatcher);
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
    ntl::status status = provider_->open({5'000});
    if (!status.is_ok()) {
      client_credentials_.reset();
      return status;
    }
    auto ipv4 = ntl::net::kernel::wsk_tcp_listener::try_listen(
        *provider_, ntl::net::kernel::ip_endpoint::any_ipv4(), limits());
    if (!ipv4) {
      provider_->close();
      client_credentials_.reset();
      return ipv4.status();
    }
    listener_v4_ = std::move(*ipv4);
    auto ipv6 = ntl::net::kernel::wsk_tcp_listener::try_listen(
        *provider_, ntl::net::kernel::ip_endpoint::any_ipv6(), limits());
    if (!ipv6) {
      listener_v4_.reset();
      provider_->close();
      client_credentials_.reset();
      return ipv6.status();
    }
    listener_v6_ = std::move(*ipv6);
    for (std::size_t index = 0; index != workers_.size(); ++index) {
      worker_contexts_[index] = {this, index % 2 == 0 ? listener_v4_.get()
                                                      : listener_v6_.get()};
      auto thread =
          ntl::system_thread::create(&worker_entry, &worker_contexts_[index]);
      if (!thread) {
        shutdown();
        return thread.status();
      }
      workers_[index] = std::move(*thread);
    }
    ready_.store(true, std::memory_order_release);
    return ntl::status::ok();
  }

  ntl::result<identity_update>
  configure(const contract::certificate_config &input) noexcept {
    if (input.server_name_size == 0 ||
        input.server_name_size > contract::maximum_server_name_size ||
        input.server_name[input.server_name_size] != '\0')
      return ntl::unexpected(STATUS_INVALID_PARAMETER);
    const std::string_view server_name(input.server_name.data(),
                                       input.server_name_size);
    auto reference = ntl::net::kernel::schannel_certificate_store_ref::make(
        input.sha1_thumbprint, L"MY");
    if (!reference)
      return ntl::unexpected(reference.status());
    auto credentials = schannel_.try_server(*reference);
    if (!credentials)
      return ntl::unexpected(credentials.status());
    try {
      identity_update update;
      update.server_name_ = server_name;
      identity_entry candidate{
          std::string(server_name),
          std::make_shared<ntl::net::kernel::schannel_credentials>(
              std::move(*credentials))};
      fast_mutex_guard guard(identity_lock_);
      for (auto &entry : identities_) {
        if (ascii_server_name_equal(entry.server_name, server_name)) {
          update.previous_ = std::move(entry);
          update.had_previous_ = true;
          entry = std::move(candidate);
          update.applied_ = true;
          return update;
        }
      }
      if (identities_.size() >= contract::identity_cache_capacity)
        return ntl::unexpected(STATUS_QUOTA_EXCEEDED);
      identities_.push_back(std::move(candidate));
      update.applied_ = true;
      return update;
    } catch (const std::bad_alloc &) {
      return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return ntl::unexpected(STATUS_UNHANDLED_EXCEPTION);
    }
  }

  void rollback(identity_update &&update) noexcept {
    if (!update.applied_)
      return;
    fast_mutex_guard guard(identity_lock_);
    const auto found =
        std::find_if(identities_.begin(), identities_.end(),
                     [&update](const identity_entry &entry) noexcept {
                       return ascii_server_name_equal(entry.server_name,
                                                      update.server_name_);
                     });
    if (update.had_previous_) {
      if (found != identities_.end()) {
        *found = std::move(update.previous_);
      } else {
        NT_ASSERT(identities_.size() < identities_.capacity());
        identities_.push_back(std::move(update.previous_));
      }
    } else if (found != identities_.end()) {
      identities_.erase(found);
    }
    update.applied_ = false;
  }

  ntl::result<origin_security_update> configure_origin_security(
      const contract::origin_security_config &input) noexcept {
    if (input.server_name_size == 0 ||
        input.server_name_size > contract::maximum_server_name_size ||
        input.server_name[input.server_name_size] != '\0')
      return ntl::unexpected(STATUS_INVALID_PARAMETER);
    const std::string_view server_name(input.server_name.data(),
                                       input.server_name_size);
    origin_security_update update;
    try {
      update.server_name_ = server_name;
    } catch (const std::bad_alloc &) {
      return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return ntl::unexpected(STATUS_UNHANDLED_EXCEPTION);
    }
    if (input.action == contract::origin_security_action::remove) {
      {
        fast_mutex_guard guard(origin_security_lock_);
        const auto found = std::find_if(
            origin_security_.begin(), origin_security_.end(),
            [server_name](const origin_security_entry &entry) {
              return ascii_server_name_equal(entry.server_name, server_name);
            });
        if (found != origin_security_.end()) {
          update.previous_ = std::move(*found);
          update.had_previous_ = true;
          origin_security_.erase(found);
        }
        update.applied_ = true;
      }
      return update;
    }
    if (input.action != contract::origin_security_action::install ||
        input.origin_leaf_der_size == 0 ||
        input.origin_leaf_der_size > input.origin_leaf_der.size())
      return ntl::unexpected(STATUS_INVALID_PARAMETER);
    auto reference = ntl::net::kernel::schannel_certificate_store_ref::make(
        input.client_sha1_thumbprint, L"MY");
    if (!reference)
      return ntl::unexpected(reference.status());
    auto credentials = schannel_.try_client(
        {.manual_peer_validation = false,
         .use_default_client_certificate = false,
         .borrowed_certificate = &*reference});
    if (!credentials)
      return ntl::unexpected(credentials.status());
    auto exact =
        ntl::net::kernel::schannel_exact_leaf_certificate_policy::try_create(
            std::span(input.origin_leaf_der).first(input.origin_leaf_der_size),
            contract::maximum_certificate_der_size);
    if (!exact)
      return ntl::unexpected(exact.status());
    try {
      origin_security_entry candidate{
          std::string(server_name),
          std::make_shared<ntl::net::kernel::schannel_credentials>(
              std::move(*credentials)),
          std::make_shared<
              ntl::net::kernel::schannel_exact_leaf_certificate_policy>(
              std::move(*exact))};
      fast_mutex_guard guard(origin_security_lock_);
      for (auto &entry : origin_security_) {
        if (ascii_server_name_equal(entry.server_name, server_name)) {
          update.previous_ = std::move(entry);
          update.had_previous_ = true;
          entry = std::move(candidate);
          update.applied_ = true;
          return update;
        }
      }
      if (origin_security_.size() >= contract::identity_cache_capacity)
        return ntl::unexpected(STATUS_QUOTA_EXCEEDED);
      origin_security_.push_back(std::move(candidate));
      update.applied_ = true;
      return update;
    } catch (const std::bad_alloc &) {
      return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return ntl::unexpected(STATUS_UNHANDLED_EXCEPTION);
    }
  }

  void rollback(origin_security_update &&update) noexcept {
    if (!update.applied_)
      return;
    fast_mutex_guard guard(origin_security_lock_);
    const auto found =
        std::find_if(origin_security_.begin(), origin_security_.end(),
                     [&update](const origin_security_entry &entry) noexcept {
                       return ascii_server_name_equal(entry.server_name,
                                                      update.server_name_);
                     });
    if (update.had_previous_) {
      if (found != origin_security_.end()) {
        *found = std::move(update.previous_);
      } else {
        NT_ASSERT(origin_security_.size() < origin_security_.capacity());
        origin_security_.push_back(std::move(update.previous_));
      }
    } else if (found != origin_security_.end()) {
      origin_security_.erase(found);
    }
    update.applied_ = false;
  }

  contract::service_info
  snapshot(const contract::quic_telemetry &quic = {}) noexcept {
    std::uint32_t identities = 0;
    {
      fast_mutex_guard guard(identity_lock_);
      identities = static_cast<std::uint32_t>(identities_.size());
    }
    contract::service_info result{};
    result.version = contract::service_info_version;
    result.size = static_cast<std::uint32_t>(sizeof(result));
    result.process_id = process_id_;
    result.tcp_port_v4 =
        listener_v4_ ? listener_v4_->local_endpoint().port() : 0;
    result.tcp_port_v6 =
        listener_v6_ ? listener_v6_->local_endpoint().port() : 0;
    result.tcp_ready = ready_.load(std::memory_order_acquire) ? 1u : 0u;
    result.workspace_lifetime_passed =
        workspace_lifetime_passed_.load(std::memory_order_acquire) ? 1u : 0u;
    result.accepted = accepted_.load(std::memory_order_relaxed);
    result.handshaken = handshaken_.load(std::memory_order_relaxed);
    result.origin_connected = origin_connected_.load(std::memory_order_relaxed);
    result.origin_completed = origin_completed_.load(std::memory_order_relaxed);
    result.permitted = permitted_.load(std::memory_order_relaxed);
    result.blocked = blocked_.load(std::memory_order_relaxed);
    result.transformed = transformed_.load(std::memory_order_relaxed);
    result.failed = failed_.load(std::memory_order_relaxed);
    result.identity_requests =
        identity_requests_.load(std::memory_order_relaxed);
    result.identity_timeouts =
        identity_timeouts_.load(std::memory_order_relaxed);
    result.capture_dropped = capture_dropped_.load(std::memory_order_relaxed);
    result.origin_peer_validated =
        origin_peer_validated_.load(std::memory_order_relaxed);
    result.identity_count = identities;
    result.identity_capacity = contract::identity_cache_capacity;
    {
      fast_mutex_guard guard(origin_security_lock_);
      result.origin_security_ready = origin_security_.empty() ? 0u : 1u;
    }
    result.active_tcp_sessions = static_cast<std::uint32_t>(
        active_sessions_.load(std::memory_order_relaxed));
    result.quic_gate = quic;
    return result;
  }

  void
  read_inspection_after(std::uint64_t after,
                        contract::inspection_read_result &result) noexcept {
    fast_mutex_guard guard(inspection_lock_);
    read_ring(after, inspection_sequence_, inspections_, result,
              &contract::inspection_read_result::record);
  }

  void read_identity_request_after(
      std::uint64_t after,
      contract::identity_request_read_result &result) noexcept {
    fast_mutex_guard guard(identity_request_lock_);
    read_ring(after, identity_request_sequence_, identity_requests_ring_,
              result, &contract::identity_request_read_result::request);
  }

  std::uint64_t reserve_session_id() noexcept {
    return next_session_id_.fetch_add(1, std::memory_order_relaxed);
  }

  void publish_external(std::uint64_t session_id,
                        const ntl::net::kernel::ip_endpoint &destination,
                        std::string_view server_name,
                        contract::inspected_protocol protocol,
                        contract::inspection_action action,
                        std::uint32_t status, NTSTATUS failure_status,
                        std::uint32_t flags, std::span<const std::byte> request,
                        std::span<const std::byte> response) noexcept {
    publish(session_id, destination, server_name, protocol, action, status,
            failure_status, flags, request, response);
  }

  /**
   * Shares the already captured WSK provider with the HTTP/3 origin fallback.
   * Every fallback transport retains the provider state it needs. Closing
   * either service first is therefore safe; the normal aggregate shutdown
   * order is only used to observe deterministic protocol completion.
   */
  std::shared_ptr<ntl::net::kernel::wsk_provider>
  origin_fallback_provider() noexcept {
    return provider_;
  }

  void shutdown() noexcept {
    if (stopping_.exchange(true, std::memory_order_acq_rel))
      return;
    ready_.store(false, std::memory_order_release);
    if (listener_v4_)
      listener_v4_->stop();
    if (listener_v6_)
      listener_v6_->stop();
    for (auto &worker : workers_) {
      if (worker)
        (void)worker.join();
    }
    stop_active_sessions();
    reap_sessions(true);
    listener_v4_.reset();
    listener_v6_.reset();
    if (dispatcher_)
      dispatcher_->close();
    std::vector<identity_entry> identities;
    std::vector<origin_security_entry> origin_security;
    std::shared_ptr<ntl::net::kernel::schannel_credentials> client_credentials;
    {
      fast_mutex_guard guard(identity_lock_);
      identities.swap(identities_);
    }
    {
      fast_mutex_guard guard(origin_security_lock_);
      origin_security.swap(origin_security_);
      client_credentials = std::move(client_credentials_);
    }
    identities.clear();
    origin_security.clear();
    dispatcher_.reset();
    session_events_.reset();
    identity_provider_.reset();
    origin_security_provider_.reset();
    client_credentials.reset();
    const ntl::status credentials_drained = schannel_.close();
    NT_ASSERT(credentials_drained.is_ok());
    if (provider_)
      provider_->close();
    provider_.reset();
  }

private:
  struct worker_context {
    tcp_service *owner = nullptr;
    ntl::net::kernel::wsk_tcp_listener *listener = nullptr;
  };
  struct session_context {
    session_context(
        tcp_service *owner_value, std::uint64_t session_id_value,
        ntl::net::kernel::wsk_tcp_transport::pointer &&accepted_value,
        ntl::net::kernel::wsk_redirected_connection &&redirect_value)
        : owner(owner_value), session_id(session_id_value),
          accepted(std::move(accepted_value)),
          original_destination(redirect_value.original_destination()),
          redirect(std::move(redirect_value)) {}

    tcp_service *owner = nullptr;
    std::uint64_t session_id = 0;
    ntl::net::kernel::wsk_tcp_transport::pointer accepted{};
    ntl::net::kernel::ip_endpoint original_destination{};
    ntl::net::kernel::wsk_redirected_connection redirect;
    ntl::system_thread thread{};
    std::atomic<bool> completed{false};
  };

  static ntl::net::kernel::wsk_listener_limits limits() noexcept {
    return {.connection = {.maximum_write_bytes = 4 * 1024 * 1024,
                           .receive_buffer_bytes = 64 * 1024,
                           .connect_timeout = std::chrono::seconds(5)},
            .accept_timeout = (std::chrono::milliseconds::max)()};
  }

  template <class Result, class Record, std::size_t Size>
  static void read_ring(std::uint64_t after, std::uint64_t sequence,
                        const std::array<Record, Size> &ring, Result &result,
                        Record Result::*member) noexcept {
    std::memset(&result, 0, sizeof(result));
    result.current_sequence = sequence;
    if (sequence == 0)
      return;
    result.oldest_sequence = sequence > Size ? sequence - Size + 1 : 1;
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
    if (next <= sequence) {
      result.available = 1;
      result.*member = ring[(next - 1) % Size];
    }
  }

  std::shared_ptr<ntl::net::kernel::schannel_credentials>
  identity(std::string_view server_name) noexcept {
    fast_mutex_guard transaction(configuration_transaction_lock_);
    fast_mutex_guard guard(identity_lock_);
    for (const auto &entry : identities_) {
      if (ascii_server_name_equal(entry.server_name, server_name))
        return entry.credentials;
    }
    return {};
  }

  void request_identity(std::uint64_t session_id,
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
    identity_requests_ring_[(request.sequence - 1) %
                            identity_requests_ring_.size()] = request;
    identity_requests_.fetch_add(1, std::memory_order_relaxed);
  }

  std::shared_ptr<ntl::net::kernel::schannel_credentials>
  wait_for_identity(std::string_view server_name,
                    std::uint64_t session_id) noexcept {
    if (auto found = identity(server_name))
      return found;
    request_identity(session_id, server_name);
    for (unsigned attempt = 0; attempt != 100; ++attempt) {
      if (stopping_.load(std::memory_order_acquire))
        return {};
      LARGE_INTEGER interval{};
      interval.QuadPart = -100 * 10'000;
      (void)KeDelayExecutionThread(KernelMode, FALSE, &interval);
      if (auto found = identity(server_name))
        return found;
    }
    identity_timeouts_.fetch_add(1, std::memory_order_relaxed);
    return {};
  }

  ntl::result<ntl::net::kernel::redirected_tls_origin_security>
  origin_security(std::string_view server_name) noexcept {
    fast_mutex_guard transaction(configuration_transaction_lock_);
    fast_mutex_guard guard(origin_security_lock_);
    for (const auto &entry : origin_security_) {
      if (ascii_server_name_equal(entry.server_name, server_name))
        return ntl::ok(ntl::net::kernel::redirected_tls_origin_security{
            entry.credentials, entry.policy});
    }
    if (!client_credentials_)
      return ntl::unexpected(STATUS_INVALID_DEVICE_STATE);
    return ntl::ok(ntl::net::kernel::redirected_tls_origin_security{
        client_credentials_, {}});
  }

  tcp_session_observer make_session_observer(
      const ntl::net::http::inspection_session_metadata &metadata) {
    ntl::net::kernel::ip_endpoint destination{};
    const std::uint64_t session_id =
        metadata.connection.connection_id.value_or(0);
    {
      fast_mutex_guard guard(session_lock_);
      for (const auto &session : sessions_) {
        if (session && session->session_id == session_id) {
          destination = session->original_destination;
          break;
        }
      }
    }
    auto owner = shared_from_this();
    return {.owner = owner,
            .context = owner.get(),
            .publish = &publish_entry,
            .permitted = &permitted_,
            .blocked = &blocked_,
            .transformed = &transformed_,
            .origin_completed = &origin_completed_,
            .session_id = session_id,
            .original_destination = destination};
  }

  static void publish_entry(void *context, std::uint64_t session_id,
                            const ntl::net::kernel::ip_endpoint &destination,
                            std::string_view server_name,
                            contract::inspected_protocol protocol,
                            contract::inspection_action action,
                            std::uint32_t status, NTSTATUS failure_status,
                            std::uint32_t flags,
                            std::span<const std::byte> request,
                            std::span<const std::byte> response) noexcept {
    if (context)
      static_cast<tcp_service *>(context)->publish(
          session_id, destination, server_name, protocol, action, status,
          failure_status, flags, request, response);
  }

  void publish(std::uint64_t session_id,
               const ntl::net::kernel::ip_endpoint &destination,
               std::string_view server_name,
               contract::inspected_protocol protocol,
               contract::inspection_action action, std::uint32_t status,
               NTSTATUS failure_status, std::uint32_t flags,
               std::span<const std::byte> request,
               std::span<const std::byte> response) noexcept {
    fast_mutex_guard guard(inspection_lock_);
    const std::uint64_t sequence = ++inspection_sequence_;
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

  static void worker_entry(void *context) noexcept {
    auto *worker = static_cast<worker_context *>(context);
    if (worker && worker->owner && worker->listener)
      worker->owner->run(*worker->listener);
    PsTerminateSystemThread(STATUS_SUCCESS);
  }

  static void session_entry(void *context) noexcept {
    auto *session = static_cast<session_context *>(context);
    if (session && session->owner) {
      session->owner->run_session(*session);
      session->owner->release_session_slot();
    }
    if (session)
      session->completed.store(true, std::memory_order_release);
    PsTerminateSystemThread(STATUS_SUCCESS);
  }

  bool try_acquire_session_slot() noexcept {
    std::size_t value = active_sessions_.load(std::memory_order_acquire);
    while (value < maximum_sessions) {
      if (active_sessions_.compare_exchange_weak(value, value + 1,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire))
        return true;
    }
    return false;
  }

  void release_session_slot() noexcept {
    std::size_t current = active_sessions_.load(std::memory_order_acquire);
    while (current != 0) {
      if (active_sessions_.compare_exchange_weak(current, current - 1,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire))
        return;
    }
    NT_ASSERT(false);
  }

  void reap_sessions(bool all = false) noexcept {
    for (;;) {
      std::unique_ptr<session_context> session;
      {
        fast_mutex_guard guard(session_lock_);
        auto found = sessions_.end();
        for (auto current = sessions_.begin(); current != sessions_.end();
             ++current) {
          if (all || (*current)->completed.load(std::memory_order_acquire)) {
            found = current;
            break;
          }
        }
        if (found == sessions_.end())
          return;
        session = std::move(*found);
        sessions_.erase(found);
      }
      if (all && session->accepted)
        session->accepted->stop();
      if (session->thread)
        (void)session->thread.join();
      session->accepted.reset();
    }
  }

  void stop_active_sessions() noexcept {
    fast_mutex_guard guard(session_lock_);
    for (auto &session : sessions_) {
      if (session->accepted)
        session->accepted->stop();
    }
  }

  ntl::net::kernel::waitable_status_task
  run_managed_session(session_context &session) noexcept {
    if (!session.accepted || !provider_ || !origin_security_provider_ ||
        !identity_provider_ || !dispatcher_ || !session_events_)
      co_return ntl::status{STATUS_INVALID_DEVICE_STATE};
    std::shared_ptr<ntl::net::io::transport_backend> backend =
        session.accepted;
    ntl::net::kernel::redirected_tls_session_limits limits;
    limits.downstream_tls.maximum_buffered_ciphertext = 4 * 1024 * 1024;
    limits.downstream_tls.maximum_plaintext_record = 64 * 1024;
    limits.downstream_tls.receive_timeout = std::chrono::seconds(5);
    limits.upstream_tls.maximum_buffered_ciphertext = 4 * 1024 * 1024;
    limits.upstream_tls.maximum_plaintext_record = 64 * 1024;
    limits.upstream_tls.receive_timeout = std::chrono::seconds(30);
    limits.upstream_transport.maximum_write_bytes = 4 * 1024 * 1024;
    limits.upstream_transport.receive_buffer_bytes = 64 * 1024;
    limits.upstream_transport.connect_timeout = std::chrono::seconds(5);
    limits.transport_receive_capacity = 4 * 1024 * 1024;
    auto managed = ntl::net::kernel::redirected_tls_session::create(
        std::move(backend), std::move(session.redirect), provider_,
        origin_security_provider_, identity_provider_, dispatcher_,
        session.session_id, session_events_, limits);
    if (!managed)
      co_return managed.status();
    co_return co_await (*managed)->run();
  }

  void run_session(session_context &session) noexcept {
    const auto destination = session.redirect.original_destination();
    auto operation = run_managed_session(session);
    const ntl::status status = operation.wait();
    if (!status.is_ok()) {
      failed_.fetch_add(1, std::memory_order_relaxed);
      publish(session.session_id, destination, {},
              contract::inspected_protocol::none,
              contract::inspection_action::failed, 0,
              static_cast<NTSTATUS>(status),
              0, {}, {});
    }
  }

  void run(ntl::net::kernel::wsk_tcp_listener &listener) noexcept {
    while (!stopping_.load(std::memory_order_acquire)) {
      reap_sessions();
      auto accepted = listener.accept();
      if (!accepted) {
        if (static_cast<NTSTATUS>(accepted.status()) == STATUS_IO_TIMEOUT)
          continue;
        if (!stopping_.load(std::memory_order_acquire))
          failed_.fetch_add(1, std::memory_order_relaxed);
        break;
      }
      accepted_.fetch_add(1, std::memory_order_relaxed);
      if (!try_acquire_session_slot()) {
        failed_.fetch_add(1, std::memory_order_relaxed);
        (*accepted)->stop();
        (void)(*accepted)->drain();
        continue;
      }
      const std::uint64_t session_id =
          next_session_id_.fetch_add(1, std::memory_order_relaxed);
      auto redirect =
          ntl::net::kernel::wsk_redirected_connection::capture(**accepted);
      if (!redirect) {
        release_session_slot();
        failed_.fetch_add(1, std::memory_order_relaxed);
        publish(session_id, {}, {}, contract::inspected_protocol::none,
                contract::inspection_action::failed, 0,
                static_cast<NTSTATUS>(redirect.status()), 0, {}, {});
        continue;
      }
      std::unique_ptr<session_context> session;
      try {
        session = std::make_unique<session_context>(
            this, session_id, std::move(*accepted), std::move(*redirect));
      } catch (...) {
        release_session_slot();
        failed_.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      auto thread = ntl::system_thread::create(&session_entry, session.get());
      if (!thread) {
        release_session_slot();
        failed_.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      session->thread = std::move(*thread);
      {
        fast_mutex_guard guard(session_lock_);
        sessions_.push_back(std::move(session));
      }
    }
    reap_sessions();
  }

  static constexpr std::size_t worker_count = 2;
  static constexpr std::size_t maximum_sessions = 64;
  ntl::net::kernel::schannel schannel_{};
  std::shared_ptr<ntl::net::kernel::wsk_provider> provider_{};
  std::shared_ptr<ntl::net::kernel::redirected_tls_server_identity_provider>
      identity_provider_{};
  std::shared_ptr<
      ntl::net::kernel::redirected_tls_origin_security_provider>
      origin_security_provider_{};
  std::shared_ptr<ntl::net::kernel::redirected_tls_session_observer>
      session_events_{};
  std::shared_ptr<ntl::net::kernel::standard_redirected_tls_inspection>
      dispatcher_{};
  ntl::net::kernel::wsk_tcp_listener::pointer listener_v4_{};
  ntl::net::kernel::wsk_tcp_listener::pointer listener_v6_{};
  std::array<worker_context, worker_count> worker_contexts_{};
  std::array<ntl::system_thread, worker_count> workers_{};
  FAST_MUTEX configuration_transaction_lock_{};
  FAST_MUTEX identity_lock_{};
  FAST_MUTEX inspection_lock_{};
  FAST_MUTEX identity_request_lock_{};
  FAST_MUTEX session_lock_{};
  FAST_MUTEX origin_security_lock_{};
  std::vector<identity_entry> identities_{};
  std::vector<origin_security_entry> origin_security_{};
  std::vector<std::unique_ptr<session_context>> sessions_{};
  std::shared_ptr<ntl::net::kernel::schannel_credentials> client_credentials_{};
  std::array<contract::inspection_record, 64> inspections_{};
  std::array<contract::identity_request, 64> identity_requests_ring_{};
  std::uint64_t inspection_sequence_ = 0;
  std::uint64_t identity_request_sequence_ = 0;
  std::uint32_t process_id_ = 0;
  std::atomic<std::uint64_t> next_session_id_{1};
  std::atomic<std::uint64_t> accepted_{0};
  std::atomic<std::uint64_t> handshaken_{0};
  std::atomic<std::uint64_t> origin_connected_{0};
  std::atomic<std::uint64_t> origin_completed_{0};
  std::atomic<std::uint64_t> origin_peer_validated_{0};
  std::atomic<std::uint64_t> permitted_{0};
  std::atomic<std::uint64_t> blocked_{0};
  std::atomic<std::uint64_t> transformed_{0};
  std::atomic<std::uint64_t> failed_{0};
  std::atomic<std::uint64_t> identity_requests_{0};
  std::atomic<std::uint64_t> identity_timeouts_{0};
  std::atomic<std::uint64_t> capture_dropped_{0};
  std::atomic<std::size_t> active_sessions_{0};
  std::atomic<bool> ready_{false};
  std::atomic<bool> workspace_lifetime_passed_{false};
  std::atomic<bool> stopping_{false};
};

} // namespace crtsys::wfp_kernel_browser_https::driver
