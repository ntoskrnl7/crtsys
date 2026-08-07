#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <cstddef>
#include <cstring>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ntl/net/http/http1_transform>
#include <ntl/net/http2/transform>
#include <ntl/net/inspection/content_decoder>
#include <ntl/net/inspection/content_encoder>

#include "inspection_policy.hpp"
#include "tls_inspection_policy.hpp"

namespace {

std::vector<std::byte> bytes(std::string_view value) {
  std::vector<std::byte> result(value.size());
  if (!value.empty())
    std::memcpy(result.data(), value.data(), value.size());
  return result;
}

std::string text(std::span<const std::byte> value) {
  return {reinterpret_cast<const char *>(value.data()), value.size()};
}

bool http1_contract() {
  auto policy = crtsys::wfp_kernel_tls::make_inspection_policy();
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
      allowed->message.headers.first("x-ntl-inspected") != "1" ||
      text(allowed->wire).find("x-ntl-inspected: 1\r\n") ==
          std::string::npos)
    return false;

  const auto blocked_header_wire = bytes(
      "GET /inspect HTTP/1.1\r\n"
      "Host: service.example.test\r\n"
      "x-ntl-block: 1\r\n\r\n");
  auto blocked_header = ntl::net::http::transform_http1_request(
      blocked_header_wire, policy.transforms_ref(), decoders, encoders,
      {.origin_scheme = "https"});
  ntl::net::http::inspection_session_metadata metadata;
  if (!blocked_header ||
      blocked_header->outcome.action !=
          ntl::net::http::rewrite_action::forward ||
      crtsys::wfp_kernel_tls::evaluate_request(
          policy, ntl::net::http::protocol::http1, 0, 1, metadata,
          blocked_header->message) != ntl::net::inspection::verdict::block)
    return false;

  const auto blocked_body_wire = bytes(
      "POST /inspect HTTP/1.1\r\n"
      "Host: service.example.test\r\n"
      "Content-Length: 7\r\n\r\nBLOCKME");
  auto blocked_body = ntl::net::http::transform_http1_request(
      blocked_body_wire, policy.transforms_ref(), decoders, encoders,
      {.origin_scheme = "https"});
  if (!blocked_body ||
      blocked_body->outcome.action !=
          ntl::net::http::rewrite_action::forward ||
      crtsys::wfp_kernel_tls::evaluate_request(
          policy, ntl::net::http::protocol::http1, 0, 2, metadata,
          blocked_body->message) != ntl::net::inspection::verdict::block)
    return false;

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
    return false;

  std::string oversized(
      crtsys::examples::wfp::tls_inspection::maximum_http_body_size + 1,
      'x');
  std::string oversized_wire =
      "POST /inspect HTTP/1.1\r\nHost: service.example.test\r\n"
      "Content-Length: " +
      std::to_string(oversized.size()) + "\r\n\r\n" + oversized;
  auto rejected = ntl::net::http::transform_http1_request(
      bytes(oversized_wire), policy.transforms_ref(), decoders, encoders,
      {.origin_scheme = "https"});
  return !rejected;
}

template <class FrameRange>
std::optional<ntl::net::http2::connection_transform_result>
consume_frames(ntl::net::http2::connection_transformer &transformer,
               const FrameRange &frames) {
  std::optional<ntl::net::http2::connection_transform_result> completed;
  for (const auto &encoded : frames) {
    auto frame = ntl::net::http2::frame_view::parse(
        ntl::net::scatter_view::from_contiguous(encoded.wire));
    if (!frame)
      return std::nullopt;
    auto transformed = transformer.consume(*frame);
    if (!transformed)
      return std::nullopt;
    if (transformed->message_complete)
      completed = std::move(*transformed);
  }
  return completed;
}

