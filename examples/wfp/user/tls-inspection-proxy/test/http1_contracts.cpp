#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <cstddef>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ntl/net/http/http1_transform>
#include <ntl/net/inspection/content_decoder>
#include <ntl/net/inspection/content_encoder>

#include "inspection_policy.hpp"

namespace {

std::vector<std::byte> bytes(std::string_view text) {
  std::vector<std::byte> result(text.size());
  if (!text.empty())
    std::memcpy(result.data(), text.data(), text.size());
  return result;
}

std::string text(std::span<const std::byte> value) {
  if (value.empty())
    return {};
  return {reinterpret_cast<const char *>(value.data()), value.size()};
}

} // namespace

int main() {
  auto policy =
      crtsys::wfp_sample::tls_inspection::make_inspection_policy();
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;

  const auto allowed_wire = bytes(
      "POST /inspect HTTP/1.1\r\n"
      "Host: service.example.test\r\n"
      "Content-Type: application/octet-stream\r\n"
      "Content-Length: 5\r\n\r\nALLOW");
  auto allowed = ntl::net::http::transform_http1_request(
      allowed_wire, policy.transforms_ref(), decoders, encoders,
      {.origin_scheme = "https"});
  if (!allowed ||
      allowed->outcome.action != ntl::net::http::rewrite_action::forward ||
      !allowed->outcome.headers_modified ||
      text(allowed->wire).find("x-ntl-inspected: 1\r\n") ==
          std::string::npos)
    return 1;

  const auto blocked_wire = bytes(
      "POST /inspect HTTP/1.1\r\n"
      "Host: service.example.test\r\n"
      "Content-Type: application/octet-stream\r\n"
      "Content-Length: 7\r\n\r\nBLOCKME");
  auto blocked = ntl::net::http::transform_http1_request(
      blocked_wire, policy.transforms_ref(), decoders, encoders,
      {.origin_scheme = "https"});
  if (!blocked ||
      blocked->outcome.action != ntl::net::http::rewrite_action::forward ||
      blocked->wire.empty())
    return 2;
  ntl::net::http::inspection_session_metadata metadata;
  metadata.connection.connection_id = 1;
  const auto blocked_context = ntl::net::http::inspection_context_view::for_request(
      ntl::net::http::protocol::http1, 0, 2,
      ntl::net::http::inspection_stage::message_complete, metadata,
      blocked->message);
  if (policy.decisions_ref().evaluate(blocked_context) !=
      ntl::net::inspection::verdict::block)
    return 2;

  const auto response_wire = bytes(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=utf-8\r\n"
      "Content-Length: 14\r\n\r\n"
      "<html>x</html>");
  auto response = ntl::net::http::transform_http1_response(
      response_wire, allowed->message, policy.transforms_ref(), decoders,
      encoders);
  if (!response ||
      response->outcome.action != ntl::net::http::rewrite_action::forward ||
      !response->outcome.body_modified ||
      text(response->wire).find("<!-- inspected by ntl -->") ==
          std::string::npos)
    return 3;

  std::cout << "TLS inspection HTTP/1 contract passed: request-transform, "
               "response-transform, block\n";
  return 0;
}
