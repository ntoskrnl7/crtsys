#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>

#include "http2_inspection.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ntl/net/http2/proxy_connection>
#include <ntl/net/http2/proxy_session>
#include <ntl/net/http2/websocket_tunnel>
#include <ntl/net/inspection/core>

namespace crtsys::wfp_sample::browser_https {
namespace {

class http2_relay_observation {
public:
  void record_status(unsigned status) noexcept {
    std::lock_guard lock(lock_);
    last_status_ = status;
  }

  void record_response(
      unsigned status,
      std::optional<std::filesystem::path> path) noexcept {
    std::lock_guard lock(lock_);
    last_status_ = status;
    if (path && !html_path_)
      html_path_ = std::move(path);
  }

  unsigned last_status() const noexcept {
    std::lock_guard lock(lock_);
    return last_status_;
  }

  std::optional<std::filesystem::path> html_path() const {
    std::lock_guard lock(lock_);
    return html_path_;
  }

private:
  mutable std::mutex lock_;
  unsigned last_status_ = 0;
  std::optional<std::filesystem::path> html_path_;
};

class browser_http2_observer {
public:
  browser_http2_observer(
      std::wstring_view server_name,
      std::shared_ptr<browser_html_logger> logger,
      std::shared_ptr<http2_relay_observation> observation)
      : server_name_(server_name), logger_(std::move(logger)),
        observation_(std::move(observation)) {}

  void on_inspection(
      const ntl::net::http::inspection_context_view &context) {
    if (context.stage() !=
            ntl::net::http::inspection_stage::message_complete ||
        !context.response())
      return;

    const auto &response = *context.response();
    parsed_http_response inspected;
    inspected.status = response.status;
    inspected.content_type = std::string(
        response.headers.first("content-type")
            .value_or(std::string_view{}));
    inspected.content_encoding = std::string(
        response.headers.first("content-encoding")
            .value_or(std::string_view{}));
    inspected.location = std::string(
        response.headers.first("location")
            .value_or(std::string_view{}));
    inspected.body.assign(response.body.begin(), response.body.end());
    inspected.wire_size = response.body.size();
    inspected.body_decoded = true;
    auto logged = logger_->record_response(server_name_, inspected);
    observation_->record_response(response.status, std::move(logged));
  }

private:
  std::wstring server_name_;
  std::shared_ptr<browser_html_logger> logger_;
  std::shared_ptr<http2_relay_observation> observation_;
};

class browser_websocket_policy {
public:
  browser_websocket_policy(
      std::wstring_view server_name,
      std::shared_ptr<browser_html_logger> logger)
      : server_name_(server_name), logger_(std::move(logger)) {}

