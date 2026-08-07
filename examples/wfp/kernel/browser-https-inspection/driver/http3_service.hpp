#pragma once

#include <ntddk.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ntl/net/borrowed_memory_resource>
#include <ntl/net/http3/msquic_backend>
#include <ntl/net/http3/proxy_connection>
#include <ntl/net/kernel/content_codecs>
#include <ntl/net/kernel/executor>
#include <ntl/net/kernel/http3_origin_client>
#include <ntl/net/kernel/http3_origin_pool>
#include <ntl/net/kernel/http_origin_fallback>
#include <ntl/net/kernel/msquic>
#include <ntl/net/kernel/wsk_datagram_relay>
#include <ntl/wfp/transparent_udp_proxy>

#include "browser_https_inspection_contract.hpp"
#include "tcp_service.hpp"

namespace crtsys::wfp_kernel_browser_https::driver {

namespace contract = wfp_kernel_browser_https_inspection;
using http3_backend_connection =
    ntl::net::http3::msquic_backend::connection;
using borrowed_accepted_connection =
    ntl::net::http3::msquic_backend::borrowed_accepted_connection;
using ntl::net::kernel::http3_origin_allocation_budget;
using ntl::net::kernel::http3_origin_attempt;
using ntl::net::kernel::http3_origin_exchange_options;
using ntl::net::kernel::http3_origin_failure_kind;
using ntl::net::kernel::http3_origin_result;
using ntl::net::kernel::http_origin_fallback_options;
using ntl::net::kernel::kernel_http3_origin_exchange;
using ntl::net::kernel::kernel_http_origin_fallback_exchange;
namespace http_origin_fallback_detail =
    ntl::net::kernel::http_origin_fallback_detail;

inline constexpr std::size_t maximum_http3_connections = 16;
inline constexpr std::size_t maximum_http3_request_streams_per_connection =
    16;
inline constexpr std::size_t maximum_http3_inspector_bytes_per_stream =
    64 * 1024;
inline constexpr std::size_t maximum_http3_request_body_bytes = 512 * 1024;
inline constexpr std::size_t maximum_http3_buffered_request_bytes =
    2 * 1024 * 1024;
inline constexpr std::size_t maximum_http3_pending_requests = 32;
static_assert(maximum_http3_request_streams_per_connection <=
              (std::numeric_limits<std::size_t>::max)() /
                  maximum_http3_inspector_bytes_per_stream);
inline constexpr std::size_t maximum_http3_inspector_bytes_per_connection =
    maximum_http3_request_streams_per_connection *
    maximum_http3_inspector_bytes_per_stream;
static_assert(maximum_http3_connections <=
              (std::numeric_limits<std::size_t>::max)() /
                  maximum_http3_inspector_bytes_per_connection);
inline constexpr std::size_t maximum_http3_inspector_bytes_process_wide =
    maximum_http3_connections * maximum_http3_inspector_bytes_per_connection;
static_assert(maximum_http3_inspector_bytes_process_wide <=
              16 * 1024 * 1024);

inline ntl::result<ntl::net::http::endpoint_metadata>
make_http_endpoint_metadata(
    const ntl::wfp::transparent_udp_endpoint &endpoint) noexcept {
  if (!endpoint.valid())
    return ntl::unexpected(STATUS_INVALID_ADDRESS);
  std::array<char, 65> text{};
  if (endpoint.family == AF_INET) {
    IN_ADDR address{};
    std::memcpy(&address, endpoint.address.data(), 4);
    if (!RtlIpv4AddressToStringA(&address, text.data()))
      return ntl::unexpected(STATUS_INVALID_ADDRESS);
  } else {
    IN6_ADDR address{};
    std::memcpy(&address, endpoint.address.data(), 16);
    if (!RtlIpv6AddressToStringA(&address, text.data()))
      return ntl::unexpected(STATUS_INVALID_ADDRESS);
  }
  try {
    return ntl::ok(ntl::net::http::endpoint_metadata{
        .address = std::string(text.data()), .port = endpoint.port});
  } catch (const std::bad_alloc &) {
    return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
  } catch (...) {
    return ntl::unexpected(STATUS_UNHANDLED_EXCEPTION);
  }
}

inline ntl::result<ntl::wfp::transparent_udp_endpoint>
make_transparent_udp_endpoint(
    const ntl::net::kernel::ip_endpoint &endpoint) noexcept {
  if (!endpoint)
    return ntl::unexpected(STATUS_INVALID_ADDRESS);
  SOCKADDR_INET native{};
  if (endpoint.length() > sizeof(native))
    return ntl::unexpected(STATUS_INVALID_ADDRESS);
  std::memcpy(&native, endpoint.borrowed_native_address(), endpoint.length());
  const auto converted =
      ntl::wfp::transparent_udp_endpoint::from_native(native);
  return converted ? ntl::ok(*converted)
                   : ntl::unexpected(STATUS_INVALID_ADDRESS);
}

inline ntl::result<ntl::net::http::inspection_session_metadata>
make_http3_session_metadata(
    std::uint64_t session_id, std::string_view server_name,
    const ntl::wfp::transparent_udp_endpoint &accepted_source,
    const ntl::wfp::transparent_udp_endpoint &accepted_destination,
    const ntl::wfp::transparent_udp_original_flow &original) noexcept {
  auto source = make_http_endpoint_metadata(accepted_source);
  auto destination = make_http_endpoint_metadata(accepted_destination);
  auto original_source = make_http_endpoint_metadata(original.source);
  auto original_destination =
      make_http_endpoint_metadata(original.destination);
  if (!source)
    return ntl::unexpected(source.status());
  if (!destination)
    return ntl::unexpected(destination.status());
  if (!original_source)
    return ntl::unexpected(original_source.status());
  if (!original_destination)
    return ntl::unexpected(original_destination.status());
  try {
    return ntl::ok(ntl::net::http::inspection_session_metadata{
        .connection =
            {.connection_id = session_id,
             .source = std::move(*source),
             .destination = std::move(*destination),
             .original_source = std::move(*original_source),
             .original_destination = std::move(*original_destination)},
        .tls = {.server_name = std::string(server_name), .alpn = "h3"}});
  } catch (const std::bad_alloc &) {
    return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
  } catch (...) {
    return ntl::unexpected(STATUS_UNHANDLED_EXCEPTION);
  }
}

class http3_service;

class browser_http3_observer final
    : public ntl::net::http3::proxy_connection_observer {
public:
  browser_http3_observer(std::weak_ptr<http3_service> owner,
                         std::uint64_t session_id,
                         std::string server_name) noexcept
      : owner_(std::move(owner)), session_id_(session_id),
        server_name_(std::move(server_name)) {}

  void on_qpack_stream_resumed(std::uint64_t) noexcept override;
  void on_webtransport_session_opened(std::uint64_t) noexcept override;
  void on_webtransport_session_closed(
      std::uint64_t, NTSTATUS) noexcept override;
  void on_webtransport_payload(
      const ntl::net::http3::webtransport::payload &payload) noexcept override;
  void on_webtransport_reset(std::uint64_t,
                             std::uint32_t) noexcept override;
  void on_stream_rejected(std::uint64_t,
                          NTSTATUS status) noexcept override;
  void on_stream_cancelled(std::uint64_t) noexcept override;
  void on_exchange_complete(
      std::uint64_t,
      const ntl::net::http::request_message &request,
      const ntl::net::http::response_message &response,
      bool terminal) noexcept override;
  void on_closed(NTSTATUS status) noexcept override;

private:
  std::weak_ptr<http3_service> owner_;
  std::uint64_t session_id_ = 0;
  std::string server_name_;
};

class browser_http3_terminal_responses final
    : public ntl::net::http3::proxy_terminal_response_provider {
public:
  ntl::result<ntl::net::http::response_message>
  response_for(
      const ntl::net::http3::proxy_terminal_context &context) noexcept override {
    try {
      if (context.reason ==
          ntl::net::http3::proxy_terminal_reason::origin_unavailable) {
        ntl::net::http::response_message response;
        response.wire_protocol = context.request.wire_protocol;
        response.status = 502;
        response.headers.append(
            "content-type", "text/plain; charset=utf-8");
        constexpr std::string_view body =
            "validated origin transport unavailable\n";
        response.body.assign(
            reinterpret_cast<const std::byte *>(body.data()),
            reinterpret_cast<const std::byte *>(body.data() + body.size()));
        return ntl::ok(std::move(response));
      }
      auto response =
          crtsys::wfp_browser_http_policy::blocked_response(
              ntl::net::http::protocol::http3);
      response.wire_protocol = context.request.wire_protocol;
      return ntl::ok(std::move(response));
    } catch (const std::bad_alloc &) {
      return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return ntl::unexpected(STATUS_UNHANDLED_EXCEPTION);
    }
  }
};

class http3_service_listener_sink final
    : public ntl::net::kernel::msquic_listener_sink {
public:
  explicit http3_service_listener_sink(
      std::weak_ptr<http3_service> owner) noexcept
      : owner_(std::move(owner)) {}

  ntl::status on_connection(
      borrowed_accepted_connection indication) noexcept override;

private:
  std::weak_ptr<http3_service> owner_;
};

struct http3_origin_target {
  std::string host;
  std::uint16_t port = 443;
};

struct http3_server_identity {
  http3_server_identity(
      std::string name,
      ntl::net::kernel::msquic_configuration &&value) noexcept
      : server_name(std::move(name)), configuration(std::move(value)) {}

