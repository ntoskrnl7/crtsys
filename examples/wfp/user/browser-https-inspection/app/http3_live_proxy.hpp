#pragma once

#include <cstdint>
#include <memory>

#include <ntl/net/http/inspection_policy>

#include "http3_origin.hpp"

namespace ntl::net {
class windows_tls_certificate_issuer;
}
namespace ntl::net::inspection {
class origin_client_identity_provider;
}
namespace ntl::net::http3 {
class origin_transport;
}
namespace crtsys::wfp_sample::browser_https {

class browser_html_logger;

/**
 * Shared dual-stack HTTP/3 inspection service used by the normal WFP runtime.
 */
class browser_http3_service {
public:
  browser_http3_service(
      std::shared_ptr<ntl::net::windows_tls_certificate_issuer> issuer,
      std::shared_ptr<browser_html_logger> logger,
      std::uint16_t listen_port,
      std::shared_ptr<
          ntl::net::inspection::origin_client_identity_provider>
          origin_identities = {},
      http3_origin_policy origin_policy =
          http3_origin_policy::require_http3,
      std::shared_ptr<ntl::net::http3::origin_transport>
          origin_transport = {},
      std::shared_ptr<const ntl::net::http::inspection_policy>
          policy = {});
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

private:
  class implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace crtsys::wfp_sample::browser_https