bool http2_contract() {
  auto policy = crtsys::wfp_kernel_tls::make_inspection_policy();
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;

  ntl::net::http::request_message allowed;
  allowed.wire_protocol = ntl::net::http::protocol::http2;
  allowed.method = "GET";
  allowed.scheme = "https";
  allowed.authority = "service.example.test";
  allowed.path = "/inspect";
  auto allowed_frames = ntl::net::http2::encode_request_frames(1, allowed, {});
  if (!allowed_frames)
    return false;
  auto allowed_exchanges =
      std::make_shared<ntl::net::http2::exchange_store>();
  ntl::net::http2::connection_transformer allowed_transformer(
      ntl::net::http2::connection_direction::requests, allowed_exchanges,
      policy.transforms_ref(), decoders, encoders);
  auto allowed_result = consume_frames(allowed_transformer, *allowed_frames);
  if (!allowed_result || !allowed_result->request ||
      allowed_result->request->headers.first("x-ntl-inspected") != "1" ||
      allowed_result->forward.empty() || !allowed_result->reverse.empty() ||
      allowed_result->terminal_status != 0)
    return false;

  auto blocked = allowed;
  blocked.headers.set("x-ntl-block", "1");
  auto blocked_frames = ntl::net::http2::encode_request_frames(3, blocked, {});
  if (!blocked_frames)
    return false;
  auto blocked_exchanges =
      std::make_shared<ntl::net::http2::exchange_store>();
  ntl::net::http2::connection_transformer blocked_transformer(
      ntl::net::http2::connection_direction::requests, blocked_exchanges,
      policy.transforms_ref(), decoders, encoders);
  auto blocked_result = consume_frames(blocked_transformer, *blocked_frames);
  ntl::net::http::inspection_session_metadata metadata;
  if (!blocked_result || !blocked_result->request ||
      blocked_result->terminal_status != 0 ||
      blocked_result->reverse.size() != 0 || blocked_result->forward.empty() ||
      crtsys::wfp_kernel_tls::evaluate_request(
          policy, ntl::net::http::protocol::http2, 3, 3, metadata,
          *blocked_result->request) != ntl::net::inspection::verdict::block)
    return false;

  auto response_exchanges =
      std::make_shared<ntl::net::http2::exchange_store>();
  if (!response_exchanges->remember(1, allowed).is_ok())
    return false;
  ntl::net::http2::connection_transformer response_transformer(
      ntl::net::http2::connection_direction::responses, response_exchanges,
      policy.transforms_ref(), decoders, encoders);
  constexpr std::string_view body = "<html><body>contract</body></html>";
  ntl::net::http::response_message response;
  response.wire_protocol = ntl::net::http::protocol::http2;
  response.status = 200;
  response.headers.append("content-type", "text/html; charset=utf-8");
  response.headers.append("content-length", std::to_string(body.size()));
  auto response_frames = ntl::net::http2::encode_response_frames(
      1, response, std::as_bytes(std::span(body)));
  if (!response_frames)
    return false;
  auto response_result = consume_frames(response_transformer, *response_frames);
  if (!response_result || !response_result->response ||
      response_result->forward.empty())
    return false;
  return text(response_result->response->body)
             .find("<!-- inspected by ntl -->") != std::string::npos;
}

} // namespace

int main() {
  if (!crtsys::wfp_kernel_tls::supports_application_protocol("http/1.1") ||
      !crtsys::wfp_kernel_tls::supports_application_protocol("h2") ||
      crtsys::wfp_kernel_tls::supports_application_protocol("h3")) {
    std::cerr << "kernel TLS ALPN policy contract failed\n";
    return 1;
  }
  if (!http1_contract()) {
    std::cerr << "kernel TLS HTTP/1 policy contract failed\n";
    return 2;
  }
  if (!http2_contract()) {
    std::cerr << "kernel TLS HTTP/2 policy contract failed\n";
    return 3;
  }
  std::cout << "kernel TLS policy contracts passed: ALPN, HTTP/1, HTTP/2, "
               "bounded fail-closed transform\n";
  return 0;
}