  std::string server_name;
  ntl::net::kernel::msquic_configuration configuration;
};

struct http3_origin_security {
  http3_origin_security(
      std::string name,
      ntl::net::kernel::msquic_configuration &&value,
      std::shared_ptr<ntl::net::kernel::schannel_credentials>
          fallback_credentials_value,
      std::shared_ptr<ntl::net::kernel::schannel_peer_certificate_policy>
          policy_value) noexcept
      : server_name(std::move(name)), configuration(std::move(value)),
        fallback_credentials(std::move(fallback_credentials_value)),
        policy(std::move(policy_value)) {}

  std::string server_name;
  ntl::net::kernel::msquic_configuration configuration;
  // The strict MsQuic configuration and the Schannel fallback credentials
  // are created from one origin_security_config and published through this
  // single owning object. `policy` is intentionally shared by both legs.
  std::shared_ptr<ntl::net::kernel::schannel_credentials>
      fallback_credentials;
  std::shared_ptr<ntl::net::kernel::schannel_peer_certificate_policy> policy;
};

inline bool ascii_equal_ci(std::string_view left,
                           std::string_view right) noexcept {
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index != left.size(); ++index) {
    const unsigned char a = static_cast<unsigned char>(left[index]);
    const unsigned char b = static_cast<unsigned char>(right[index]);
    const unsigned char lower_a =
        a >= 'A' && a <= 'Z' ? static_cast<unsigned char>(a + 0x20) : a;
    const unsigned char lower_b =
        b >= 'A' && b <= 'Z' ? static_cast<unsigned char>(b + 0x20) : b;
    if (lower_a != lower_b)
      return false;
  }
  return true;
}

inline ntl::result<http3_origin_target> parse_http3_authority(
    std::string_view authority, std::string_view expected_server_name) noexcept {
  try {
    if (authority.empty() || authority.find('@') != std::string_view::npos)
      return ntl::unexpected(STATUS_INVALID_ADDRESS);
    std::string_view host = authority;
    std::string_view port;
    if (authority.front() == '[') {
      const std::size_t close = authority.find(']');
      if (close == std::string_view::npos || close == 1)
        return ntl::unexpected(STATUS_INVALID_ADDRESS);
      host = authority.substr(1, close - 1);
      if (close + 1 != authority.size()) {
        if (authority[close + 1] != ':')
          return ntl::unexpected(STATUS_INVALID_ADDRESS);
        port = authority.substr(close + 2);
      }
    } else {
      const std::size_t colon = authority.rfind(':');
      if (colon != std::string_view::npos) {
        if (authority.find(':') != colon)
          return ntl::unexpected(STATUS_INVALID_ADDRESS);
        host = authority.substr(0, colon);
        port = authority.substr(colon + 1);
      }
    }
    if (host.empty() || !ascii_equal_ci(host, expected_server_name))
      return ntl::unexpected(STATUS_INVALID_ADDRESS);
    http3_origin_target result{.host = std::string(host), .port = 443};
    if (!port.empty()) {
      unsigned parsed = 0;
      const auto converted = std::from_chars(
          port.data(), port.data() + port.size(), parsed);
      if (converted.ec != std::errc{} ||
          converted.ptr != port.data() + port.size() || parsed == 0 ||
          parsed > 65535)
        return ntl::unexpected(STATUS_INVALID_ADDRESS);
      result.port = static_cast<std::uint16_t>(parsed);
    }
    return ntl::ok(std::move(result));
  } catch (const std::bad_alloc &) {
    return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
  } catch (...) {
    return ntl::unexpected(STATUS_UNHANDLED_EXCEPTION);
  }
}

