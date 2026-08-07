#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "http3_live_proxy.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

#include <ntl/net/grpc/transform>
#include <ntl/net/http/inspection_policy>
#include <ntl/net/http3/async_origin_pool>
#include <ntl/net/http3/msquic_runtime>
#include <ntl/net/http3/msquic_server>
#include <ntl/net/http3/proxy_connection>
#include <ntl/net/inspection/standard_content_decoders>
#include <ntl/net/inspection/standard_content_encoders>
#include <ntl/net/tls/certificate>
#include <ntl/net/tls/inspection_frontend>

#include "browser_log.hpp"
#include "browser_http_policy.hpp"
#include "http3_inspection.hpp"
#include "http3_origin.hpp"

namespace crtsys::wfp_sample::browser_https {
namespace {

constexpr std::size_t maximum_dynamic_hosts = 256;
constexpr std::array<std::string_view, 1> h3_alpn{"h3"};

std::wstring widen_dns_name(std::string_view value) {
  std::wstring result;
  result.reserve(value.size());
  for (const unsigned char character : value) {
    if (character == 0 || character > 0x7f)
      throw std::invalid_argument(
          "HTTP/3 SNI is not an ASCII DNS name");
    result.push_back(static_cast<wchar_t>(character));
  }
  return result;
}

std::string normalize_dns_name(std::string_view value) {
  std::string result(value);
  for (char &character : result) {
    if (character >= 'A' && character <= 'Z')
      character = static_cast<char>(character - 'A' + 'a');
  }
  if (result.size() > 1 && result.back() == '.')
    result.pop_back();
  if (result.empty() || result.find_first_of("/\\:") != std::string::npos)
    throw std::invalid_argument("HTTP/3 SNI is not a DNS host name");
  return result;
}

ntl::net::http::endpoint_metadata endpoint_from(
    const QUIC_ADDR &address) {
  ntl::net::http::endpoint_metadata result;
  result.port = QuicAddrGetPort(&address);
  char text[INET6_ADDRSTRLEN]{};
  const auto family = QuicAddrGetFamily(&address);
  const void *bytes = nullptr;
  if (family == QUIC_ADDRESS_FAMILY_INET) {
    bytes = &reinterpret_cast<const sockaddr_in *>(&address)->sin_addr;
  } else if (family == QUIC_ADDRESS_FAMILY_INET6) {
    bytes = &reinterpret_cast<const sockaddr_in6 *>(&address)->sin6_addr;
  }
  if (!bytes || !::InetNtopA(
                    static_cast<INT>(family),
                    const_cast<void *>(bytes), text,
                    static_cast<DWORD>(sizeof(text))))
    throw std::invalid_argument("HTTP/3 peer address is invalid");
  result.address = text;
  return result;
}

struct http3_identity {
  ntl::net::issued_tls_certificate certificate;
  ntl::net::http3::msquic_backend::configuration configuration;

  http3_identity(
      ntl::net::issued_tls_certificate issued,
      ntl::net::http3::msquic_backend::configuration configured) noexcept
      : certificate(std::move(issued)),
        configuration(std::move(configured)) {}
};

class dynamic_http3_identity_provider {
public:
  dynamic_http3_identity_provider(
      std::shared_ptr<ntl::net::http3::msquic_backend::runtime> runtime,
      QUIC_SETTINGS settings,
      std::shared_ptr<ntl::net::windows_tls_certificate_issuer> issuer,
      std::shared_ptr<browser_html_logger> logger) noexcept
      : runtime_(std::move(runtime)), settings_(settings),
        issuer_(std::move(issuer)), logger_(std::move(logger)) {}

