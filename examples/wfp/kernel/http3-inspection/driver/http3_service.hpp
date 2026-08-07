#pragma once

#include <ntddk.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ntl/net/borrowed_memory_resource>
#include <ntl/net/http3/inspection_proxy>
#include <ntl/net/http3/msquic_backend>
#include <ntl/net/http3/proxy_connection>
#include <ntl/net/kernel/content_codecs>
#include <ntl/net/kernel/executor>
#include <ntl/net/kernel/msquic>

#include "http3_inspection_contract.hpp"
#include "http3_inspection_policy.hpp"

namespace crtsys::wfp_kernel_http3::driver {

namespace contract = wfp_kernel_http3_inspection;
using backend_connection = ntl::net::http3::msquic_backend::connection;
using borrowed_accepted_connection =
    ntl::net::http3::msquic_backend::borrowed_accepted_connection;

class fast_mutex_guard {
public:
  explicit fast_mutex_guard(FAST_MUTEX &value) noexcept : value_(&value) {
    ExAcquireFastMutex(value_);
  }
  fast_mutex_guard(const fast_mutex_guard &) = delete;
  fast_mutex_guard &operator=(const fast_mutex_guard &) = delete;
  ~fast_mutex_guard() { ExReleaseFastMutex(value_); }

private:
  FAST_MUTEX *value_ = nullptr;
};

class http3_service;

class service_listener_sink final
    : public ntl::net::kernel::msquic_listener_sink {
public:
  explicit service_listener_sink(
      std::weak_ptr<http3_service> owner) noexcept
      : owner_(std::move(owner)) {}

  ntl::status on_connection(
      borrowed_accepted_connection indication) noexcept override;

private:
  std::weak_ptr<http3_service> owner_;
};

class service_observer final
    : public ntl::net::http3::proxy_connection_observer {
public:
  explicit service_observer(
      std::weak_ptr<http3_service> owner) noexcept
      : owner_(std::move(owner)) {}

  void on_qpack_stream_resumed(std::uint64_t) noexcept override;
  void on_webtransport_session_opened(std::uint64_t) noexcept override;
  void on_webtransport_payload(
      const ntl::net::http3::webtransport::payload &payload) noexcept override;
  void on_webtransport_reset(
      std::uint64_t, std::uint32_t) noexcept override;
  void on_exchange_complete(
      std::uint64_t,
      const ntl::net::http::request_message &request,
      const ntl::net::http::response_message &response,
      bool terminal) noexcept override;
  void on_closed(NTSTATUS status) noexcept override;

private:
  std::weak_ptr<http3_service> owner_;
};

class http3_service final
    : public std::enable_shared_from_this<http3_service> {
public:
  http3_service() noexcept {
    ExInitializeFastMutex(&connection_lock_);
    ExInitializeFastMutex(&capture_lock_);
  }

  ~http3_service() { shutdown(); }

  ntl::status configure(
      const contract::certificate_config &certificate) noexcept {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
        ready_.load(std::memory_order_acquire))
      return STATUS_DEVICE_BUSY;
    try {
      connections_.reserve(maximum_connections);
      auto policy = crtsys::examples::wfp::http3_inspection::
          make_ordinary_policy();
      auto origin = std::make_shared<
          crtsys::examples::wfp::http3_inspection::ordinary_origin>(
              policy->content_encoders());
      auto async_origin = std::make_shared<
          ntl::net::http3::immediate_origin_transport_adapter>(origin);
      auto terminals = std::make_shared<
          crtsys::examples::wfp::http3_inspection::
              ordinary_terminal_responses>();

      auto provider = ntl::net::kernel::msquic_provider::try_open(
          {.module_id = contract::msquic_module_id,
           .registration_timeout_milliseconds = 10'000});
      if (!provider)
        return provider.status();
      auto registration = ntl::net::kernel::msquic_registration::try_open(
          *provider,
          {.application_name = "crtsys-kernel-http3-inspection"});
      if (!registration)
        return registration.status();

      QUIC_SETTINGS settings = transport_settings();
      constexpr std::array<std::string_view, 1> protocols{"h3"};
      auto configuration =
          ntl::net::kernel::msquic_configuration::try_open(
              *registration, protocols, &settings);
      if (!configuration)
        return configuration.status();
      const ntl::status credentials =
          configuration->load_server_certificate(
              certificate.sha1_thumbprint, "MY", false);
      if (!credentials.is_ok())
        return credentials;

      auto listener_sink =
          std::make_shared<service_listener_sink>(weak_from_this());
      QUIC_ADDR address{};
      QuicAddrSetFamily(&address, QUIC_ADDRESS_FAMILY_UNSPEC);
      QuicAddrSetPort(&address, 0);
      auto listener = ntl::net::kernel::msquic_listener::try_listen(
          *registration, std::move(listener_sink), protocols, &address);
      if (!listener)
        return listener.status();
      const auto local = listener->local_address();
      if (!local || QuicAddrGetPort(&*local) == 0) {
        listener->close();
        return local ? ntl::status{STATUS_INVALID_ADDRESS}
                     : local.status();
      }

      provider_ = std::move(*provider);
      registration_ = std::move(*registration);
      configuration_ = std::move(*configuration);
      policy_ = std::move(policy);
      origin_ = std::move(async_origin);
      terminal_responses_ = std::move(terminals);
      listener_ = std::move(*listener);
      port_ = QuicAddrGetPort(&*local);
      ready_.store(true, std::memory_order_release);
      return ntl::status::ok();
    } catch (const std::bad_alloc &) {
      listener_.close();
      return STATUS_INSUFFICIENT_RESOURCES;
    } catch (...) {
      listener_.close();
      return STATUS_UNHANDLED_EXCEPTION;
    }
  }

  ntl::status on_connection(
      borrowed_accepted_connection indication) noexcept {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
        !ready_.load(std::memory_order_acquire) || !configuration_)
      return STATUS_DEVICE_NOT_READY;
    const auto information = indication.information();
    reap_closed_connections();
    const std::size_t slot =
        connection_slots_.fetch_add(1, std::memory_order_acq_rel);
    if (slot >= maximum_connections) {
      connection_slots_.fetch_sub(1, std::memory_order_acq_rel);
      return STATUS_QUOTA_EXCEEDED;
    }
    update_peak_connections(slot + 1);

    ntl::status factory_status{STATUS_PENDING};
    std::shared_ptr<ntl::net::http3::proxy_connection> proxy;
    auto accepted = backend_connection::try_accept_with_sink(
        configuration_.make_connection_context(), std::move(indication),
        [this, &information, &proxy, &factory_status](
            std::shared_ptr<ntl::net::quic::transport_backend> backend)
            -> std::shared_ptr<ntl::net::quic::backend_sink> {
          const std::string_view indicated_name = information.server_name;
          auto observer =
              std::make_shared<service_observer>(weak_from_this());
          auto webtransport =
              crtsys::examples::wfp::http3_inspection::
                  make_webtransport_policy();
          auto memory = std::make_shared<
              ntl::net::bounded_memory_resource>(
                  ntl::net::bounded_memory_limits{
                      .maximum_allocated_bytes = 512 * 1024,
                      .maximum_single_allocation = 128 * 1024});
          auto created = ntl::net::http3::proxy_connection::create(
              std::move(backend), origin_, policy_,
              ntl::net::http::inspection_session_metadata{
                  .tls = {
                      .server_name = indicated_name.empty()
                                         ? std::optional<std::string>(
                                               "localhost")
                                         : std::optional<std::string>(
                                               indicated_name),
                      .alpn = "h3"}},
              std::move(observer), std::move(webtransport),
              std::make_shared<
                  ntl::net::http3::webtransport_echo_handler>(),
              proxy_limits(),
              webtransport_limits(), terminal_responses_, std::move(memory));
          factory_status = created ? ntl::status::ok() : created.status();
          if (!created)
            return {};
          proxy = std::move(*created);
          return std::static_pointer_cast<
              ntl::net::quic::backend_sink>(proxy);
        },
        backend_limits());
    if (!accepted || !proxy) {
      connection_slots_.fetch_sub(1, std::memory_order_acq_rel);
      return accepted ? factory_status : accepted.status();
    }
    try {
      fast_mutex_guard guard(connection_lock_);
      connections_.push_back(
          connection_record{std::move(*accepted), std::move(proxy)});
    } catch (...) {
      (*accepted)->stop();
      (void)(*accepted)->drain();
      connection_slots_.fetch_sub(1, std::memory_order_acq_rel);
      return STATUS_INSUFFICIENT_RESOURCES;
    }
    accepted_.fetch_add(1, std::memory_order_relaxed);
    return ntl::status::ok();
  }

  contract::service_info snapshot() noexcept {
    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
      reap_closed_connections();
    return {port_,
            0,
            ready_.load(std::memory_order_acquire) ? 1u : 0u,
            wfp_ipv4_.load(std::memory_order_relaxed),
            wfp_ipv6_.load(std::memory_order_relaxed),
            accepted_.load(std::memory_order_relaxed),
            permitted_.load(std::memory_order_relaxed),
            blocked_.load(std::memory_order_relaxed),
            failed_.load(std::memory_order_relaxed),
            qpack_resumed_.load(std::memory_order_relaxed),
            gzip_responses_.load(std::memory_order_relaxed),
            deflate_responses_.load(std::memory_order_relaxed),
            brotli_responses_.load(std::memory_order_relaxed),
            webtransport_sessions_.load(std::memory_order_relaxed),
            webtransport_bidirectional_.load(std::memory_order_relaxed),
            webtransport_unidirectional_.load(std::memory_order_relaxed),
            webtransport_datagrams_.load(std::memory_order_relaxed),
            webtransport_capsules_.load(std::memory_order_relaxed),
            webtransport_resets_.load(std::memory_order_relaxed),
            connection_slots_.load(std::memory_order_relaxed),
            peak_connections_.load(std::memory_order_relaxed),
            reaped_connections_.load(std::memory_order_relaxed)};
  }

  void capture(contract::inspection_record &result) noexcept {
    fast_mutex_guard guard(capture_lock_);
    result = capture_;
  }

  void publish(
      const ntl::net::http::request_message &request,
      const ntl::net::http::response_message &response,
      bool terminal) noexcept {
    try {
      std::string request_text;
      request_text.reserve(256 + request.body.size());
      request_text.append(request.method);
      request_text.push_back(' ');
      request_text.append(request.path);
      request_text.append(" HTTP/3\r\n");
      for (const auto &field : request.headers.fields()) {
        request_text.append(field.name);
        request_text.append(": ");
        request_text.append(field.value);
        request_text.append("\r\n");
      }
      request_text.append("\r\n");
      request_text.append(
          reinterpret_cast<const char *>(request.body.data()),
          request.body.size());

      fast_mutex_guard guard(capture_lock_);
      ++capture_.sequence;
      capture_.status = response.status;
      capture_.request.fill(std::byte{});
      capture_.response.fill(std::byte{});
      const std::size_t request_size =
          (std::min)(request_text.size(), contract::maximum_capture_size);
      const std::size_t response_size =
          (std::min)(response.body.size(), contract::maximum_capture_size);
      capture_.request_size = static_cast<std::uint32_t>(request_size);
      capture_.response_size = static_cast<std::uint32_t>(response_size);
      std::memcpy(capture_.request.data(), request_text.data(), request_size);
      std::memcpy(capture_.response.data(), response.body.data(), response_size);
      (terminal || response.status == 403 ? blocked_ : permitted_)
          .fetch_add(1, std::memory_order_relaxed);
      const auto coding = response.headers.first("content-encoding");
      if (coding)
        record_content_encoding(*coding);
    } catch (...) {
      record_failure();
    }
  }

  void record_failure() noexcept {
    failed_.fetch_add(1, std::memory_order_relaxed);
  }
  void record_wfp_v4() noexcept {
    wfp_ipv4_.fetch_add(1, std::memory_order_relaxed);
  }
  void record_wfp_v6() noexcept {
    wfp_ipv6_.fetch_add(1, std::memory_order_relaxed);
  }
  void record_qpack_resume() noexcept {
    qpack_resumed_.fetch_add(1, std::memory_order_relaxed);
  }
  void record_content_encoding(std::string_view value) noexcept {
    if (value == "gzip")
      gzip_responses_.fetch_add(1, std::memory_order_relaxed);
    else if (value == "deflate")
      deflate_responses_.fetch_add(1, std::memory_order_relaxed);
    else if (value == "br")
      brotli_responses_.fetch_add(1, std::memory_order_relaxed);
  }
  void record_webtransport_session() noexcept {
    webtransport_sessions_.fetch_add(1, std::memory_order_relaxed);
  }
  void record_webtransport_payload(
      const ntl::net::http3::webtransport::payload &payload) noexcept {
    using kind = ntl::net::http3::webtransport::payload_kind;
    using direction = ntl::net::http3::webtransport::stream_direction;
    if (payload.kind == kind::datagram)
      webtransport_datagrams_.fetch_add(1, std::memory_order_relaxed);
    else if (payload.kind == kind::capsule)
      webtransport_capsules_.fetch_add(1, std::memory_order_relaxed);
    else if (payload.direction == direction::bidirectional)
      webtransport_bidirectional_.fetch_add(1, std::memory_order_relaxed);
    else
      webtransport_unidirectional_.fetch_add(1, std::memory_order_relaxed);
  }
  void record_webtransport_reset() noexcept {
    webtransport_resets_.fetch_add(1, std::memory_order_relaxed);
  }

  void connection_closed() noexcept {
    bool expected = false;
    if (!reap_scheduled_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
      return;
    auto owner = weak_from_this().lock();
    if (!owner) {
      reap_scheduled_.store(false, std::memory_order_release);
      return;
    }
    const ntl::status queued = reaper_.post(
        owner, &http3_service::run_reaper);
    if (!queued.is_ok())
      reap_scheduled_.store(false, std::memory_order_release);
  }

  void shutdown() noexcept {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
      return;
    if (!ready_.exchange(false, std::memory_order_acq_rel))
      return;
    listener_.close();
    std::vector<connection_record> connections;
    {
      fast_mutex_guard guard(connection_lock_);
      connections.swap(connections_);
    }
    for (auto &connection : connections)
      connection.proxy->close();
    reaper_.stop_accepting();
    (void)reaper_.drain();
    for (auto &connection : connections) {
      const ntl::status drained = connection.backend->drain();
      if (!drained.is_ok())
        connection.backend->drain_exact();
    }
    connections.clear();
    connection_slots_.store(0, std::memory_order_release);
    terminal_responses_.reset();
    origin_.reset();
    policy_.reset();
    configuration_.close();
    registration_.shutdown(0, true);
    registration_.close();
    provider_.close();
    port_ = 0;
  }

private:
  static constexpr std::size_t maximum_connections = 64;

  struct connection_record {
    std::shared_ptr<backend_connection> backend;
    std::shared_ptr<ntl::net::http3::proxy_connection> proxy;
  };

  static QUIC_SETTINGS transport_settings() noexcept {
    QUIC_SETTINGS settings{};
    settings.PeerBidiStreamCount = 64;
    settings.IsSet.PeerBidiStreamCount = TRUE;
    settings.PeerUnidiStreamCount = 8;
    settings.IsSet.PeerUnidiStreamCount = TRUE;
    settings.DatagramReceiveEnabled = TRUE;
    settings.IsSet.DatagramReceiveEnabled = TRUE;
#ifdef QUIC_API_ENABLE_PREVIEW_FEATURES
    settings.ReliableResetEnabled = TRUE;
    settings.IsSet.ReliableResetEnabled = TRUE;
#endif
    settings.IdleTimeoutMs = 30'000;
    settings.IsSet.IdleTimeoutMs = TRUE;
    return settings;
  }

  static ntl::net::http3::proxy_connection_limits proxy_limits() noexcept {
    return {.maximum_concurrent_request_streams = 32,
            .maximum_buffered_bytes_per_stream = 64 * 1024,
            .maximum_aggregate_body_bytes = 256 * 1024,
            .maximum_frame_payload = 64 * 1024,
            .maximum_decoded_header_bytes = 16 * 1024,
            .maximum_control_stream_bytes = 4096,
            .maximum_extension_stream_bytes = 64 * 1024,
            .maximum_concurrent_extension_streams = 32,
            .maximum_aggregate_extension_stream_bytes = 256 * 1024,
            .maximum_capsule_wire_bytes = 64 * 1024,
            .maximum_blocked_streams = 8,
            .maximum_concurrent_webtransport_sessions = 8,
            .qpack_table_capacity = 256,
            .require_http3_origin = true,
            .require_server_name_authority_binding = true,
            .enable_webtransport = true};
  }

  static ntl::net::http3::webtransport::session_limits
  webtransport_limits() noexcept {
    return {.maximum_bidirectional_streams = 8,
            .maximum_unidirectional_streams = 8,
            .maximum_stream_data = 64 * 1024,
            .maximum_datagram_payload = 4096,
            .maximum_datagrams = 32};
  }

  static ntl::net::http3::msquic_backend::connection_limits
  backend_limits() noexcept {
    return {.maximum_streams = 128,
            .maximum_receive_indication = 128 * 1024,
            .maximum_send_size = 128 * 1024,
            .maximum_prefix_bytes = 8,
            .shutdown_timeout = std::chrono::seconds(10)};
  }

  static void run_reaper(http3_service &self) noexcept {
    for (;;) {
      self.reap_closed_connections();
      self.reap_scheduled_.store(false, std::memory_order_release);
      if (!self.has_closed_connections())
        return;
      bool expected = false;
      if (!self.reap_scheduled_.compare_exchange_strong(
              expected, true, std::memory_order_acq_rel))
        return;
    }
  }

  bool has_closed_connections() noexcept {
    fast_mutex_guard guard(connection_lock_);
    return std::any_of(
        connections_.begin(), connections_.end(),
        [](const auto &connection) { return connection.proxy->closed(); });
  }

  void reap_closed_connections() noexcept {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
      return;
    for (;;) {
      connection_record closed;
      {
        fast_mutex_guard guard(connection_lock_);
        const auto found = std::find_if(
            connections_.begin(), connections_.end(),
            [](const auto &connection) { return connection.proxy->closed(); });
        if (found == connections_.end())
          return;
        closed = std::move(*found);
        connections_.erase(found);
      }
      const ntl::status drained = closed.backend->drain();
      if (!drained.is_ok())
        closed.backend->drain_exact();
      closed = {};
      std::size_t slots =
          connection_slots_.load(std::memory_order_acquire);
      while (slots != 0 &&
             !connection_slots_.compare_exchange_weak(
                 slots, slots - 1, std::memory_order_acq_rel,
                 std::memory_order_acquire)) {
      }
      if (slots == 0) {
        NT_ASSERT(false);
        return;
      }
      reaped_connections_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  void update_peak_connections(std::size_t value) noexcept {
    std::uint64_t peak = peak_connections_.load(std::memory_order_relaxed);
    while (peak < value &&
           !peak_connections_.compare_exchange_weak(
               peak, value, std::memory_order_relaxed)) {
    }
  }

  ntl::net::kernel::msquic_provider provider_{};
  ntl::net::kernel::msquic_registration registration_{};
  ntl::net::kernel::msquic_configuration configuration_{};
  ntl::net::kernel::msquic_listener listener_{};
  ntl::net::kernel::executor reaper_{};
  std::shared_ptr<const ntl::net::http::inspection_policy> policy_;
  std::shared_ptr<ntl::net::http3::async_origin_transport> origin_;
  std::shared_ptr<ntl::net::http3::proxy_terminal_response_provider>
      terminal_responses_;
  FAST_MUTEX connection_lock_{};
  FAST_MUTEX capture_lock_{};
  std::vector<connection_record> connections_;
  contract::inspection_record capture_{};
  std::uint16_t port_ = 0;
  std::atomic<bool> ready_{false};
  std::atomic<std::uint64_t> wfp_ipv4_{0};
  std::atomic<std::uint64_t> wfp_ipv6_{0};
  std::atomic<std::uint64_t> accepted_{0};
  std::atomic<std::uint64_t> permitted_{0};
  std::atomic<std::uint64_t> blocked_{0};
  std::atomic<std::uint64_t> failed_{0};
  std::atomic<std::uint64_t> qpack_resumed_{0};
  std::atomic<std::uint64_t> gzip_responses_{0};
  std::atomic<std::uint64_t> deflate_responses_{0};
  std::atomic<std::uint64_t> brotli_responses_{0};
  std::atomic<std::uint64_t> webtransport_sessions_{0};
  std::atomic<std::uint64_t> webtransport_bidirectional_{0};
  std::atomic<std::uint64_t> webtransport_unidirectional_{0};
  std::atomic<std::uint64_t> webtransport_datagrams_{0};
  std::atomic<std::uint64_t> webtransport_capsules_{0};
  std::atomic<std::uint64_t> webtransport_resets_{0};
  std::atomic<std::size_t> connection_slots_{0};
  std::atomic<std::uint64_t> peak_connections_{0};
  std::atomic<std::uint64_t> reaped_connections_{0};
  std::atomic<bool> reap_scheduled_{false};
};

inline ntl::status service_listener_sink::on_connection(
    borrowed_accepted_connection indication) noexcept {
  const auto owner = owner_.lock();
  return owner ? owner->on_connection(std::move(indication))
               : ntl::status{STATUS_DELETE_PENDING};
}

inline void service_observer::on_qpack_stream_resumed(
    std::uint64_t) noexcept {
  if (const auto owner = owner_.lock())
    owner->record_qpack_resume();
}

inline void service_observer::on_webtransport_session_opened(
    std::uint64_t) noexcept {
  if (const auto owner = owner_.lock())
    owner->record_webtransport_session();
}

inline void service_observer::on_webtransport_payload(
    const ntl::net::http3::webtransport::payload &payload) noexcept {
  if (const auto owner = owner_.lock())
    owner->record_webtransport_payload(payload);
}

inline void service_observer::on_webtransport_reset(
    std::uint64_t, std::uint32_t) noexcept {
  if (const auto owner = owner_.lock())
    owner->record_webtransport_reset();
}

inline void service_observer::on_exchange_complete(
    std::uint64_t,
    const ntl::net::http::request_message &request,
    const ntl::net::http::response_message &response,
    bool terminal) noexcept {
  if (const auto owner = owner_.lock())
    owner->publish(request, response, terminal);
}

inline void service_observer::on_closed(NTSTATUS status) noexcept {
  if (const auto owner = owner_.lock()) {
    if (!NT_SUCCESS(status))
      owner->record_failure();
    owner->connection_closed();
  }
}

} // namespace crtsys::wfp_kernel_http3::driver
