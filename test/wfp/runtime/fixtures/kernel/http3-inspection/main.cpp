#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include "controller_process.hpp"
#include "msquic_client.hpp"

namespace {

using crtsys::wfp_kernel_http3::exchange_http3;
using crtsys::wfp_kernel_http3::exercise_blocked_webtransport;
using crtsys::wfp_kernel_http3::exercise_webtransport;
using crtsys::wfp_kernel_http3::http3_connect_is_blocked;
using crtsys::wfp_kernel_http3::response;
using crtsys::wfp_kernel_http3::webtransport_result;

class winsock_session {
public:
  winsock_session() {
    WSADATA data{};
    const int status = ::WSAStartup(MAKEWORD(2, 2), &data);
    if (status)
      throw std::system_error(status, std::system_category(), "WSAStartup");
  }
  ~winsock_session() { (void)::WSACleanup(); }
};

std::filesystem::path current_executable() {
  std::wstring value(32768, L'\0');
  const DWORD size = ::GetModuleFileNameW(
      nullptr, value.data(), static_cast<DWORD>(value.size()));
  if (!size || size == value.size())
    throw std::system_error(::GetLastError(), std::system_category(),
                            "GetModuleFileNameW(HTTP/3 acceptance)");
  value.resize(size);
  return std::filesystem::canonical(value);
}

std::map<std::string, std::uint64_t> parse_stats(std::string_view text) {
  std::map<std::string, std::uint64_t> result;
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    const auto end = text.find('\n', cursor);
    const auto line = text.substr(
        cursor, end == std::string_view::npos ? text.size() - cursor
                                              : end - cursor);
    const auto equal = line.find('=');
    if (equal != std::string_view::npos && equal + 1 != line.size()) {
      const std::string key(line.substr(0, equal));
      const std::string value(line.substr(equal + 1));
      char *tail = nullptr;
      const auto parsed = std::strtoull(value.c_str(), &tail, 10);
      if (tail && *tail == '\0')
        result.emplace(key, parsed);
    }
    if (end == std::string_view::npos)
      break;
    cursor = end + 1;
  }
  return result;
}

std::uint64_t required(const std::map<std::string, std::uint64_t> &stats,
                       std::string_view name) {
  const auto found = stats.find(std::string(name));
  if (found == stats.end())
    throw std::runtime_error("controller stats missing " + std::string(name));
  return found->second;
}

bool contains(std::span<const std::byte> bytes, std::string_view needle) {
  if (needle.empty() || bytes.size() < needle.size())
    return false;
  return std::string_view(reinterpret_cast<const char *>(bytes.data()),
                          bytes.size())
             .find(needle) != std::string_view::npos;
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    throw std::runtime_error("missing controller capture: " + path.string());
  return {std::istreambuf_iterator<char>(stream), {}};
}

