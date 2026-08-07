#include "browser_http_policy.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <ntl/net/inspection/standard_content_decoders>
#include <ntl/net/inspection/standard_content_encoders>

namespace {

std::byte byte(char value) noexcept {
  return static_cast<std::byte>(
      static_cast<unsigned char>(value));
}

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

ntl::net::http::request_message grpc_request(
    std::span<const std::byte> body) {
  ntl::net::http::request_message request;
  request.wire_protocol = ntl::net::http::protocol::http2;
  request.method = "POST";
  request.authority = "example.test";
  request.path = "/sample.Service/Inspect";
  request.headers.append("content-type", "application/grpc+proto");
  request.body.assign(body.begin(), body.end());
  return request;
}

std::string grpc_payload_text(std::span<const std::byte> wire) {
  if (wire.size() < 5)
    throw std::runtime_error("gRPC wire message is truncated");
  return std::string(
      reinterpret_cast<const char *>(wire.data() + 5),
      wire.size() - 5);
}

} // namespace

int main() {
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  ntl::net::inspection::register_standard_content_decoders(*decoders);
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  ntl::net::inspection::register_standard_content_encoders(*encoders);
  std::size_t requests = 0;
  std::size_t responses = 0;
  auto grpc =
      std::make_shared<ntl::net::grpc::message_transform_pipeline>();
  grpc->inspect([&](const ntl::net::grpc::semantic_message &message) {
    if (message.flow == ntl::net::grpc::direction::request)
      ++requests;
    else
      ++responses;
    return ntl::net::inspection::verdict::permit;
  });
  crtsys::wfp_browser_http_policy::configure_grpc_transforms(*grpc);
  ntl::net::http::inspection_policy policy(
      crtsys::wfp_browser_http_policy::transform_limits());
  crtsys::wfp_browser_http_policy::configure_browser_inspection_policy(
      policy, grpc, decoders, encoders);
  auto &http = policy.transforms_ref();

  const auto payload = std::as_bytes(std::span(
      crtsys::wfp_browser_http_policy::grpc_transform_fixture));
  const auto wire = ntl::net::grpc::encode_message(
      payload, false, 4096);
  require(wire.has_value(), "cannot create a gRPC contract message");

  auto request = grpc_request(*wire);
  const auto request_outcome = http.apply(request);
  require(request_outcome.action ==
              ntl::net::http::rewrite_action::forward &&
              request_outcome.body_modified && requests == 1 &&
              grpc_payload_text(request.body) ==
                  "ntl-grpc-transform|request",
          "gRPC request did not traverse the semantic policy");

  ntl::net::http::response_message response;
  response.wire_protocol = ntl::net::http::protocol::http2;
  response.status = 200;
  response.headers.append("content-type", "application/grpc");
  response.body = request.body;
  const auto response_outcome = http.apply(request, response);
  require(response_outcome.action ==
              ntl::net::http::rewrite_action::forward &&
              response_outcome.body_modified && responses == 1 &&
              grpc_payload_text(response.body) ==
                  "ntl-grpc-transform|request|response",
          "gRPC response did not traverse the semantic policy");

  for (const auto protocol : {
           ntl::net::http::protocol::http1,
           ntl::net::http::protocol::http3}) {
    auto protocol_request = grpc_request(*wire);
    protocol_request.wire_protocol = protocol;
    const auto protocol_request_outcome = http.apply(protocol_request);
    require(protocol_request_outcome.action ==
                ntl::net::http::rewrite_action::forward &&
                protocol_request_outcome.body_modified,
            "gRPC request protocol path did not traverse the policy");

    ntl::net::http::response_message protocol_response;
    protocol_response.wire_protocol = protocol;
    protocol_response.status = 200;
    protocol_response.headers.append(
        "content-type", "application/grpc");
    protocol_response.body = protocol_request.body;
    const auto protocol_response_outcome =
        http.apply(protocol_request, protocol_response);
    require(protocol_response_outcome.action ==
                ntl::net::http::rewrite_action::forward &&
                protocol_response_outcome.body_modified,
            "gRPC response protocol path did not traverse the policy");
  }
  require(requests == 3 && responses == 3,
          "HTTP/1.1, HTTP/2, and HTTP/3 gRPC paths were not all inspected");

  const std::array<std::byte, 3> truncated{
      byte(0), byte(0), byte(0)};
  auto malformed = grpc_request(truncated);
  const auto malformed_outcome = http.apply(malformed);
  require(malformed_outcome.action ==
              ntl::net::http::rewrite_action::block,
          "truncated gRPC message did not fail closed");

  ntl::net::http::request_message unsupported;
  unsupported.wire_protocol = ntl::net::http::protocol::http2;
  unsupported.method = "CONNECT";
  unsupported.authority = "example.test";
  unsupported.path = "/webtransport";
  unsupported.extended_protocol = "webtransport";
  require(http.apply(unsupported).action ==
              ntl::net::http::rewrite_action::block,
          "unsupported Extended CONNECT did not fail closed");

  ntl::net::http::request_message websocket = unsupported;
  websocket.extended_protocol = "WebSocket";
  require(http.apply(websocket).action ==
              ntl::net::http::rewrite_action::forward,
          "WebSocket Extended CONNECT was incorrectly blocked");

  ntl::net::http::request_message http3_websocket = websocket;
  http3_websocket.wire_protocol = ntl::net::http::protocol::http3;
  require(http.apply(http3_websocket).action ==
              ntl::net::http::rewrite_action::block,
          "unsupported HTTP/3 WebSocket tunnel did not fail closed");

  ntl::net::http::request_message webtransport = unsupported;
  webtransport.wire_protocol = ntl::net::http::protocol::http3;
  webtransport.extended_protocol =
      ntl::net::http3::webtransport::upgrade_token;
  require(http.apply(webtransport).action ==
              ntl::net::http::rewrite_action::forward,
          "HTTP/3 WebTransport Extended CONNECT was incorrectly blocked");

  ntl::net::http::request_message legacy_webtransport = webtransport;
  legacy_webtransport.extended_protocol = "webtransport";
  require(http.apply(legacy_webtransport).action ==
              ntl::net::http::rewrite_action::block,
          "unsupported HTTP/3 WebTransport token did not fail closed");

  ntl::net::http::request_message ordinary_connect = unsupported;
  ordinary_connect.extended_protocol.reset();
  ordinary_connect.scheme.clear();
  ordinary_connect.path.clear();
  require(http.apply(ordinary_connect).action ==
              ntl::net::http::rewrite_action::block,
          "ordinary CONNECT tunnel did not fail closed");

  ntl::net::http::inspection_session_metadata session;
  ntl::net::http::request_message blocked_request;
  blocked_request.wire_protocol = ntl::net::http::protocol::http3;
  blocked_request.method = "POST";
  blocked_request.path = "/ntl-policy-block";
  blocked_request.headers.append("x-ntl-policy", "block");
  auto blocked_context = ntl::net::http::inspection_context_view::for_request(
      ntl::net::http::protocol::http3, 4, 4,
      ntl::net::http::inspection_stage::headers, session, blocked_request);
  require(policy.decisions_ref().evaluate(blocked_context) ==
              ntl::net::inspection::verdict::block,
          "shared staged header decision did not block");
  blocked_request.path = "/body";
  blocked_request.headers.erase("x-ntl-policy");
  blocked_request.headers.append("x-ntl-body-policy", "enabled");
  constexpr std::string_view drop_marker = "NTL-DROP-FLOW";
  blocked_request.body.assign(
      reinterpret_cast<const std::byte *>(drop_marker.data()),
      reinterpret_cast<const std::byte *>(drop_marker.data() +
                                           drop_marker.size()));
  auto drop_context = ntl::net::http::inspection_context_view::for_request(
      ntl::net::http::protocol::http3, 4, 4,
      ntl::net::http::inspection_stage::message_complete, session,
      blocked_request);
  require(policy.decisions_ref().evaluate(drop_context) ==
              ntl::net::inspection::verdict::drop_flow,
          "shared complete-body decision did not drop the flow");

  ntl::net::http::request_message ordinary;
  ordinary.wire_protocol = ntl::net::http::protocol::http2;
  ordinary.method = "POST";
  ordinary.authority = "example.test";
  ordinary.path = "/not-grpc";
  ordinary.headers.append("content-type", "application/grpcfoo");
  ordinary.body = {byte('n'), byte('o'), byte('t'), byte('-'), byte('g'),
                   byte('r'), byte('p'), byte('c')};
  const auto ordinary_outcome = http.apply(ordinary);
  require(ordinary_outcome.action ==
              ntl::net::http::rewrite_action::forward &&
              !ordinary_outcome.body_modified && requests == 3,
          "non-gRPC media type was misclassified as gRPC");

  require(crtsys::wfp_browser_http_policy::grpc_content_type(
              "Application/Grpc+Proto") &&
              crtsys::wfp_browser_http_policy::grpc_content_type(
                  "application/grpc ; charset=binary") &&
              !crtsys::wfp_browser_http_policy::grpc_content_type(
                  "application/grpcfoo"),
          "gRPC media-type boundary classification is incorrect");

  ntl::net::http3::webtransport::transform_session webtransport_transforms;
  crtsys::wfp_browser_http_policy::configure_browser_webtransport_policy(
      webtransport_transforms);
  ntl::net::http3::webtransport::payload webtransport_payload{
      .kind = ntl::net::http3::webtransport::payload_kind::stream,
      .bytes = std::vector<std::byte>(
          reinterpret_cast<const std::byte *>(
              crtsys::wfp_browser_http_policy::webtransport_fixture_input.data()),
          reinterpret_cast<const std::byte *>(
              crtsys::wfp_browser_http_policy::webtransport_fixture_input.data() +
              crtsys::wfp_browser_http_policy::webtransport_fixture_input.size()))};
  const auto webtransport_outcome =
      webtransport_transforms.apply(webtransport_payload);
  require(webtransport_outcome.modified &&
              std::string_view(
                  reinterpret_cast<const char *>(webtransport_payload.bytes.data()),
                  webtransport_payload.bytes.size()) ==
                  crtsys::wfp_browser_http_policy::webtransport_fixture_output,
          "shared browser WebTransport policy was not applied");

  std::cout
      << "Browser HTTP policy contracts passed: "
         "grpc-http1=pass grpc-http2=pass grpc-http3=pass "
         "grpc-request=transformed grpc-response=transformed "
         "grpc-malformed=blocked "
         "non-grpc-media-type=pass "
         "h2-unsupported-extended-connect=blocked "
         "ordinary-connect=blocked "
         "websocket-extended-connect=pass "
         "h3-websocket-extended-connect=blocked "
         "h3-webtransport-extended-connect=pass "
         "webtransport-transform=pass staged-decisions=pass\n";
  return 0;
}
