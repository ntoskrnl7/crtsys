#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "request_capture.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

using crtsys::wfp_kernel_browser_https::driver::request_capture_limits;
using crtsys::wfp_kernel_browser_https::driver::sanitize_request_capture;

namespace {

bool contains(std::string_view text, std::string_view value) {
  return text.find(value) != std::string_view::npos;
}

} // namespace

int main() {
  ntl::net::http::request_message request;
  request.wire_protocol = ntl::net::http::protocol::http3;
  request.method = "POST";
  request.path = "/upload?token=query-secret";
  request.extended_protocol = "webtransport";
  request.headers.append("authorization", "Bearer authorization-secret",
                         true);
  request.headers.append("proxy-authorization", "Basic proxy-secret", true);
  request.headers.append("cookie", "session=cookie-secret", true);
  request.headers.append("set-cookie", "reply=set-cookie-secret", true);
  request.headers.append("x-private", "private-header-secret", true);
  request.headers.append("content-encoding", "gzip");
  constexpr std::string_view body = "request-body-secret";
  const auto body_bytes = std::as_bytes(std::span(body));
  request.body.assign(body_bytes.begin(), body_bytes.end());

  const auto captured = sanitize_request_capture(request);
  if (!captured)
    return 1;
  constexpr std::array forbidden{
      "authorization",       "authorization-secret",
      "proxy-authorization", "proxy-secret",
      "cookie",              "cookie-secret",
      "set-cookie",          "set-cookie-secret",
      "x-private",           "private-header-secret",
      "request-body-secret", "content-encoding",
      "path:",               "query-secret"};
  for (const std::string_view value : forbidden) {
    if (contains(*captured, value))
      return 2;
  }
  if (!contains(*captured, "method: POST") ||
      !contains(*captured, "protocol: h3") ||
      !contains(*captured, "header-count: 6") ||
      !contains(*captured, "body-bytes: 19") ||
      !contains(*captured, "tunnel: yes") ||
      !contains(*captured, "content-coded: yes"))
    return 3;

  request.method.assign(200, 'M');
  request.path.assign(4096, 'P');
  constexpr request_capture_limits small{
      .maximum_size = 128,
      .maximum_method_size = 8};
  const auto bounded = sanitize_request_capture(request, small);
  if (!bounded || bounded->size() > small.maximum_size ||
      !contains(*bounded, "method: MMMMMMMM") ||
      contains(*bounded, "path:") || contains(*bounded, "PPPPPPPP"))
    return 4;

  std::cout << "Kernel browser sanitized request capture contract PASS: "
               "headers=redacted body=redacted bounded=pass\n";
  return 0;
}
