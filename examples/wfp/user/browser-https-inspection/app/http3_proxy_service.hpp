#pragma once

#include <cstdint>
#include <filesystem>

namespace crtsys::wfp_sample::browser_https {

int run_managed_http3_proxy(
    std::uint16_t listen_port,
    const std::filesystem::path &log_directory,
    std::uint32_t duration_seconds);

int run_wfp_managed_http3_proxy(
    const std::filesystem::path &managed_client,
    std::uint16_t listen_port,
    const std::filesystem::path &log_directory,
    std::uint32_t duration_seconds);

} // namespace crtsys::wfp_sample::browser_https
