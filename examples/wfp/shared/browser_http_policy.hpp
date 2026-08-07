#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ntl/net/grpc/transform>
#include <ntl/net/http/inspection_conditions>
#include <ntl/net/http/inspection_policy>
#include <ntl/net/http/standard_inspection_policy>
#include <ntl/net/http/transform>
#include <ntl/net/http3/webtransport_transform>
#include <ntl/net/inspection/content_decoder>
#include <ntl/net/inspection/content_encoder>
#include <ntl/net/inspection/standard_content_decoders>
#include <ntl/net/inspection/standard_content_encoders>

namespace crtsys::wfp_browser_http_policy {

inline constexpr char inspected_header_name[] = "x-ntl-inspected";
inline constexpr char inspected_header_value[] = "1";
inline constexpr std::string_view inspected_html_marker =
    "<!-- inspected and transformed by ntl -->";
inline constexpr std::string_view blocked_body =
    "<html><body>blocked by browser inspection policy</body></html>";
inline constexpr std::string_view webtransport_fixture_input =
    "client-payload";
inline constexpr std::string_view webtransport_fixture_output =
    "ntl-inspected-payload";

inline ntl::net::http::transform_limits transform_limits() noexcept {
  return {.maximum_header_count = 256,
          .maximum_header_bytes = 64 * 1024,
          .maximum_encoded_body_bytes = 4 * 1024 * 1024,
          .maximum_decoded_body_bytes = 16 * 1024 * 1024,
          .maximum_expansion_ratio = 64,
          .maximum_coding_layers = 4,
          .on_failure =
              ntl::net::http::transform_failure_policy::block};
}

inline bool ascii_equal_ci(std::string_view left,
                           std::string_view right) noexcept {
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index != left.size(); ++index) {
    const auto lower = [](unsigned char value) noexcept {
      return value >= 'A' && value <= 'Z'
                 ? static_cast<unsigned char>(value + ('a' - 'A'))
                 : value;
    };
    if (lower(static_cast<unsigned char>(left[index])) !=
        lower(static_cast<unsigned char>(right[index])))
      return false;
  }
  return true;
}

inline bool grpc_content_type(std::string_view value) noexcept {
  constexpr std::string_view prefix = "application/grpc";
  if (value.size() < prefix.size() ||
      !ascii_equal_ci(value.substr(0, prefix.size()), prefix))
    return false;
  std::string_view suffix = value.substr(prefix.size());
  if (suffix.empty() || suffix.front() == '+')
    return true;
  while (!suffix.empty() &&
         (suffix.front() == ' ' || suffix.front() == '\t'))
    suffix.remove_prefix(1);
  return suffix.empty() || suffix.front() == ';';
}

inline bool unsupported_extended_connect(
    const ntl::net::http::request_message &request) noexcept {
  if (!ascii_equal_ci(request.method, "CONNECT"))
    return false;
  if (!request.extended_protocol)
    return true;
  if (request.wire_protocol == ntl::net::http::protocol::http2 &&
      ascii_equal_ci(*request.extended_protocol, "websocket"))
    return false;
  return request.wire_protocol != ntl::net::http::protocol::http3 ||
         !ascii_equal_ci(
             *request.extended_protocol,
             ntl::net::http3::webtransport::upgrade_token);
}

inline constexpr std::string_view grpc_transform_fixture =
    "ntl-grpc-transform";

inline bool grpc_payload_equals(
    const std::vector<std::byte> &payload,
    std::string_view value) noexcept {
  return payload.size() == value.size() &&
         std::equal(
             payload.begin(), payload.end(),
             reinterpret_cast<const std::byte *>(value.data()));
}

inline ntl::net::http::response_message blocked_response(
    ntl::net::http::protocol protocol) {
  ntl::net::http::response_message response;
  response.wire_protocol = protocol;
  response.status = 403;
  response.headers.append("content-type", "text/html; charset=utf-8");
  response.body.assign(
      reinterpret_cast<const std::byte *>(blocked_body.data()),
      reinterpret_cast<const std::byte *>(
          blocked_body.data() + blocked_body.size()));
  return response;
}

