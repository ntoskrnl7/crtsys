#pragma once

#include <filesystem>

namespace crtsys::wfp_kernel_http3 {

int run_controller(const std::filesystem::path &controlled_application,
                   const std::filesystem::path &ipc_directory);

} // namespace crtsys::wfp_kernel_http3
