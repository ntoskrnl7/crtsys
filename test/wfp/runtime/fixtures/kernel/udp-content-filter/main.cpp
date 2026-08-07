#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winioctl.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "content_filter_fixture.hpp"
#include "udp_content_filter_contract.hpp"

namespace {

namespace contract = wfp_kernel_udp_content_filter;
namespace fixture = crtsys::test::wfp::content_filter_fixture;

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
  SOCKET value_;
};

struct receiver {
  socket_owner socket;
  int family = AF_UNSPEC;
  std::uint16_t port = 0;
};

receiver make_receiver(int family, std::uint16_t requested_port = 0) {
  socket_owner socket(::socket(family, SOCK_DGRAM, IPPROTO_UDP));
  if (socket.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "socket(receiver)");
  sockaddr_storage storage{};
  int size = 0;
  if (family == AF_INET) {
    auto &address = reinterpret_cast<sockaddr_in &>(storage);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(requested_port);
    size = sizeof(address);
  } else {
    DWORD v6_only = 1;
    if (::setsockopt(socket.get(), IPPROTO_IPV6, IPV6_V6ONLY,
                     reinterpret_cast<const char *>(&v6_only),
                     sizeof(v6_only)) == SOCKET_ERROR)
      throw std::system_error(::WSAGetLastError(), std::system_category(),
                              "setsockopt(IPV6_V6ONLY)");
    auto &address = reinterpret_cast<sockaddr_in6 &>(storage);
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = htons(requested_port);
    size = sizeof(address);
  }
  if (::bind(socket.get(), reinterpret_cast<const sockaddr *>(&storage),
             size) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "bind(receiver)");
  size = sizeof(storage);
  if (::getsockname(socket.get(), reinterpret_cast<sockaddr *>(&storage),
                    &size) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "getsockname");
  const auto port =
      family == AF_INET
          ? ntohs(reinterpret_cast<const sockaddr_in &>(storage).sin_port)
          : ntohs(reinterpret_cast<const sockaddr_in6 &>(storage).sin6_port);
  return {std::move(socket), family, port};
}

void send_datagram(int family, std::uint16_t port, std::string_view payload) {
  socket_owner socket(::socket(family, SOCK_DGRAM, IPPROTO_UDP));
  if (socket.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "socket(sender)");
  sockaddr_storage storage{};
  int size = 0;
  if (family == AF_INET) {
    auto &address = reinterpret_cast<sockaddr_in &>(storage);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    size = sizeof(address);
  } else {
    auto &address = reinterpret_cast<sockaddr_in6 &>(storage);
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = htons(port);
    size = sizeof(address);
  }
  if (::connect(socket.get(), reinterpret_cast<const sockaddr *>(&storage),
                size) == SOCKET_ERROR ||
      ::send(socket.get(), payload.data(), static_cast<int>(payload.size()),
             0) != static_cast<int>(payload.size()))
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "connect/send(datagram)");
}

std::string make_record(
    crtsys::examples::wfp::content_filter::classification category,
    std::uint32_t rule_id, std::string_view body) {
  std::string result(
      crtsys::examples::wfp::content_filter::wire_size(body.size()), '\0');
  const auto status = crtsys::examples::wfp::content_filter::encode(
      std::as_writable_bytes(std::span(result.data(), result.size())),
      category, rule_id, std::as_bytes(std::span(body)),
      contract::maximum_record_body_size);
  if (!status.is_ok())
    throw std::runtime_error("cannot encode content-filter record");
  return result;
}

std::optional<std::string> receive_datagram(SOCKET socket, DWORD timeout_ms) {
  if (::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&timeout_ms),
                   sizeof(timeout_ms)) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "setsockopt(SO_RCVTIMEO)");
  std::array<char, 8192> buffer{};
  const int received =
      ::recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
  if (received == SOCKET_ERROR) {
    const int error = ::WSAGetLastError();
    if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK)
      return std::nullopt;
    throw std::system_error(error, std::system_category(), "recv(datagram)");
  }
  return std::string(buffer.data(), static_cast<std::size_t>(received));
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    winsock_session winsock;
    const auto controller = fixture::controller_argument(
        argc, argv, L"crtsys_wfp_kernel_udp_content_filter_controller.exe");
    auto ipv4 = make_receiver(AF_INET);
    auto ipv6 = make_receiver(AF_INET6, ipv4.port);
    fixture::state_directory state(L"kernel-udp-content-filter");
    fixture::controller_process policy(controller, ipv4.port, state.path());
    policy.wait_ready();

    const std::string allowed = make_record(
        crtsys::examples::wfp::content_filter::classification::ordinary,
        1001, "ordinary UDP body may contain BLOCKME");
    const std::string blocked = make_record(
        crtsys::examples::wfp::content_filter::classification::restricted,
        2001, "restricted by the typed record header");
    std::string malformed = allowed;
    malformed[0] = 'X';
    send_datagram(AF_INET, ipv4.port, allowed);
    send_datagram(AF_INET, ipv4.port, blocked);
    send_datagram(AF_INET, ipv4.port, malformed);
    send_datagram(AF_INET6, ipv6.port, allowed);
    send_datagram(AF_INET6, ipv6.port, blocked);
    send_datagram(AF_INET6, ipv6.port, malformed);
    const auto delivered_v4 = receive_datagram(ipv4.socket.get(), 2000);
    const auto delivered_v6 = receive_datagram(ipv6.socket.get(), 2000);
    if (!delivered_v4 || *delivered_v4 != allowed || !delivered_v6 ||
        *delivered_v6 != allowed ||
        receive_datagram(ipv4.socket.get(), 500) ||
        receive_datagram(ipv6.socket.get(), 500))
      throw std::runtime_error("kernel UDP traffic verdict is incorrect");

    policy.request_stop();
    policy.wait();
    const auto stats = fixture::read_stats(policy.stats_file());
    if (fixture::require_stat(stats, "after.inspected") <
            fixture::require_stat(stats, "before.inspected") + 6 ||
        fixture::require_stat(stats, "after.permitted") <
            fixture::require_stat(stats, "before.permitted") + 2 ||
        fixture::require_stat(stats, "after.blocked") <
            fixture::require_stat(stats, "before.blocked") + 4 ||
        fixture::require_stat(stats, "after.malformed") <
            fixture::require_stat(stats, "before.malformed") + 2 ||
        fixture::require_stat(stats, "after.failed") !=
            fixture::require_stat(stats, "before.failed"))
      throw std::runtime_error("kernel UDP controller statistics are wrong");
    send_datagram(AF_INET, ipv4.port, blocked);
    send_datagram(AF_INET6, ipv6.port, blocked);
    const auto restored_v4 = receive_datagram(ipv4.socket.get(), 2000);
    const auto restored_v6 = receive_datagram(ipv6.socket.get(), 2000);
    if (!restored_v4 || *restored_v4 != blocked || !restored_v6 ||
        *restored_v6 != blocked)
      throw std::runtime_error("kernel UDP policy removal was not observed");
    std::wcout
        << L"Kernel UDP content-filter acceptance PASS: "
           L"IPv4/IPv6 typed permit/block, malformed fail-close, "
           L"statistics, cleanup.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Kernel UDP content-filter acceptance failed: "
              << error.what() << '\n';
    return 1;
  }
}
