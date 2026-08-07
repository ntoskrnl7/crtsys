#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "browser_proxy.hpp"

#include <iostream>
#include <stdexcept>
#include <syncstream>
#include <utility>

#include "browser_tunnels.hpp"

namespace crtsys::wfp_sample::browser_https {
namespace {

class browser_http_observer final
    : public ntl::net::user::redirected_http_inspection_observer {
public:
  explicit browser_http_observer(
      std::shared_ptr<browser_html_logger> logger) noexcept
      : logger_(std::move(logger)) {}

  void on_inspection(
      const ntl::net::http::inspection_context_view &context) noexcept override {
    if (context.stage() !=
        ntl::net::http::inspection_stage::message_complete)
      return;
    const std::string server_name =
        context.tls().server_name.value_or(std::string{});
    const std::wstring wide_name(server_name.begin(), server_name.end());
    if (context.direction() ==
        ntl::net::http::message_direction::request) {
      logger_->record_request(wide_name, context.method(), context.target());
      return;
    }
    const auto *response = context.response();
    if (!response)
      return;
    try {
      parsed_http_response inspected;
      inspected.status = response->status;
      inspected.content_type = std::string(
          response->headers.first("content-type")
              .value_or(std::string_view{}));
      inspected.content_encoding = std::string(
          response->headers.first("content-encoding")
              .value_or(std::string_view{}));
      inspected.location = std::string(
          response->headers.first("location")
              .value_or(std::string_view{}));
      inspected.body.assign(response->body.begin(), response->body.end());
      inspected.wire_size = response->body.size();
      inspected.body_decoded = true;
      if (auto path = logger_->record_response(wide_name, inspected)) {
        std::osyncstream(std::cout)
            << "NTL WFP browser HTTPS HTML logged: host=" << server_name
            << ", status=" << response->status
            << ", file=" << path->string() << '\n';
      }
    } catch (...) {
      logger_->record_error("browser HTTP observation failed");
    }
  }

private:
  std::shared_ptr<browser_html_logger> logger_;
};

} // namespace

std::shared_ptr<ntl::net::user::standard_redirected_tls_inspection>
make_browser_http_dispatcher(
    std::shared_ptr<const ntl::net::http::inspection_policy> policy,
    std::shared_ptr<browser_html_logger> logger) {
  if (!policy || !logger)
    throw std::invalid_argument("browser HTTP dispatcher dependency is null");
  ntl::net::user::redirected_tls_inspection_options options;
  auto observer = std::make_shared<browser_http_observer>(logger);
  options.make_observer =
      [observer](const ntl::net::http::inspection_session_metadata &) {
        return observer;
      };
  options.http1_upgrades = make_browser_http1_upgrade_factory(logger);
  options.http2_tunnels = make_browser_http2_tunnel_factory(logger);
  return std::make_shared<
      ntl::net::user::standard_redirected_tls_inspection>(
      std::move(policy), std::move(options));
}

} // namespace crtsys::wfp_sample::browser_https
