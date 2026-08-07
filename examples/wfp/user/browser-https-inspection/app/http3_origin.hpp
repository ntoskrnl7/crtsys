#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ntl/net/http3/inspection_proxy>

#include "http1_support.hpp"

namespace ntl::net::inspection {
class origin_client_identity_provider;
}

namespace crtsys::wfp_sample::browser_https {

class browser_html_logger;

enum class http3_origin_policy : std::uint8_t {
  require_http3 = 0,
  allow_tls_tcp_fallback = 1,
};

using http3_header_fields =
    std::vector<std::pair<std::string, std::string>>;

struct http3_origin_response {
  parsed_http_response message;
  http3_header_fields headers;
  std::string negotiated_protocol;
};

/**
 * Forwards one decoded HTTP/3 request to an HTTPS origin through WinHTTP.
 *
 * WinHTTP is configured to require HTTP/3 and the negotiated protocol is
 * verified after the response. It does not silently turn an "H3-to-H3"
 * inspection test into an H3-to-H2/H1 proxy.
 */
http3_origin_response fetch_http3_origin_winhttp(
    std::wstring_view server_name,
    std::string_view method,
    std::string_view path,
    const http3_header_fields &headers,
    std::span<const std::byte> body,
    ntl::net::inspection::origin_client_identity_provider
        &origin_identities);

/**
 * Tries HTTP/3 first and retries the same bounded request over TLS/TCP when
 * the network reports an HTTP/3 transport failure. Automatic replay is
 * limited to safe methods (GET, HEAD, OPTIONS, and TRACE), because WinHTTP can
 * report a transport failure after request bytes reached the origin.
 * Certificate, mTLS, request-validation, and non-safe method failures never
 * trigger the fallback.
 */
http3_origin_response
fetch_http_origin_with_transport_fallback_winhttp(
    std::wstring_view server_name,
    std::string_view method,
    std::string_view path,
    const http3_header_fields &headers,
    std::span<const std::byte> body,
    ntl::net::inspection::origin_client_identity_provider
        &origin_identities);

/** Browser-specific origin choice behind the generic async origin pool. */
class browser_http3_origin_transport final
    : public ntl::net::http3::origin_transport {
public:
  browser_http3_origin_transport(
      std::shared_ptr<browser_html_logger> logger,
      std::shared_ptr<ntl::net::inspection::origin_client_identity_provider>
          origin_identities,
      http3_origin_policy policy) noexcept;

  ntl::result<ntl::net::http3::origin_response>
  send(const ntl::net::http3::origin_request &request)
      noexcept override;

private:
  std::shared_ptr<browser_html_logger> logger_;
  std::shared_ptr<ntl::net::inspection::origin_client_identity_provider>
      origin_identities_;
  http3_origin_policy policy_ =
      http3_origin_policy::require_http3;
};

} // namespace crtsys::wfp_sample::browser_https
