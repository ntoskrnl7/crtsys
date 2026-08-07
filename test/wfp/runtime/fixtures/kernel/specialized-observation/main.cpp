#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <windows.h>
#include <winioctl.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include "runtime_controller_fixture.hpp"
#include "specialized_observation_contract.hpp"

namespace {

namespace fixture = crtsys::test::wfp::runtime_fixture;

class winsock_session {
public:
  winsock_session() {
    WSADATA data{};
    const int result = ::WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0)
      throw std::system_error(result, std::system_category(), "WSAStartup");
  }
  ~winsock_session() { ::WSACleanup(); }
};

class socket_owner {
public:
  explicit socket_owner(SOCKET value = INVALID_SOCKET) noexcept
      : value_(value) {}
  socket_owner(const socket_owner &) = delete;
  socket_owner &operator=(const socket_owner &) = delete;
  socket_owner(socket_owner &&other) noexcept
      : value_(std::exchange(other.value_, INVALID_SOCKET)) {}
  ~socket_owner() {
    if (value_ != INVALID_SOCKET)
      ::closesocket(value_);
  }
  SOCKET get() const noexcept { return value_; }

private:
  SOCKET value_ = INVALID_SOCKET;
};

class icmp_owner {
public:
  explicit icmp_owner(HANDLE value = INVALID_HANDLE_VALUE) noexcept
      : value_(value) {}
  icmp_owner(const icmp_owner &) = delete;
  icmp_owner &operator=(const icmp_owner &) = delete;
  ~icmp_owner() {
    if (value_ != INVALID_HANDLE_VALUE)
      ::IcmpCloseHandle(value_);
  }
  HANDLE get() const noexcept { return value_; }

private:
  HANDLE value_ = INVALID_HANDLE_VALUE;
};

void exchange_loopback(int family) {
  socket_owner listener(::socket(family, SOCK_STREAM, IPPROTO_TCP));
  if (listener.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "socket(listener)");
  sockaddr_storage storage{};
  int address_size = 0;
  if (family == AF_INET) {
    auto &address = reinterpret_cast<sockaddr_in &>(storage);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address_size = sizeof(address);
  } else {
    DWORD v6_only = 1;
    if (::setsockopt(listener.get(), IPPROTO_IPV6, IPV6_V6ONLY,
                     reinterpret_cast<const char *>(&v6_only),
                     sizeof(v6_only)) == SOCKET_ERROR)
      throw std::system_error(::WSAGetLastError(), std::system_category(),
                              "IPV6_V6ONLY");
    auto &address = reinterpret_cast<sockaddr_in6 &>(storage);
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address_size = sizeof(address);
  }
  if (::bind(listener.get(), reinterpret_cast<const sockaddr *>(&storage),
             address_size) == SOCKET_ERROR ||
      ::listen(listener.get(), 1) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "bind/listen");
  int actual_size = sizeof(storage);
  if (::getsockname(listener.get(), reinterpret_cast<sockaddr *>(&storage),
                    &actual_size) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "getsockname");

  std::exception_ptr server_failure;
  std::thread server([&] {
    try {
      socket_owner accepted(::accept(listener.get(), nullptr, nullptr));
      if (accepted.get() == INVALID_SOCKET)
        throw std::system_error(::WSAGetLastError(), std::system_category(),
                                "accept");
      char byte = 0;
      if (::recv(accepted.get(), &byte, 1, 0) != 1 || byte != 'x')
        throw std::runtime_error("loopback exchange payload mismatch");
    } catch (...) {
      server_failure = std::current_exception();
    }
  });

  socket_owner client(::socket(family, SOCK_STREAM, IPPROTO_TCP));
  if (client.get() == INVALID_SOCKET ||
      ::connect(client.get(), reinterpret_cast<const sockaddr *>(&storage),
                address_size) == SOCKET_ERROR ||
      ::send(client.get(), "x", 1, 0) != 1) {
    const auto error = ::WSAGetLastError();
    server.join();
    throw std::system_error(error, std::system_category(), "connect/send");
  }
  (void)::shutdown(client.get(), SD_BOTH);
  server.join();
  if (server_failure)
    std::rethrow_exception(server_failure);
}

void exercise_ethernet(std::wstring_view target,
                       std::chrono::milliseconds duration) {
  sockaddr_in destination{};
  destination.sin_family = AF_INET;
  const std::wstring target_text(target);
  if (::InetPtonW(AF_INET, target_text.c_str(), &destination.sin_addr) != 1)
    throw std::invalid_argument("--traffic-target must be an IPv4 address");

  icmp_owner icmp(::IcmpCreateFile());
  if (icmp.get() == INVALID_HANDLE_VALUE)
    throw std::system_error(::GetLastError(), std::system_category(),
                            "IcmpCreateFile");

  constexpr char payload[] = "ntl-wfp-specialized-observation";
  alignas(ICMP_ECHO_REPLY)
      std::array<std::byte, sizeof(ICMP_ECHO_REPLY) + sizeof(payload) + 8>
      reply{};
  const auto deadline = std::chrono::steady_clock::now() + duration;
  std::uint64_t replies = 0;
  do {
    replies += ::IcmpSendEcho(
        icmp.get(), destination.sin_addr.s_addr,
        const_cast<char *>(payload), static_cast<WORD>(sizeof(payload)),
        nullptr, reply.data(), static_cast<DWORD>(reply.size()), 100);
  } while (std::chrono::steady_clock::now() < deadline);

  if (replies == 0)
    throw std::runtime_error(
        "the specialized traffic target did not return an ICMP echo");
}

