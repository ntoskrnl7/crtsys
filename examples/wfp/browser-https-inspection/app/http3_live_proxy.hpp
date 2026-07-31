#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace ntl::net {
class windows_tls_certificate_issuer;
}
namespace ntl::net::inspection {
class origin_client_identity_provider;
}
namespace ntl::net::http3 {
class origin_transport;
}
namespace ntl::net::http {
class transform_pipeline;
}

namespace crtsys::wfp_sample::browser_https {

class browser_html_logger;

enum class http3_origin_policy : std::uint8_t {
  require_http3 = 0,
  allow_tls_tcp_fallback = 1,
};

/**
 * Shared dual-stack HTTP/3 inspection service used by the normal WFP runtime
 * and by the separate transport diagnostic.
 */
class browser_http3_service {
public:
  browser_http3_service(
      ntl::net::windows_tls_certificate_issuer &issuer,
      browser_html_logger &logger,
      std::uint16_t listen_port,
      ntl::net::inspection::origin_client_identity_provider
          *origin_identities = nullptr,
      http3_origin_policy origin_policy =
          http3_origin_policy::require_http3,
      ntl::net::http3::origin_transport
          *origin_transport = nullptr,
      const ntl::net::http::transform_pipeline
          *transforms = nullptr);
  ~browser_http3_service();

  browser_http3_service(
      const browser_http3_service &) = delete;
  browser_http3_service &
  operator=(const browser_http3_service &) = delete;
  browser_http3_service(browser_http3_service &&) = delete;
  browser_http3_service &
  operator=(browser_http3_service &&) = delete;

  void stop() noexcept;
  bool wait_for_drain(
      std::uint32_t timeout_seconds) noexcept;
  std::uint16_t port() const noexcept;
  std::size_t delivered_requests() const noexcept;
  std::size_t dynamic_hosts() const noexcept;
  std::string certificate_spki(
      std::string_view server_name);

private:
  class implementation;
  std::unique_ptr<implementation> implementation_;
};

/**
 * Runs the isolated browser HTTP/3 SPKI transport diagnostic.
 *
 * The caller maps exactly one browser origin to the returned local UDP port.
 * This mode does not change the normal browser profile or install WFP policy.
 */
int run_browser_http3_spki_proxy(
    std::wstring_view server_name,
    std::uint16_t listen_port,
    const std::filesystem::path &log_directory,
    std::uint32_t duration_seconds);

/**
 * Runs an explicit managed-client HTTP/3 inspection endpoint.
 *
 * The managed client keeps the origin SNI and :authority but connects to this
 * loopback endpoint. No browser switch, browser profile change, or WFP UDP
 * redirection is involved.
 */
int run_managed_http3_proxy(
    std::uint16_t listen_port,
    const std::filesystem::path &log_directory,
    std::uint32_t duration_seconds);

/**
 * Runs the same managed HTTP/3 endpoint behind application-scoped WFP
 * UDP/443 connect redirection. The managed client connects to the original
 * destination and supplies only the application-owned inspection CA.
 */
int run_wfp_managed_http3_proxy(
    const std::filesystem::path &managed_client,
    std::uint16_t listen_port,
    const std::filesystem::path &log_directory,
    std::uint32_t duration_seconds);

/**
 * Runs a deterministic, loopback-only H3 -> inspection -> H3 acceptance
 * topology. Both private CAs remain application-owned and are never installed
 * in a Windows or browser trust store.
 */
int run_controlled_http3_end_to_end(
    std::uint16_t proxy_port,
    std::uint16_t origin_port,
    const std::filesystem::path &log_directory,
    std::uint32_t duration_seconds);

} // namespace crtsys::wfp_sample::browser_https
