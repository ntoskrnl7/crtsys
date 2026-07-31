#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "http1_support.hpp"

namespace ntl::net::inspection {
class origin_client_identity_provider;
}

namespace crtsys::wfp_sample::browser_https {

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
 * the network reports an HTTP/3 transport failure. Certificate, mTLS, and
 * request-validation failures never trigger the fallback.
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

} // namespace crtsys::wfp_sample::browser_https