  ntl::net::websocket::rewrite_result transform(
      std::uint32_t,
      ntl::net::http2::connection_direction direction,
      ntl::net::websocket::message &message) {
    logger_->record_websocket(
        server_name_,
        direction == ntl::net::http2::connection_direction::requests
            ? "browser-to-origin-h2"
            : "origin-to-browser-h2",
        message.operation, message.payload.size(), true, false,
        message.payload.size());
    return ntl::net::websocket::rewrite_result::unchanged();
  }

private:
  std::wstring server_name_;
  std::shared_ptr<browser_html_logger> logger_;
};

class browser_http2_tunnels final
    : public ntl::net::user::redirected_http2_tunnel_handler {
public:
  browser_http2_tunnels(
      std::wstring server_name,
      std::shared_ptr<browser_html_logger> logger)
      : policy_(std::make_shared<browser_websocket_policy>(
            server_name, std::move(logger))),
        tunnels_(policy_, make_options()) {}

  ntl::result<ntl::net::http2::connect_disposition> admit(
      std::uint32_t stream_id,
      const ntl::net::http::request_message &request) noexcept override {
    return tunnels_.admit(stream_id, request);
  }

  void observe_response(
      std::uint32_t stream_id,
      const ntl::net::http::response_message &response) override {
    tunnels_.observe_response(stream_id, response);
  }

  ntl::result<std::optional<std::vector<
      ntl::net::http2::outbound_frame>>>
  transform(ntl::net::http2::connection_direction direction,
            const ntl::net::http2::frame_view &frame) noexcept override {
    return tunnels_.transform(direction, frame);
  }

  void reset(std::uint32_t stream_id) noexcept override {
    tunnels_.reset(stream_id);
  }

private:
  static ntl::net::http2::websocket_tunnel_options make_options() {
    ntl::net::http2::websocket_tunnel_options options;
    options.unsupported =
        ntl::net::http2::unsupported_tunnel_action::block;
    options.client_mask_provider = [] {
      std::array<std::byte, 4> key{};
      if (!BCRYPT_SUCCESS(::BCryptGenRandom(
              nullptr, reinterpret_cast<PUCHAR>(key.data()),
              static_cast<ULONG>(key.size()),
              BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
        throw std::runtime_error(
            "HTTP/2 WebSocket mask generation failed");
      return key;
    };
    return options;
  }

  std::shared_ptr<browser_websocket_policy> policy_;
  ntl::net::http2::websocket_tunnel_handler tunnels_;
};

class browser_http2_tunnel_factory final
    : public ntl::net::user::redirected_http2_tunnel_handler_factory {
public:
  explicit browser_http2_tunnel_factory(
      std::shared_ptr<browser_html_logger> logger) noexcept
      : logger_(std::move(logger)) {}

  std::unique_ptr<ntl::net::user::redirected_http2_tunnel_handler> create(
      const ntl::net::http::inspection_session_metadata &metadata) override {
    const std::string server_name =
        metadata.tls.server_name.value_or(std::string{});
    return std::make_unique<browser_http2_tunnels>(
        std::wstring(server_name.begin(), server_name.end()), logger_);
  }

private:
  std::shared_ptr<browser_html_logger> logger_;
};

template <class HttpPolicy>
ntl::net::user::task<browser_proxy_result> relay_http2_connection_impl(
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
  auto observation = std::make_shared<http2_relay_observation>();
  auto websocket_policy = std::make_shared<browser_websocket_policy>(
      server_name, logger);
  ntl::net::http2::websocket_tunnel_options websocket_options;
  websocket_options.unsupported =
      ntl::net::http2::unsupported_tunnel_action::block;
  websocket_options.client_mask_provider = [] {
    std::array<std::byte, 4> key{};
    if (!BCRYPT_SUCCESS(::BCryptGenRandom(
            nullptr, reinterpret_cast<PUCHAR>(key.data()),
            static_cast<ULONG>(key.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
      throw std::runtime_error(
          "HTTP/2 WebSocket mask generation failed");
    return key;
  };
  auto extended_connect =
      std::make_shared<ntl::net::http2::websocket_tunnel_handler>(
          std::move(websocket_policy), std::move(websocket_options));

  auto observer = std::make_shared<browser_http2_observer>(
      server_name, logger, observation);
  std::shared_ptr<ntl::net::http2::proxy_connection> connection;
  if constexpr (std::is_same_v<HttpPolicy,
                               ntl::net::http::inspection_policy>) {
    connection = std::make_shared<ntl::net::http2::proxy_connection>(
        policy, metadata, ntl::net::http2::inspection_observer(observer));
  } else {
    connection = std::make_shared<ntl::net::http2::proxy_connection>(
        policy, decoders, encoders, metadata,
        ntl::net::http2::inspection_observer(observer));
  }

  const auto session = co_await ntl::net::http2::run_proxy_session(
      std::move(inbound), std::move(outbound), std::move(connection),
      std::move(extended_connect));
  if (session.last_terminal_status != 0)
    observation->record_status(session.last_terminal_status);

  co_return browser_proxy_result{
      std::move(server_name), observation->last_status(),
      observation->html_path()};
}

} // namespace

std::shared_ptr<
    ntl::net::user::redirected_http2_tunnel_handler_factory>
make_browser_http2_tunnel_factory(
    std::shared_ptr<browser_html_logger> logger) {
  if (!logger)
    throw std::invalid_argument("browser HTTP/2 logger is null");
  return std::make_shared<browser_http2_tunnel_factory>(std::move(logger));
}

ntl::net::user::task<browser_proxy_result> relay_http2_connection(
    std::shared_ptr<ntl::net::tls_stream> inbound,
    std::shared_ptr<ntl::net::tls_stream> outbound,
    ntl::net::http::inspection_session_metadata metadata,
    std::shared_ptr<const ntl::net::inspection::content_decoder_registry>
        decoders,
    std::shared_ptr<const ntl::net::inspection::content_encoder_registry>
        encoders,
    std::shared_ptr<const ntl::net::http::transform_pipeline> transforms,
    std::shared_ptr<browser_html_logger> logger) {
  co_return co_await relay_http2_connection_impl(
      std::move(inbound), std::move(outbound),
      std::move(metadata), std::move(decoders), std::move(encoders),
      std::move(transforms), std::move(logger));
}

ntl::net::user::task<browser_proxy_result> relay_http2_connection(
    std::shared_ptr<ntl::net::tls_stream> inbound,
    std::shared_ptr<ntl::net::tls_stream> outbound,
    ntl::net::http::inspection_session_metadata metadata,
    std::shared_ptr<const ntl::net::http::inspection_policy> policy,
    std::shared_ptr<browser_html_logger> logger) {
  co_return co_await relay_http2_connection_impl(
      std::move(inbound), std::move(outbound),
      std::move(metadata), policy->content_decoders(),
      policy->content_encoders(),
      std::move(policy), std::move(logger));
}

} // namespace crtsys::wfp_sample::browser_https
