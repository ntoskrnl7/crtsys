#pragma once

#include <cstdint>
#include <filesystem>

namespace crtsys::wfp_kernel_browser_https {

int run_controller(const std::filesystem::path &browser,
                   const std::filesystem::path &log_directory,
                   std::uint32_t duration_seconds);

} // namespace crtsys::wfp_kernel_browser_https
