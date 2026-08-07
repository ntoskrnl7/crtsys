#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <winioctl.h>

#include "browser_https_inspection_contract.hpp"

#include <span>
#include <string_view>

namespace crtsys::wfp_kernel_browser_https {

class device_handle {
public:
  device_handle();
  device_handle(const device_handle &) = delete;
  device_handle &operator=(const device_handle &) = delete;
  ~device_handle();

  HANDLE get() const noexcept { return value_; }

private:
  HANDLE value_ = INVALID_HANDLE_VALUE;
};

wfp_kernel_browser_https_inspection::service_info
query_service(HANDLE device);
wfp_kernel_browser_https_inspection::inspection_read_result
read_inspection(HANDLE device, std::uint64_t after_sequence);
wfp_kernel_browser_https_inspection::identity_request_read_result
read_identity_request(HANDLE device, std::uint64_t after_sequence);
wfp_kernel_browser_https_inspection::quic_telemetry
query_quic_telemetry(HANDLE device);
void configure_identity(
    HANDLE device,
    const wfp_kernel_browser_https_inspection::certificate_config &identity);
void configure_origin_security(
    HANDLE device, std::string_view server_name,
    std::span<const std::byte> client_sha1_thumbprint,
    std::span<const std::byte> origin_leaf_der);
void remove_origin_security(HANDLE device, std::string_view server_name);

} // namespace crtsys::wfp_kernel_browser_https
