#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <ntl/net/http/inspection_policy>
#include <ntl/net/http3/proxy_connection>

namespace crtsys::wfp_sample::browser_https {

class browser_html_logger;

/** Privacy-preserving browser logging; no QPACK/session glue lives here. */
class browser_http3_observer final
    : public ntl::net::http3::proxy_connection_observer {
public:
  explicit browser_http3_observer(
      std::shared_ptr<browser_html_logger> logger) noexcept;

  void on_connected(std::string_view alpn) noexcept override;
  void on_inspection(
      const ntl::net::http::inspection_context_view &context) noexcept override;
  void on_exchange_complete(
      std::uint64_t stream_id,
      const ntl::net::http::request_message &request,
      const ntl::net::http::response_message &response,
      bool terminal) noexcept override;
  void on_closed(NTSTATUS status) noexcept override;

  std::size_t delivered_requests() const noexcept;

private:
  std::shared_ptr<browser_html_logger> logger_;
  std::atomic<std::size_t> delivered_{0};
};

} // namespace crtsys::wfp_sample::browser_https
