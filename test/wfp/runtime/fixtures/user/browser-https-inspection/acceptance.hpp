#pragma once

#include <cstdint>
#include <filesystem>

namespace crtsys::wfp_sample::browser_https {

int run_controlled_http3_end_to_end(
    std::uint16_t proxy_port,
    std::uint16_t origin_port,
    const std::filesystem::path &log_directory,
    std::uint32_t duration_seconds);

} // namespace crtsys::wfp_sample::browser_https
