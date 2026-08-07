#include <zlib.h>

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ntl/net/http3/inspection_proxy>
#include <ntl/net/http3/standard_inspection_proxy>
#include <ntl/net/http/transform>
#include <ntl/net/inspection/standard_content_decoders>

namespace {

std::vector<std::byte> bytes_of(std::string_view value) {
  const auto bytes = std::as_bytes(std::span(value));
  return {bytes.begin(), bytes.end()};
}

std::vector<std::byte>
gzip_encode(std::span<const std::byte> input) {
  if (input.size() >
      (std::numeric_limits<uInt>::max)())
    return {};
  z_stream stream{};
  if (::deflateInit2(
          &stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
          MAX_WBITS + 16, 8,
          Z_DEFAULT_STRATEGY) != Z_OK)
    return {};
  struct cleanup {
    z_stream *stream;
    ~cleanup() { (void)::deflateEnd(stream); }
  } cleanup{&stream};
  std::vector<std::byte> output(
      static_cast<std::size_t>(
          ::deflateBound(
              &stream, static_cast<uLong>(input.size()))));
  stream.next_in = reinterpret_cast<Bytef *>(
      const_cast<std::byte *>(input.data()));
  stream.avail_in = static_cast<uInt>(input.size());
  stream.next_out =
      reinterpret_cast<Bytef *>(output.data());
  stream.avail_out = static_cast<uInt>(output.size());
  if (::deflate(&stream, Z_FINISH) != Z_STREAM_END)
    return {};
  output.resize(stream.total_out);
  return output;
}

ntl::net::http3::incoming_request make_request(
    std::string_view authority =
        "inspection-proxy.test") {
  ntl::net::http3::incoming_request request;
  request.server_name = "inspection-proxy.test";
  request.headers = {
      {":method", "GET"},
      {":scheme", "https"},
      {":authority", std::string(authority)},
      {":path", "/document"},
      {"accept", "text/html"}};
  return request;
}

class fake_origin final
    : public ntl::net::http3::origin_transport {
public:
  ntl::result<ntl::net::http3::origin_response>
  send(const ntl::net::http3::origin_request &request)
      noexcept override {
    ++calls;
    last_request = request;
    last_server_name = request.server_name;
    last_authority = request.authority;
    return ntl::ok(response);
  }

  ntl::net::http3::origin_response response;
  std::size_t calls = 0;
  ntl::net::http3::origin_request last_request;
  std::string last_server_name;
  std::string last_authority;
};

class recording_policy final
    : public ntl::net::http3::inspection_policy {
public:
  ntl::net::inspection::verdict inspect_request(
      const ntl::net::http3::request_view &request)
      noexcept override {
    ++request_calls;
    request_body.assign(
        request.decoded_body.begin(),
        request.decoded_body.end());
    return request_verdict;
  }

  ntl::net::inspection::verdict inspect_response(
      const ntl::net::http3::response_view &response)
      noexcept override {
    ++response_calls;
    response_body.assign(
        response.decoded_body.begin(),
        response.decoded_body.end());
    response_encoding.assign(response.content_encoding);
    protocol = response.message.negotiated_protocol;
    return response_verdict;
  }

  void on_failure(NTSTATUS status) noexcept override {
    last_failure = status;
  }

  ntl::net::inspection::verdict request_verdict =
      ntl::net::inspection::verdict::permit;
  ntl::net::inspection::verdict response_verdict =
      ntl::net::inspection::verdict::permit;
  std::size_t request_calls = 0;
  std::size_t response_calls = 0;
  NTSTATUS last_failure = STATUS_SUCCESS;
  std::vector<std::byte> request_body;
  std::vector<std::byte> response_body;
  std::string response_encoding;
  std::string protocol;
};

bool test_valid_gzip_exchange() {
  const std::string html =
      "<!doctype html><html><body>"
      "generic HTTP/3 inspection proxy"
      "</body></html>";
  const auto plain = bytes_of(html);
  const auto gzip = gzip_encode(plain);
  if (gzip.empty())
    return false;

  auto origin_owner = std::make_shared<fake_origin>();
  auto &origin = *origin_owner;
  origin.response.status = 200;
  origin.response.headers = {
      {"content-type", "text/html; charset=utf-8"},
      {"content-encoding", "gzip"},
      {"content-length", std::to_string(gzip.size())}};
  origin.response.body = gzip;
  origin.response.negotiated_protocol = "h3";
  auto policy_owner = std::make_shared<recording_policy>();
  auto &policy = *policy_owner;
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::register_standard_content_decoders(
      decoders);
  ntl::net::http3::inspection_proxy proxy(
      origin_owner, policy_owner, decoders);

  auto response = proxy.forward(make_request());
  return response && response->status == 200 &&
         response->body == gzip && origin.calls == 1 &&
         origin.last_server_name ==
             "inspection-proxy.test" &&
         origin.last_authority ==
             "inspection-proxy.test" &&
         policy.request_calls == 1 &&
         policy.response_calls == 1 &&
         policy.response_body == plain &&
         policy.response_encoding == "gzip" &&
         policy.protocol == "h3" &&
         policy.last_failure == STATUS_SUCCESS;
}

bool test_authority_and_pseudo_header_rules() {
  auto origin_owner = std::make_shared<fake_origin>();
  auto &origin = *origin_owner;
  origin.response.status = 204;
  origin.response.negotiated_protocol = "h3";
  auto policy_owner = std::make_shared<recording_policy>();
  auto &policy = *policy_owner;
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::http3::inspection_proxy proxy(
      origin_owner, policy_owner, decoders);

  auto mismatch =
      proxy.forward(make_request("other.test"));
  if (mismatch || origin.calls != 0 ||
      policy.request_calls != 0)
    return false;

  auto reordered = make_request();
  std::swap(
      reordered.headers[3], reordered.headers[4]);
  auto invalid_order =
      proxy.forward(std::move(reordered));
  return !invalid_order && origin.calls == 0;
}

bool test_origin_protocol_and_bounds() {
  auto origin_owner = std::make_shared<fake_origin>();
  auto &origin = *origin_owner;
  origin.response.status = 200;
  origin.response.negotiated_protocol = "h2";
  auto policy_owner = std::make_shared<recording_policy>();
  auto &policy = *policy_owner;
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::http3::inspection_proxy proxy(
      origin_owner, policy_owner, decoders,
      {.maximum_response_body_bytes = 64});

  auto wrong_protocol = proxy.forward(make_request());
  if (wrong_protocol || origin.calls != 1 ||
      policy.response_calls != 0)
    return false;

  origin.response.negotiated_protocol = "h3";
  origin.response.body =
      std::vector<std::byte>(65, std::byte{0x41});
  auto oversized = proxy.forward(make_request());
  return !oversized && origin.calls == 2 &&
         policy.response_calls == 0;
}

bool test_policy_is_enforced() {
  auto origin_owner = std::make_shared<fake_origin>();
  auto &origin = *origin_owner;
  origin.response.status = 200;
  origin.response.negotiated_protocol = "h3";
  auto policy_owner = std::make_shared<recording_policy>();
  auto &policy = *policy_owner;
  policy.response_verdict =
      ntl::net::inspection::verdict::block;
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::http3::inspection_proxy proxy(
      origin_owner, policy_owner, decoders);

  auto blocked = proxy.forward(make_request());
  return !blocked && origin.calls == 1 &&
         policy.response_calls == 1 &&
         policy.last_failure == STATUS_ACCESS_DENIED;
}

bool test_request_content_is_decoded_before_policy() {
  const auto plain = bytes_of("request-policy-content");
  const auto gzip = gzip_encode(plain);
  if (gzip.empty())
    return false;

  auto origin_owner = std::make_shared<fake_origin>();
  auto &origin = *origin_owner;
  origin.response.status = 204;
  origin.response.negotiated_protocol = "h3";
  auto policy_owner = std::make_shared<recording_policy>();
  auto &policy = *policy_owner;
  policy.request_verdict =
      ntl::net::inspection::verdict::block;
  ntl::net::http3::standard_inspection_proxy proxy(
      origin_owner, policy_owner);

  auto request = make_request();
  request.headers[0].value = "POST";
  request.headers.push_back(
      {"content-encoding", "gzip"});
  request.headers.push_back(
      {"content-length", std::to_string(gzip.size())});
  request.body = gzip;
  auto blocked = proxy.forward(std::move(request));
  return !blocked && origin.calls == 0 &&
         policy.request_calls == 1 &&
         policy.response_calls == 0 &&
         policy.request_body == plain &&
         policy.last_failure == STATUS_ACCESS_DENIED;
}

bool test_functional_convenience_api() {
  std::size_t origin_calls = 0;
  auto origin = ntl::net::http3::make_origin_transport(
      [&origin_calls](
          const ntl::net::http3::origin_request &)
          noexcept
          -> ntl::result<ntl::net::http3::origin_response> {
        ++origin_calls;
        ntl::net::http3::origin_response response;
        response.status = 204;
        response.negotiated_protocol = "h3";
        return ntl::ok(std::move(response));
      });
  std::size_t response_calls = 0;
  auto policy = ntl::net::http3::make_inspection_policy(
      [](const ntl::net::http3::request_view &) noexcept {
        return ntl::net::inspection::verdict::permit;
      },
      [&response_calls](
          const ntl::net::http3::response_view &response)
          noexcept {
        ++response_calls;
        return response.message.status == 204
                   ? ntl::net::inspection::verdict::permit
                   : ntl::net::inspection::verdict::block;
      });
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::http3::inspection_proxy proxy(
      origin, policy, decoders);
  origin.reset();
  policy.reset();
  auto response = proxy.forward(make_request());
  return response && response->status == 204 &&
         origin_calls == 1 && response_calls == 1;
}

bool test_common_transform_pipeline() {
  const auto plain =
      bytes_of("<html><body>http3</body></html>");
  const auto gzip = gzip_encode(plain);
  if (gzip.empty())
    return false;

  auto origin_owner = std::make_shared<fake_origin>();
  auto &origin = *origin_owner;
  origin.response.status = 200;
  origin.response.headers = {
      {"content-type", "text/html"},
      {"content-encoding", "gzip"},
      {"content-length", std::to_string(gzip.size())},
      {"etag", "\"old\""}};
  origin.response.body = gzip;
  origin.response.negotiated_protocol = "h3";
  auto policy_owner = std::make_shared<recording_policy>();

  ntl::net::http::transform_pipeline transforms;
  transforms.requests().transform(
      [](ntl::net::http::request_message &request) {
        request.headers.set("x-ntl-inspected", "h3");
        return ntl::net::http::rewrite_result::
            headers_changed();
      });
  transforms.responses().html().transform(
      [](const ntl::net::http::request_message &,
         ntl::net::http::response_message &response) {
        std::string rewritten(
            reinterpret_cast<const char *>(
                response.body.data()),
            response.body.size());
        rewritten.append("<!-- rewritten-h3 -->");
        return ntl::net::http::rewrite_result::replace_text(
            std::move(rewritten));
      });

  ntl::net::http3::standard_inspection_proxy proxy(
      origin_owner, policy_owner, transforms);
  auto transformed = proxy.forward(make_request());
  if (!transformed || origin.calls != 1 ||
      transformed->body == gzip ||
      transformed->headers.end() !=
          std::find_if(
              transformed->headers.begin(),
              transformed->headers.end(),
              [](const ntl::net::http3::proxy_header &field) {
                return field.name == "etag";
              }))
    return false;
  const auto request_marker = std::find_if(
      origin.last_request.headers.begin(),
      origin.last_request.headers.end(),
      [](const ntl::net::http3::proxy_header &field) {
        return field.name == "x-ntl-inspected" &&
               field.value == "h3";
      });
  if (request_marker == origin.last_request.headers.end())
    return false;

  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::register_standard_content_decoders(
      decoders);
  auto decoded =
      ntl::net::inspection::decode_content_encoding(
          decoders,
          ntl::net::scatter_view::from_contiguous(
              transformed->body),
          "gzip",
          {.maximum_encoded_size = 1024 * 1024,
           .maximum_decoded_size = 1024 * 1024,
           .maximum_expansion_ratio = 64,
           .maximum_coding_layers = 4});
  if (!decoded)
    return false;
  const std::string decoded_text(
      reinterpret_cast<const char *>(decoded->data()),
      decoded->size());
  return decoded_text.ends_with("<!-- rewritten-h3 -->");
}

bool test_common_transform_synthetic_response() {
  auto origin_owner = std::make_shared<fake_origin>();
  auto &origin = *origin_owner;
  auto policy_owner = std::make_shared<recording_policy>();
  ntl::net::http::transform_pipeline transforms;
  transforms.requests().decide(
      [](const ntl::net::http::request_message &) {
        return ntl::net::inspection::verdict::block;
      });
  ntl::net::http3::standard_inspection_proxy proxy(
      origin_owner, policy_owner, transforms);
  auto response = proxy.forward(make_request());
  if (!response || response->status != 403 ||
      response->negotiated_protocol != "h3" ||
      origin.calls != 0)
    return false;
  const auto length = std::find_if(
      response->headers.begin(), response->headers.end(),
      [](const ntl::net::http3::proxy_header &field) {
        return field.name == "content-length";
      });
  return length != response->headers.end() &&
         length->value ==
             std::to_string(response->body.size());
}

bool test_head_response_metadata_without_body() {
  auto origin_owner = std::make_shared<fake_origin>();
  auto &origin = *origin_owner;
  origin.response.status = 200;
  origin.response.headers = {
      {"content-encoding", "gzip"},
      {"content-length", "123"}};
  origin.response.negotiated_protocol = "h3";
  auto policy_owner = std::make_shared<recording_policy>();
  auto &legacy_policy = *policy_owner;
  ntl::net::http::transform_pipeline transforms;
  transforms.responses().transform(
      [](const ntl::net::http::request_message &,
         ntl::net::http::response_message &response) {
        response.headers.set("x-ntl-bodyless", "true");
        return ntl::net::http::rewrite_result::
            headers_changed();
      });
  ntl::net::http3::standard_inspection_proxy proxy(
      origin_owner, policy_owner, transforms);
  auto request = make_request();
  request.headers[0].value = "HEAD";
  auto response = proxy.forward(std::move(request));
  if (!response || !response->body.empty() ||
      !legacy_policy.response_body.empty())
    return false;
  const auto length = std::find_if(
      response->headers.begin(), response->headers.end(),
      [](const ntl::net::http3::proxy_header &field) {
        return field.name == "content-length";
      });
  const auto marker = std::find_if(
      response->headers.begin(), response->headers.end(),
      [](const ntl::net::http3::proxy_header &field) {
        return field.name == "x-ntl-bodyless" &&
               field.value == "true";
      });
  return length != response->headers.end() &&
         length->value == "123" &&
         marker != response->headers.end();
}

} // namespace

int main() {
  if (!test_valid_gzip_exchange() ||
      !test_authority_and_pseudo_header_rules() ||
      !test_origin_protocol_and_bounds() ||
      !test_policy_is_enforced() ||
      !test_request_content_is_decoded_before_policy() ||
      !test_functional_convenience_api() ||
      !test_common_transform_pipeline() ||
      !test_common_transform_synthetic_response() ||
      !test_head_response_metadata_without_body()) {
    std::cerr
        << "NTL HTTP/3 inspection proxy contracts failed\n";
    return 1;
  }
  std::cout
      << "NTL HTTP/3 inspection proxy contracts passed\n";
  return 0;
}
