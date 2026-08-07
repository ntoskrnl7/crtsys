#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "managed_tcp_scenario.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <ntl/net/buffer/scatter_view>
#include <ntl/net/grpc/framing>
#include <ntl/net/http/transform>
#include <ntl/net/http2/transform>
#include <ntl/net/inspection/content_decoder>
#include <ntl/net/inspection/content_encoder>
#include <ntl/net/inspection/standard_content_decoders>
#include <ntl/net/inspection/standard_content_encoders>
#include <ntl/net/io/async_socket>
#include <ntl/net/tls/framed_stream>
#include <ntl/net/tls/stream>
#include <ntl/net/user/task>
#include <ntl/net/websocket/framing>
#include <ntl/net/websocket/permessage_deflate>
#include <ntl/net/websocket/transform>

#include "browser_http_policy.hpp"
#include "capture_log.hpp"
#include "http1_support.hpp"
#include "managed_http3_client.hpp"
#include "windows_support.hpp"

namespace crtsys::wfp_kernel_browser_https {
namespace {

namespace contract = wfp_kernel_browser_https_inspection;
using namespace crtsys::wfp_sample;
using namespace std::chrono_literals;

constexpr std::wstring_view managed_server_name = L"localhost";
constexpr std::string_view managed_server_name_ascii = "localhost";
constexpr std::string_view kernel_transform_marker =
    "<!-- inspected and transformed by ntl -->";

socket_owner connect_loopback(int family, std::uint16_t port) {
  socket_owner socket(::WSASocketW(family, SOCK_STREAM, IPPROTO_TCP, nullptr,
                                   0, WSA_FLAG_OVERLAPPED));
  if (socket.get() == INVALID_SOCKET)
    throw_socket("WSASocketW(managed TCP client)");
  if (family == AF_INET) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::connect(socket.get(), reinterpret_cast<const sockaddr *>(&address),
                  sizeof(address)) == SOCKET_ERROR)
      throw_socket("connect(managed TCP IPv4)");
  } else if (family == AF_INET6) {
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = htons(port);
    if (::connect(socket.get(), reinterpret_cast<const sockaddr *>(&address),
                  sizeof(address)) == SOCKET_ERROR)
      throw_socket("connect(managed TCP IPv6)");
  } else {
    throw std::invalid_argument("managed TCP family is invalid");
  }
  return socket;
}

std::vector<std::byte> bytes_of(std::string_view text) {
  const auto bytes = std::as_bytes(std::span(text));
  return {bytes.begin(), bytes.end()};
}

std::string text_of(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

ntl::net::user::task<unsigned> close_tls(ntl::net::tls_stream &stream) {
  co_await stream.shutdown();
  std::array<std::byte, 4096> discard{};
  while (!stream.received_close_notify()) {
    if (co_await stream.read_some_borrowed(discard) == 0)
      break;
  }
  if (!stream.received_close_notify())
    throw std::runtime_error("managed TLS peer omitted close_notify");
  co_return 0;
}

std::vector<std::byte> grpc_wire(std::string_view payload) {
  const auto encoded = ntl::net::grpc::encode_message(
      std::as_bytes(std::span(payload)), false, 4096);
  if (!encoded)
    throw std::runtime_error("managed gRPC fixture encoding failed");
  return *encoded;
}

std::string grpc_payload_text(std::span<const std::byte> wire) {
  const auto header = ntl::net::grpc::inspect_header(
      ntl::net::scatter_view::from_contiguous(wire), 4096);
  if (!header || header->compressed || wire.size() != 5 + header->payload_size)
    throw std::runtime_error("managed gRPC wire message is invalid");
  return text_of(wire.subspan(5, header->payload_size));
}

std::vector<std::byte> encode_content(
    const ntl::net::inspection::content_encoder_registry &encoders,
    std::span<const std::byte> body, std::string_view coding) {
  if (coding.empty())
    return {body.begin(), body.end()};
  auto encoded = ntl::net::inspection::encode_content_encoding(
      encoders, body, coding,
      {.maximum_input_size = 1024 * 1024,
       .maximum_encoded_size = 1024 * 1024,
       .maximum_coding_layers = 2});
  if (!encoded)
    throw std::runtime_error("managed origin content encoding failed");
  return std::move(*encoded);
}

std::vector<std::byte> decode_content(
    const ntl::net::inspection::content_decoder_registry &decoders,
    std::span<const std::byte> body, std::string_view coding) {
  if (coding.empty() || ascii_equal_ci(coding, "identity"))
    return {body.begin(), body.end()};
  auto decoded = ntl::net::inspection::decode_content_encoding(
      decoders, ntl::net::scatter_view::from_contiguous(body), coding,
      {.maximum_encoded_size = 1024 * 1024,
       .maximum_decoded_size = 4 * 1024 * 1024,
       .maximum_expansion_ratio = 128,
       .maximum_coding_layers = 2});
  if (!decoded)
    throw std::runtime_error("managed client content decoding failed");
  return std::move(*decoded);
}

std::string_view request_path(std::string_view wire) {
  const std::size_t first_space = wire.find(' ');
  if (first_space == std::string_view::npos)
    return {};
  const std::size_t second_space = wire.find(' ', first_space + 1);
  if (second_space == std::string_view::npos)
    return {};
  return wire.substr(first_space + 1, second_space - first_space - 1);
}

std::span<const std::byte> http1_body(std::span<const std::byte> wire) {
  const std::string_view text(
      reinterpret_cast<const char *>(wire.data()), wire.size());
  const auto delimiter = text.find("\r\n\r\n");
  if (delimiter == std::string_view::npos)
    throw std::runtime_error("managed HTTP/1 message has no header delimiter");
  return wire.subspan(delimiter + 4);
}


#include "managed_websocket_scenario.inl"
#include "managed_http2_scenario.inl"
#include "managed_dynamic_sni_scenario.inl"
#include "managed_http1_scenario.inl"
#include "managed_http3_fallback_scenario.inl"

} // namespace

