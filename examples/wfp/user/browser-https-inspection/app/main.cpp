#include <charconv>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "windows_support.hpp"
#include "browser_log.hpp"
#include "browser_runtime.hpp"

namespace {

std::uint32_t parse_duration_seconds(std::wstring_view value) {
  const std::string text =
      crtsys::wfp_sample::browser_https::narrow_dns_name(value);
  std::uint32_t result = 0;
  const auto converted = std::from_chars(
      text.data(), text.data() + text.size(), result);
  if (converted.ec != std::errc{} ||
      converted.ptr != text.data() + text.size())
    throw std::invalid_argument(
        "browser inspection duration must be an unsigned integer");
  return result;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    std::wcout << std::unitbuf;
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    crtsys::wfp_sample::winsock_session winsock;
    if (argc == 3 || argc == 4) {
      crtsys::wfp_sample::browser_https::
          browser_inspection_security_providers providers{};
      return crtsys::wfp_sample::browser_https::
          run_browser_inspection(
              argv[1], argv[2],
              argc == 4
                  ? parse_duration_seconds(argv[3])
                  : 0,
              providers);
    }
    throw std::invalid_argument(
        "usage: crtsys_wfp_browser_https_inspection_controller.exe "
        "<browser.exe> <log-directory> [duration-seconds]");
  } catch (const std::exception &error) {
    std::cerr
        << "NTL browser HTTPS inspection sample failed: "
        << error.what() << '\n';
    return 1;
  }
}
