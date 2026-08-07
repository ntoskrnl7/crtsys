#include "controller.hpp"
#include "control_server.hpp"
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::uint32_t parse_timeout(std::wstring_view input) {
  std::size_t used = 0;
  const unsigned long parsed = std::stoul(std::wstring(input), &used, 10);
  if (used != input.size() || parsed == 0 ||
      parsed > (std::numeric_limits<std::uint32_t>::max)())
    throw std::invalid_argument("timeout must be a positive integer");
  return static_cast<std::uint32_t>(parsed);
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc == 4 && std::wstring_view(argv[1]) == L"--control-server")
      return crtsys::wfp_kernel_browser_https::run_control_server(
          argv[2], argv[3]);
    if (argc == 3 || argc == 4) {
      return crtsys::wfp_kernel_browser_https::run_controller(
          argv[1], argv[2], argc == 4 ? parse_timeout(argv[3]) : 0);
    }
    throw std::invalid_argument(
        "usage: crtsys_wfp_kernel_browser_https_inspection_controller.exe "
        "<browser.exe> <log-directory> [duration-seconds]\n"
        "       crtsys_wfp_kernel_browser_https_inspection_controller.exe "
        "--control-server <pipe-name> <controlled-application.exe>");
  } catch (const std::exception &error) {
    std::cerr << "Kernel browser HTTPS inspection failed: " << error.what()
              << '\n';
    return 1;
  }
}