class http3_service final
    : public std::enable_shared_from_this<http3_service> {
public:
  http3_service(std::shared_ptr<tcp_service> captures,
                ntl::wfp::transparent_udp_route_resolver udp_routes) noexcept
      : captures_(std::move(captures)), udp_routes_(std::move(udp_routes)),
        configuration_transaction_lock_(
                                  &captures_->configuration_transaction_lock()) {
    ExInitializeFastMutex(&configuration_lock_);
    ExInitializeFastMutex(&connection_lock_);
    KeInitializeEvent(&shutdown_event_, NotificationEvent, FALSE);
  }

  ~http3_service() { shutdown(); }

  ntl::status configure(
      const contract::certificate_config &certificate) noexcept {
    if (certificate.server_name_size == 0 ||
        certificate.server_name_size > contract::maximum_server_name_size ||
        certificate.server_name[certificate.server_name_size] != '\0')
      return STATUS_INVALID_PARAMETER;
    const std::string_view server_name(certificate.server_name.data(),
                                       certificate.server_name_size);
    std::shared_ptr<http3_server_identity> retired_identity;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
      return STATUS_INVALID_DEVICE_STATE;
    fast_mutex_guard configuration_guard(configuration_lock_);
    if (stopping_.load(std::memory_order_acquire))
      return STATUS_DELETE_PENDING;
    QUIC_SETTINGS settings = transport_settings();
    constexpr std::array<std::string_view, 1> protocols{"h3"};

    if (ready_.load(std::memory_order_acquire)) {
      auto configuration =
          ntl::net::kernel::msquic_configuration::try_open(
              registration_, protocols, &settings);
      if (!configuration)
        return configuration.status();
      const ntl::status loaded = configuration->load_server_certificate(
          certificate.sha1_thumbprint, "MY", false);
      if (!loaded.is_ok())
        return loaded;
      try {
        auto identity = std::make_shared<http3_server_identity>(
            std::string(server_name), std::move(*configuration));
        for (auto &entry : identities_) {
          if (ascii_equal_ci(entry->server_name, server_name)) {
            retired_identity = std::move(entry);
            entry = std::move(identity);
            return ntl::status::ok();
          }
        }
        if (identities_.size() >= contract::identity_cache_capacity)
          return STATUS_QUOTA_EXCEEDED;
        identities_.push_back(std::move(identity));
        return ntl::status::ok();
      } catch (const std::bad_alloc &) {
        return STATUS_INSUFFICIENT_RESOURCES;
      } catch (...) {
        return STATUS_UNHANDLED_EXCEPTION;
      }
    }

    try {
      connections_.reserve(maximum_connections);
      identities_.reserve(contract::identity_cache_capacity);
      origin_security_.reserve(contract::identity_cache_capacity);
    } catch (const std::bad_alloc &) {
      return STATUS_INSUFFICIENT_RESOURCES;
    } catch (...) {
      return STATUS_UNHANDLED_EXCEPTION;
    }
    auto provider = ntl::net::kernel::msquic_provider::try_open(
        {.module_id = contract::msquic_module_id,
         .registration_timeout_milliseconds = 10'000});
    if (!provider)
      return provider.status();
    auto registration = ntl::net::kernel::msquic_registration::try_open(
        *provider,
        {.application_name = "crtsys-kernel-browser-https-inspection"});
    if (!registration)
      return registration.status();
    auto configuration = ntl::net::kernel::msquic_configuration::try_open(
        *registration, protocols, &settings);
    if (!configuration)
      return configuration.status();
    const ntl::status credential_status =
        configuration->load_server_certificate(
            certificate.sha1_thumbprint, "MY", false);
    if (!credential_status.is_ok())
      return credential_status;
    auto client_configuration =
        ntl::net::kernel::msquic_configuration::try_open(
            *registration, protocols, &settings);
    if (!client_configuration)
      return client_configuration.status();
    const ntl::status client_credentials =
        client_configuration->load_client_credentials(true, true);
    if (!client_credentials.is_ok())
      return client_credentials;
    auto fallback_client = schannel_.try_client();
    if (!fallback_client)
      return fallback_client.status();
    std::shared_ptr<http3_server_identity> identity;
    std::shared_ptr<http3_origin_security> default_origin;
    try {
      identity = std::make_shared<http3_server_identity>(
          std::string(server_name), std::move(*configuration));
      auto fallback_credentials =
          std::make_shared<ntl::net::kernel::schannel_credentials>(
              std::move(*fallback_client));
      default_origin = std::make_shared<http3_origin_security>(
          std::string{}, std::move(*client_configuration),
          std::move(fallback_credentials), nullptr);
    } catch (const std::bad_alloc &) {
      return STATUS_INSUFFICIENT_RESOURCES;
    } catch (...) {
      return STATUS_UNHANDLED_EXCEPTION;
    }
    auto origin_pool = try_create_origin_pool();
    if (!origin_pool)
      return origin_pool.status();
    std::shared_ptr<http3_service_listener_sink> listener_sink;
    try {
      listener_sink = std::make_shared<http3_service_listener_sink>(
          weak_from_this());
    } catch (...) {
      return STATUS_INSUFFICIENT_RESOURCES;
    }
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
      return local ? ntl::status{STATUS_INVALID_ADDRESS} : local.status();
    }
    const std::uint16_t native_port = QuicAddrGetPort(&*local);
    ntl::net::kernel::wsk_provider relay_provider;
    const ntl::status relay_provider_opened = relay_provider.open();
    if (!relay_provider_opened.is_ok()) {
      listener->close();
      return relay_provider_opened;
    }
    auto relay = ntl::net::kernel::wsk_datagram_relay::try_create(
        std::move(relay_provider), native_port,
        {.maximum_flows = maximum_connections,
         .maximum_pending_datagrams = 1024,
         .socket = {.maximum_datagram_bytes = 65'535,
                    .maximum_datagrams_per_indication = 64}});
    if (!relay) {
      listener->close();
      return relay.status();
    }
    const std::uint16_t relay_port = relay->local_port();
    if (relay_port == 0) {
      relay->close();
      listener->close();
      return STATUS_INVALID_ADDRESS;
    }

    provider_ = std::move(*provider);
    registration_ = std::move(*registration);
    identities_.push_back(std::move(identity));
    default_origin_security_ = std::move(default_origin);
    origin_pool_ = std::move(*origin_pool);
    relay_ = std::move(*relay);
    listener_ = std::move(*listener);
    native_port_ = native_port;
    port_ = relay_port;
    ready_.store(true, std::memory_order_release);
    return ntl::status::ok();
  }

  ntl::status configure_origin_security(
      const contract::origin_security_config &input) noexcept {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
        input.server_name_size == 0 ||
        input.server_name_size > contract::maximum_server_name_size ||
        input.server_name[input.server_name_size] != '\0')
      return STATUS_INVALID_PARAMETER;
    const std::string_view server_name(input.server_name.data(),
                                       input.server_name_size);
    if (input.action == contract::origin_security_action::remove) {
      std::shared_ptr<http3_origin_security> retired;
      {
        fast_mutex_guard guard(configuration_lock_);
        const auto found = std::find_if(
            origin_security_.begin(), origin_security_.end(),
            [server_name](const auto &entry) noexcept {
              return ascii_equal_ci(entry->server_name, server_name);
            });
        if (found != origin_security_.end()) {
          retired = std::move(*found);
          origin_security_.erase(found);
        }
      }
      return ntl::status::ok();
    }
    if (input.action != contract::origin_security_action::install ||
        input.origin_leaf_der_size == 0 ||
        input.origin_leaf_der_size > input.origin_leaf_der.size())
      return STATUS_INVALID_PARAMETER;

    if (fail_next_origin_security_configuration_.exchange(
            false, std::memory_order_acq_rel))
      return STATUS_INSUFFICIENT_RESOURCES;

    auto fallback_reference =
        ntl::net::kernel::schannel_certificate_store_ref::make(
            input.client_sha1_thumbprint, L"MY");
    if (!fallback_reference)
      return fallback_reference.status();
    auto fallback_client = schannel_.try_client(
            {.manual_peer_validation = false,
             .use_default_client_certificate = false,
             .borrowed_certificate = &*fallback_reference});
    if (!fallback_client)
      return fallback_client.status();
    auto exact =
        ntl::net::kernel::schannel_exact_leaf_certificate_policy::try_create(
            std::span(input.origin_leaf_der)
                .first(input.origin_leaf_der_size),
            contract::maximum_certificate_der_size);
    if (!exact)
      return exact.status();
    std::shared_ptr<ntl::net::kernel::schannel_peer_certificate_policy>
        policy;
    std::shared_ptr<ntl::net::kernel::schannel_credentials>
        fallback_credentials;
    std::shared_ptr<http3_origin_security> retired_security;
    try {
      policy = std::make_shared<
          ntl::net::kernel::schannel_exact_leaf_certificate_policy>(
          std::move(*exact));
      fallback_credentials =
          std::make_shared<ntl::net::kernel::schannel_credentials>(
              std::move(*fallback_client));
    } catch (const std::bad_alloc &) {
      return STATUS_INSUFFICIENT_RESOURCES;
    } catch (...) {
      return STATUS_UNHANDLED_EXCEPTION;
    }

    fast_mutex_guard guard(configuration_lock_);
    if (!ready_.load(std::memory_order_acquire) || !registration_)
      return STATUS_DEVICE_NOT_READY;
    QUIC_SETTINGS settings = transport_settings();
    constexpr std::array<std::string_view, 1> protocols{"h3"};
    auto configuration = ntl::net::kernel::msquic_configuration::try_open(
        registration_, protocols, &settings);
    if (!configuration)
      return configuration.status();
    const ntl::status loaded = configuration->load_client_certificate(
        input.client_sha1_thumbprint, "MY", true, true);
    if (!loaded.is_ok())
      return loaded;
    try {
      auto state = std::make_shared<http3_origin_security>(
          std::string(server_name), std::move(*configuration),
          std::move(fallback_credentials), std::move(policy));
      for (auto &entry : origin_security_) {
        if (ascii_equal_ci(entry->server_name, server_name)) {
          retired_security = std::move(entry);
          entry = std::move(state);
          return ntl::status::ok();
        }
      }
      if (origin_security_.size() >= contract::identity_cache_capacity)
        return STATUS_QUOTA_EXCEEDED;
      origin_security_.push_back(std::move(state));
      return ntl::status::ok();
    } catch (const std::bad_alloc &) {
      return STATUS_INSUFFICIENT_RESOURCES;
    } catch (...) {
      return STATUS_UNHANDLED_EXCEPTION;
    }
  }

  void arm_origin_security_rollback_test() noexcept {
    fail_next_origin_security_configuration_.store(
        true, std::memory_order_release);
  }

  ntl::status on_connection(
      borrowed_accepted_connection indication) noexcept {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
        !ready_.load(std::memory_order_acquire) ||
        stopping_.load(std::memory_order_acquire))
      return STATUS_DEVICE_NOT_READY;
    reap_closed_connections();
    const auto information = indication.information();
    const std::string_view server_name = information.server_name;
    if (server_name.empty())
      return STATUS_INVALID_ADDRESS;
    if (!information.has_remote_address || !information.has_local_address)
      return STATUS_INVALID_ADDRESS;
    const ULONG native_length =
        information.remote_address.si_family == AF_INET
            ? sizeof(SOCKADDR_IN)
            : information.remote_address.si_family == AF_INET6
                  ? sizeof(SOCKADDR_IN6)
                  : 0;
    const auto upstream_peer = ntl::net::kernel::ip_endpoint::from_native(
        reinterpret_cast<const SOCKADDR *>(&information.remote_address),
        native_length);
    auto relay_client = relay_.resolve_client_endpoint(upstream_peer);
    if (!relay_client)
      return relay_client.status();
    auto accepted_source = make_transparent_udp_endpoint(*relay_client);
    if (!accepted_source)
      return accepted_source.status();
    ntl::wfp::transparent_udp_endpoint accepted_destination{};
    accepted_destination.family = accepted_source->family;
    accepted_destination.port = port_;
    if (accepted_destination.family == AF_INET) {
      accepted_destination.address[0] = std::byte{127};
      accepted_destination.address[3] = std::byte{1};
    } else if (accepted_destination.family == AF_INET6) {
      accepted_destination.address[15] = std::byte{1};
    } else {
      return STATUS_INVALID_ADDRESS;
    }
    ntl::wfp::transparent_udp_original_flow original{
        .source = *accepted_source,
        .destination = accepted_destination,
        .compartment = UNSPECIFIED_COMPARTMENT_ID};
    auto resolved =
        udp_routes_.resolve_original_flow(*accepted_source, port_);
    if (resolved) {
      original = *resolved;
    } else if (resolved.status() != STATUS_NOT_FOUND) {
      return resolved.status();
    }
    std::shared_ptr<http3_server_identity> identity;
    {
      fast_mutex_guard transaction(*configuration_transaction_lock_);
      fast_mutex_guard guard(configuration_lock_);
      for (const auto &entry : identities_) {
        if (ascii_equal_ci(entry->server_name, server_name)) {
          identity = entry;
          break;
        }
      }
    }
    if (!identity)
      return STATUS_INVALID_ADDRESS;
    const std::size_t slot =
        connection_slots_.fetch_add(1, std::memory_order_acq_rel);
    if (slot >= maximum_connections) {
      connection_slots_.fetch_sub(1, std::memory_order_acq_rel);
      return STATUS_QUOTA_EXCEEDED;
    }
    std::uint64_t peak = peak_connections_.load(std::memory_order_relaxed);
    while (peak < slot + 1 &&
           !peak_connections_.compare_exchange_weak(
               peak, slot + 1, std::memory_order_relaxed)) {
    }
    const std::uint64_t session_id = captures_->reserve_session_id();
    auto session = make_http3_session_metadata(
        session_id, server_name, *accepted_source,
        accepted_destination, original);
    if (!session) {
      connection_slots_.fetch_sub(1, std::memory_order_acq_rel);
      return session.status();
    }
    ntl::status factory_status{STATUS_PENDING};
    std::shared_ptr<ntl::net::http3::proxy_connection> proxy;
    const auto configured = identity->configuration.make_connection_context();
    auto accepted = http3_backend_connection::try_accept_with_sink(
        configured, std::move(indication),
         [this, session_id, server_name = std::string(server_name),
          session = std::move(*session),
          &proxy, &factory_status](
            std::shared_ptr<ntl::net::quic::transport_backend> backend)
            -> std::shared_ptr<ntl::net::quic::backend_sink> {
          auto origin = origin_pool_.make_transport();
          if (!origin) {
            factory_status = STATUS_DELETE_PENDING;
            return {};
          }
          try {
            auto grpc = std::make_shared<
                ntl::net::grpc::message_transform_pipeline>();
            crtsys::wfp_browser_http_policy::configure_grpc_transforms(
                *grpc);
            auto policy =
                crtsys::wfp_browser_http_policy::
                    make_browser_inspection_policy(
                        grpc);
            auto observer = std::make_shared<browser_http3_observer>(
                weak_from_this(), session_id, server_name);
            auto webtransport =
                crtsys::wfp_browser_http_policy::
                    make_browser_webtransport_policy();
            auto terminals =
                std::make_shared<browser_http3_terminal_responses>();
            auto memory = std::make_shared<
                ntl::net::bounded_memory_resource>(
                    ntl::net::bounded_memory_limits{
                        .maximum_allocated_bytes = 64 * 1024 * 1024,
                        .maximum_single_allocation = 16 * 1024 * 1024});
            auto created = ntl::net::http3::proxy_connection::create(
                std::move(backend), std::move(origin), std::move(policy),
                std::move(session),
                std::move(observer), std::move(webtransport),
                std::make_shared<
                    ntl::net::http3::webtransport_echo_handler>(),
                {.maximum_concurrent_request_streams =
                     maximum_http3_request_streams_per_connection,
                 .maximum_buffered_bytes_per_stream =
                     maximum_http3_inspector_bytes_per_stream,
                 .maximum_aggregate_body_bytes =
                     maximum_http3_buffered_request_bytes,
                 .maximum_frame_payload = 128 * 1024,
                 .maximum_decoded_header_bytes = 32 * 1024,
                 .maximum_control_stream_bytes = 4096,
                 .maximum_extension_stream_bytes = 64 * 1024,
                 .maximum_concurrent_extension_streams = 32,
                 .maximum_aggregate_extension_stream_bytes = 2 * 1024 * 1024,
                 .maximum_capsule_wire_bytes = 64 * 1024,
                 .maximum_blocked_streams = 8,
                 .maximum_concurrent_webtransport_sessions = 8,
                 .qpack_table_capacity = 256,
                 .require_http3_origin = false,
                 .require_server_name_authority_binding = true,
                 .enable_webtransport = true},
                {.maximum_bidirectional_streams = 8,
                 .maximum_unidirectional_streams = 8,
                 .maximum_stream_data = 64 * 1024,
                 .maximum_datagram_payload = 4096,
                 .maximum_datagrams = 32},
                std::move(terminals), std::move(memory));
            factory_status =
                created ? ntl::status::ok() : created.status();
            if (!created)
              return {};
            proxy = std::move(*created);
            return std::static_pointer_cast<
                ntl::net::quic::backend_sink>(proxy);
          } catch (const std::bad_alloc &) {
            factory_status = STATUS_INSUFFICIENT_RESOURCES;
          } catch (...) {
            factory_status = STATUS_UNHANDLED_EXCEPTION;
          }
          return {};
        },
        {.maximum_streams = 64,
         .maximum_receive_indication = 128 * 1024,
         .maximum_send_size = 128 * 1024,
         .maximum_prefix_bytes = 8,
         .shutdown_timeout = std::chrono::seconds(10)});
    if (!accepted || !proxy) {
      connection_slots_.fetch_sub(1, std::memory_order_acq_rel);
      return accepted ? factory_status : accepted.status();
    }
    try {
      fast_mutex_guard guard(connection_lock_);
      connections_.push_back(
          connection_record{std::move(*accepted), std::move(proxy)});
    } catch (const std::bad_alloc &) {
      connection_slots_.fetch_sub(1, std::memory_order_acq_rel);
      return STATUS_INSUFFICIENT_RESOURCES;
    } catch (...) {
      connection_slots_.fetch_sub(1, std::memory_order_acq_rel);
      return STATUS_UNHANDLED_EXCEPTION;
    }
    accepted_.fetch_add(1, std::memory_order_relaxed);
    return ntl::status::ok();
  }

  void contribute(contract::service_info &result) noexcept {
    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
      reap_closed_connections();
    const auto origin_work = origin_pool_.statistics();
    result.http3_accepted = accepted_.load(std::memory_order_relaxed);
    result.http3_permitted = permitted_.load(std::memory_order_relaxed);
    result.http3_blocked = blocked_.load(std::memory_order_relaxed);
    result.http3_failed = failed_.load(std::memory_order_relaxed);
    result.http3_origin_connected =
        origin_connected_.load(std::memory_order_relaxed);
    result.http3_origin_completed =
        origin_completed_.load(std::memory_order_relaxed);
    result.http3_origin_failed =
        origin_failed_.load(std::memory_order_relaxed);
    result.http3_origin_peer_validated =
        origin_peer_validated_.load(std::memory_order_relaxed);
    result.http3_origin_h3_negotiated =
        origin_h3_negotiated_.load(std::memory_order_relaxed);
    result.http3_origin_peer_settings =
        origin_peer_settings_.load(std::memory_order_relaxed);
    result.http3_origin_qpack_acknowledgements =
        origin_qpack_acknowledgements_.load(std::memory_order_relaxed);
    result.origin_fallback_attempted =
        origin_fallback_attempted_.load(std::memory_order_relaxed);
    result.origin_fallback_succeeded =
        origin_fallback_succeeded_.load(std::memory_order_relaxed);
    result.origin_fallback_h2 =
        origin_fallback_h2_.load(std::memory_order_relaxed);
    result.origin_fallback_http1 =
        origin_fallback_http1_.load(std::memory_order_relaxed);
    result.origin_fallback_rejected =
        origin_fallback_rejected_.load(std::memory_order_relaxed);
    result.origin_last_status =
        origin_last_status_.load(std::memory_order_relaxed);
    result.origin_last_failure_kind =
        origin_last_failure_kind_.load(std::memory_order_relaxed);
    result.origin_last_failure_stage =
        origin_last_failure_stage_.load(std::memory_order_relaxed);
    result.origin_last_fallback_phase =
        origin_last_fallback_phase_.load(std::memory_order_relaxed);
    result.http3_active_connections =
        connection_slots_.load(std::memory_order_relaxed);
    result.http3_peak_connections =
        peak_connections_.load(std::memory_order_relaxed);
    result.http3_reaped_connections =
        reaped_connections_.load(std::memory_order_relaxed);
    result.http3_worker_requests =
        worker_requests_.load(std::memory_order_relaxed);
    result.http3_irql_violations =
        irql_violations_.load(std::memory_order_relaxed);
    result.qpack_resumed = qpack_resumed_.load(std::memory_order_relaxed);
    result.gzip_responses = gzip_responses_.load(std::memory_order_relaxed);
    result.deflate_responses =
        deflate_responses_.load(std::memory_order_relaxed);
    result.brotli_responses = brotli_responses_.load(std::memory_order_relaxed);
    result.webtransport_sessions =
        webtransport_sessions_.load(std::memory_order_relaxed);
    result.webtransport_bidirectional =
        webtransport_bidirectional_.load(std::memory_order_relaxed);
    result.webtransport_unidirectional =
        webtransport_unidirectional_.load(std::memory_order_relaxed);
    result.webtransport_datagrams =
        webtransport_datagrams_.load(std::memory_order_relaxed);
    result.webtransport_capsules =
        webtransport_capsules_.load(std::memory_order_relaxed);
    result.webtransport_resets =
        webtransport_resets_.load(std::memory_order_relaxed);
    result.webtransport_last_rejection_stage =
        webtransport_last_rejection_stage_.load(std::memory_order_relaxed);
    result.webtransport_last_rejection_status =
        webtransport_last_rejection_status_.load(std::memory_order_relaxed);
    result.webtransport_last_transform_action =
        webtransport_last_transform_action_.load(std::memory_order_relaxed);
    result.webtransport_last_transform_rule =
        webtransport_last_transform_rule_.load(std::memory_order_relaxed);
    result.webtransport_last_transform_before_flags =
        webtransport_last_transform_before_flags_.load(
            std::memory_order_relaxed);
    result.webtransport_last_transform_after_flags =
        webtransport_last_transform_after_flags_.load(
            std::memory_order_relaxed);
    result.webtransport_last_transform_body_size =
        webtransport_last_transform_body_size_.load(std::memory_order_relaxed);
    result.http3_buffered_request_bytes =
        origin_work.buffered_request_bytes;
    result.http3_peak_buffered_request_bytes =
        origin_work.peak_buffered_request_bytes;
    result.http3_buffer_quota_rejections =
        buffer_quota_rejections_.load(std::memory_order_relaxed) +
        origin_work.byte_quota_rejections;
    result.http3_canceled_streams =
        canceled_streams_.load(std::memory_order_relaxed);
    result.http3_pending_requests =
        origin_work.pending_operations;
    result.http3_peak_pending_requests =
        origin_work.peak_pending_operations;
    result.http3_origin_allocation_bytes =
        origin_allocation_budget_.current();
    result.http3_origin_peak_allocation_bytes =
        origin_allocation_budget_.peak();
    result.http3_origin_allocation_quota_rejections =
        origin_allocation_quota_rejections_.load(std::memory_order_relaxed);
    result.http3_last_stream_rejection_status =
        last_stream_rejection_status_.load(std::memory_order_relaxed);
    {
      fast_mutex_guard guard(connection_lock_);
      for (const auto &entry : connections_) {
        const auto streams = entry.backend->peer_streams();
        result.http3_peer_bidirectional_started +=
            streams.bidirectional_started;
        result.http3_peer_unidirectional_started +=
            streams.unidirectional_started;
        result.http3_peer_receive_events += streams.receive_events;
        result.http3_peer_receive_fin_events += streams.receive_fin_events;
        result.http3_peer_send_shutdown_events +=
            streams.peer_send_shutdown_events;
        result.http3_request_streams_classified +=
            streams.request_streams_classified;
        result.http3_request_sink_calls += streams.request_sink_calls;
        result.http3_request_sink_final_calls +=
            streams.request_sink_final_calls;
        result.http3_last_request_sink_status =
            streams.last_request_sink_status;
        const auto proxy = entry.sink->statistics();
        result.http3_proxy_request_stream_calls +=
            proxy.request_stream_calls;
        result.http3_proxy_request_stream_final_calls +=
            proxy.request_stream_final_calls;
        result.http3_proxy_request_inspector_retries +=
            proxy.request_inspector_retries;
        result.http3_proxy_request_headers += proxy.request_headers;
        result.http3_proxy_request_stream_ends +=
            proxy.request_stream_ends;
        result.http3_proxy_blocked_request_streams +=
            proxy.blocked_request_streams;
        result.http3_proxy_active_requests += proxy.active_requests;
        result.http3_proxy_origin_submit_calls +=
            proxy.origin_submit_calls;
        result.http3_proxy_last_stream_end_status =
            proxy.last_stream_end_status;
        result.http3_proxy_last_origin_submit_status =
            proxy.last_origin_submit_status;
      }
    }
    {
      fast_mutex_guard guard(configuration_lock_);
      result.http3_port = port_;
      result.http3_ready =
          ready_.load(std::memory_order_acquire) ? 1u : 0u;
      result.http3_origin_security_ready =
          origin_security_.empty() ? 0u : 1u;
      result.http3_identity_count =
          static_cast<std::uint32_t>(identities_.size());
    }
  }

  void publish_capture(std::uint64_t session_id,
                       std::string_view server_name, bool blocked,
                       std::uint32_t status, std::uint32_t flags,
                       std::span<const std::byte> request,
                       std::span<const std::byte> response) noexcept {
    captures_->publish_external(
        session_id, {}, server_name, contract::inspected_protocol::http3,
        blocked ? contract::inspection_action::blocked
                : contract::inspection_action::permitted,
        status, STATUS_SUCCESS, flags, request, response);
  }

  void publish(std::uint64_t session_id, std::string_view server_name,
               bool blocked, std::uint32_t status, std::uint32_t flags,
               std::span<const std::byte> request,
               std::span<const std::byte> response) noexcept {
    publish_capture(session_id, server_name, blocked, status, flags, request,
                    response);
    (blocked ? blocked_ : permitted_)
        .fetch_add(1, std::memory_order_relaxed);
  }

  void record_permitted() noexcept {
    permitted_.fetch_add(1, std::memory_order_relaxed);
  }

  void record_failure() noexcept {
    failed_.fetch_add(1, std::memory_order_relaxed);
  }
  void record_stream_rejection(NTSTATUS status) noexcept {
    last_stream_rejection_status_.store(status, std::memory_order_relaxed);
    if (status == STATUS_BUFFER_OVERFLOW ||
        status == STATUS_QUOTA_EXCEEDED ||
        status == STATUS_INSUFFICIENT_RESOURCES)
      record_buffer_rejection();
  }
  void record_worker_request() noexcept {
    worker_requests_.fetch_add(1, std::memory_order_relaxed);
  }
  bool require_passive_callback() noexcept {
    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
      return true;
    irql_violations_.fetch_add(1, std::memory_order_relaxed);
    record_failure();
    return false;
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
  void record_webtransport_stream(
      ntl::net::http3::webtransport::stream_direction direction) noexcept {
    (direction == ntl::net::http3::webtransport::stream_direction::bidirectional
         ? webtransport_bidirectional_
         : webtransport_unidirectional_)
        .fetch_add(1, std::memory_order_relaxed);
  }
  void record_webtransport_datagram() noexcept {
    webtransport_datagrams_.fetch_add(1, std::memory_order_relaxed);
  }
  void record_webtransport_capsule() noexcept {
    webtransport_capsules_.fetch_add(1, std::memory_order_relaxed);
  }
  void record_webtransport_reset() noexcept {
    webtransport_resets_.fetch_add(1, std::memory_order_relaxed);
  }
  void record_webtransport_rejection(
      contract::webtransport_rejection_stage stage,
      NTSTATUS status) noexcept {
    webtransport_last_rejection_status_.store(status,
                                               std::memory_order_relaxed);
    webtransport_last_rejection_stage_.store(stage,
                                              std::memory_order_release);
  }
  void record_webtransport_transform(
      ntl::net::http::rewrite_action action, std::size_t terminal_rule,
      std::uint32_t before_flags, std::uint32_t after_flags,
      std::size_t body_size) noexcept {
    webtransport_last_transform_action_.store(
        static_cast<std::uint32_t>(action), std::memory_order_relaxed);
    webtransport_last_transform_rule_.store(
        terminal_rule == ntl::net::http::pipeline_outcome::no_terminal_rule
            ? UINT32_MAX
            : static_cast<std::uint32_t>((std::min)(
                  terminal_rule, static_cast<std::size_t>(UINT32_MAX - 1))),
        std::memory_order_relaxed);
    webtransport_last_transform_before_flags_.store(before_flags,
                                                     std::memory_order_relaxed);
    webtransport_last_transform_after_flags_.store(after_flags,
                                                    std::memory_order_relaxed);
    webtransport_last_transform_body_size_.store(
        static_cast<std::uint32_t>((std::min)(
            body_size, static_cast<std::size_t>(UINT32_MAX))),
        std::memory_order_release);
  }
  void record_canceled_stream() noexcept {
    canceled_streams_.fetch_add(1, std::memory_order_relaxed);
  }

  void record_buffer_rejection() noexcept {
    buffer_quota_rejections_.fetch_add(1, std::memory_order_relaxed);
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
    const ntl::status queued =
        reaper_.post(owner, &http3_service::run_reaper);
    if (!queued.is_ok())
      reap_scheduled_.store(false, std::memory_order_release);
  }

  ntl::result<ntl::net::kernel::http3_origin_pool>
  try_create_origin_pool() noexcept {
    try {
      return ntl::net::kernel::http3_origin_pool::try_create(
          [owner = weak_from_this()](
              ntl::net::http3::origin_request request,
              ntl::net::kernel::origin_cancellation_view cancellation)
              -> ntl::result<ntl::net::http3::origin_response> {
            const auto service = owner.lock();
            if (!service)
              return ntl::unexpected(STATUS_DELETE_PENDING);
            return service->exchange_origin(
                std::move(request), cancellation);
          },
          {.maximum_pending_operations = maximum_pending_requests,
           .maximum_buffered_request_bytes =
               maximum_http3_buffered_request_bytes});
    } catch (const std::bad_alloc &) {
      return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return ntl::unexpected(STATUS_UNHANDLED_EXCEPTION);
    }
  }

  ntl::result<ntl::net::http3::origin_response> exchange_origin(
      ntl::net::http3::origin_request request,
      ntl::net::kernel::origin_cancellation_view cancellation) noexcept {
    if (!require_passive_callback() || request.server_name.empty())
      return ntl::unexpected(STATUS_INVALID_DEVICE_STATE);
    record_worker_request();
    try {
      ntl::net::http::request_message message;
      message.wire_protocol = ntl::net::http::protocol::http3;
      message.method = std::move(request.method);
      message.scheme = std::move(request.scheme);
      message.authority = std::move(request.authority);
      message.path = std::move(request.path);
      for (auto &field : request.headers)
        message.headers.append(
            std::move(field.name), std::move(field.value));
      message.body.assign(request.body.begin(), request.body.end());
      message.trailers.reserve(request.trailers.size());
      for (auto &field : request.trailers)
        message.trailers.push_back(
            {std::move(field.name), std::move(field.value), false});

      auto target = parse_http3_authority(
          message.authority, request.server_name);
      if (!target)
        return ntl::unexpected(target.status());
      auto fetched = fetch_origin(
          *target, std::move(message),
          cancellation.borrowed_native_event());
      if (!fetched)
        return ntl::unexpected(fetched.status());

      const bool strict_h3 =
          fetched->negotiated_protocol == "h3" &&
          fetched->peer_settings_received &&
          fetched->qpack_decoder_acknowledgement_queued;
      const bool validated_fallback =
          (fetched->negotiated_protocol == "h2" ||
           fetched->negotiated_protocol == "http/1.1") &&
          !fetched->peer_settings_received &&
          !fetched->qpack_decoder_acknowledgement_queued;
      if (!fetched->peer_validated ||
          (!strict_h3 && !validated_fallback))
        return ntl::unexpected(STATUS_ACCESS_DENIED);

      ntl::net::http3::origin_response response;
      response.status = fetched->message.status;
      response.body.assign(fetched->message.body.begin(),
                           fetched->message.body.end());
      response.negotiated_protocol = fetched->negotiated_protocol;
      response.headers.reserve(fetched->message.headers.size());
      for (const auto &field : fetched->message.headers.fields())
        response.headers.push_back(
            {std::string(field.name), std::string(field.value)});
      response.trailers.reserve(fetched->message.trailers.size());
      for (const auto &field : fetched->message.trailers)
        response.trailers.push_back(
            {std::string(field.name), std::string(field.value)});
      return ntl::ok(std::move(response));
    } catch (const std::bad_alloc &) {
      return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return ntl::unexpected(STATUS_UNHANDLED_EXCEPTION);
    }
  }

  ntl::result<http3_origin_result> fetch_origin(
      const http3_origin_target &target,
      ntl::net::http::request_message request,
      PKEVENT request_cancellation_event) noexcept {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
        !ready_.load(std::memory_order_acquire) ||
        KeReadStateEvent(&shutdown_event_) != 0)
      return ntl::unexpected(STATUS_INVALID_DEVICE_STATE);
    std::shared_ptr<http3_origin_security> security;
    {
      fast_mutex_guard transaction(*configuration_transaction_lock_);
      fast_mutex_guard guard(configuration_lock_);
      for (const auto &entry : origin_security_) {
        if (ascii_equal_ci(entry->server_name, target.host)) {
          security = entry;
          break;
        }
      }
      if (!security)
        security = default_origin_security_;
    }
    if (!security)
      return ntl::unexpected(STATUS_INVALID_DEVICE_STATE);

    http_origin_fallback_options fallback_options{
        .borrowed_allocation_budget = &origin_allocation_budget_,
        .borrowed_request_cancellation_event = request_cancellation_event,
        .borrowed_service_cancellation_event = &shutdown_event_};
    auto fallback_provider = captures_->origin_fallback_provider();
    if (!fallback_provider)
      return ntl::unexpected(STATUS_INVALID_DEVICE_STATE);
    std::wstring wide_host;
    try {
      wide_host.reserve(target.host.size());
      for (const unsigned char character : target.host) {
        if (character == 0 || character > 0x7f)
          return ntl::unexpected(STATUS_INVALID_ADDRESS);
        wide_host.push_back(static_cast<wchar_t>(character));
      }
    } catch (const std::bad_alloc &) {
      return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return ntl::unexpected(STATUS_UNHANDLED_EXCEPTION);
    }
    auto resolved = http_origin_fallback_detail::resolve_origin(
        *fallback_provider, wide_host, target.port,
        request_cancellation_event, fallback_options);
    if (!resolved)
      return ntl::unexpected(resolved.status());
    const auto valid_endpoint = [](const auto &endpoint) noexcept {
      return endpoint && endpoint.length() <= sizeof(QUIC_ADDR);
    };
    http3_origin_exchange_options strict_options{
        .borrowed_allocation_budget = &origin_allocation_budget_,
        .borrowed_request_cancellation_event = request_cancellation_event,
        .borrowed_service_cancellation_event = &shutdown_event_};
    const std::size_t valid_endpoint_count = static_cast<std::size_t>(
        std::count_if(resolved->begin(), resolved->end(), valid_endpoint));
    if (valid_endpoint_count == 0)
      return ntl::unexpected(STATUS_INVALID_ADDRESS);
    const auto total_connect_milliseconds =
        strict_options.limits.connect_timeout.count();
    strict_options.limits.connect_timeout = std::chrono::milliseconds(
        (std::max)(std::chrono::milliseconds::rep{1},
                   (total_connect_milliseconds +
                    static_cast<std::chrono::milliseconds::rep>(
                        valid_endpoint_count - 1)) /
                       static_cast<std::chrono::milliseconds::rep>(
                           valid_endpoint_count)));
    std::optional<http3_origin_attempt> attempt;
    for (auto current = resolved->begin(); current != resolved->end();
         ++current) {
      if (!valid_endpoint(*current))
        continue;

      QUIC_ADDR remote_address{};
      static_assert(sizeof(remote_address) >= sizeof(SOCKADDR_IN6));
      std::memcpy(&remote_address, current->borrowed_native_address(),
                  current->length());
      strict_options.resolved_remote_address = remote_address;

      origin_connected_.fetch_add(1, std::memory_order_relaxed);
      auto exchange = kernel_http3_origin_exchange::try_create(
          security->configuration.make_connection_context(), target.host,
          target.port, std::move(request), security->policy, strict_options);
      if (!exchange) {
        if (exchange.status() == STATUS_QUOTA_EXCEEDED)
          origin_allocation_quota_rejections_.fetch_add(
              1, std::memory_order_relaxed);
        origin_failed_.fetch_add(1, std::memory_order_relaxed);
        return ntl::unexpected(exchange.status());
      }
      attempt.emplace((*exchange)->run_classified());
      if (attempt->outcome() || !attempt->route_retry_permitted())
        break;
      const auto next = std::find_if(
          std::next(current), resolved->end(), valid_endpoint);
      if (next == resolved->end())
        break;
      auto retry = attempt->take_uncommitted_request_for_route_retry();
      if (!retry)
        break;
      request = std::move(*retry);
      attempt.reset();
    }
    if (!attempt)
      return ntl::unexpected(STATUS_INVALID_ADDRESS);
    auto &strict_attempt = *attempt;
    origin_last_status_.store(
        static_cast<NTSTATUS>(strict_attempt.failure().status),
        std::memory_order_relaxed);
    origin_last_failure_kind_.store(
        static_cast<std::uint32_t>(strict_attempt.failure().kind),
        std::memory_order_relaxed);
    origin_last_failure_stage_.store(
        contract::origin_failure_stage::strict_http3,
        std::memory_order_relaxed);
    ntl::result<http3_origin_result> response =
        ntl::unexpected(strict_attempt.failure().status);
    if (strict_attempt.outcome()) {
      response = strict_attempt.take_outcome();
    } else if (!strict_attempt.transport_fallback_permitted()) {
      origin_fallback_rejected_.fetch_add(1, std::memory_order_relaxed);
      response = strict_attempt.take_outcome();
    } else {
      origin_fallback_attempted_.fetch_add(1, std::memory_order_relaxed);
      if (!security->fallback_credentials) {
        response = ntl::unexpected(STATUS_INVALID_DEVICE_STATE);
      } else {
        auto fallback = kernel_http_origin_fallback_exchange::try_create(
            fallback_provider, target.host, target.port,
            std::move(strict_attempt), *security->fallback_credentials,
            security->policy, fallback_options);
        if (!fallback) {
          origin_last_status_.store(
              static_cast<NTSTATUS>(fallback.status()),
              std::memory_order_relaxed);
          origin_last_failure_stage_.store(
              contract::origin_failure_stage::fallback_create,
              std::memory_order_relaxed);
          response = ntl::unexpected(fallback.status());
        } else {
          response = (*fallback)->run();
          origin_last_fallback_phase_.store(
              static_cast<contract::origin_fallback_phase>(
                  (*fallback)->current_phase()),
              std::memory_order_relaxed);
          if (!response) {
            origin_last_status_.store(
                static_cast<NTSTATUS>(response.status()),
                std::memory_order_relaxed);
            origin_last_failure_stage_.store(
                contract::origin_failure_stage::fallback_exchange,
                std::memory_order_relaxed);
          }
        }
      }
      if (response) {
        origin_fallback_succeeded_.fetch_add(1, std::memory_order_relaxed);
        if (response->negotiated_protocol == "h2")
          origin_fallback_h2_.fetch_add(1, std::memory_order_relaxed);
        else if (response->negotiated_protocol == "http/1.1")
          origin_fallback_http1_.fetch_add(1, std::memory_order_relaxed);
      }
    }
    if (response) {
      origin_last_status_.store(STATUS_SUCCESS, std::memory_order_relaxed);
      origin_last_failure_kind_.store(
          static_cast<std::uint32_t>(http3_origin_failure_kind::none),
          std::memory_order_relaxed);
      origin_last_failure_stage_.store(
          contract::origin_failure_stage::none,
          std::memory_order_relaxed);
      origin_last_fallback_phase_.store(
          contract::origin_fallback_phase::complete,
          std::memory_order_relaxed);
      origin_completed_.fetch_add(1, std::memory_order_relaxed);
      if (response->peer_validated)
        origin_peer_validated_.fetch_add(1, std::memory_order_relaxed);
      if (response->negotiated_protocol == "h3")
        origin_h3_negotiated_.fetch_add(1, std::memory_order_relaxed);
      if (response->peer_settings_received)
        origin_peer_settings_.fetch_add(1, std::memory_order_relaxed);
      if (response->qpack_decoder_acknowledgement_queued)
        origin_qpack_acknowledgements_.fetch_add(
            1, std::memory_order_relaxed);
    } else {
      if (response.status() == STATUS_QUOTA_EXCEEDED)
        origin_allocation_quota_rejections_.fetch_add(
            1, std::memory_order_relaxed);
      origin_failed_.fetch_add(1, std::memory_order_relaxed);
    }
    return response;
  }

  void shutdown() noexcept {
    NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    {
      fast_mutex_guard guard(configuration_lock_);
      if (!ready_.exchange(false, std::memory_order_acq_rel))
        return;
      stopping_.store(true, std::memory_order_release);
    }
    KeSetEvent(&shutdown_event_, IO_NO_INCREMENT, FALSE);
    relay_.close();
    listener_.close();
    std::vector<connection_record> connections;
    {
      fast_mutex_guard guard(connection_lock_);
      connections = connections_;
    }
    for (auto &connection : connections)
      connection.sink->stop();
    origin_pool_.close();
    for (auto &connection : connections) {
      const ntl::status drained = connection.sink->drain();
      if (!drained.is_ok())
        connection.backend->drain_exact();
    }
    reaper_.stop_accepting();
    (void)reaper_.drain();
    reap_closed_connections();
    std::vector<connection_record> remaining;
    {
      fast_mutex_guard guard(connection_lock_);
      remaining.swap(connections_);
    }
    connections.clear();
    for (auto &connection : remaining) {
      connection.sink->stop();
      const ntl::status drained = connection.sink->drain();
      if (!drained.is_ok())
        connection.backend->drain_exact();
    }
    remaining.clear();
    connection_slots_.store(0, std::memory_order_release);
    std::vector<std::shared_ptr<http3_server_identity>> identities;
    std::vector<std::shared_ptr<http3_origin_security>> origin_security;
    std::shared_ptr<http3_origin_security> default_origin;
    {
      fast_mutex_guard guard(configuration_lock_);
      identities.swap(identities_);
      origin_security.swap(origin_security_);
      default_origin = std::move(default_origin_security_);
      port_ = 0;
      native_port_ = 0;
    }
    origin_security.clear();
    default_origin.reset();
    identities.clear();
    const ntl::status credentials_drained = schannel_.close();
    NT_ASSERT(credentials_drained.is_ok());
    registration_.shutdown(0, true);
    registration_.close();
    provider_.close();
  }

