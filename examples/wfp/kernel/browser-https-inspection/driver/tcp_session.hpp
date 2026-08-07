#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

#include <ntl/net/kernel/redirected_tls_inspection>
#include <ntl/net/kernel/websocket_tunnel>
#include <ntl/net/http2/websocket_tunnel>

#include "http1_proxy.hpp"
#include "websocket_proxy.hpp"

namespace crtsys::wfp_kernel_browser_https::driver {

inline constexpr std::size_t browser_http1_maximum_wire =
    4 * 1024 * 1024 + 64 * 1024;
inline constexpr std::size_t browser_http2_maximum_frame_payload =
    1024 * 1024;

class browser_tcp_observer final
    : public ntl::net::kernel::redirected_http_inspection_observer {
public:
  browser_tcp_observer(tcp_session_observer observer,
                       std::string_view server_name)
      : http1_(observer, server_name), observer_(observer),
        server_name_(server_name) {}

  void on_inspection(
      const ntl::net::http::inspection_context_view &context) noexcept override {
    if (context.wire_protocol() != ntl::net::http::protocol::http2 ||
        context.direction() != ntl::net::http::message_direction::request ||
        context.stage() !=
            ntl::net::http::inspection_stage::message_complete)
      return;
    try {
      std::lock_guard lock(lock_);
      const auto stream_id =
          static_cast<std::uint32_t>(context.stream_id());
      if (requests_.size() >= 256 && !requests_.contains(stream_id))
        return;
      requests_.insert_or_assign(stream_id, context.request());
    } catch (...) {
    }
  }

  void on_http1_request(
      const ntl::net::http::http1_request_event_view &event) noexcept override {
    try {
      http1_.on_request(event);
    } catch (...) {
    }
  }

  void on_http1_response(
      const ntl::net::http::http1_response_event_view &event) noexcept override {
    try {
      http1_.on_response(event);
    } catch (...) {
    }
  }

  ntl::status on_http2_step(
      ntl::net::http2::connection_direction direction,
      const ntl::net::http2::proxy_connection_step &step) noexcept override {
    try {
      if (direction == ntl::net::http2::connection_direction::requests &&
          step.request && step.terminal_status != 0) {
        publish(*step.request, nullptr, step.terminal_status, true);
        if (observer_.blocked)
          observer_.blocked->fetch_add(1, std::memory_order_relaxed);
        return ntl::status::ok();
      }
      if (direction != ntl::net::http2::connection_direction::responses ||
          !step.response)
        return ntl::status::ok();
      std::optional<ntl::net::http::request_message> request;
      {
        std::lock_guard lock(lock_);
        const auto found = requests_.find(step.stream_id);
        if (found != requests_.end()) {
          request = found->second;
          requests_.erase(found);
        }
      }
      if (!request)
        return STATUS_DATA_ERROR;
      const bool blocked = step.terminal_status != 0;
      publish(*request, &*step.response,
              blocked ? step.terminal_status : step.response->status,
              blocked);
      auto *counter = blocked ? observer_.blocked : observer_.permitted;
      if (counter)
        counter->fetch_add(1, std::memory_order_relaxed);
      if (!blocked) {
        if (observer_.transformed)
          observer_.transformed->fetch_add(1, std::memory_order_relaxed);
        if (observer_.origin_completed)
          observer_.origin_completed->fetch_add(1,
                                                std::memory_order_relaxed);
      }
      return ntl::status::ok();
    } catch (const std::bad_alloc &) {
      return STATUS_INSUFFICIENT_RESOURCES;
    } catch (...) {
      return STATUS_UNHANDLED_EXCEPTION;
    }
  }

  void on_session_error(
      ntl::net::kernel::inspected_http_protocol protocol,
      const ntl::net::http::inspection_session_metadata &,
      std::uint32_t status,
      ntl::net::kernel::redirected_http_session_error_stage stage,
      ntl::net::kernel::tls_close_stage close_stage,
      ntl::net::io::async_transport_close_reason close_reason,
      ntl::net::kernel::redirected_http_session_io_direction io_direction,
      ntl::net::kernel::redirected_http_session_failure_point failure_point)
      noexcept override {
    if (!observer_.publish)
      return;
    std::uint32_t flags = 0;
    if (stage == ntl::net::kernel::
                     redirected_http_session_error_stage::protocol)
      flags |= contract::failure_protocol;
    if (stage == ntl::net::kernel::
                     redirected_http_session_error_stage::origin_shutdown)
      flags |= contract::failure_origin_tls_close;
    switch (close_stage) {
    case ntl::net::kernel::tls_close_stage::generate_close_notify:
      flags |= contract::failure_tls_close_generate;
      break;
    case ntl::net::kernel::tls_close_stage::send_close_notify:
      flags |= contract::failure_tls_close_send;
      break;
    case ntl::net::kernel::tls_close_stage::receive_close_notify:
      flags |= contract::failure_tls_close_receive;
      break;
    case ntl::net::kernel::tls_close_stage::transport_half_close:
      flags |= contract::failure_tls_transport_half_close;
      break;
    default:
      break;
    }
    flags |= static_cast<std::uint32_t>(close_reason) << 12;
    flags |= static_cast<std::uint32_t>(io_direction) << 20;
    flags |= static_cast<std::uint32_t>(failure_point) << 24;
    observer_.publish(
        observer_.context, observer_.session_id,
        observer_.original_destination, server_name_,
        protocol == ntl::net::kernel::inspected_http_protocol::http2
            ? contract::inspected_protocol::http2
            : contract::inspected_protocol::http1,
        contract::inspection_action::failed, 0,
        static_cast<NTSTATUS>(status),
        flags | (static_cast<std::uint32_t>(stage) << 8), {}, {});
  }

private:
  void publish(const ntl::net::http::request_message &request,
               const ntl::net::http::response_message *response,
               unsigned status, bool blocked) noexcept {
    if (!observer_.publish)
      return;
    std::uint32_t flags = contract::request_transformed;
    if (response)
      flags |= contract::response_transformed;
    if (has_grpc_content_type(request.headers) ||
        (response && has_grpc_content_type(response->headers)))
      flags |= contract::grpc_message;
    if (request.extended_protocol)
      flags |= contract::websocket_or_extended_connect;
    if (response && has_content_encoding(response->headers))
      flags |= contract::compressed_content;
    if (response && has_html_content_type(response->headers))
      flags |= contract::html_content;
    const std::string captured = capture_request(request);
    constexpr std::string_view denied =
        crtsys::wfp_browser_http_policy::blocked_body;
    const auto body = blocked
        ? std::as_bytes(std::span(denied))
        : response ? std::span<const std::byte>(response->body)
                   : std::span<const std::byte>{};
    observer_.publish(
        observer_.context, observer_.session_id,
        observer_.original_destination, server_name_,
        contract::inspected_protocol::http2,
        blocked ? contract::inspection_action::blocked
                : contract::inspection_action::permitted,
        status, STATUS_SUCCESS, flags,
        std::as_bytes(std::span(captured)), body);
  }

  browser_http1_observer http1_;
  tcp_session_observer observer_{};
  std::string server_name_;
  std::mutex lock_;
  std::unordered_map<std::uint32_t,
                     ntl::net::http::request_message> requests_;
};

class browser_http1_upgrade final
    : public ntl::net::kernel::redirected_http1_upgrade_handler {
public:
  ntl::result<ntl::net::http::http1_tunnel_disposition> admit(
      const ntl::net::http::http1_tunnel_offer_view &offer) noexcept override {
    return ntl::ok(
        offer.kind == ntl::net::http::http1_tunnel_kind::websocket
            ? ntl::net::http::http1_tunnel_disposition::inspect
            : ntl::net::http::http1_tunnel_disposition::reject);
  }

  ntl::net::kernel::task<ntl::status> run(
      context_type &context) noexcept override {
    try {
      auto client = std::make_shared<
          ntl::net::websocket::message_transform_pipeline>(
          make_websocket_pipeline());
      auto server = std::make_shared<
          ntl::net::websocket::message_transform_pipeline>(
          make_websocket_pipeline());
      ntl::net::kernel::websocket_tunnel_options options;
      options.client_mask_provider = &websocket_mask;
      co_return co_await ntl::net::kernel::run_websocket_tunnel(
          context, std::move(client), std::move(server),
          std::move(options));
    } catch (const std::bad_alloc &) {
      co_return ntl::status{STATUS_INSUFFICIENT_RESOURCES};
    } catch (...) {
      co_return ntl::status{STATUS_UNHANDLED_EXCEPTION};
    }
  }
};

class browser_http1_upgrade_factory final
    : public ntl::net::kernel::redirected_http1_upgrade_handler_factory {
public:
  std::shared_ptr<ntl::net::kernel::redirected_http1_upgrade_handler> create(
      const ntl::net::http::inspection_session_metadata &) noexcept override {
    try {
      return std::make_shared<browser_http1_upgrade>();
    } catch (...) {
      return {};
    }
  }
};

class browser_http2_tunnels final
    : public ntl::net::kernel::redirected_http2_tunnel_handler {
public:
  explicit browser_http2_tunnels(std::string_view server_name)
      : policy_(std::make_shared<browser_websocket_policy>()),
        handler_(policy_, make_options()) {
    (void)server_name;
  }

  ntl::result<ntl::net::http2::connect_disposition> admit(
      std::uint32_t stream_id,
      const ntl::net::http::request_message &request) noexcept override {
    return handler_.admit(stream_id, request);
  }
  void observe_response(
      std::uint32_t stream_id,
      const ntl::net::http::response_message &response) override {
    handler_.observe_response(stream_id, response);
  }
  ntl::result<std::optional<std::vector<
      ntl::net::http2::outbound_frame>>>
  transform(ntl::net::http2::connection_direction direction,
            const ntl::net::http2::frame_view &frame) noexcept override {
    return handler_.transform(direction, frame);
  }
  void reset(std::uint32_t stream_id) noexcept override {
    handler_.reset(stream_id);
  }

private:
  static ntl::net::http2::websocket_tunnel_options make_options() {
    return {.maximum_sessions = 256,
            .maximum_http2_frame_payload =
                ntl::net::http2::default_maximum_frame_size,
            .unsupported =
                ntl::net::http2::unsupported_tunnel_action::block,
            .message_limits =
                {.maximum_wire_frame_bytes = 1024 * 1024,
                 .maximum_decoded_message_bytes = 4 * 1024 * 1024,
                 .maximum_encoded_message_bytes = 4 * 1024 * 1024,
                 .maximum_output_frame_payload = 64 * 1024,
                 .validate_text_utf8 = true},
            .stream_limits =
                {.maximum_pending_wire_bytes = 2 * 1024 * 1024 + 14,
                 .maximum_output_bytes_per_push = 8 * 1024 * 1024},
            .client_mask_provider = &websocket_mask};
  }

  std::shared_ptr<browser_websocket_policy> policy_;
  ntl::net::http2::websocket_tunnel_handler handler_;
};

class browser_http2_tunnel_factory final
    : public ntl::net::kernel::redirected_http2_tunnel_handler_factory {
public:
  std::shared_ptr<ntl::net::kernel::redirected_http2_tunnel_handler> create(
      const ntl::net::http::inspection_session_metadata &metadata) noexcept
      override {
    try {
      return std::make_shared<browser_http2_tunnels>(
          metadata.tls.server_name.value_or(std::string{}));
    } catch (...) {
      return {};
    }
  }
};

using browser_tcp_observer_factory = std::function<tcp_session_observer(
    const ntl::net::http::inspection_session_metadata &)>;

inline ntl::result<std::shared_ptr<
    ntl::net::kernel::standard_redirected_tls_inspection>>
make_browser_tcp_dispatcher(
    std::shared_ptr<const ntl::net::http::inspection_policy> policy,
    browser_tcp_observer_factory make_observer) noexcept {
  if (!policy || !make_observer)
    return ntl::unexpected(STATUS_INVALID_PARAMETER);
  ntl::net::kernel::redirected_tls_inspection_options options;
  options.resources.http1.framing = {
      .maximum_header_size = 64 * 1024,
      .maximum_body_size = 4 * 1024 * 1024,
      .maximum_chunk_line_size = 4 * 1024,
      .maximum_trailer_size = 16 * 1024};
  options.resources.http1.maximum_wire_message_size =
      browser_http1_maximum_wire;
  options.resources.http1.request_receive_chunk_size = 16 * 1024;
  options.resources.http1.response_receive_chunk_size = 16 * 1024;
  options.resources.http2.maximum_frame_payload =
      browser_http2_maximum_frame_payload;
  options.http2_preflight.maximum_frame_payload =
      browser_http2_maximum_frame_payload;
  options.http2_preflight.maximum_buffered_origin_bytes = 16 * 1024 * 1024;
  options.http2_preflight.advertise_extended_connect = true;
  options.resources.maximum_concurrent_http1_sessions = 4;
  options.resources.maximum_concurrent_http2_sessions = 8;
  options.http1_upgrades =
      std::make_shared<browser_http1_upgrade_factory>();
  options.http2_tunnels =
      std::make_shared<browser_http2_tunnel_factory>();
  options.make_observer =
      [factory = std::move(make_observer)](
          const ntl::net::http::inspection_session_metadata &metadata)
          -> std::shared_ptr<
              ntl::net::kernel::redirected_http_inspection_observer> {
        try {
          auto state = factory(metadata);
          if (!state.owner)
            return {};
          return std::make_shared<browser_tcp_observer>(
              std::move(state),
              metadata.tls.server_name.value_or(std::string{}));
        } catch (...) {
          return {};
        }
      };
  return ntl::net::kernel::standard_redirected_tls_inspection::create(
      std::move(policy), std::move(options));
}

} // namespace crtsys::wfp_kernel_browser_https::driver