struct acceptance_options {
  std::filesystem::path controller = fixture::sibling_executable(
      L"crtsys_wfp_specialized_observation_controller.exe");
  std::wstring traffic_target;
  bool require_mac = false;
  bool require_vswitch = false;
  std::uint32_t traffic_duration_ms = 5'000;

  std::uint32_t required_mask() const noexcept {
    const auto bit = [](wfp_specialized_observation::counter value) {
      return 1u << static_cast<std::size_t>(value);
    };
    const auto endpoints =
        bit(wfp_specialized_observation::counter::endpoint_v4) |
        bit(wfp_specialized_observation::counter::endpoint_v6);
    const auto mac = bit(wfp_specialized_observation::counter::mac_in) |
                     bit(wfp_specialized_observation::counter::mac_out);
    const auto vswitch =
        bit(wfp_specialized_observation::counter::vswitch_in) |
        bit(wfp_specialized_observation::counter::vswitch_out);
    return endpoints | (require_mac ? mac : 0u) |
           (require_vswitch ? vswitch : 0u);
  }
};

std::uint32_t parse_u32(std::wstring_view value, const char *name) {
  std::size_t consumed = 0;
  const auto parsed = std::stoul(std::wstring(value), &consumed, 10);
  if (consumed != value.size() || parsed > UINT32_MAX)
    throw std::invalid_argument(name);
  return static_cast<std::uint32_t>(parsed);
}

bool parse_bool(std::wstring_view value, const char *name) {
  if (value == L"true")
    return true;
  if (value == L"false")
    return false;
  throw std::invalid_argument(name);
}

acceptance_options parse_options(int argc, wchar_t **argv) {
  acceptance_options result;
  if ((argc % 2) == 0)
    throw std::invalid_argument("specialized acceptance option lacks a value");
  for (int index = 1; index < argc; index += 2) {
    const std::wstring_view name(argv[index]);
    const std::wstring_view value(argv[index + 1]);
    if (name == L"--controller")
      result.controller = std::filesystem::absolute(value);
    else if (name == L"--traffic-target")
      result.traffic_target = value;
    else if (name == L"--require-mac")
      result.require_mac = parse_bool(value, "invalid --require-mac");
    else if (name == L"--require-vswitch")
      result.require_vswitch =
          parse_bool(value, "invalid --require-vswitch");
    else if (name == L"--traffic-duration-ms")
      result.traffic_duration_ms =
          parse_u32(value, "invalid --traffic-duration-ms");
    else
      throw std::invalid_argument("unknown specialized acceptance option");
  }
  if (result.traffic_duration_ms < 100 ||
      result.traffic_duration_ms > 300'000)
    throw std::invalid_argument("--traffic-duration-ms is out of range");
  if (result.traffic_target.empty() &&
      (result.require_mac || result.require_vswitch))
    throw std::invalid_argument(
        "MAC/vSwitch requirements need --traffic-target");
  return result;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    const auto options = parse_options(argc, argv);
    winsock_session winsock;
    fixture::state_directory state(L"kernel-specialized-observation");
    fixture::controller_process policy(
        options.controller, state.path(),
        {{L"--application-path", fixture::current_executable().wstring()}});
    policy.wait_ready();
    exchange_loopback(AF_INET);
    exchange_loopback(AF_INET6);
    if (!options.traffic_target.empty())
      exercise_ethernet(options.traffic_target,
                        std::chrono::milliseconds(
                            options.traffic_duration_ms));
    policy.request_stop();
    policy.wait();

    const auto stats = fixture::read_stats(policy.stats_file());
    constexpr std::uint32_t all_layers_mask =
        wfp_specialized_observation::all_layers_mask;
    if (fixture::require_stat(stats, "after.registered_mask") !=
            all_layers_mask ||
        fixture::require_stat(stats, "after.indication0") <=
            fixture::require_stat(stats, "before.indication0") ||
        fixture::require_stat(stats, "after.indication1") <=
            fixture::require_stat(stats, "before.indication1"))
      throw std::runtime_error(
          "specialized endpoint-closure observations are incomplete");

    std::uint32_t exercised_mask = 0;
    for (std::size_t index = 0;
         index != wfp_specialized_observation::counter_count; ++index) {
      const auto before = fixture::require_stat(
          stats, "before.indication" + std::to_string(index));
      const auto after = fixture::require_stat(
          stats, "after.indication" + std::to_string(index));
      if (after > before)
        exercised_mask |= 1u << index;
    }
    const auto required_mask = options.required_mask();
    if ((exercised_mask & required_mask) != required_mask)
      throw std::runtime_error(
          "specialized observation missed a required classify layer");
    std::wcout << L"Kernel specialized-observation acceptance PASS: "
                  L"registered-mask="
               << all_layers_mask << L", exercised-mask=" << exercised_mask
               << L", required-mask=" << required_mask
               << L", endpoint-v4/v6=observed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Kernel specialized-observation acceptance failed: "
              << error.what() << '\n';
    return 1;
  }
}