private:
  static constexpr std::size_t maximum_connections =
      maximum_http3_connections;
  static constexpr std::size_t maximum_pending_requests =
      maximum_http3_pending_requests;

  struct connection_record {
    std::shared_ptr<http3_backend_connection> backend;
    std::shared_ptr<ntl::net::http3::proxy_connection> sink;
  };

  static QUIC_SETTINGS transport_settings() noexcept {
    QUIC_SETTINGS settings{};
    settings.PeerBidiStreamCount =
        maximum_http3_request_streams_per_connection;
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
    return std::any_of(connections_.begin(), connections_.end(),
                       [](const auto &connection) noexcept {
                         return connection.sink->closed();
                       });
  }

  void reap_closed_connections() noexcept {
    NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    for (;;) {
      connection_record closed;
      {
        fast_mutex_guard guard(connection_lock_);
        const auto found = std::find_if(
            connections_.begin(), connections_.end(),
            [](const auto &connection) noexcept {
              return connection.sink->closed();
            });
        if (found == connections_.end())
          return;
        closed = std::move(*found);
        connections_.erase(found);
      }
      const ntl::status drained = closed.sink->drain();
      if (!drained.is_ok())
        closed.backend->drain_exact();
      closed = {};
      std::size_t current =
          connection_slots_.load(std::memory_order_acquire);
      while (current != 0 &&
             !connection_slots_.compare_exchange_weak(
                 current, current - 1, std::memory_order_acq_rel,
                 std::memory_order_acquire)) {
      }
      if (current == 0) {
        NT_ASSERT(false);
        return;
      }
      reaped_connections_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  ntl::net::kernel::schannel schannel_{};
  std::shared_ptr<tcp_service> captures_;
  ntl::wfp::transparent_udp_route_resolver udp_routes_;
  FAST_MUTEX *configuration_transaction_lock_;
  ntl::net::kernel::msquic_provider provider_{};
  ntl::net::kernel::msquic_registration registration_{};
  ntl::net::kernel::msquic_listener listener_{};
  ntl::net::kernel::wsk_datagram_relay relay_{};
  ntl::net::kernel::http3_origin_pool origin_pool_{};
  ntl::net::kernel::executor reaper_{};
  mutable FAST_MUTEX configuration_lock_{};
  FAST_MUTEX connection_lock_{};
  std::vector<connection_record> connections_;
  std::vector<std::shared_ptr<http3_server_identity>> identities_;
  std::vector<std::shared_ptr<http3_origin_security>> origin_security_;
  std::shared_ptr<http3_origin_security> default_origin_security_;
  std::uint16_t port_ = 0;
  std::uint16_t native_port_ = 0;
  std::atomic<bool> ready_{false};
  std::atomic<bool> stopping_{false};
  std::atomic<bool> fail_next_origin_security_configuration_{false};
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
  std::atomic<contract::webtransport_rejection_stage>
      webtransport_last_rejection_stage_{
          contract::webtransport_rejection_stage::none};
  std::atomic<NTSTATUS> webtransport_last_rejection_status_{STATUS_SUCCESS};
  std::atomic<std::uint32_t> webtransport_last_transform_action_{0};
  std::atomic<std::uint32_t> webtransport_last_transform_rule_{UINT32_MAX};
  std::atomic<std::uint32_t> webtransport_last_transform_before_flags_{0};
  std::atomic<std::uint32_t> webtransport_last_transform_after_flags_{0};
  std::atomic<std::uint32_t> webtransport_last_transform_body_size_{0};
  std::atomic<std::uint64_t> buffer_quota_rejections_{0};
  std::atomic<NTSTATUS> last_stream_rejection_status_{STATUS_SUCCESS};
  std::atomic<std::uint64_t> canceled_streams_{0};
  std::atomic<std::uint64_t> origin_connected_{0};
  std::atomic<std::uint64_t> origin_completed_{0};
  std::atomic<std::uint64_t> origin_failed_{0};
  std::atomic<std::uint64_t> origin_peer_validated_{0};
  std::atomic<std::uint64_t> origin_h3_negotiated_{0};
  std::atomic<std::uint64_t> origin_peer_settings_{0};
  std::atomic<std::uint64_t> origin_qpack_acknowledgements_{0};
  std::atomic<std::uint64_t> origin_fallback_attempted_{0};
  std::atomic<std::uint64_t> origin_fallback_succeeded_{0};
  std::atomic<std::uint64_t> origin_fallback_h2_{0};
  std::atomic<std::uint64_t> origin_fallback_http1_{0};
  std::atomic<std::uint64_t> origin_fallback_rejected_{0};
  std::atomic<NTSTATUS> origin_last_status_{STATUS_SUCCESS};
  std::atomic<std::uint32_t> origin_last_failure_kind_{
      static_cast<std::uint32_t>(http3_origin_failure_kind::none)};
  std::atomic<contract::origin_failure_stage> origin_last_failure_stage_{
      contract::origin_failure_stage::none};
  std::atomic<contract::origin_fallback_phase> origin_last_fallback_phase_{
      contract::origin_fallback_phase::none};
  http3_origin_allocation_budget origin_allocation_budget_{
      128 * 1024 * 1024};
  std::atomic<std::uint64_t> origin_allocation_quota_rejections_{0};
  KEVENT shutdown_event_{};
  std::atomic<std::uint64_t> worker_requests_{0};
  std::atomic<std::uint64_t> irql_violations_{0};
  std::atomic<std::size_t> connection_slots_{0};
  std::atomic<std::uint64_t> reaped_connections_{0};
  std::atomic<std::uint64_t> peak_connections_{0};
  std::atomic<bool> reap_scheduled_{false};
};

inline void browser_http3_observer::on_qpack_stream_resumed(
    std::uint64_t) noexcept {
  if (const auto owner = owner_.lock())
    owner->record_qpack_resume();
}

inline void browser_http3_observer::on_webtransport_session_opened(
    std::uint64_t) noexcept {
  if (const auto owner = owner_.lock()) {
    owner->record_webtransport_session();
    owner->record_permitted();
  }
}

inline void browser_http3_observer::on_webtransport_session_closed(
    std::uint64_t, NTSTATUS status) noexcept {
  const auto owner = owner_.lock();
  if (!owner)
    return;
  constexpr std::string_view response =
      "webtransport session inspected";
  owner->publish_capture(
      session_id_, server_name_, false, 200,
      contract::request_transformed | contract::response_transformed |
          contract::websocket_or_extended_connect |
          contract::datagram_or_webtransport,
      {}, std::as_bytes(std::span(response)));
  if (status != STATUS_SUCCESS)
    owner->record_failure();
}

inline void browser_http3_observer::on_webtransport_payload(
    const ntl::net::http3::webtransport::payload &payload) noexcept {
  const auto owner = owner_.lock();
  if (!owner)
    return;
  using kind = ntl::net::http3::webtransport::payload_kind;
  if (payload.kind == kind::datagram)
    owner->record_webtransport_datagram();
  else if (payload.kind == kind::capsule)
    owner->record_webtransport_capsule();
  else
    owner->record_webtransport_stream(payload.direction);
  owner->record_webtransport_transform(
      ntl::net::http::rewrite_action::forward,
      ntl::net::http::pipeline_outcome::no_terminal_rule, 0, 0,
      payload.bytes.size());
}

inline void browser_http3_observer::on_webtransport_reset(
    std::uint64_t, std::uint32_t) noexcept {
  if (const auto owner = owner_.lock())
    owner->record_webtransport_reset();
}

inline void browser_http3_observer::on_stream_rejected(
    std::uint64_t, NTSTATUS status) noexcept {
  if (const auto owner = owner_.lock())
    owner->record_stream_rejection(status);
}

inline void browser_http3_observer::on_stream_cancelled(
    std::uint64_t) noexcept {
  if (const auto owner = owner_.lock())
    owner->record_canceled_stream();
}

inline void browser_http3_observer::on_exchange_complete(
    std::uint64_t,
    const ntl::net::http::request_message &request,
    const ntl::net::http::response_message &response,
    bool terminal) noexcept {
  const auto owner = owner_.lock();
  if (!owner)
    return;
  try {
    const std::string request_capture = capture_request(request);
    std::uint32_t flags =
        contract::request_transformed | contract::response_transformed;
    const std::string content_encoding =
        response.headers.joined("content-encoding");
    if (!content_encoding.empty())
      flags |= contract::compressed_content;
    if (has_html_content_type(response.headers))
      flags |= contract::html_content;
    if (request.extended_protocol)
      flags |= contract::websocket_or_extended_connect;
    if (has_grpc_content_type(request.headers) ||
        has_grpc_content_type(response.headers))
      flags |= contract::grpc_message;
    const bool blocked = terminal || response.status == 403;
    owner->publish(
        session_id_, server_name_, blocked, response.status, flags,
        std::as_bytes(std::span(request_capture)),
        std::span<const std::byte>(response.body.data(),
                                  response.body.size()));
    owner->record_content_encoding(content_encoding);
  } catch (...) {
    owner->record_failure();
  }
}

inline void browser_http3_observer::on_closed(NTSTATUS status) noexcept {
  if (const auto owner = owner_.lock()) {
    if (status != STATUS_SUCCESS && status != STATUS_CANCELLED &&
        status != STATUS_DELETE_PENDING)
      owner->record_failure();
    owner->connection_closed();
  }
}

inline ntl::status http3_service_listener_sink::on_connection(
    borrowed_accepted_connection indication) noexcept {
  const auto owner = owner_.lock();
  return owner ? owner->on_connection(std::move(indication))
               : ntl::status{STATUS_DELETE_PENDING};
}

} // namespace crtsys::wfp_kernel_browser_https::driver
