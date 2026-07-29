#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "http1_inspection.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <ntl/net/http/http1_framing>
#include <ntl/net/inspection/core>
#include <ntl/net/tls/framed_stream>
#include <ntl/net/websocket/framing>
#include <ntl/net/websocket/permessage_deflate>

#include "bidirectional_relay.hpp"
#include "http1_inspection_support.hpp"
#include "http1_support.hpp"

namespace crtsys::wfp_sample::browser_https {
namespace {

coroutine_task<std::size_t> relay_websocket_frames(
    ntl::net::tls_stream &source,
    ntl::net::tls_stream &destination,
    ntl::net::websocket::sender_role sender,
    std::span<const std::byte> initial_plaintext,
    std::wstring_view server_name,
    std::string_view direction,
    const ntl::net::websocket::permessage_deflate_parameters
        &compression,
    browser_html_logger &logger) {
  constexpr std::size_t maximum_frame_size =
      2 * 1024 * 1024;
  constexpr std::size_t maximum_message_size =
      8 * 1024 * 1024;
  ntl::net::tls_framed_stream frames(
      source,
      ntl::net::websocket::frame_framer(
          sender,
          {maximum_frame_size,
           compression.enabled ? std::uint8_t{0x04}
                               : std::uint8_t{0}}),
      {maximum_frame_size + 14}, 16 * 1024);
  frames.append_buffered(initial_plaintext);
  ntl::net::websocket::message_assembler messages(
      maximum_message_size);
  auto decoder =
      ntl::net::websocket::make_permessage_deflate_decoder(
          compression, sender, maximum_message_size);
  std::size_t relayed = 0;
  for (;;) {
    auto frame = co_await frames.read_frame_or_eof();
    if (!frame)
      co_return relayed;
    const auto wire = ntl::net::scatter_view::from_contiguous(
        frame->frame());
    const auto header = ntl::net::websocket::inspect_header(
        wire, sender,
        {maximum_frame_size,
         compression.enabled ? std::uint8_t{0x04}
                             : std::uint8_t{0}});
    if (!header)
      throw std::runtime_error(
          "validated WebSocket frame lost its header");
    const auto payload = ntl::net::websocket::decode_payload(
        wire, *header, maximum_frame_size);
    if (!payload)
      throw std::runtime_error(
          "validated WebSocket frame lost its payload");
    const auto completed = messages.consume(*header, *payload);
    if (!completed)
      throw std::runtime_error(
          "invalid WebSocket message fragmentation");

    std::size_t inspected_message_size = 0;
    bool message_compressed = false;
    if (*completed && !(*completed)->control) {
      message_compressed = (*completed)->compressed();
      auto inspected = decoder.decode(
          (*completed)->payload, message_compressed);
      if (!inspected)
        throw std::system_error(
            static_cast<int>(inspected.status()),
            std::system_category(),
            "WebSocket permessage-deflate decoder rejected "
            "the message");
      inspected_message_size = inspected->size();
    }
    logger.record_websocket(
        server_name, direction, header->operation,
        payload->size(), completed->has_value(),
        message_compressed, inspected_message_size);
    if (co_await destination.write_all(frame->frame()) !=
        frame->size())
      throw std::runtime_error(
          "WebSocket relay completed short");
    relayed += frame->size();
    if (header->operation == ntl::net::websocket::opcode::close)
      co_return relayed;
  }
}

struct forwarded_http1_request {
  std::vector<std::byte> buffered_plaintext;
  std::string websocket_extensions;
  bool websocket_upgrade = false;
};

nested_task<forwarded_http1_request> forward_http1_request(
    ntl::net::tls_stream &inbound,
    ntl::net::tls_stream &outbound) {
  ntl::net::tls_framed_stream requests(
      inbound,
      make_http_framer(
          ntl::net::http::http1_message_kind::request),
      {maximum_http_message_size}, 4096);
  auto request = co_await requests.read_frame_or_eof();
  if (!request)
    throw std::runtime_error(
        "browser closed before its HTTP request");

  auto upstream =
      http1_detail::rewrite_browser_request(request->frame());
  auto buffered_plaintext = requests.release_buffered();
  if (co_await outbound.write_all(upstream.wire) !=
      upstream.wire.size())
    throw std::runtime_error(
        "browser proxy upstream request completed short");
  co_return forwarded_http1_request{
      std::move(buffered_plaintext),
      std::move(upstream.websocket_extensions),
      upstream.websocket_upgrade};
}

struct forwarded_http1_response {
  std::vector<std::byte> buffered_plaintext;
  ntl::net::websocket::permessage_deflate_parameters
      websocket_compression;
  unsigned status = 0;
  std::optional<std::filesystem::path> html_path;
  bool websocket_upgrade = false;
};

nested_task<forwarded_http1_response> forward_http1_response(
    ntl::net::tls_stream &outbound,
    ntl::net::tls_stream &inbound,
    std::wstring_view server_name,
    const forwarded_http1_request &request,
    const ntl::net::inspection::content_decoder_registry &decoders,
    browser_html_logger &logger) {
  ntl::net::tls_framed_stream responses(
      outbound,
      make_http_framer(
          ntl::net::http::http1_message_kind::response, true),
      {maximum_http_message_size}, 16 * 1024);
  auto response = co_await responses.read_frame_or_eof();
  if (!response)
    throw std::runtime_error(
        "browser HTTPS server closed before its response");

  auto parsed = parse_http_response(*response);
  forwarded_http1_response forwarded;
  forwarded.buffered_plaintext = responses.release_buffered();
  http1_detail::inspect_http1_response(
      parsed, request.websocket_extensions, decoders,
      forwarded.websocket_compression);
  forwarded.html_path =
      logger.record_response(server_name, parsed);
  forwarded.status = parsed.status;
  forwarded.websocket_upgrade = parsed.websocket_upgrade();
  if (co_await inbound.write_all(response->frame()) !=
      response->size())
    throw std::runtime_error(
        "browser proxy downstream response completed short");
  co_return forwarded;
}

} // namespace

nested_task<browser_proxy_result> relay_http1_connection(
    SOCKET inbound_socket,
    SOCKET outbound_socket,
    ntl::net::tls_stream &inbound,
    ntl::net::tls_stream &outbound,
    std::wstring server_name,
    const ntl::net::inspection::content_decoder_registry &decoders,
    browser_html_logger &logger) {
  auto request = co_await forward_http1_request(inbound, outbound);
  auto response = co_await forward_http1_response(
      outbound, inbound, server_name, request, decoders, logger);

  if (response.websocket_upgrade) {
    auto downstream_to_upstream = relay_websocket_frames(
        inbound, outbound,
        ntl::net::websocket::sender_role::client,
        request.buffered_plaintext, server_name,
        "browser-to-origin", response.websocket_compression, logger);
    auto upstream_to_downstream = relay_websocket_frames(
        outbound, inbound,
        ntl::net::websocket::sender_role::server,
        response.buffered_plaintext, server_name,
        "origin-to-browser", response.websocket_compression, logger);
    co_await join_bidirectional_relays(
        std::move(downstream_to_upstream),
        std::move(upstream_to_downstream),
        inbound_socket, outbound_socket);
  }

  co_await inbound.shutdown();
  co_await outbound.shutdown();
  co_return browser_proxy_result{
      std::move(server_name), response.status,
      std::move(response.html_path)};
}

} // namespace crtsys::wfp_sample::browser_https
