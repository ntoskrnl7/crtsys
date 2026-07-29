#include <charconv>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "windows_support.hpp"
#include "browser_log.hpp"
#include "http3_live_proxy.hpp"
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

std::uint16_t parse_port(std::wstring_view value) {
  const auto parsed = parse_duration_seconds(value);
  if (parsed == 0 || parsed > 65535)
    throw std::invalid_argument(
        "HTTP/3 inspection port must be between 1 and 65535");
  return static_cast<std::uint16_t>(parsed);
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    std::wcout << std::unitbuf;
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    crtsys::wfp_sample::winsock_session winsock;
    if (argc == 6 &&
        std::wstring_view(argv[1]) == L"--http3-spki-proxy") {
      return crtsys::wfp_sample::browser_https::
          run_browser_http3_spki_proxy(
              argv[2], parse_port(argv[3]), argv[4],
              parse_duration_seconds(argv[5]));
    }
    if ((argc == 4 || argc == 5) &&
        std::wstring_view(argv[1]) ==
            L"--managed-http3-proxy") {
      return crtsys::wfp_sample::browser_https::
          run_managed_http3_proxy(
              parse_port(argv[2]), argv[3],
              argc == 5
                  ? parse_duration_seconds(argv[4])
                  : 0);
    }
    if ((argc == 5 || argc == 6) &&
        std::wstring_view(argv[1]) ==
            L"--wfp-managed-http3-proxy") {
      return crtsys::wfp_sample::browser_https::
          run_wfp_managed_http3_proxy(
              argv[2], parse_port(argv[3]), argv[4],
              argc == 6
                  ? parse_duration_seconds(argv[5])
                  : 0);
    }
    if ((argc == 5 || argc == 6) &&
        std::wstring_view(argv[1]) ==
            L"--controlled-http3-e2e") {
      return crtsys::wfp_sample::browser_https::
          run_controlled_http3_end_to_end(
              parse_port(argv[2]), parse_port(argv[3]),
              argv[4],
              argc == 6
                  ? parse_duration_seconds(argv[5])
                  : 0);
    }
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
        "usage: crtsys_wfp_browser_https_inspection_app.exe "
        "<browser.exe> <log-directory> [duration-seconds]\n"
        "   or: --managed-http3-proxy <listen-port> "
        "<log-directory> [duration-seconds]\n"
        "   or: --wfp-managed-http3-proxy <managed-client.exe> "
        "<listen-port> <log-directory> [duration-seconds]\n"
        "   or: --controlled-http3-e2e <proxy-port> "
        "<origin-port> <log-directory> [duration-seconds]\n"
        "   or: --http3-spki-proxy <server-name> <listen-port> "
        "<log-directory> <duration-seconds>");
  } catch (const std::exception &error) {
    std::cerr
        << "NTL browser HTTPS inspection sample failed: "
        << error.what() << '\n';
    return 1;
  }
}
