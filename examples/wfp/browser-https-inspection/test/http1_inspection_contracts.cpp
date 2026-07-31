#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <brotli/encode.h>
#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <ntl/net/buffer/scatter_view>
#include <ntl/net/framing>
#include <ntl/net/http/http1_framing>
#include <ntl/net/inspection/content_decoder_zlib>
#include <ntl/net/inspection/standard_content_decoders>

#include "http1_inspection_support.hpp"

namespace sample = crtsys::wfp_sample;
namespace http1 =
    crtsys::wfp_sample::browser_https::http1_detail;

namespace {

std::vector<std::byte> bytes_of(std::string_view value) {
  const auto bytes = std::as_bytes(std::span(value));
  return {bytes.begin(), bytes.end()};
}

std::string text_of(std::span<const std::byte> value) {
  return {
      reinterpret_cast<const char *>(value.data()), value.size()};
}

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string(message));
}

std::vector<std::byte> zlib_encode(
    std::span<const std::byte> input,
    ntl::net::inspection::zlib_stream_format format) {
  if (input.size() >
      (std::numeric_limits<uInt>::max)())
    return {};
  z_stream stream{};
  if (::deflateInit2(
          &stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
          static_cast<int>(format), 8,
          Z_DEFAULT_STRATEGY) != Z_OK)
    return {};
  struct stream_guard {
    z_stream *stream;
    ~stream_guard() { (void)::deflateEnd(stream); }
  } guard{&stream};

  std::vector<std::byte> output(
      static_cast<std::size_t>(::deflateBound(
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

std::vector<std::byte> brotli_encode(
    std::span<const std::byte> input) {
  std::vector<std::byte> output(
      ::BrotliEncoderMaxCompressedSize(input.size()));
  std::size_t output_size = output.size();
  if (::BrotliEncoderCompress(
          BROTLI_DEFAULT_QUALITY, BROTLI_DEFAULT_WINDOW,
          BROTLI_MODE_GENERIC, input.size(),
          reinterpret_cast<const std::uint8_t *>(input.data()),
          &output_size,
          reinterpret_cast<std::uint8_t *>(output.data())) ==
      BROTLI_FALSE)
    return {};
  output.resize(output_size);
  return output;
}

void test_regular_request_rewrite() {
  const auto request = bytes_of(
      "POST /inspect HTTP/1.1\r\n"
      "Host: example.test\r\n"
      "Proxy-Connection: keep-alive\r\n"
      "Connection: keep-alive\r\n"
      "Accept-Encoding: gzip, br\r\n"
      "Content-Length: 7\r\n\r\n"
      "payload");
  const auto rewritten =
      http1::rewrite_browser_request(request);
  const auto text = text_of(rewritten.wire);
  require(!rewritten.websocket_upgrade,
          "ordinary request became a WebSocket upgrade");
  require(rewritten.websocket_extensions.empty(),
          "ordinary request retained WebSocket extensions");
  require(text.find("Proxy-Connection:") == std::string::npos,
          "proxy-only connection header reached the origin");
  require(text.find("Accept-Encoding: gzip, br\r\n") != std::string::npos,
          "browser compression offer was not preserved");
  require(text.find("Connection: keep-alive") == std::string::npos,
          "persistent HTTP/1 connection was not bounded");
  require(
      text.find(
          "Connection: close\r\n\r\n"
          "payload") != std::string::npos,
      "request body or bounded replacement fields were lost");
}

void test_websocket_request_rewrite() {
  const auto request = bytes_of(
      "GET /socket HTTP/1.1\r\n"
      "Host: example.test\r\n"
      "Connection: keep-alive, Upgrade\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Extensions: permessage-deflate; "
      "client_max_window_bits\r\n"
      "Accept-Encoding: gzip\r\n\r\n");
  const auto rewritten =
      http1::rewrite_browser_request(request);
  const auto text = text_of(rewritten.wire);
  require(rewritten.websocket_upgrade,
          "WebSocket request was not recognized");
  require(
      rewritten.websocket_extensions.find("permessage-deflate") !=
          std::string::npos,
      "WebSocket extension offer was not captured");
  require(
      text.find("Connection: keep-alive, Upgrade\r\n") !=
          std::string::npos &&
          text.find("Upgrade: websocket\r\n") != std::string::npos &&
          text.find("Accept-Encoding: gzip\r\n") !=
              std::string::npos,
      "WebSocket handshake fields were rewritten");
  require(text.find("Connection: close") == std::string::npos,
          "WebSocket handshake was forced closed");
}

void test_incremental_http1_framing() {
  const std::string request =
      "POST /fragmented HTTP/1.1\r\n"
      "Host: example.test\r\n"
      "Content-Length: 5\r\n\r\nhello";
  const std::string suffix = "GET /next HTTP/1.1\r\n\r\n";
  ntl::net::http::http1_message_framer framer(
      ntl::net::http::http1_message_kind::request);
  const auto prefix = std::as_bytes(
      std::span(request.data(), request.size() - 2));
  const auto partial = framer.probe(
      ntl::net::scatter_view::from_contiguous(prefix));
  require(
      partial.state() == ntl::net::framing::probe_state::need_more,
      "fragmented HTTP request completed early");

  const auto combined = bytes_of(request + suffix);
  const auto complete = framer.probe(
      ntl::net::scatter_view::from_contiguous(combined));
  require(
      complete.state() == ntl::net::framing::probe_state::complete &&
          complete.frame_size() == request.size(),
      "HTTP framer did not preserve the coalesced next request");
}

void test_standard_response_decoders() {
  const auto plain = bytes_of(
      "<!doctype html><html><body>HTTP/1 inspection</body></html>");
  const auto gzip = zlib_encode(
      plain, ntl::net::inspection::zlib_stream_format::gzip);
  const auto deflate = zlib_encode(
      plain, ntl::net::inspection::zlib_stream_format::zlib);
  const auto brotli = brotli_encode(plain);
  require(!gzip.empty() && !deflate.empty() && !brotli.empty(),
          "test compression backend failed");

  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::register_standard_content_decoders(decoders);
  const auto check =
      [&decoders, &plain](
          std::string encoding, std::vector<std::byte> encoded) {
        sample::parsed_http_response response;
        response.status = 200;
        response.content_encoding = std::move(encoding);
        response.body = std::move(encoded);
        response.body_decoded = false;
        ntl::net::websocket::permessage_deflate_parameters
            websocket;
        http1::inspect_http1_response(
            response, {}, decoders, websocket);
        require(
            response.body_decoded && response.body == plain,
            "HTTP response content decoder returned wrong bytes");
      };
  check("gzip", gzip);
  check("deflate", deflate);
  check("br", brotli);
}

void test_invalid_response_encoding_fails_closed() {
  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::register_standard_content_decoders(decoders);
  sample::parsed_http_response response;
  response.status = 200;
  response.content_encoding = "gzip";
  response.body = bytes_of("not-a-gzip-stream");
  response.body_decoded = false;
  ntl::net::websocket::permessage_deflate_parameters websocket;
  try {
    http1::inspect_http1_response(
        response, {}, decoders, websocket);
  } catch (const std::system_error &) {
    return;
  }
  throw std::runtime_error(
      "invalid compressed HTTP response was accepted");
}

sample::parsed_http_response websocket_response(
    std::string extensions) {
  sample::parsed_http_response response;
  response.status = 101;
  response.connection = "Upgrade";
  response.upgrade = "websocket";
  response.websocket_extensions = std::move(extensions);
  return response;
}

void test_websocket_compression_negotiation() {
  ntl::net::inspection::content_decoder_registry decoders;
  auto accepted = websocket_response(
      "permessage-deflate; client_no_context_takeover; "
      "server_no_context_takeover");
  ntl::net::websocket::permessage_deflate_parameters compression;
  http1::inspect_http1_response(
      accepted, "permessage-deflate; client_max_window_bits",
      decoders, compression);
  require(
      compression.enabled &&
          compression.client_no_context_takeover &&
          compression.server_no_context_takeover,
      "valid WebSocket compression parameters were lost");

  auto unsolicited =
      websocket_response("permessage-deflate");
  try {
    http1::inspect_http1_response(
        unsolicited, {}, decoders, compression);
  } catch (const std::runtime_error &) {
    return;
  }
  throw std::runtime_error(
      "unsolicited WebSocket compression was accepted");
}

} // namespace

int main() {
  try {
    test_regular_request_rewrite();
    test_websocket_request_rewrite();
    test_incremental_http1_framing();
    test_standard_response_decoders();
    test_invalid_response_encoding_fails_closed();
    test_websocket_compression_negotiation();
    std::cout << "HTTP/1 inspection contracts passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
