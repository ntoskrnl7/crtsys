#include "inspection_policy.hpp"

#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <ntl/net/http2/transform>
#include <ntl/net/inspection/content_decoder>
#include <ntl/net/inspection/content_encoder>

namespace {

bool run_contract() {
  auto policy =
      crtsys::wfp_sample::tls_inspection::make_inspection_policy();
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;

  ntl::net::http::request_message allowed;
  allowed.wire_protocol = ntl::net::http::protocol::http2;
  allowed.method = "GET";
  allowed.scheme = "https";
  allowed.authority = "service.example.test";
  allowed.path = "/inspect";
  auto encoded_allowed =
      ntl::net::http2::encode_request_frames(1, allowed, {});
  if (!encoded_allowed)
    return false;
  auto allowed_exchanges =
      std::make_shared<ntl::net::http2::exchange_store>();
  ntl::net::http2::connection_transformer allowed_transformer(
      ntl::net::http2::connection_direction::requests,
      allowed_exchanges, policy.transforms_ref(), decoders, encoders);
  std::optional<ntl::net::http2::connection_transform_result>
      allowed_result;
  for (const auto &encoded : *encoded_allowed) {
    auto frame = ntl::net::http2::frame_view::parse(
        ntl::net::scatter_view::from_contiguous(encoded.wire));
    if (!frame)
      return false;
    auto transformed = allowed_transformer.consume(*frame);
    if (!transformed)
      return false;
    if (transformed->message_complete)
      allowed_result = std::move(*transformed);
  }
  if (!allowed_result || !allowed_result->request ||
      allowed_result->request->headers.first("x-ntl-inspected") != "1" ||
      allowed_result->forward.empty() ||
      !allowed_result->reverse.empty() ||
      allowed_result->terminal_status != 0)
    return false;

  auto blocked = allowed;
  blocked.headers.set("x-ntl-block", "1");
  auto encoded_blocked =
      ntl::net::http2::encode_request_frames(3, blocked, {});
  if (!encoded_blocked)
    return false;
  auto blocked_exchanges =
      std::make_shared<ntl::net::http2::exchange_store>();
  ntl::net::http2::connection_transformer blocked_transformer(
      ntl::net::http2::connection_direction::requests,
      blocked_exchanges, policy.transforms_ref(), decoders, encoders);
  std::optional<ntl::net::http2::connection_transform_result>
      blocked_result;
  for (const auto &encoded : *encoded_blocked) {
    auto frame = ntl::net::http2::frame_view::parse(
        ntl::net::scatter_view::from_contiguous(encoded.wire));
    if (!frame)
      return false;
    auto transformed = blocked_transformer.consume(*frame);
    if (!transformed)
      return false;
    if (transformed->message_complete)
      blocked_result = std::move(*transformed);
  }
  if (!blocked_result || !blocked_result->request ||
      blocked_result->terminal_status != 0 ||
      !blocked_result->reverse.empty() || blocked_result->forward.empty())
    return false;
  ntl::net::http::inspection_session_metadata metadata;
  metadata.connection.connection_id = 1;
  const auto blocked_context = ntl::net::http::inspection_context_view::for_request(
      ntl::net::http::protocol::http2, 3, 2,
      ntl::net::http::inspection_stage::message_complete, metadata,
      *blocked_result->request);
  if (policy.decisions_ref().evaluate(blocked_context) !=
      ntl::net::inspection::verdict::block)
    return false;

  auto response_exchanges =
      std::make_shared<ntl::net::http2::exchange_store>();
  if (!response_exchanges->remember(1, allowed).is_ok())
    return false;
  ntl::net::http2::connection_transformer response_transformer(
      ntl::net::http2::connection_direction::responses,
      response_exchanges, policy.transforms_ref(), decoders, encoders);
  constexpr std::string_view body =
      "<html><body>contract</body></html>";
  ntl::net::http::response_message response;
  response.wire_protocol = ntl::net::http::protocol::http2;
  response.status = 200;
  response.headers.append("content-type", "text/html; charset=utf-8");
  response.headers.append("content-length", std::to_string(body.size()));
  auto encoded_response = ntl::net::http2::encode_response_frames(
      1, response, std::as_bytes(std::span(body)));
  if (!encoded_response)
    return false;
  std::optional<ntl::net::http2::connection_transform_result>
      response_result;
  for (const auto &encoded : *encoded_response) {
    auto frame = ntl::net::http2::frame_view::parse(
        ntl::net::scatter_view::from_contiguous(encoded.wire));
    if (!frame)
      return false;
    auto transformed = response_transformer.consume(*frame);
    if (!transformed)
      return false;
    if (transformed->message_complete)
      response_result = std::move(*transformed);
  }
  if (!response_result || !response_result->response ||
      response_result->forward.empty())
    return false;
  const std::string transformed_body(
      reinterpret_cast<const char *>(response_result->response->body.data()),
      response_result->response->body.size());
  return transformed_body.find("inspected by ntl") !=
         std::string::npos;
}

} // namespace

int main() {
  try {
    if (!run_contract()) {
      std::cerr << "TLS inspection HTTP/2 contract failed\n";
      return 1;
    }
    std::cout << "TLS inspection HTTP/2 contract passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "TLS inspection HTTP/2 contract failed: " << error.what()
              << '\n';
    return 1;
  }
}