void require_response(const response &value, unsigned status,
                      std::string_view body) {
  if (value.status != status || !contains(value.body, body) ||
      !value.dynamic_qpack_acknowledged)
    throw std::runtime_error("HTTP/3 response evidence is incomplete");
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument(
          "usage: crtsys_wfp_kernel_http3_inspection_acceptance.exe "
          "<controller.exe> <ipc-directory>");
    winsock_session winsock;
    const auto ipc = std::filesystem::absolute(argv[2]);
    crtsys::wfp_test::controller_process controller(
        argv[1], {current_executable().wstring(), ipc.wstring()}, ipc);
    controller.wait_ready();
    const auto before = parse_stats(controller.stats());
    const auto port = static_cast<std::uint16_t>(required(before, "port"));
    if (!port || required(before, "ready") != 1)
      throw std::runtime_error("kernel HTTP/3 controller is not ready");

    const auto allowed = exchange_http3(AF_INET, port, "/allowed", false);
    const auto gzip = exchange_http3(AF_INET, port, "/gzip", false);
    const auto deflate = exchange_http3(AF_INET6, port, "/deflate", false);
    const auto brotli = exchange_http3(AF_INET, port, "/br", false);
    const webtransport_result webtransport =
        exercise_webtransport(AF_INET6, port);
    if (!exercise_blocked_webtransport(AF_INET, port))
      throw std::runtime_error("WebTransport x-ntl-block policy was bypassed");
    constexpr std::uint64_t sequential_connection_churn = 96;
    for (std::uint64_t index = 0; index != sequential_connection_churn;
         ++index) {
      const auto churn = exchange_http3(
          (index & 1u) == 0 ? AF_INET : AF_INET6, port, "/allowed", false);
      require_response(churn, 200, "NTL HTTP/3 inspection allowed");
    }
    controller.command(L"direct.request");
    controller.wait_file(L"direct.ready");
    require_response(exchange_http3(AF_INET, port, "/allowed", false), 200,
                     "NTL HTTP/3 inspection allowed");
    require_response(exchange_http3(AF_INET6, port, "/allowed", false), 200,
                     "NTL HTTP/3 inspection allowed");
    controller.command(L"direct-done.request");
    controller.wait_file(L"direct-done.ready");

    controller.command(L"unavailable.request");
    controller.wait_file(L"unavailable.ready");
    if (!http3_connect_is_blocked(AF_INET, port) ||
        !http3_connect_is_blocked(AF_INET6, port))
      throw std::runtime_error(
          "unavailable kernel HTTP/3 callout allowed a handshake");
    controller.command(L"unavailable-done.request");
    controller.wait_file(L"unavailable-done.ready");
    require_response(exchange_http3(AF_INET, port, "/allowed", false), 200,
                     "NTL HTTP/3 inspection allowed");
    require_response(exchange_http3(AF_INET6, port, "/allowed", false), 200,
                     "NTL HTTP/3 inspection allowed");
    const auto blocked = exchange_http3(AF_INET6, port, "/blocked", true);

    controller.command(L"snapshot.request");
    controller.wait_file(L"snapshot.ready");
    const auto after = parse_stats(controller.stats());
    require_response(allowed, 200, "NTL HTTP/3 inspection allowed");
    require_response(gzip, 200, "NTL HTTP/3 inspection allowed");
    require_response(deflate, 200, "NTL HTTP/3 inspection allowed");
    require_response(brotli, 200, "NTL HTTP/3 inspection allowed");
    require_response(blocked, 403, "blocked by NTL HTTP/3 policy");
    if (gzip.content_encoding != "gzip" ||
        deflate.content_encoding != "deflate" ||
        brotli.content_encoding != "br" || !webtransport.connected ||
        !webtransport.settings || !webtransport.extended_connect ||
        !webtransport.bidirectional || !webtransport.unidirectional ||
        !webtransport.datagram || !webtransport.capsule ||
        !webtransport.reliable_reset ||
        required(after, "wfp_ipv4") <= required(before, "wfp_ipv4") ||
        required(after, "wfp_ipv6") <= required(before, "wfp_ipv6") ||
        required(after, "accepted") <
            required(before, "accepted") + sequential_connection_churn + 11 ||
        required(after, "permitted") < required(before, "permitted") + 4 ||
        required(after, "blocked") < required(before, "blocked") + 2 ||
        required(after, "qpack_resumed") <
            required(before, "qpack_resumed") + 5 ||
        required(after, "gzip_responses") <
            required(before, "gzip_responses") + 1 ||
        required(after, "deflate_responses") <
            required(before, "deflate_responses") + 1 ||
        required(after, "brotli_responses") <
            required(before, "brotli_responses") + 1 ||
        required(after, "webtransport_sessions") <
            required(before, "webtransport_sessions") + 1 ||
        required(after, "webtransport_bidirectional") <
            required(before, "webtransport_bidirectional") + 1 ||
        required(after, "webtransport_unidirectional") <
            required(before, "webtransport_unidirectional") + 1 ||
        required(after, "webtransport_datagrams") <
            required(before, "webtransport_datagrams") + 1 ||
        required(after, "webtransport_capsules") <
            required(before, "webtransport_capsules") + 1 ||
        required(after, "webtransport_resets") <
            required(before, "webtransport_resets") + 1 ||
        required(after, "direct_counter_unchanged") != 1 ||
        required(after, "unavailable_origin_unchanged") != 1 ||
        required(after, "restored") != 1)
      throw std::runtime_error("kernel HTTP/3 acceptance evidence is incomplete");
    const auto request = read_file(ipc / L"request.txt");
    const auto response_body = read_file(ipc / L"response.html");
    if (request.find("x-ntl-block: 1") == std::string::npos ||
        response_body.find("blocked by NTL HTTP/3 policy") ==
            std::string::npos)
      throw std::runtime_error("kernel HTTP/3 capture evidence is incomplete");

    controller.stop();
    std::cout
        << "Kernel HTTP/3 inspection PASS: IPv4/IPv6 WFP, "
           "kernel MsQuic TLS 1.3, SETTINGS, dynamic QPACK resume/ack, "
           "gzip/deflate/Brotli HTML, Extended CONNECT/WebTransport "
           "streams/datagram/capsule/reliable-reset, permit/block, "
           "webtransport-block=pass, policy-removed-direct=IPv4/IPv6, "
           "unavailable-callout=blocked, origin-hit=0, "
           "restored=IPv4/IPv6, 96-connection sequential churn, "
           "capture, cleanup PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "kernel HTTP/3 inspection acceptance failed: "
              << error.what() << '\n';
    return 1;
  }
}
