#pragma once

#include "tcp_common.hpp"

namespace crtsys::wfp_kernel_browser_https::driver {

/** Browser-specific HTTP/1 telemetry; framing and relay are owned by NTL. */
class browser_http1_observer {
public:
  browser_http1_observer(tcp_session_observer observer,
                         std::string_view server_name)
      : observer_(observer), server_name_(server_name) {}

  void on_request(const ntl::net::http::http1_request_event_view &event) {
    request_ = capture_request(event.message);
    flags_ = contract::request_transformed;
    if (has_content_encoding(event.message.headers))
      flags_ |= contract::compressed_content;
    if (has_grpc_content_type(event.message.headers))
      flags_ |= contract::grpc_message;
    if (event.websocket_upgrade)
      flags_ |= contract::websocket_or_extended_connect;
  }

  void on_response(const ntl::net::http::http1_response_event_view &event) {
    if (event.informational)
      return;
    std::uint32_t flags = flags_ | contract::response_transformed;
    if (has_content_encoding(event.message.headers))
      flags |= contract::compressed_content;
    if (has_html_content_type(event.message.headers))
      flags |= contract::html_content;
    if (event.websocket_upgrade)
      flags |= contract::websocket_or_extended_connect;
    const bool blocked = event.synthetic;
    if (observer_.publish)
      observer_.publish(
          observer_.context, observer_.session_id,
          observer_.original_destination, server_name_,
          contract::inspected_protocol::http1,
          blocked ? contract::inspection_action::blocked
                  : contract::inspection_action::permitted,
          event.message.status, STATUS_SUCCESS, flags,
          std::as_bytes(std::span(request_)), event.message.body);
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
  }

private:
  tcp_session_observer observer_{};
  std::string server_name_;
  std::string request_;
  std::uint32_t flags_ = 0;
};

} // namespace crtsys::wfp_kernel_browser_https::driver