  ntl::result<ntl::net::http3::msquic_backend::configuration>
  configuration_for(
      std::string_view server_name) noexcept {
    try {
      const std::string normalized =
          normalize_dns_name(server_name);
      std::lock_guard guard(lock_);
      if (const auto found = identities_.find(normalized);
          found != identities_.end())
        return ntl::ok(found->second->configuration);
      if (identities_.size() >= maximum_dynamic_hosts)
        return ntl::unexpected(STATUS_QUOTA_EXCEEDED);

      auto certificate = issuer_->issue(widen_dns_name(normalized));
      ntl::net::http3::msquic_backend::configuration configured;
      ntl::status status = configured.open(
          *runtime_, h3_alpn, &settings_);
      if (!status.is_ok())
        return ntl::unexpected(static_cast<NTSTATUS>(status));

      QUIC_CREDENTIAL_CONFIG credential{};
      credential.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_CONTEXT;
      credential.CertificateContext =
          reinterpret_cast<QUIC_CERTIFICATE *>(
              const_cast<CERT_CONTEXT *>(
                  certificate.borrowed_certificate()));
      status = configured.load_credential(credential);
      if (!status.is_ok())
        return ntl::unexpected(static_cast<NTSTATUS>(status));

      auto inserted = identities_.emplace(
          normalized,
          std::make_unique<http3_identity>(
              std::move(certificate), std::move(configured)));
      logger_->record_lifecycle(
          "http3-identity host=" + normalized);
      return ntl::ok(inserted.first->second->configuration);
    } catch (const std::bad_alloc &) {
      return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
    } catch (const std::exception &error) {
      logger_->record_error(error.what());
      return ntl::unexpected(STATUS_INVALID_PARAMETER);
    } catch (...) {
      logger_->record_error(
          "unknown HTTP/3 identity selection failure");
      return ntl::unexpected(STATUS_UNHANDLED_EXCEPTION);
    }
  }