inline void configure_grpc_transforms(
    ntl::net::grpc::message_transform_pipeline &grpc) {
  grpc.transform([](ntl::net::grpc::semantic_message &message) {
    constexpr std::string_view request_value =
        "ntl-grpc-transform|request";
    const bool fixture =
        grpc_payload_equals(message.payload, grpc_transform_fixture) ||
        (message.flow == ntl::net::grpc::direction::response &&
         grpc_payload_equals(message.payload, request_value));
    if (!fixture)
      return ntl::net::grpc::transform_result::unchanged();
    const std::string_view suffix =
        message.flow == ntl::net::grpc::direction::request
            ? std::string_view("|request")
            : std::string_view("|response");
    message.payload.insert(
        message.payload.end(),
        reinterpret_cast<const std::byte *>(suffix.data()),
        reinterpret_cast<const std::byte *>(suffix.data() + suffix.size()));
    return ntl::net::grpc::transform_result::replace(
        std::move(message.payload));
  });
}

inline void configure_http_transforms(
    ntl::net::http::transform_pipeline &http) {
  http.requests().transform([](ntl::net::http::request_message &request) {
    return request.headers.erase("proxy-connection") != 0
               ? ntl::net::http::rewrite_result::headers_changed()
               : ntl::net::http::rewrite_result::unchanged();
  });
  http.requests().transform([](ntl::net::http::request_message &request) {
    request.headers.set(inspected_header_name, inspected_header_value);
    return ntl::net::http::rewrite_result::headers_changed();
  });
  http.requests().transform(
      [](ntl::net::http::request_message &request) {
        bool blocked = request.headers.first("x-ntl-block") == "1";
        constexpr std::string_view marker = "BLOCKME";
        blocked = blocked ||
            std::search(
                request.body.begin(), request.body.end(),
                reinterpret_cast<const std::byte *>(marker.data()),
                reinterpret_cast<const std::byte *>(
                    marker.data() + marker.size())) != request.body.end();
        return blocked
                   ? ntl::net::http::rewrite_result::respond(
                         blocked_response(request.wire_protocol))
                   : ntl::net::http::rewrite_result::unchanged();
      });
  http.responses().html().transform(
      [](const ntl::net::http::request_message &,
         ntl::net::http::response_message &response) {
        response.body.insert(
            response.body.end(),
            reinterpret_cast<const std::byte *>(inspected_html_marker.data()),
            reinterpret_cast<const std::byte *>(inspected_html_marker.data() +
                                                 inspected_html_marker.size()));
        return ntl::net::http::rewrite_result::replace_body(
            std::move(response.body),
            ntl::net::http::transformed_body_coding::preserve);
      });
}

inline void configure_protocol_transforms(
    ntl::net::http::transform_pipeline &http,
    std::shared_ptr<const ntl::net::grpc::message_transform_pipeline> grpc,
    std::shared_ptr<const ntl::net::inspection::content_decoder_registry>
        decoders,
    std::shared_ptr<const ntl::net::inspection::content_encoder_registry>
        encoders) {
  http.requests()
      .when([](const ntl::net::http::request_message &request) noexcept {
        return unsupported_extended_connect(request);
      })
      .decide([](const ntl::net::http::request_message &) noexcept {
        return ntl::net::inspection::verdict::block;
      });
  http.requests()
      .when([](const ntl::net::http::request_message &request) noexcept {
        const auto value = request.headers.first("content-type");
        return value && grpc_content_type(*value);
      })
      .transform([grpc, decoders, encoders](
                     ntl::net::http::request_message &request) {
        ntl::net::grpc::stream_transformer transformer(
            ntl::net::grpc::direction::request,
            std::string(request.headers.first("grpc-encoding")
                            .value_or(std::string_view{})),
            *grpc, *decoders, *encoders);
        auto outcome = transformer.feed(request.body, true);
        return outcome.action == ntl::net::grpc::transform_action::forward &&
                       outcome.failure == STATUS_SUCCESS
                   ? ntl::net::http::rewrite_result::replace_body(
                         std::move(outcome.wire))
                   : ntl::net::http::rewrite_result::block();
      });
  http.responses()
      .when([](const ntl::net::http::request_message &,
               const ntl::net::http::response_message &response) noexcept {
        const auto value = response.headers.first("content-type");
        return value && grpc_content_type(*value);
      })
      .transform([grpc, decoders, encoders](
                     const ntl::net::http::request_message &,
                     ntl::net::http::response_message &response) {
        ntl::net::grpc::stream_transformer transformer(
            ntl::net::grpc::direction::response,
            std::string(response.headers.first("grpc-encoding")
                            .value_or(std::string_view{})),
            *grpc, *decoders, *encoders);
        auto outcome = transformer.feed(response.body, true);
        return outcome.action == ntl::net::grpc::transform_action::forward &&
                       outcome.failure == STATUS_SUCCESS
                   ? ntl::net::http::rewrite_result::replace_body(
                         std::move(outcome.wire))
                   : ntl::net::http::rewrite_result::block();
      });
}

