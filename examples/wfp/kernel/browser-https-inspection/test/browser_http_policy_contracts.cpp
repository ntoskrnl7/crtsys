#include "browser_http_policy.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <ntl/net/inspection/standard_content_decoders>
#include <ntl/net/inspection/standard_content_encoders>

namespace {

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

std::string payload_text(std::span<const std::byte> wire) {
  if (wire.size() < 5)
    throw std::runtime_error("gRPC wire message is truncated");
  return {reinterpret_cast<const char *>(wire.data() + 5), wire.size() - 5};
}

ntl::net::http::request_message make_request(
    ntl::net::http::protocol protocol,
    std::span<const std::byte> wire) {
  ntl::net::http::request_message request;
  request.wire_protocol = protocol;
  request.method = "POST";
  request.authority = "kernel.example";
  request.path = "/sample.Service/Inspect";
  request.headers.append("content-type", "application/grpc+proto");
  request.body.assign(wire.begin(), wire.end());
  return request;
}

} // namespace

int main() {
  try {
  auto decoders = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  ntl::net::inspection::register_standard_content_decoders(*decoders);
  auto encoders = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  ntl::net::inspection::register_standard_content_encoders(*encoders);
  auto grpc =
      std::make_shared<ntl::net::grpc::message_transform_pipeline>();
  crtsys::wfp_browser_http_policy::configure_grpc_transforms(*grpc);
  ntl::net::http::inspection_policy policy(
      crtsys::wfp_browser_http_policy::transform_limits());
  crtsys::wfp_browser_http_policy::configure_browser_inspection_policy(
      policy, grpc, decoders, encoders);
  auto &http = policy.transforms_ref();

  const auto fixture = std::as_bytes(std::span<const char>(
      crtsys::wfp_browser_http_policy::grpc_transform_fixture.data(),
      crtsys::wfp_browser_http_policy::grpc_transform_fixture.size()));
  const auto encoded = ntl::net::grpc::encode_message(fixture, false, 4096);
  require(encoded.has_value(), "cannot encode gRPC fixture");

  for (const auto protocol : {
           ntl::net::http::protocol::http1,
           ntl::net::http::protocol::http2,
           ntl::net::http::protocol::http3}) {
    auto request = make_request(protocol, *encoded);
    const auto request_outcome = http.apply(request);
    require(request_outcome.action == ntl::net::http::rewrite_action::forward &&
                request_outcome.body_modified &&
                payload_text(request.body) == "ntl-grpc-transform|request",
            "kernel gRPC request transform contract failed");

    ntl::net::http::response_message response;
    response.wire_protocol = protocol;
    response.status = 200;
    response.headers.append("content-type", "application/grpc");
    response.body = request.body;
    const auto response_outcome = http.apply(request, response);
    require(response_outcome.action ==
                ntl::net::http::rewrite_action::forward &&
                response_outcome.body_modified &&
                payload_text(response.body) ==
                    "ntl-grpc-transform|request|response",
            "kernel gRPC response transform contract failed");
  }

  const std::array<std::byte, 3> truncated{};
  auto malformed = make_request(
      ntl::net::http::protocol::http2, truncated);
  require(http.apply(malformed).action ==
              ntl::net::http::rewrite_action::block,
          "truncated gRPC message did not fail closed");

  ntl::net::http::request_message connect;
  connect.wire_protocol = ntl::net::http::protocol::http2;
  connect.method = "CONNECT";
  connect.authority = "kernel.example";
  connect.path = "/tunnel";
  connect.extended_protocol = "webtransport";
  require(http.apply(connect).action == ntl::net::http::rewrite_action::block,
          "unsupported HTTP/2 WebTransport tunnel was not blocked");

  connect.extended_protocol = "websocket";
  require(http.apply(connect).action ==
              ntl::net::http::rewrite_action::forward,
          "supported HTTP/2 WebSocket tunnel was blocked");
  connect.wire_protocol = ntl::net::http::protocol::http3;
  require(http.apply(connect).action == ntl::net::http::rewrite_action::block,
          "unsupported HTTP/3 WebSocket tunnel was not blocked");
  connect.extended_protocol =
      ntl::net::http3::webtransport::upgrade_token;
  require(http.apply(connect).action ==
              ntl::net::http::rewrite_action::forward,
          "supported HTTP/3 WebTransport tunnel was blocked");
  connect.extended_protocol = "webtransport";
  require(http.apply(connect).action == ntl::net::http::rewrite_action::block,
          "unsupported HTTP/3 WebTransport token was not blocked");
  connect.extended_protocol =
      ntl::net::http3::webtransport::upgrade_token;
  connect.headers.set("x-ntl-block", "1");
  require(http.apply(connect).action ==
              ntl::net::http::rewrite_action::respond,
          "HTTP/3 WebTransport bypassed the shared request policy");
  connect.headers.erase("x-ntl-block");
  connect.extended_protocol.reset();
  connect.scheme.clear();
  connect.path.clear();
  require(http.apply(connect).action == ntl::net::http::rewrite_action::block,
          "ordinary CONNECT tunnel was not blocked");

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

  require(!crtsys::wfp_browser_http_policy::grpc_content_type(
              "application/grpcfoo"),
          "non-gRPC media type was misclassified");

  ntl::net::http3::webtransport::transform_session webtransport_policy;
  crtsys::wfp_browser_http_policy::configure_browser_webtransport_policy(
      webtransport_policy);
  ntl::net::http3::webtransport::payload webtransport_payload{
      .kind = ntl::net::http3::webtransport::payload_kind::stream,
      .bytes = std::vector<std::byte>(
          reinterpret_cast<const std::byte *>(
              crtsys::wfp_browser_http_policy::webtransport_fixture_input.data()),
          reinterpret_cast<const std::byte *>(
              crtsys::wfp_browser_http_policy::webtransport_fixture_input.data() +
              crtsys::wfp_browser_http_policy::webtransport_fixture_input.size()))};
  const auto webtransport_outcome =
      webtransport_policy.apply(webtransport_payload);
  require(webtransport_outcome.modified &&
              std::string_view(
                  reinterpret_cast<const char *>(webtransport_payload.bytes.data()),
                  webtransport_payload.bytes.size()) ==
                  crtsys::wfp_browser_http_policy::webtransport_fixture_output,
          "shared browser WebTransport policy was not applied");

  std::cout
      << "Kernel browser HTTP policy contracts passed: "
         "grpc-http1=transformed grpc-http2=transformed "
         "grpc-http3=transformed grpc-malformed=fail-closed "
         "h2-websocket=permit h3-webtransport=permit "
         "h3-webtransport-policy-block=pass "
         "h2-webtransport=blocked h3-websocket=blocked "
         "ordinary-connect=blocked media-type-boundary=pass "
         "webtransport-transform=pass staged-decisions=pass\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Kernel browser HTTP policy contract failed: "
              << error.what() << '\n';
    return 1;
  }
}