  std::size_t size() const noexcept {
    std::lock_guard guard(lock_);
    return identities_.size();
  }

private:
  std::shared_ptr<ntl::net::http3::msquic_backend::runtime> runtime_;
  QUIC_SETTINGS settings_{};
  std::shared_ptr<ntl::net::windows_tls_certificate_issuer> issuer_;
  std::shared_ptr<browser_html_logger> logger_;
  mutable std::mutex lock_;
  std::unordered_map<std::string, std::unique_ptr<http3_identity>>
      identities_;
};

QUIC_SETTINGS make_quic_settings(
    http3_origin_policy origin_policy) noexcept {
  QUIC_SETTINGS settings{};
  settings.IdleTimeoutMs =
      origin_policy == http3_origin_policy::allow_tls_tcp_fallback
          ? 90'000
          : 30'000;
  settings.IsSet.IdleTimeoutMs = TRUE;
  settings.PeerBidiStreamCount = 256;
  settings.IsSet.PeerBidiStreamCount = TRUE;
  settings.PeerUnidiStreamCount = 32;
  settings.IsSet.PeerUnidiStreamCount = TRUE;
  settings.DatagramReceiveEnabled = TRUE;
  settings.IsSet.DatagramReceiveEnabled = TRUE;
#ifdef QUIC_API_ENABLE_PREVIEW_FEATURES
  settings.ReliableResetEnabled = TRUE;
  settings.IsSet.ReliableResetEnabled = TRUE;
#endif
  return settings;
}

ntl::net::http3::proxy_connection_limits proxy_limits(
    http3_origin_policy origin_policy) noexcept {
  ntl::net::http3::proxy_connection_limits limits;
  limits.require_http3_origin =
      origin_policy == http3_origin_policy::require_http3;
  limits.require_server_name_authority_binding = true;
  limits.enable_webtransport = true;
  return limits;
}

ntl::net::http3::msquic_backend::server_runtime_limits
server_limits() noexcept {
  return {
      .maximum_active_connections = 256,
      .connection =
          {.maximum_streams = 1024,
           .maximum_receive_indication = 1024 * 1024,
           .maximum_send_size = 4 * 1024 * 1024,
           .maximum_prefix_bytes = 8,
           .shutdown_timeout = std::chrono::seconds(15)},
      .shutdown_timeout = std::chrono::seconds(15)};
}

[[noreturn]] void throw_status(
    ntl::status status, std::string_view operation) {
  throw std::system_error(
      static_cast<int>(static_cast<NTSTATUS>(status)),
      std::system_category(), std::string(operation));
}

} // namespace

class browser_http3_service::implementation {
public:
  implementation(
      std::shared_ptr<ntl::net::windows_tls_certificate_issuer> issuer,
      std::shared_ptr<browser_html_logger> logger,
      std::uint16_t listen_port,
      std::shared_ptr<
          ntl::net::inspection::origin_client_identity_provider>
          origin_identities,
      http3_origin_policy origin_policy,
      std::shared_ptr<ntl::net::http3::origin_transport> origin_transport,
      std::shared_ptr<const ntl::net::http::inspection_policy> policy)
      : runtime_(std::make_shared<
            ntl::net::http3::msquic_backend::runtime>()),
        settings_(make_quic_settings(origin_policy)),
        identities_(runtime_, settings_, issuer, logger),
        origin_identities_(
            origin_identities
                ? std::move(origin_identities)
                : std::make_shared<ntl::net::inspection::
                      unavailable_origin_client_identity>()),
        selected_origin_(
            origin_transport
                ? std::move(origin_transport)
                : std::make_shared<browser_http3_origin_transport>(
                      logger, origin_identities_, origin_policy)),
        origin_pool_(selected_origin_),
        default_policy_(crtsys::wfp_browser_http_policy::
                            make_browser_inspection_policy()),
        observer_(std::make_shared<browser_http3_observer>(logger)),
        webtransport_policy_(crtsys::wfp_browser_http_policy::
                                 make_browser_webtransport_policy()),
        policy_(policy ? std::move(policy) : default_policy_),
        origin_policy_(origin_policy), port_(listen_port) {
    if (!issuer || !logger || port_ == 0)
      throw std::invalid_argument(
          "HTTP/3 service requires a nonzero listen port");
    ntl::status status = runtime_->open(
        "crtsys-ntl-browser-http3",
        QUIC_EXECUTION_PROFILE_LOW_LATENCY);
    if (!status.is_ok())
      throw_status(status, "MsQuic runtime open");

    open_server(server_v4_);
    open_server(server_v6_);
    start_server(server_v4_, QUIC_ADDRESS_FAMILY_INET);
    try {
      start_server(server_v6_, QUIC_ADDRESS_FAMILY_INET6);
    } catch (...) {
      server_v4_.stop();
      (void)server_v4_.drain(std::chrono::seconds(15));
      throw;
    }
  }

  void stop() noexcept {
    server_v4_.stop();
    server_v6_.stop();
    origin_pool_.stop_accepting();
  }

  bool wait_for_drain(std::uint32_t seconds) noexcept {
    const auto timeout = std::chrono::seconds(seconds);
    const bool servers =
        server_v4_.drain(timeout).is_ok() &&
        server_v6_.drain(timeout).is_ok();
    return servers && origin_pool_.drain().is_ok();
  }

