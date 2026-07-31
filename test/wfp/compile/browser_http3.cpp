#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <brotli/encode.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <ntl/net/http3/qpack>
#include <ntl/net/buffer/scatter_view>
#include <ntl/net/inspection/standard_content_decoders>

#include "browser_log.hpp"
#include "http3_inspection.hpp"

namespace {

using crtsys::wfp_sample::browser_https::
    browser_html_logger;
using crtsys::wfp_sample::browser_https::
    browser_http3_inspector;
using crtsys::wfp_sample::browser_https::
    http3_inspection_direction;

void append_varint(
    std::vector<std::byte> &output,
    std::uint64_t value) {
  if (value < 64) {
    output.push_back(static_cast<std::byte>(value));
    return;
  }
  if (value < 16384) {
    output.push_back(static_cast<std::byte>(
        0x40u | static_cast<unsigned>(value >> 8)));
    output.push_back(
        static_cast<std::byte>(value & 0xffu));
    return;
  }
  throw std::runtime_error(
      "test frame exceeds two-byte QUIC varint");
}

void append_frame(
    std::vector<std::byte> &output,
    std::uint64_t type,
    std::span<const std::byte> payload) {
  append_varint(output, type);
  append_varint(output, payload.size());
  output.insert(
      output.end(), payload.begin(), payload.end());
}

std::vector<std::byte>
brotli_encode(std::span<const std::byte> input) {
  std::vector<std::byte> output(
      ::BrotliEncoderMaxCompressedSize(input.size()));
  std::size_t output_size = output.size();
  if (::BrotliEncoderCompress(
          BROTLI_DEFAULT_QUALITY,
          BROTLI_DEFAULT_WINDOW,
          BROTLI_MODE_GENERIC, input.size(),
          reinterpret_cast<const std::uint8_t *>(
              input.data()),
          &output_size,
          reinterpret_cast<std::uint8_t *>(
              output.data())) == BROTLI_FALSE)
    return {};
  output.resize(output_size);
  return output;
}

bool consume_split(
    browser_http3_inspector &inspector,
    std::uint64_t stream_id,
    std::span<const std::byte> wire) {
  if (wire.size() < 4)
    return false;
  const std::size_t first_size = 1;
  const std::size_t second_size = wire.size() / 2;
  const auto first =
      ntl::net::scatter_view::from_contiguous(
          wire.first(first_size));
  const auto second =
      ntl::net::scatter_view::from_contiguous(
          wire.subspan(
              first_size, second_size - first_size));
  const auto third =
      ntl::net::scatter_view::from_contiguous(
          wire.subspan(second_size));
  if (!inspector
           .consume_stream(
               stream_id, first, false)
           .is_ok() ||
      !inspector
           .consume_stream(
               stream_id, second, false)
           .is_ok() ||
      !inspector
           .consume_stream(
               stream_id, third, false)
           .is_ok())
    return false;

  // HTTP/3 message completion belongs to QUIC FIN, which can arrive
  // without another plaintext byte.
  return inspector
      .consume_stream(stream_id, {}, true)
      .is_ok();
}

std::string read_file(
    const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>()};
}

struct directory_cleanup {
  std::filesystem::path path;
  ~directory_cleanup() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

bool test_browser_http3() {
  const std::filesystem::path log_directory =
      std::filesystem::temp_directory_path() /
      (L"crtsys-browser-http3-contract-" +
       std::to_wstring(::GetCurrentProcessId()) + L"-" +
       std::to_wstring(::GetTickCount64()));
  directory_cleanup cleanup{log_directory};
  browser_html_logger logger(log_directory);

  ntl::net::inspection::content_decoder_registry decoders;
  ntl::net::inspection::register_standard_content_decoders(
      decoders);

  ntl::net::http3::qpack_decoder_adapter<
      ntl::net::http3::bounded_static_qpack_decoder>
      request_qpack;
  browser_http3_inspector request(
      http3_inspection_direction::browser_to_origin,
      request_qpack, L"browser-http3.test", decoders,
      logger);
  // Required Insert Count=0, Delta Base=0, followed by static
  // :method=GET and :scheme=https, a literal :authority value using
  // static name index 0, and static :path=/.
  std::vector<std::byte> request_headers{
      std::byte{0x00}, std::byte{0x00},
      std::byte{0xd1}, std::byte{0xd7},
      std::byte{0x50}, std::byte{0x12}};
  constexpr std::string_view authority =
      "browser-http3.test";
  const auto authority_bytes =
      std::as_bytes(std::span(authority));
  request_headers.insert(
      request_headers.end(), authority_bytes.begin(),
      authority_bytes.end());
  request_headers.push_back(std::byte{0xc1});
  std::vector<std::byte> request_wire;
  append_frame(request_wire, 0x01, request_headers);
  if (!consume_split(request, 0, request_wire))
    return false;
  std::vector<std::byte> misplaced_settings;
  append_frame(
      misplaced_settings, 0x04,
      std::span<const std::byte>{});
  if (request
          .consume_stream(
              4,
              ntl::net::scatter_view::from_contiguous(
                  std::span<const std::byte>(
                      misplaced_settings)),
              true)
          .is_ok())
    return false;

  const std::string html =
      "<!doctype html><html><body>"
      "browser HTTP/3 inspection"
      "</body></html>";
  const auto plain =
      std::as_bytes(std::span(html));
  const auto compressed = brotli_encode(plain);
  if (compressed.empty())
    return false;

  ntl::net::http3::qpack_decoder_adapter<
      ntl::net::http3::bounded_static_qpack_decoder>
      response_qpack;
  browser_http3_inspector response(
      http3_inspection_direction::origin_to_browser,
      response_qpack, L"browser-http3.test", decoders,
      logger);
  // Static :status=200, content-encoding=br, and
  // content-type=text/html; charset=utf-8.
  const std::vector<std::byte> response_headers{
      std::byte{0x00}, std::byte{0x00},
      std::byte{0xd9}, std::byte{0xea},
      std::byte{0xf4}};
  std::vector<std::byte> response_wire;
  append_frame(response_wire, 0x01, response_headers);
  append_frame(response_wire, 0x00, compressed);
  if (!consume_split(response, 0, response_wire) ||
      response.last_status() != 200 ||
      logger.html_files() != 1)
    return false;
  const auto html_path = response.html_path();
  return html_path &&
         std::filesystem::is_regular_file(*html_path) &&
         read_file(*html_path) == html;
}

} // namespace

int main() {
  try {
    if (!test_browser_http3()) {
      std::cerr
          << "browser HTTP/3 inspection contract failed\n";
      return 1;
    }
    std::cout
        << "browser HTTP/3 inspection contract ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "browser HTTP/3 inspection exception: "
              << error.what() << '\n';
    return 1;
  }
}
