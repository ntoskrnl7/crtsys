#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ntl/net/http/inspection_conditions>
#include <ntl/net/http/inspection_policy>
#include <ntl/net/http/standard_inspection_policy>
#include <ntl/net/http3/webtransport_transform>
#include <ntl/net/http3/inspection_proxy>
#include <ntl/net/http3/proxy_connection>
#include <ntl/net/inspection/standard_content_decoders>
#include <ntl/net/inspection/standard_content_encoders>

namespace crtsys::examples::wfp::http3_inspection {

inline constexpr std::string_view allowed_html =
    "<!doctype html><html><body>NTL HTTP/3 inspection allowed</body></html>";
inline constexpr std::string_view blocked_html =
    "<!doctype html><html><body>blocked by NTL HTTP/3 policy</body></html>";
inline constexpr std::string_view webtransport_fixture_input =
    "client-payload";
inline constexpr std::string_view webtransport_fixture_output =
    "ntl-inspected-payload";

/**
 * @brief Returns true when any sample policy header explicitly requests a
 * block.
 *
 * Header fields can legally be repeated.  Treating only the last occurrence
 * as authoritative would let a later `x-ntl-block: 0` hide an earlier block
 * request, so both user- and kernel-runtime examples use this any-match rule.
 */
template <class HeaderField>
bool block_requested(std::span<const HeaderField> fields) noexcept {
  for (const auto &field : fields) {
    if (field.name == std::string_view{"x-ntl-block"} &&
        field.value == std::string_view{"1"})
      return true;
  }
  return false;
}

template <class HeaderField>
bool ordinary_request_blocked(
    std::span<const HeaderField> fields) noexcept {
  std::string_view method;
  std::string_view path;
  for (const auto &field : fields) {
    if (field.name == std::string_view{":method"})
      method = field.value;
    else if (field.name == std::string_view{":path"})
      path = field.value;
  }
  return method == "GET" && path == "/blocked" &&
         block_requested(fields);
}

inline void configure_ordinary_policy(
    ntl::net::http::inspection_policy &policy) {
  using namespace ntl::net::http::condition;
  policy.requests()
      .at_headers()
      .when(method_is("GET"))
      .when(path_is("/blocked"))
      .when(header_is("x-ntl-block", "1"))
      .decide([](const ntl::net::http::inspection_context_view &) {
        return ntl::net::inspection::verdict::block;
      });
  policy.requests()
      .at_headers()
      .when(method_is("CONNECT"))
      .when(path_is("/webtransport"))
      .when(header_is("x-ntl-block", "1"))
      .decide([](const ntl::net::http::inspection_context_view &) {
        return ntl::net::inspection::verdict::block;
      });
}

inline void configure_webtransport_policy(
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
make_webtransport_policy() {
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
  configure_webtransport_policy(*result);
  return result;
}

inline std::shared_ptr<ntl::net::inspection::content_decoder_registry>
make_standard_decoders() {
  auto result = std::make_shared<
      ntl::net::inspection::content_decoder_registry>();
  ntl::net::inspection::register_standard_content_decoders(*result);
  return result;
}

inline std::shared_ptr<ntl::net::inspection::content_encoder_registry>
make_standard_encoders() {
  auto result = std::make_shared<
      ntl::net::inspection::content_encoder_registry>();
  ntl::net::inspection::register_standard_content_encoders(*result);
  return result;
}

inline std::shared_ptr<ntl::net::http::inspection_policy>
make_ordinary_policy() {
  auto result = ntl::net::http::make_standard_inspection_policy();
  configure_ordinary_policy(*result);
  return result;
}

class ordinary_origin final : public ntl::net::http3::origin_transport {
public:
  explicit ordinary_origin(
      std::shared_ptr<const ntl::net::inspection::content_encoder_registry>
          encoders)
      : encoders_(std::move(encoders)) {
    if (!encoders_)
      throw std::invalid_argument("HTTP/3 encoder registry is null");
  }

  ntl::result<ntl::net::http3::origin_response>
  send(const ntl::net::http3::origin_request &request) noexcept override {
    try {
      if (request.method != "GET" || !request.body.empty() ||
          !request.trailers.empty())
        return ntl::unexpected(STATUS_DATA_ERROR);
      std::string encoding;
      if (request.path == "/gzip")
        encoding = "gzip";
      else if (request.path == "/deflate")
        encoding = "deflate";
      else if (request.path == "/br")
        encoding = "br";
      else if (request.path != "/allowed")
        return ntl::unexpected(STATUS_NOT_FOUND);

      ntl::net::http3::origin_response response;
      response.status = 200;
      response.headers = {{"content-type", "text/html"}};
      if (encoding.empty()) {
        response.body.assign(
            reinterpret_cast<const std::byte *>(allowed_html.data()),
            reinterpret_cast<const std::byte *>(
                allowed_html.data() + allowed_html.size()));
      } else {
        auto body = ntl::net::inspection::encode_content_encoding(
            *encoders_, std::as_bytes(std::span(allowed_html)), encoding,
            {.maximum_input_size = 64 * 1024,
             .maximum_encoded_size = 64 * 1024,
             .maximum_coding_layers = 1});
        if (!body)
          return ntl::unexpected(body.status());
        response.headers.emplace_back("content-encoding", encoding);
        response.body = std::move(*body);
      }
      response.negotiated_protocol = "h3";
      return ntl::ok(std::move(response));
    } catch (const std::bad_alloc &) {
      return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return ntl::unexpected(STATUS_UNHANDLED_EXCEPTION);
    }
  }

private:
  std::shared_ptr<const ntl::net::inspection::content_encoder_registry>
      encoders_;
};

class ordinary_terminal_responses final
    : public ntl::net::http3::proxy_terminal_response_provider {
public:
  ntl::result<ntl::net::http::response_message>
  response_for(
      const ntl::net::http3::proxy_terminal_context &context) noexcept override {
    try {
      ntl::net::http::response_message response;
      response.wire_protocol = ntl::net::http::protocol::http3;
      response.headers.append("content-type", "text/html; charset=utf-8");
      if (context.reason ==
          ntl::net::http3::proxy_terminal_reason::origin_unavailable) {
        response.status = 502;
        constexpr std::string_view unavailable =
            "<html><body>origin transport unavailable</body></html>";
        response.body.assign(
            reinterpret_cast<const std::byte *>(unavailable.data()),
            reinterpret_cast<const std::byte *>(
                unavailable.data() + unavailable.size()));
      } else {
        response.status = 403;
        response.body.assign(
            reinterpret_cast<const std::byte *>(blocked_html.data()),
            reinterpret_cast<const std::byte *>(
                blocked_html.data() + blocked_html.size()));
      }
      return ntl::ok(std::move(response));
    } catch (const std::bad_alloc &) {
      return ntl::unexpected(STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return ntl::unexpected(STATUS_UNHANDLED_EXCEPTION);
    }
  }
};

} // namespace crtsys::examples::wfp::http3_inspection