  std::size_t delivered_requests() const noexcept {
    return observer_->delivered_requests();
  }
  std::size_t dynamic_hosts() const noexcept {
    return identities_.size();
  }
  std::uint16_t port() const noexcept { return port_; }

private:
  void open_server(
      ntl::net::http3::msquic_backend::server &server) {
    const ntl::status opened = server.open(
        *runtime_,
        [this](
            const ntl::net::http3::msquic_backend::
                accepted_connection_info_view &information) {
          return identities_.configuration_for(information.server_name);
        },
        [this](
            std::shared_ptr<ntl::net::quic::transport_backend> backend,
            const ntl::net::http3::msquic_backend::
                accepted_connection_info_view &information)
            -> ntl::result<std::shared_ptr<
                ntl::net::quic::backend_sink>> {
          ntl::net::http::inspection_session_metadata session;
          session.tls.server_name =
              std::string(information.server_name);
          session.tls.alpn = "h3";
          if (information.has_local_address)
            session.connection.destination =
                endpoint_from(information.local_address);
          if (information.has_remote_address)
            session.connection.source =
                endpoint_from(information.remote_address);
          auto connection = ntl::net::http3::proxy_connection::create(
              std::move(backend), origin_pool_.make_transport(), policy_,
              std::move(session), observer_,
              webtransport_policy_,
              std::make_shared<
                  ntl::net::http3::webtransport_echo_handler>(),
              proxy_limits(origin_policy_));
          return connection
                     ? ntl::ok(std::static_pointer_cast<
                           ntl::net::quic::backend_sink>(
                           std::move(connection).value()))
                     : ntl::result<std::shared_ptr<
                           ntl::net::quic::backend_sink>>(
                           ntl::unexpected(connection.status()));
        },
        server_limits());
    if (!opened.is_ok())
      throw_status(opened, "MsQuic HTTP/3 listener open");
  }

  void start_server(
      ntl::net::http3::msquic_backend::server &server,
      QUIC_ADDRESS_FAMILY family) {
    QUIC_ADDR address{};
    QuicAddrSetFamily(&address, family);
    QuicAddrSetToLoopback(&address);
    QuicAddrSetPort(&address, port_);
    const ntl::status started = server.start(h3_alpn, &address);
    if (!started.is_ok())
      throw_status(started, "MsQuic HTTP/3 listener start");
  }

  std::shared_ptr<ntl::net::http3::msquic_backend::runtime> runtime_;
  QUIC_SETTINGS settings_{};
  dynamic_http3_identity_provider identities_;
  std::shared_ptr<
      ntl::net::inspection::origin_client_identity_provider>
      origin_identities_;
  std::shared_ptr<ntl::net::http3::origin_transport> selected_origin_;
  ntl::net::http3::async_origin_pool origin_pool_;
  std::shared_ptr<ntl::net::http::inspection_policy> default_policy_;
  std::shared_ptr<browser_http3_observer> observer_;
  std::shared_ptr<ntl::net::http3::webtransport::transform_session>
      webtransport_policy_;
  std::shared_ptr<const ntl::net::http::inspection_policy> policy_;
  http3_origin_policy origin_policy_ =
      http3_origin_policy::require_http3;
  std::uint16_t port_ = 0;
  ntl::net::http3::msquic_backend::server server_v4_;
  ntl::net::http3::msquic_backend::server server_v6_;
};

browser_http3_service::browser_http3_service(
    std::shared_ptr<ntl::net::windows_tls_certificate_issuer> issuer,
    std::shared_ptr<browser_html_logger> logger,
    std::uint16_t listen_port,
    std::shared_ptr<
        ntl::net::inspection::origin_client_identity_provider>
        origin_identities,
    http3_origin_policy origin_policy,
    std::shared_ptr<ntl::net::http3::origin_transport> origin_transport,
    std::shared_ptr<const ntl::net::http::inspection_policy> policy)
    : implementation_(std::make_unique<implementation>(
          std::move(issuer), std::move(logger), listen_port,
          std::move(origin_identities), origin_policy,
          std::move(origin_transport), std::move(policy))) {}

browser_http3_service::~browser_http3_service() = default;

void browser_http3_service::stop() noexcept {
  implementation_->stop();
}

bool browser_http3_service::wait_for_drain(
    std::uint32_t timeout_seconds) noexcept {
  return implementation_->wait_for_drain(timeout_seconds);
}

std::uint16_t browser_http3_service::port() const noexcept {
  return implementation_->port();
}

std::size_t
browser_http3_service::delivered_requests() const noexcept {
  return implementation_->delivered_requests();
}

std::size_t
browser_http3_service::dynamic_hosts() const noexcept {
  return implementation_->dynamic_hosts();
}

} // namespace crtsys::wfp_sample::browser_https