managed_tcp_acceptance_result run_managed_tcp_acceptance(
    acceptance_controller &device,
    const contract::service_info &service, capture_log &logger,
    std::uint64_t capture_baseline, PCCERT_CONTEXT origin_certificate,
    PCCERT_CONTEXT client_certificate) {
  managed_tcp_acceptance_result result;
  try {
    result = require_http1(device, service, origin_certificate,
                           client_certificate, logger, capture_baseline);
  } catch (const std::exception &error) {
    std::string diagnostic;
    try {
      const auto snapshot = query_service(device);
      std::uint64_t cursor = capture_baseline;
      std::optional<contract::inspection_record> last_failure;
      for (;;) {
        const auto next = read_inspection(device, cursor);
        if (!next.available)
          break;
        cursor = next.record.sequence;
        if (next.record.action == contract::inspection_action::failed &&
            (!last_failure ||
             (last_failure->protocol == contract::inspected_protocol::none &&
              next.record.protocol != contract::inspected_protocol::none)))
          last_failure = next.record;
      }
      diagnostic = " accepted=" + std::to_string(snapshot.accepted) +
                   " handshaken=" + std::to_string(snapshot.handshaken) +
                   " origin-connected=" +
                   std::to_string(snapshot.origin_connected) +
                   " origin-completed=" +
                   std::to_string(snapshot.origin_completed) +
                   " failed=" + std::to_string(snapshot.failed) +
                   " active=" +
                   std::to_string(snapshot.active_tcp_sessions);
      if (last_failure) {
        diagnostic +=
            " last-failure-status=" +
            std::to_string(last_failure->failure_status) +
            " protocol=" +
            std::to_string(
                static_cast<std::uint32_t>(last_failure->protocol)) +
            " flags=" + std::to_string(last_failure->flags) +
            " sequence=" + std::to_string(last_failure->sequence);
      }
    } catch (...) {
      diagnostic += " diagnostics-unavailable";
    }
    throw std::runtime_error(std::string("managed HTTP/1 acceptance: ") +
                             error.what() + diagnostic);
  }
  const auto h2_baseline =
      read_inspection(device, (std::numeric_limits<std::uint64_t>::max)())
          .current_sequence;
  managed_tcp_acceptance_result h2;
  try {
    h2 = require_http2(device, query_service(device), origin_certificate,
                       client_certificate, logger, h2_baseline);
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("managed HTTP/2 acceptance: ") +
                             error.what());
  }
  result.http2_policy_pipeline = h2.http2_policy_pipeline;
  result.http2_multiplexing = h2.http2_multiplexing;
  result.http2_flow_control = h2.http2_flow_control;
  result.http2_goaway = h2.http2_goaway;
  result.http2_compression = h2.http2_compression;
  result.http2_grpc = h2.http2_grpc;
  result.http2_websocket = h2.http2_websocket;
  result.http2_extended_connect = h2.http2_extended_connect;
  result.http2_unsupported_connect_fail_closed =
      h2.http2_unsupported_connect_fail_closed;
  result.http2_ipv4_ipv6_wfp = h2.http2_ipv4_ipv6_wfp;
  result.origin_system_validation =
      result.origin_system_validation && h2.origin_system_validation;
  result.origin_exact_pin = result.origin_exact_pin && h2.origin_exact_pin;
  result.origin_mtls = result.origin_mtls && h2.origin_mtls;
  result.capture_records += h2.capture_records;
  return result;
}

managed_http3_fallback_acceptance_result
run_managed_http3_fallback_acceptance(
    acceptance_controller &device,
    PCCERT_CONTEXT origin_certificate,
    PCCERT_CONTEXT client_certificate) {
  managed_http3_fallback_acceptance_result result{};
  result.http2 = require_fallback_protocol(
      device, origin_certificate, client_certificate, true);
  result.http1 = require_fallback_protocol(
      device, origin_certificate, client_certificate, false);
  result.non_safe_rejected =
      require_non_safe_fallback_rejection(device);
  return result;
}

dynamic_sni_acceptance_result run_dynamic_sni_acceptance(
    acceptance_controller &device,
    const contract::service_info &service, std::string_view server_name,
    PCCERT_CONTEXT origin_certificate, PCCERT_CONTEXT client_certificate,
    const std::function<void()> &replace_identity) {
  if (server_name.empty() || !origin_certificate || !client_certificate ||
      !replace_identity)
    throw std::invalid_argument("dynamic SNI acceptance input is incomplete");
  return require_dynamic_sni(
      device, service, std::string(server_name),
      origin_certificate, client_certificate, replace_identity);
}

} // namespace crtsys::wfp_kernel_browser_https
