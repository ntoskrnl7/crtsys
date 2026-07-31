#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>

#include "http1_inspection.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <ntl/net/http/http1_framing>
#include <ntl/net/http/http1_transform>
#include <ntl/net/inspection/core>
#include <ntl/net/tls/framed_stream>
#include <ntl/net/websocket/framing>
#include <ntl/net/websocket/permessage_deflate>
#include <ntl/net/websocket/transform>

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
    const ntl::net::websocket::message_transform_pipeline &policy,
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
  auto mask_provider = [] {
    std::array<std::byte, 4> key{};
    if (!BCRYPT_SUCCESS(::BCryptGenRandom(
            nullptr, reinterpret_cast<PUCHAR>(key.data()),
            static_cast<ULONG>(key.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
      throw std::runtime_error("WebSocket mask generation failed");
    return key;
  };
  ntl::net::websocket::wire_transformer transformer(
      sender, policy,
      compression.enabled
          ? std::optional<ntl::net::websocket::
                              permessage_deflate_parameters>(compression)
          : std::nullopt,
      sender == ntl::net::websocket::sender_role::client
          ? std::function<std::array<std::byte, 4>()>(mask_provider)
          : std::function<std::array<std::byte, 4>()>{});
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
    const auto transformed = transformer.consume(wire);
    if (transformed.action !=
            ntl::net::websocket::rewrite_action::forward ||
        transformed.failure != STATUS_SUCCESS)
      throw std::system_error(
          static_cast<int>(transformed.failure), std::system_category(),
          "WebSocket transform policy rejected the message");
    if (!transformed.wire.empty()) {
      if (co_await destination.write_all(transformed.wire) !=
          transformed.wire.size())
        throw std::runtime_error("WebSocket relay completed short");
      relayed += transformed.wire.size();
    }
    if (header->operation == ntl::net::websocket::opcode::close)
      co_return relayed;
  }
}

struct forwarded_http1_request {
  ntl::net::http::request_message message;
  std::vector<std::byte> buffered_plaintext;
  std::string websocket_extensions;
  unsigned terminal_status = 0;
  bool websocket_upgrade = false;
  bool terminal = false;
};

ntl::net::http::response_message forbidden_response() {
  ntl::net::http::response_message response;
  response.wire_protocol = ntl::net::http::protocol::http1;
  response.status = 403;
  response.headers.append(
      "content-type", "text/plain; charset=utf-8");
  constexpr std::string_view text = "Request blocked by policy.\n";
  response.body.assign(
      reinterpret_cast<const std::byte *>(text.data()),
      reinterpret_cast<const std::byte *>(
          text.data() + text.size()));
  return response;
}

nested_task<unsigned> send_terminal_response(
    ntl::net::tls_stream &inbound,
    std::optional<ntl::net::http::response_message> response,
    const ntl::net::inspection::content_encoder_registry &encoders,
    const ntl::net::http::transform_limits &limits,
    bool response_to_head) {
  auto message =
      response ? std::move(*response) : forbidden_response();
  message.wire_protocol = ntl::net::http::protocol::http1;
  const std::string original_coding =
      message.headers.joined("content-encoding");
  auto encoded = ntl::net::http::encode_body(
      message.headers, message.body, original_coding,
      ntl::net::http::transformed_body_coding::preserve,
      encoders, limits);
  if (!encoded)
    throw std::system_error(
        static_cast<int>(encoded.status()),
        std::system_category(),
        "HTTP/1 terminal response encoder rejected the body");
  auto wire = ntl::net::http::serialize_http1_response(
      message,
      response_to_head
          ? std::span<const std::byte>{}
          : std::span<const std::byte>(*encoded),
      limits, response_to_head);
  if (!wire)
    throw std::system_error(
        static_cast<int>(wire.status()),
        std::system_category(),
        "HTTP/1 terminal response serializer rejected the message");
  if (co_await inbound.write_all(*wire) != wire->size())
    throw std::runtime_error(
        "HTTP/1 terminal response completed short");
  co_return message.status;
}

nested_task<forwarded_http1_request> forward_http1_request(
    ntl::net::tls_stream &inbound,
    ntl::net::tls_stream &outbound,
    const ntl::net::inspection::content_decoder_registry &decoders,
    const ntl::net::inspection::content_encoder_registry &encoders,
    const ntl::net::http::transform_pipeline &transforms) {
  ntl::net::tls_framed_stream requests(
      inbound,
      make_http_framer(
          ntl::net::http::http1_message_kind::request),
      {maximum_http_message_size}, 4096);
  auto request = co_await requests.read_frame_or_eof();
  if (!request)
    throw std::runtime_error(
        "browser closed before its HTTP request");

  auto upstream = ntl::net::http::transform_http1_request(
      request->frame(), transforms, decoders, encoders);
  if (!upstream)
    throw std::system_error(
        static_cast<int>(upstream.status()),
        std::system_category(),
        "HTTP/1 request transform rejected the message");
  auto buffered_plaintext = requests.release_buffered();
  if (upstream->outcome.action ==
      ntl::net::http::rewrite_action::drop)
    co_return forwarded_http1_request{
        std::move(upstream->message),
        std::move(buffered_plaintext),
        std::move(upstream->websocket_extensions),
        0, upstream->websocket_upgrade, true};
  if (upstream->outcome.action !=
      ntl::net::http::rewrite_action::forward) {
    const unsigned status = co_await send_terminal_response(
        inbound, std::move(upstream->outcome.response),
        encoders, transforms.limits(),
        upstream->message.method == "HEAD");
    co_return forwarded_http1_request{
        std::move(upstream->message),
        std::move(buffered_plaintext),
        std::move(upstream->websocket_extensions),
        status, upstream->websocket_upgrade, true};
  }
  if (co_await outbound.write_all(upstream->wire) !=
      upstream->wire.size())
    throw std::runtime_error(
        "browser proxy upstream request completed short");
  co_return forwarded_http1_request{
      std::move(upstream->message),
      std::move(buffered_plaintext),
      std::move(upstream->websocket_extensions),
      0, upstream->websocket_upgrade, false};
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
    const ntl::net::inspection::content_encoder_registry &encoders,
    const ntl::net::http::transform_pipeline &transforms,
    browser_html_logger &logger) {
  ntl::net::tls_framed_stream responses(
      outbound,
      make_http_framer(
          ntl::net::http::http1_message_kind::response, true,
          request.message.method == "HEAD"),
      {maximum_http_message_size}, 16 * 1024);
  auto response = co_await responses.read_frame_or_eof();
  if (!response)
    throw std::runtime_error(
        "browser HTTPS server closed before its response");

  auto transformed = ntl::net::http::transform_http1_response(
      response->frame(), request.message, transforms,
      decoders, encoders);
  if (!transformed)
    throw std::system_error(
        static_cast<int>(transformed.status()),
        std::system_category(),
        "HTTP/1 response transform rejected the message");
  forwarded_http1_response forwarded;
  forwarded.buffered_plaintext = responses.release_buffered();
  if (transformed->outcome.action ==
      ntl::net::http::rewrite_action::drop)
    co_return forwarded;
  if (transformed->outcome.action !=
      ntl::net::http::rewrite_action::forward) {
    forwarded.status = co_await send_terminal_response(
        inbound, std::move(transformed->outcome.response),
        encoders, transforms.limits(),
        request.message.method == "HEAD");
    co_return forwarded;
  }

  parsed_http_response parsed;
  parsed.status = transformed->message.status;
  parsed.location = std::string(
      transformed->message.headers.first("location")
          .value_or(std::string_view{}));
  parsed.content_type = std::string(
      transformed->message.headers.first("content-type")
          .value_or(std::string_view{}));
  parsed.content_encoding = std::string(
      transformed->message.headers.first("content-encoding")
          .value_or(std::string_view{}));
  parsed.connection = std::string(
      transformed->message.headers.first("connection")
          .value_or(std::string_view{}));
  parsed.upgrade = std::string(
      transformed->message.headers.first("upgrade")
          .value_or(std::string_view{}));
  parsed.websocket_extensions = std::string(
      transformed->message.headers
          .first("sec-websocket-extensions")
          .value_or(std::string_view{}));
  parsed.body = transformed->message.body;
  parsed.wire_size = transformed->wire.size();
  parsed.body_decoded = true;
  http1_detail::inspect_http1_response(
      parsed, request.websocket_extensions, decoders,
      forwarded.websocket_compression);
  forwarded.html_path =
      logger.record_response(server_name, parsed);
  forwarded.status = parsed.status;
  forwarded.websocket_upgrade = parsed.websocket_upgrade();
  if (co_await inbound.write_all(transformed->wire) !=
      transformed->wire.size())
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
    const ntl::net::inspection::content_encoder_registry &encoders,
    const ntl::net::http::transform_pipeline &transforms,
    browser_html_logger &logger) {
  auto request = co_await forward_http1_request(
      inbound, outbound, decoders, encoders, transforms);
  if (request.terminal) {
    co_await inbound.shutdown();
    co_await outbound.shutdown();
    co_return browser_proxy_result{
        std::move(server_name), request.terminal_status,
        std::nullopt};
  }
  auto response = co_await forward_http1_response(
      outbound, inbound, server_name, request, decoders,
      encoders, transforms, logger);

  if (response.websocket_upgrade) {
    ntl::net::websocket::message_transform_pipeline websocket_policy;
    websocket_policy.inspect(
        [](const ntl::net::websocket::message &) {
          return ntl::net::inspection::verdict::permit;
        });
    auto downstream_to_upstream = relay_websocket_frames(
        inbound, outbound,
        ntl::net::websocket::sender_role::client,
        request.buffered_plaintext, server_name,
        "browser-to-origin", response.websocket_compression,
        websocket_policy, logger);
    auto upstream_to_downstream = relay_websocket_frames(
        outbound, inbound,
        ntl::net::websocket::sender_role::server,
        response.buffered_plaintext, server_name,
        "origin-to-browser", response.websocket_compression,
        websocket_policy, logger);
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
