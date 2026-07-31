#include <ntl/net/http/transform>
#include <ntl/net/http/http1_stream_transform>
#include <ntl/net/http2/stream_transform>
#include <ntl/net/http3/stream_transform>
#include <ntl/net/inspection/content_stream>
#include <ntl/net/inspection/standard_content_decoders>
#include <ntl/net/inspection/standard_content_encoders>

#include <cstddef>
#include <span>

int main() {
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::content_encoder_registry encoders;
  ntl::net::inspection::register_standard_content_decoders(
      decoders);
  ntl::net::inspection::register_standard_content_encoders(
      encoders);
  constexpr std::byte content[]{std::byte{'n'}, std::byte{'t'},
                                std::byte{'l'}};
  auto encoded =
      ntl::net::inspection::encode_content_encoding(
          encoders, std::span(content), "br");
  if (!encoded || encoded->empty())
    return 1;

  auto stream =
      ntl::net::inspection::content_encoding_stream::create(
          decoders, encoders, "br");
  if (!stream)
    return 1;
  auto streamed = stream->encode(std::span(content), true);
  if (!streamed || !streamed->final || streamed->bytes.empty())
    return 1;

  ntl::net::http::transform_pipeline pipeline;
  pipeline.requests().transform(
      [](ntl::net::http::request_message &request) {
        request.headers.set("x-installed-consumer", "true");
        return ntl::net::http::rewrite_result::
            headers_changed();
      });
  ntl::net::http::request_message request;
  request.wire_protocol =
      ntl::net::http::protocol::http2;
  request.method = "GET";
  request.scheme = "https";
  request.authority = "installed.example";
  request.path = "/";
  const auto outcome = pipeline.apply(request);
  return outcome.action ==
                 ntl::net::http::rewrite_action::forward &&
             request.headers.first("x-installed-consumer") ==
                 "true"
         ? 0
         : 1;
}
