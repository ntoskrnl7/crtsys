#pragma once

#include <filesystem>
#include <string_view>

namespace crtsys::wfp_kernel_browser_https {

int run_control_server(std::wstring_view pipe_name,
                       const std::filesystem::path &application_path);

} // namespace crtsys::wfp_kernel_browser_https
