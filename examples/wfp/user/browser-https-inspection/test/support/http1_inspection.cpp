#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include "http1_inspection.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include <ntl/net/http/http1_proxy_connection>
#include <ntl/net/inspection/core>
#include <ntl/net/user/structured_concurrency>
#include <ntl/net/user/task>
#include <ntl/net/websocket/framing>
#include <ntl/net/websocket/permessage_deflate>
#include <ntl/net/websocket/transform>

#include "http1_support.hpp"

namespace crtsys::wfp_sample::browser_https {
namespace {

ntl::net::user::task<std::size_t> relay_websocket_frames(
    ntl::net::tls_stream &source, ntl::net::tls_stream &destination,
    ntl::net::websocket::sender_role sender,
    std::span<const std::byte> initial_plaintext,
    std::wstring_view server_name, std::string_view direction,
    const ntl::net::websocket::permessage_deflate_parameters &compression,
    const ntl::net::websocket::message_transform_pipeline &policy,
    browser_html_logger &logger) {
  constexpr std::size_t maximum_frame_size = 2 * 1024 * 1024;
  constexpr std::size_t maximum_message_size = 8 * 1024 * 1024;
  ntl::net::tls_framed_stream frames(
      source,
      ntl::net::websocket::frame_framer(
          sender,
          {maximum_frame_size,
           compression.enabled ? std::uint8_t{0x04} : std::uint8_t{0}}),
      {maximum_frame_size + 14}, 16 * 1024);
  frames.append_buffered(initial_plaintext);
  ntl::net::websocket::message_assembler messages(maximum_message_size);
  auto decoder = ntl::net::websocket::make_permessage_deflate_decoder(
      compression, sender, maximum_message_size);
  const auto mask_provider = [] {
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
    const auto wire = ntl::net::scatter_view::from_contiguous(frame->frame());
    const auto header = ntl::net::websocket::inspect_header(
        wire, sender,
        {maximum_frame_size,
         compression.enabled ? std::uint8_t{0x04} : std::uint8_t{0}});
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
      throw std::runtime_error("invalid WebSocket message fragmentation");

    std::size_t inspected_message_size = 0;
    bool message_compressed = false;
    if (*completed && !(*completed)->control) {
      message_compressed = (*completed)->compressed();
      auto inspected = decoder.decode((*completed)->payload,
                                      message_compressed);
      if (!inspected)
        throw std::system_error(
            static_cast<int>(inspected.status()), std::system_category(),
            "WebSocket permessage-deflate decoder rejected the message");
      inspected_message_size = inspected->size();
    }
    logger.record_websocket(
        server_name, direction, header->operation, payload->size(),
        completed->has_value(), message_compressed, inspected_message_size);
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

ntl::net::user::task<void> relay_websocket_upgrade(
    ntl::net::http::http1_upgrade_context_view<ntl::net::tls_stream,
                                          ntl::net::tls_stream> &context,
    std::wstring_view server_name, browser_html_logger &logger) {
  if (context.kind != ntl::net::http::http1_tunnel_kind::websocket)
    throw std::system_error(
        ERROR_NOT_SUPPORTED, std::system_category(),
        "this example has no policy adapter for the HTTP/1 tunnel");
  auto compression =
      ntl::net::websocket::parse_permessage_deflate_response(
          context.selected_websocket_extensions);
  if (!compression)
    throw std::system_error(
        static_cast<int>(compression.status()), std::system_category(),
        "origin negotiated an unsupported WebSocket extension");
  if (compression->enabled &&
      !ascii_contains_ci(context.offered_websocket_extensions,
                         "permessage-deflate"))
    throw std::runtime_error(
        "origin selected permessage-deflate without a matching offer");

  ntl::net::websocket::message_transform_pipeline websocket_policy;
  websocket_policy.inspect([](const ntl::net::websocket::message &) {
    return ntl::net::inspection::verdict::permit;
  });
  auto downstream_to_upstream = relay_websocket_frames(
      context.downstream, context.upstream,
      ntl::net::websocket::sender_role::client,
      context.downstream_carry, server_name, "browser-to-origin",
      *compression, websocket_policy, logger);
  auto upstream_to_downstream = relay_websocket_frames(
      context.upstream, context.downstream,
      ntl::net::websocket::sender_role::server,
      context.upstream_carry, server_name, "origin-to-browser",
      *compression, websocket_policy, logger);
  co_await ntl::net::user::when_all_stop_on_first(
      std::move(downstream_to_upstream),
      std::move(upstream_to_downstream),
      [&downstream = context.downstream,
       &upstream = context.upstream]() noexcept {
        downstream.cancel();
        upstream.cancel();
      });
}

struct browser_http1_tunnel_handler {
  std::wstring_view server_name;
  browser_html_logger *logger = nullptr;

  ntl::result<ntl::net::http::http1_tunnel_disposition> admit(
      const ntl::net::http::http1_tunnel_offer_view &offer) const noexcept {
    return ntl::ok(
        offer.kind == ntl::net::http::http1_tunnel_kind::websocket
            ? ntl::net::http::http1_tunnel_disposition::inspect
            : ntl::net::http::http1_tunnel_disposition::reject);
  }

  ntl::net::user::task<void> operator()(
      ntl::net::http::http1_upgrade_context_view<
          ntl::net::tls_stream, ntl::net::tls_stream> &context) const {
    return relay_websocket_upgrade(context, server_name, *logger);
  }
};

struct browser_http1_observer {
  std::wstring_view server_name;
  browser_html_logger *logger = nullptr;
  std::optional<std::filesystem::path> *last_html_path = nullptr;

  void on_response(const ntl::net::http::http1_response_event_view &event) const {
    if (event.informational || event.synthetic)
      return;
    parsed_http_response parsed;
    parsed.status = event.message.status;
    parsed.location = std::string(
        event.message.headers.first("location").value_or(std::string_view{}));
    parsed.content_type = std::string(event.message.headers
                                          .first("content-type")
                                          .value_or(std::string_view{}));
    parsed.content_encoding = std::string(
        event.message.headers.first("content-encoding")
            .value_or(std::string_view{}));
    parsed.connection = std::string(event.message.headers
                                        .first("connection")
                                        .value_or(std::string_view{}));
    parsed.upgrade = std::string(
        event.message.headers.first("upgrade").value_or(std::string_view{}));
    parsed.websocket_extensions = std::string(
        event.message.headers.first("sec-websocket-extensions")
            .value_or(std::string_view{}));
    parsed.body.assign(
        event.message.body.begin(), event.message.body.end());
    parsed.wire_size = event.wire_size;
    parsed.body_decoded = true;
    if (auto path = logger->record_response(server_name, parsed))
      *last_html_path = std::move(path);
  }
};

class browser_http1_upgrade final
    : public ntl::net::user::redirected_http1_upgrade_handler {
public:
  browser_http1_upgrade(
      std::wstring server_name,
      std::shared_ptr<browser_html_logger> logger) noexcept
      : server_name_(std::move(server_name)), logger_(std::move(logger)) {}

  ntl::result<ntl::net::http::http1_tunnel_disposition> admit(
      const ntl::net::http::http1_tunnel_offer_view &offer) noexcept override {
    return ntl::ok(
        offer.kind == ntl::net::http::http1_tunnel_kind::websocket
            ? ntl::net::http::http1_tunnel_disposition::inspect
            : ntl::net::http::http1_tunnel_disposition::reject);
  }

  ntl::net::user::task<void> run(context_type &context) override {
    return relay_websocket_upgrade(context, server_name_, *logger_);
  }

private:
  std::wstring server_name_;
  std::shared_ptr<browser_html_logger> logger_;
};

class browser_http1_upgrade_factory final
    : public ntl::net::user::redirected_http1_upgrade_handler_factory {
public:
  explicit browser_http1_upgrade_factory(
      std::shared_ptr<browser_html_logger> logger) noexcept
      : logger_(std::move(logger)) {}

  std::unique_ptr<ntl::net::user::redirected_http1_upgrade_handler> create(
      const ntl::net::http::inspection_session_metadata &metadata) override {
    const std::string server_name =
        metadata.tls.server_name.value_or(std::string{});
    return std::make_unique<browser_http1_upgrade>(
        std::wstring(server_name.begin(), server_name.end()), logger_);
  }

private:
  std::shared_ptr<browser_html_logger> logger_;
};

} // namespace

std::shared_ptr<
    ntl::net::user::redirected_http1_upgrade_handler_factory>
make_browser_http1_upgrade_factory(
    std::shared_ptr<browser_html_logger> logger) {
  if (!logger)
    throw std::invalid_argument("browser HTTP/1 logger is null");
  return std::make_shared<browser_http1_upgrade_factory>(std::move(logger));
}

template <class HttpPolicy>
ntl::net::user::task<browser_proxy_result> relay_http1_connection_impl(
    std::shared_ptr<ntl::net::tls_stream> inbound,
    std::shared_ptr<ntl::net::tls_stream> outbound,
    ntl::net::http::inspection_session_metadata metadata,
    std::shared_ptr<const ntl::net::inspection::content_decoder_registry>
        decoders,
    std::shared_ptr<const ntl::net::inspection::content_encoder_registry>
        encoders,
    std::shared_ptr<const HttpPolicy> policy,
    std::shared_ptr<browser_html_logger> logger) {
  const std::string server_name_utf8 =
      metadata.tls.server_name.value_or(std::string{});
  const std::wstring server_name(server_name_utf8.begin(),
                                 server_name_utf8.end());
  std::optional<std::filesystem::path> last_html_path;
  ntl::net::http::http1_proxy_limits limits;
  limits.framing = {
      .maximum_header_size = maximum_http_header_size,
      .maximum_body_size = maximum_http_body_size,
      .maximum_chunk_line_size = 4 * 1024,
      .maximum_trailer_size = 16 * 1024,
      .allow_close_delimited_response = true,
      .response_body_forbidden = false};
  limits.maximum_wire_message_size = maximum_http_message_size;
  limits.require_server_name_authority_binding = true;

  using connection_type = ntl::net::http::http1_proxy_connection<
      ntl::net::tls_stream, ntl::net::tls_stream>;
  std::shared_ptr<connection_type> connection;
  if constexpr (std::is_same_v<HttpPolicy,
                               ntl::net::http::inspection_policy>) {
    connection = std::make_shared<connection_type>(
        inbound, outbound, policy,
        ntl::net::http::http1_request_target_context{
            .origin_scheme = "https"},
        limits, std::move(metadata));
  } else {
    connection = std::make_shared<connection_type>(
        inbound, outbound, policy, decoders, encoders,
        ntl::net::http::http1_request_target_context{
            .origin_scheme = "https"},
        limits, std::move(metadata));
  }
  const auto result = co_await connection->run(
      browser_http1_observer{server_name, logger.get(), &last_html_path},
      browser_http1_tunnel_handler{
          server_name, logger.get()});
  co_return browser_proxy_result{
      std::move(server_name), result.last_status,
      std::move(last_html_path)};
}

ntl::net::user::task<browser_proxy_result> relay_http1_connection(
    std::shared_ptr<ntl::net::tls_stream> inbound,
    std::shared_ptr<ntl::net::tls_stream> outbound,
    ntl::net::http::inspection_session_metadata metadata,
    std::shared_ptr<const ntl::net::inspection::content_decoder_registry>
        decoders,
    std::shared_ptr<const ntl::net::inspection::content_encoder_registry>
        encoders,
    std::shared_ptr<const ntl::net::http::transform_pipeline> transforms,
    std::shared_ptr<browser_html_logger> logger) {
  co_return co_await relay_http1_connection_impl(
      std::move(inbound), std::move(outbound), std::move(metadata),
      std::move(decoders),
      std::move(encoders), std::move(transforms), std::move(logger));
}

ntl::net::user::task<browser_proxy_result> relay_http1_connection(
    std::shared_ptr<ntl::net::tls_stream> inbound,
    std::shared_ptr<ntl::net::tls_stream> outbound,
    ntl::net::http::inspection_session_metadata metadata,
    std::shared_ptr<const ntl::net::http::inspection_policy> policy,
    std::shared_ptr<browser_html_logger> logger) {
  co_return co_await relay_http1_connection_impl(
      std::move(inbound), std::move(outbound), std::move(metadata),
      policy->content_decoders(), policy->content_encoders(),
      std::move(policy), std::move(logger));
}

} // namespace crtsys::wfp_sample::browser_https
