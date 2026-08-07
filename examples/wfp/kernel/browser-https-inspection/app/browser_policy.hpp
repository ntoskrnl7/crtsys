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
#include <filesystem>

#include <ntl/wfp/management>

#include "browser_https_inspection_contract.hpp"

namespace crtsys::wfp_kernel_browser_https {

struct native_quic_policy_evidence {
  std::uint64_t application_id_hash = 0;
  std::uint64_t filter_id_v4 = 0;
  std::uint64_t filter_id_v6 = 0;
  std::uint16_t layer_id_v4 = 0;
  std::uint16_t layer_id_v6 = 0;
};

native_quic_policy_evidence install_browser_policy(
    ntl::wfp::policy_session &session,
    const ntl::wfp::application_id &browser,
    const wfp_kernel_browser_https_inspection::service_info &service);

void report_browser_policy_evidence(
    const native_quic_policy_evidence &evidence,
    const std::filesystem::path &log_directory);

} // namespace crtsys::wfp_kernel_browser_https
