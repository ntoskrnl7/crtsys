#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "http3_live_proxy.hpp"

#include <brotli/encode.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <ntl/net/http3/inspection_proxy>
#include <ntl/net/http3/msh3_client>
#include <ntl/net/tls/certificate>

#include "browser_log.hpp"
#include "test_certificate.hpp"

namespace crtsys::wfp_sample::browser_https {
namespace {

constexpr std::wstring_view controlled_host =
    L"controlled-h3.test";
constexpr std::size_t controlled_upstream_body_limit =
    1024 * 1024;

std::vector<std::byte> bytes_of(std::string_view value) {
  const auto bytes = std::as_bytes(std::span(value));
  return {bytes.begin(), bytes.end()};
}

std::vector<std::byte> encode_zlib(
    std::span<const std::byte> input,
    int window_bits) {
  if (input.size() >
      (std::numeric_limits<uInt>::max)())
    throw std::length_error(
        "controlled HTTP/3 compression input is too large");

  z_stream stream{};
  if (::deflateInit2(
          &stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
          window_bits, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    throw std::runtime_error(
        "controlled HTTP/3 deflate initialization failed");
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
  const int encoded = ::deflate(&stream, Z_FINISH);
  if (encoded != Z_STREAM_END)
    throw std::runtime_error(
        "controlled HTTP/3 deflate encoding failed");
  output.resize(stream.total_out);
  return output;
}

std::vector<std::byte> encode_brotli(
    std::span<const std::byte> input) {
  std::vector<std::byte> output(
      ::BrotliEncoderMaxCompressedSize(input.size()));
  std::size_t output_size = output.size();
  if (::BrotliEncoderCompress(
          BROTLI_DEFAULT_QUALITY,
          BROTLI_DEFAULT_WINDOW,
          BROTLI_MODE_TEXT, input.size(),
          reinterpret_cast<const std::uint8_t *>(
              input.data()),
          &output_size,
          reinterpret_cast<std::uint8_t *>(
              output.data())) == BROTLI_FALSE)
    throw std::runtime_error(
        "controlled HTTP/3 Brotli encoding failed");
  output.resize(output_size);
  return output;
}

ntl::net::http3::origin_response make_text_response(
    unsigned status,
    std::string_view content_type,
    std::vector<std::byte> body,
    std::string_view content_encoding = {}) {
  ntl::net::http3::origin_response result;
  result.status = status;
  result.body = std::move(body);
  result.headers.push_back(
      {"content-type", std::string(content_type)});
  if (!content_encoding.empty())
    result.headers.push_back(
        {"content-encoding",
         std::string(content_encoding)});
  result.negotiated_protocol = "h3";
  return result;
}

class controlled_origin_application final
    : public ntl::net::http3::origin_transport {
public:
  ntl::result<ntl::net::http3::origin_response>
  send(const ntl::net::http3::origin_request &request)
      noexcept override {
    try {
      if (request.server_name !=
          narrow_dns_name(controlled_host))
        return ntl::ok(make_text_response(
            421, "text/plain; charset=utf-8",
            bytes_of(
                "The controlled origin accepts only "
                "controlled-h3.test.\n")));
      if (request.method != "GET")
        return ntl::ok(make_text_response(
            405, "text/plain; charset=utf-8",
            bytes_of(
                "The controlled origin accepts GET only.\n")));

      if (request.path == "/oversized") {
        return ntl::ok(make_text_response(
            200, "application/octet-stream",
            std::vector<std::byte>(
                controlled_upstream_body_limit + 1,
                std::byte{0x5a})));
      }

      if (request.path == "/delay") {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(75));
      } else if (
          request.path != "/" &&
          request.path != "/identity" &&
          request.path != "/gzip" &&
          request.path != "/deflate" &&
          request.path != "/br") {
        return ntl::ok(make_text_response(
            404, "text/plain; charset=utf-8",
            bytes_of(
                "controlled HTTP/3 path not found\n")));
      }

      const std::string html =
          "<!doctype html><html><head><title>"
          "NTL controlled HTTP/3</title></head><body>"
          "<main id=\"ntl-controlled-h3\">"
          "client-h3 inspection-proxy-h3 origin-h3 "
          "private-ca loopback-only"
          "</main></body></html>";
      const auto plain = bytes_of(html);
      if (request.path == "/gzip")
        return ntl::ok(make_text_response(
            200, "text/html; charset=utf-8",
            encode_zlib(
                plain, MAX_WBITS + 16),
            "gzip"));
      if (request.path == "/deflate")
        return ntl::ok(make_text_response(
            200, "text/html; charset=utf-8",
            encode_zlib(plain, MAX_WBITS), "deflate"));
      if (request.path == "/br")
        return ntl::ok(make_text_response(
            200, "text/html; charset=utf-8",
            encode_brotli(plain), "br"));
      return ntl::ok(make_text_response(
          200, "text/html; charset=utf-8", plain));
    } catch (const std::bad_alloc &) {
      return ntl::unexpected(
          STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return ntl::unexpected(
          STATUS_UNHANDLED_EXCEPTION);
    }
  }
};

bool is_forwardable_header(std::string_view name) noexcept {
  return !name.empty() && name.front() != ':' &&
         name != "connection" &&
         name != "keep-alive" &&
         name != "proxy-connection" &&
         name != "transfer-encoding" &&
         name != "upgrade";
}

class controlled_msh3_origin_transport final
    : public ntl::net::http3::origin_transport {
public:
  controlled_msh3_origin_transport(
      PCCERT_CONTEXT origin_authority,
      std::uint16_t origin_port)
      : client_(
            origin_authority,
            {.maximum_request_headers = 128,
             .maximum_request_header_bytes =
                 32 * 1024,
             .maximum_request_body_bytes =
                 2 * 1024 * 1024,
             .maximum_response_headers = 256,
             .maximum_response_header_bytes =
                 48 * 1024,
             .maximum_response_body_bytes =
                 controlled_upstream_body_limit,
             .connect_timeout =
                 std::chrono::seconds(10),
             .response_timeout =
                 std::chrono::seconds(15),
             .shutdown_timeout =
                 std::chrono::seconds(5)}),
        origin_port_(origin_port) {}

  ntl::result<ntl::net::http3::origin_response>
  send(const ntl::net::http3::origin_request &message)
      noexcept override {
    try {
      ntl::net::http3::msh3_client::request request;
      request.server_name = message.server_name;
      request.port = 443;
      request.peer =
          ntl::net::http3::msh3_client::peer_endpoint::
              ipv4_loopback(origin_port_);
      request.method = message.method;
      request.path = message.path;
      request.body = message.body;
      request.headers.reserve(message.headers.size());
      for (const auto &field : message.headers) {
        if (is_forwardable_header(field.name))
          request.headers.push_back(
              {field.name, field.value});
      }

      auto received = client_.send(request);
      if (!received)
        return ntl::unexpected(received.status());

      ntl::net::http3::origin_response result;
      result.status = received->status;
      result.body = std::move(received->body);
      result.headers.reserve(received->headers.size());
      for (auto &field : received->headers) {
        if (is_forwardable_header(field.name))
          result.headers.push_back(
              {std::move(field.name),
               std::move(field.value)});
      }
      result.negotiated_protocol = "h3";
      return ntl::ok(std::move(result));
    } catch (const std::bad_alloc &) {
      return ntl::unexpected(
          STATUS_INSUFFICIENT_RESOURCES);
    } catch (...) {
      return ntl::unexpected(
          STATUS_UNHANDLED_EXCEPTION);
    }
  }

private:
  ntl::net::http3::msh3_client::private_ca_client client_;
  std::uint16_t origin_port_ = 0;
};

void clear_stop_request(
    const std::filesystem::path &path) noexcept {
  std::error_code ignored;
  (void)std::filesystem::remove(path, ignored);
}

std::unique_ptr<ephemeral_certificate>
make_controlled_authority(bool &machine_keys) {
  if (machine_keys)
    return std::make_unique<ephemeral_certificate>(true);
  try {
    return std::make_unique<ephemeral_certificate>(false);
  } catch (const std::system_error &) {
    // VMware Tools and service-style logons may not load a user CAPI
    // profile. A deletable machine key is the bounded fallback; neither
    // path installs the certificate in a trust store.
    machine_keys = true;
    return std::make_unique<ephemeral_certificate>(true);
  }
}

} // namespace

int run_controlled_http3_end_to_end(
    std::uint16_t proxy_port,
    std::uint16_t origin_port,
    const std::filesystem::path &log_argument,
    std::uint32_t duration_seconds) {
  if (proxy_port == 0 || origin_port == 0 ||
      proxy_port == origin_port)
    throw std::invalid_argument(
        "controlled HTTP/3 requires two distinct nonzero ports");

  const auto log_directory =
      std::filesystem::absolute(log_argument);
  std::filesystem::create_directories(log_directory);
  const auto stop_path = log_directory / L"stop.request";
  clear_stop_request(stop_path);

  browser_html_logger proxy_logger(
      log_directory / L"proxy");
  browser_html_logger origin_logger(
      log_directory / L"origin");

  bool machine_keys = false;
  auto origin_authority =
      make_controlled_authority(machine_keys);
  const auto origin_ca_path =
      log_directory / L"ntl-controlled-origin-ca.cer";
  origin_authority->export_public_certificate(
      origin_ca_path);
  ntl::net::windows_tls_certificate_issuer
      origin_issuer(
          origin_authority->get(),
          {.key_name_prefix =
               L"crtsys-ntl-controlled-origin",
           .rsa_bits = 2048,
           .validity_days = 2,
           .machine_keys = machine_keys,
           .reuse_leaf_key = false});
  controlled_origin_application origin_application;
  browser_http3_service origin_service(
      origin_issuer, origin_logger, origin_port, nullptr,
      http3_origin_policy::require_http3,
      &origin_application);

  controlled_msh3_origin_transport origin_transport(
      origin_authority->get(), origin_port);

  auto inspection_authority =
      make_controlled_authority(machine_keys);
  const auto inspection_ca_path =
      log_directory / L"ntl-browser-inspection-ca.cer";
  inspection_authority->export_public_certificate(
      inspection_ca_path);
  ntl::net::windows_tls_certificate_issuer
      inspection_issuer(
          inspection_authority->get(),
          {.key_name_prefix =
               L"crtsys-ntl-controlled-inspection",
           .rsa_bits = 2048,
           .validity_days = 2,
           .machine_keys = machine_keys,
           .reuse_leaf_key = false});
  browser_http3_service proxy_service(
      inspection_issuer, proxy_logger, proxy_port, nullptr,
      http3_origin_policy::require_http3,
      &origin_transport);

  std::wcout
      << L"NTL controlled HTTP/3 ready: host="
      << controlled_host << L", client-peer=127.0.0.1:"
      << proxy_port << L", origin-peer=127.0.0.1:"
      << origin_port
      << L", downstream=h3, upstream=h3"
      << L", inspection-ca="
      << inspection_ca_path.wstring()
      << L", origin-ca=" << origin_ca_path.wstring()
      << L", key-scope="
      << (machine_keys ? L"machine-ephemeral"
                       : L"user-ephemeral")
      << L", trust-store-writes=none, wfp=not-used\n";

  const auto deadline =
      duration_seconds == 0
          ? (std::chrono::steady_clock::time_point::max)()
          : std::chrono::steady_clock::now() +
                std::chrono::seconds(duration_seconds);
  while (std::chrono::steady_clock::now() < deadline) {
    std::error_code error;
    if (std::filesystem::exists(stop_path, error))
      break;
    if (error)
      throw std::system_error(
          error,
          "query controlled HTTP/3 stop file");
    std::this_thread::sleep_for(
        std::chrono::milliseconds(50));
  }

  proxy_service.stop();
  if (!proxy_service.wait_for_drain(20))
    throw std::runtime_error(
        "controlled HTTP/3 proxy did not drain");
  origin_service.stop();
  if (!origin_service.wait_for_drain(20))
    throw std::runtime_error(
        "controlled HTTP/3 origin did not drain");
  clear_stop_request(stop_path);

  std::wcout
      << L"NTL controlled HTTP/3 stopped: "
      << L"proxy-requests="
      << proxy_service.delivered_requests()
      << L", origin-requests="
      << origin_service.delivered_requests()
      << L", proxy-html=" << proxy_logger.html_files()
      << L", origin-html=" << origin_logger.html_files()
      << L", downstream=h3, upstream=h3"
      << L", trust-store-writes=none, wfp=not-used\n";
  return 0;
}

} // namespace crtsys::wfp_sample::browser_https
