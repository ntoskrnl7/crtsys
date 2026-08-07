#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>

#include <cstdint>

#include <ntl/wfp/management>

#include "browser_https_inspection_contract.hpp"

namespace crtsys::wfp_kernel_browser_https {

struct managed_transport_policy_evidence {
  std::uint64_t application_id_hash = 0;
  std::uint64_t http3_filter_id_v4 = 0;
  std::uint64_t http3_filter_id_v6 = 0;
  std::uint64_t datagram_filter_id_v4 = 0;
  std::uint64_t datagram_filter_id_v6 = 0;
  std::uint64_t reverse_filter_id_v4 = 0;
  std::uint64_t reverse_filter_id_v6 = 0;
};

struct managed_tcp_policy_evidence {
  std::uint64_t application_id_hash = 0;
  std::uint64_t filter_id_v4 = 0;
  std::uint64_t filter_id_v6 = 0;
};

managed_transport_policy_evidence install_managed_http3_policy(
    ntl::wfp::policy_session &session,
    const ntl::wfp::application_id &application,
    const wfp_kernel_browser_https_inspection::service_info &service,
    std::uint16_t original_port);

managed_tcp_policy_evidence install_managed_tcp_policy(
    ntl::wfp::policy_session &session,
    const ntl::wfp::application_id &application,
    const wfp_kernel_browser_https_inspection::service_info &service,
    std::uint16_t original_port_v4, std::uint16_t original_port_v6);

} // namespace crtsys::wfp_kernel_browser_https