inline void configure_decisions(
    ntl::net::http::inspection_policy &policy) {
  using namespace ntl::net::http::condition;

  policy.requests()
      .at_headers()
      .when(method_is_any({"POST", "PUT"}))
      .when(path_is("/ntl-policy-block"))
      .when(header_is("x-ntl-policy", "block"))
      .decide([](const ntl::net::http::inspection_context_view &) {
        return ntl::net::inspection::verdict::block;
      });
  policy.requests()
      .at_message_complete()
      .when(header_is("x-ntl-body-policy", "enabled"))
      .when(complete_body_contains("NTL-DROP-FLOW"))
      .decide([](const ntl::net::http::inspection_context_view &) {
        return ntl::net::inspection::verdict::drop_flow;
      });
  policy.responses()
      .at_message_complete()
      .when(request_header_is("x-ntl-block-response", "1"))
      .when(response_status_between(400, 599))
      .decide([](const ntl::net::http::inspection_context_view &) {
        return ntl::net::inspection::verdict::block;
      });
}

inline void configure_browser_inspection_policy(
    ntl::net::http::inspection_policy &policy,
    std::shared_ptr<const ntl::net::grpc::message_transform_pipeline> grpc,
    std::shared_ptr<const ntl::net::inspection::content_decoder_registry>
        decoders,
    std::shared_ptr<const ntl::net::inspection::content_encoder_registry>
        encoders) {
  grpc = policy.own_dependency(std::move(grpc));
  policy.use_content_codecs(decoders, encoders);
  configure_http_transforms(policy.transforms_ref());
  configure_protocol_transforms(policy.transforms_ref(), grpc, decoders,
                                encoders);
  configure_decisions(policy);
}

inline std::shared_ptr<ntl::net::http::inspection_policy>
make_browser_inspection_policy(
    std::shared_ptr<const ntl::net::grpc::message_transform_pipeline> grpc,
    std::shared_ptr<const ntl::net::inspection::content_decoder_registry>
        decoders,
    std::shared_ptr<const ntl::net::inspection::content_encoder_registry>
        encoders) {
  auto result = std::make_shared<ntl::net::http::inspection_policy>(
      transform_limits());
  configure_browser_inspection_policy(
      *result, std::move(grpc), std::move(decoders), std::move(encoders));
  return result;
}

inline std::shared_ptr<ntl::net::http::inspection_policy>
make_browser_inspection_policy(
    std::shared_ptr<const ntl::net::grpc::message_transform_pipeline> grpc) {
  auto result = ntl::net::http::make_standard_inspection_policy(
      transform_limits());
  configure_browser_inspection_policy(
      *result, std::move(grpc), result->content_decoders(),
      result->content_encoders());
  return result;
}

inline std::shared_ptr<ntl::net::http::inspection_policy>
make_browser_inspection_policy() {
  auto grpc = std::make_shared<
      ntl::net::grpc::message_transform_pipeline>();
  configure_grpc_transforms(*grpc);
  return make_browser_inspection_policy(std::move(grpc));
}

inline void configure_browser_webtransport_policy(
    ntl::net::http3::webtransport::transform_session &policy) {
  policy.inspect(
      [](const ntl::net::http3::webtransport::payload &) {
        return ntl::net::inspection::verdict::permit;
      });
  policy.transform([](ntl::net::http3::webtransport::payload &payload) {
    const std::string_view value(
        reinterpret_cast<const char *>(payload.bytes.data()),
        payload.bytes.size());
    if (value != webtransport_fixture_input)
      return ntl::net::http3::webtransport::transform_result::unchanged();
    return ntl::net::http3::webtransport::transform_result::replace(
        std::vector<std::byte>(
            reinterpret_cast<const std::byte *>(
                webtransport_fixture_output.data()),
            reinterpret_cast<const std::byte *>(
                webtransport_fixture_output.data() +
                webtransport_fixture_output.size())));
  });
}

inline std::shared_ptr<
    ntl::net::http3::webtransport::transform_session>
make_browser_webtransport_policy() {
  auto result = std::make_shared<
      ntl::net::http3::webtransport::transform_session>(
      ntl::net::http3::webtransport::transform_limits{
          .session = {.maximum_bidirectional_streams = 8,
                      .maximum_unidirectional_streams = 8,
                      .maximum_stream_data = 64 * 1024,
                      .maximum_datagram_payload = 4096,
                      .maximum_datagrams = 32},
          .maximum_rewritten_payload = 4096,
          .maximum_expansion_ratio = 4});
  configure_browser_webtransport_policy(*result);
  return result;
}

} // namespace crtsys::wfp_browser_http_policy
