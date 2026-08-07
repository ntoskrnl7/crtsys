#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>

#include "browser_tunnels.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <utility>

#include <ntl/net/http2/websocket_tunnel>
#include <ntl/net/user/websocket_tunnel>

namespace crtsys::wfp_sample::browser_https {
namespace {

std::array<std::byte, 4> websocket_mask() {
  std::array<std::byte, 4> key{};
  if (!BCRYPT_SUCCESS(::BCryptGenRandom(
          nullptr, reinterpret_cast<PUCHAR>(key.data()),
          static_cast<ULONG>(key.size()),
          BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
    throw std::runtime_error("WebSocket mask generation failed");
  return key;
}

std::shared_ptr<const ntl::net::websocket::message_transform_pipeline>
make_websocket_policy(
    std::shared_ptr<browser_html_logger> logger,
    std::wstring server_name, std::string direction) {
  auto policy = std::make_shared<
      ntl::net::websocket::message_transform_pipeline>();
  policy->inspect(
      [logger = std::move(logger), server_name = std::move(server_name),
       direction = std::move(direction)](
          const ntl::net::websocket::message &message) {
        logger->record_websocket(
            server_name, direction, message.operation,
            message.payload.size(), true, message.compressed(),
            message.payload.size());
        return ntl::net::inspection::verdict::permit;
      });
  return policy;
}

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
    ntl::net::user::websocket_tunnel_options options;
    options.client_mask_provider = &websocket_mask;
    return ntl::net::user::run_websocket_tunnel(
        context,
        make_websocket_policy(
            logger_, server_name_, "browser-to-origin"),
        make_websocket_policy(
            logger_, server_name_, "origin-to-browser"),
        std::move(options));
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

class browser_http2_policy {
public:
  browser_http2_policy(
      std::wstring server_name,
      std::shared_ptr<browser_html_logger> logger) noexcept
      : server_name_(std::move(server_name)), logger_(std::move(logger)) {}

  ntl::net::websocket::rewrite_result transform(
      std::uint32_t,
      ntl::net::http2::connection_direction direction,
      ntl::net::websocket::message &message) noexcept {
    logger_->record_websocket(
        server_name_,
        direction == ntl::net::http2::connection_direction::requests
            ? "browser-to-origin-h2"
            : "origin-to-browser-h2",
        message.operation, message.payload.size(), true,
        message.compressed(), message.payload.size());
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
      : policy_(std::make_shared<browser_http2_policy>(
            std::move(server_name), std::move(logger))),
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
    options.client_mask_provider = &websocket_mask;
    return options;
  }

  std::shared_ptr<browser_http2_policy> policy_;
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

} // namespace

std::shared_ptr<
    ntl::net::user::redirected_http1_upgrade_handler_factory>
make_browser_http1_upgrade_factory(
    std::shared_ptr<browser_html_logger> logger) {
  if (!logger)
    throw std::invalid_argument("browser HTTP/1 logger is null");
  return std::make_shared<browser_http1_upgrade_factory>(std::move(logger));
}

std::shared_ptr<
    ntl::net::user::redirected_http2_tunnel_handler_factory>
make_browser_http2_tunnel_factory(
    std::shared_ptr<browser_html_logger> logger) {
  if (!logger)
    throw std::invalid_argument("browser HTTP/2 logger is null");
  return std::make_shared<browser_http2_tunnel_factory>(std::move(logger));
}

} // namespace crtsys::wfp_sample::browser_https
