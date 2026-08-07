#include <charconv>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "windows_support.hpp"
#include "browser_log.hpp"
#include "http3_proxy_service.hpp"

namespace {

std::uint32_t parse_unsigned(std::wstring_view value) {
  const std::string text =
      crtsys::wfp_sample::browser_https::narrow_dns_name(value);
  std::uint32_t result = 0;
  const auto converted = std::from_chars(
      text.data(), text.data() + text.size(), result);
  if (converted.ec != std::errc{} ||
      converted.ptr != text.data() + text.size())
    throw std::invalid_argument(
        "service argument must be an unsigned integer");
  return result;
}

std::uint16_t parse_port(std::wstring_view value) {
  const auto parsed = parse_unsigned(value);
  if (parsed == 0 || parsed > 65535)
    throw std::invalid_argument(
        "HTTP/3 service port must be between 1 and 65535");
  return static_cast<std::uint16_t>(parsed);
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    std::wcout << std::unitbuf;
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    crtsys::wfp_sample::winsock_session winsock;
    if ((argc == 4 || argc == 5) &&
        std::wstring_view(argv[1]) == L"--managed-http3-proxy") {
      return crtsys::wfp_sample::browser_https::run_managed_http3_proxy(
          parse_port(argv[2]), argv[3],
          argc == 5 ? parse_unsigned(argv[4]) : 0);
    }
    if ((argc == 5 || argc == 6) &&
        std::wstring_view(argv[1]) == L"--wfp-managed-http3-proxy") {
      return crtsys::wfp_sample::browser_https::run_wfp_managed_http3_proxy(
          argv[2], parse_port(argv[3]), argv[4],
          argc == 6 ? parse_unsigned(argv[5]) : 0);
    }
    throw std::invalid_argument(
        "usage: crtsys_wfp_browser_https_inspection_http3_proxy_service.exe "
        "--managed-http3-proxy <listen-port> <log-directory> "
        "[duration-seconds]\n"
        "   or: --wfp-managed-http3-proxy <managed-client.exe> "
        "<listen-port> <log-directory> [duration-seconds]");
  } catch (const std::exception &error) {
    std::cerr << "NTL browser HTTP/3 proxy service failed: "
              << error.what() << '\n';
    return 1;
  }
}
