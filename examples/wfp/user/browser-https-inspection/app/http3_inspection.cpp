#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "http3_inspection.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <syncstream>
#include <utility>

#include "browser_log.hpp"

namespace crtsys::wfp_sample::browser_https {

browser_http3_observer::browser_http3_observer(
    std::shared_ptr<browser_html_logger> logger) noexcept
    : logger_(std::move(logger)) {}

void browser_http3_observer::on_connected(
    std::string_view alpn) noexcept {
  logger_->record_lifecycle(
      "http3-connection alpn=" + std::string(alpn));
}

void browser_http3_observer::on_inspection(
    const ntl::net::http::inspection_context_view &context) noexcept {
  try {
    const std::string host =
        context.tls().server_name.value_or(
            std::string(context.authority()));
    const std::wstring wide_host(host.begin(), host.end());
    if (context.direction() ==
            ntl::net::http::message_direction::request &&
        context.stage() ==
            ntl::net::http::inspection_stage::headers) {
      logger_->record_request(
          wide_host, context.method(), context.target());
      return;
    }
    if (context.direction() !=
            ntl::net::http::message_direction::response ||
        context.stage() !=
            ntl::net::http::inspection_stage::message_complete ||
        !context.response())
      return;

    const auto &message = *context.response();
    parsed_http_response inspected;
    inspected.status = message.status;
    if (const auto content_type =
            message.headers.first("content-type"))
      inspected.content_type.assign(*content_type);
    inspected.content_encoding =
        message.headers.joined("content-encoding");
    inspected.body.assign(message.body.begin(), message.body.end());
    inspected.wire_size = message.body.size();
    inspected.body_decoded = true;
    if (const auto location = message.headers.first("location"))
      inspected.location.assign(*location);
    logger_->record_protocol(wide_host, "h3");
    const auto html = logger_->record_response(wide_host, inspected);
    std::wosyncstream(std::wcout)
        << L"NTL HTTP/3 inspected: host=" << wide_host
        << L", downstream=h3, status=" << message.status
        << L", html="
        << (html ? html->wstring() : std::wstring(L"none"))
        << L'\n';
  } catch (const std::exception &error) {
    logger_->record_error(error.what());
  } catch (...) {
    logger_->record_error("unknown HTTP/3 observer failure");
  }
}

void browser_http3_observer::on_exchange_complete(
    std::uint64_t, const ntl::net::http::request_message &,
    const ntl::net::http::response_message &, bool) noexcept {
  delivered_.fetch_add(1, std::memory_order_relaxed);
}

void browser_http3_observer::on_closed(NTSTATUS status) noexcept {
  if (NT_SUCCESS(status))
    return;
  logger_->record_error(
      "HTTP/3 transport status=" +
      std::to_string(static_cast<unsigned long>(status)));
}

std::size_t browser_http3_observer::delivered_requests() const noexcept {
  return delivered_.load(std::memory_order_relaxed);
}

} // namespace crtsys::wfp_sample::browser_https
