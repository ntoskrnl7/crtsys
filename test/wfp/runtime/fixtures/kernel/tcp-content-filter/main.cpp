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

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "content_filter_fixture.hpp"
#include "tcp_content_filter_contract.hpp"

namespace {

namespace contract = wfp_kernel_tcp_content_filter;
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

struct listener {
  socket_owner socket;
  int family = AF_UNSPEC;
  std::uint16_t port = 0;
};

listener make_listener(int family, std::uint16_t requested_port = 0) {
  socket_owner socket(::socket(family, SOCK_STREAM, IPPROTO_TCP));
  if (socket.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "socket(listener)");
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
             size) == SOCKET_ERROR ||
      ::listen(socket.get(), 8) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "bind/listen");
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

socket_owner connect_to(const listener &server) {
  socket_owner socket(::socket(server.family, SOCK_STREAM, IPPROTO_TCP));
  if (socket.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "socket(client)");
  sockaddr_storage storage{};
  int size = 0;
  if (server.family == AF_INET) {
    auto &address = reinterpret_cast<sockaddr_in &>(storage);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(server.port);
    size = sizeof(address);
  } else {
    auto &address = reinterpret_cast<sockaddr_in6 &>(storage);
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = htons(server.port);
    size = sizeof(address);
  }
  if (::connect(socket.get(), reinterpret_cast<const sockaddr *>(&storage),
                size) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "connect");
  return socket;
}

socket_owner accept_one(const listener &server) {
  socket_owner accepted(::accept(server.socket.get(), nullptr, nullptr));
  if (accepted.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "accept");
  return accepted;
}

std::string frame(std::string_view content) {
  if (content.size() > contract::maximum_record_size)
    throw std::length_error("content exceeds the sample frame limit");
  const auto size = static_cast<std::uint32_t>(content.size());
  std::string result(contract::length_prefix_size, '\0');
  result[0] = static_cast<char>((size >> 24) & 0xff);
  result[1] = static_cast<char>((size >> 16) & 0xff);
  result[2] = static_cast<char>((size >> 8) & 0xff);
  result[3] = static_cast<char>(size & 0xff);
  result.append(content);
  return result;
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

void send_all(SOCKET socket, std::string_view bytes, bool split_prefix) {
  std::size_t offset = 0;
  while (offset != bytes.size()) {
    std::size_t amount = bytes.size() - offset;
    if (split_prefix && offset == 0)
      amount = 2;
    const int sent = ::send(
        socket, bytes.data() + offset,
        static_cast<int>((std::min)(amount,
                                    static_cast<std::size_t>(INT_MAX))),
        0);
    if (sent <= 0)
      throw std::system_error(::WSAGetLastError(), std::system_category(),
                              "send");
    offset += static_cast<std::size_t>(sent);
  }
}

bool exchange(const listener &server, std::string_view content,
              bool expect_delivery, bool split_prefix = false) {
  auto client = connect_to(server);
  auto peer = accept_one(server);
  const std::string bytes = frame(content);
  send_all(client.get(), bytes, split_prefix);
  DWORD timeout = 1500;
  if (::setsockopt(peer.get(), SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&timeout),
                   sizeof(timeout)) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "setsockopt(SO_RCVTIMEO)");
  std::string received(bytes.size(), '\0');
  std::size_t total = 0;
  while (total != received.size()) {
    const int amount = ::recv(peer.get(), received.data() + total,
                              static_cast<int>(received.size() - total), 0);
    if (amount <= 0)
      break;
    total += static_cast<std::size_t>(amount);
  }
  return expect_delivery ? total == bytes.size() && received == bytes
                         : total == 0;
}

bool exchange_two(const listener &server, std::string_view first,
                  std::string_view second) {
  auto client = connect_to(server);
  auto peer = accept_one(server);
  const std::string bytes = frame(first) + frame(second);
  send_all(client.get(), bytes, true);
  DWORD timeout = 2000;
  (void)::setsockopt(peer.get(), SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char *>(&timeout),
                     sizeof(timeout));
  std::string received(bytes.size(), '\0');
  std::size_t total = 0;
  while (total != received.size()) {
    const int amount = ::recv(peer.get(), received.data() + total,
                              static_cast<int>(received.size() - total), 0);
    if (amount <= 0)
      break;
    total += static_cast<std::size_t>(amount);
  }
  if (received != bytes)
    return false;
  constexpr std::string_view reply = "unframed-outbound-reply";
  send_all(peer.get(), reply, false);
  timeout = 2000;
  if (::setsockopt(client.get(), SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&timeout),
                   sizeof(timeout)) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "setsockopt(SO_RCVTIMEO reply)");
  std::array<char, 64> buffer{};
  const int reply_size =
      ::recv(client.get(), buffer.data(), static_cast<int>(buffer.size()), 0);
  return reply_size == reply.size() &&
         std::string_view(buffer.data(), static_cast<std::size_t>(reply_size)) ==
             reply;
}

bool reject_wire(const listener &server, std::string_view bytes) {
  auto client = connect_to(server);
  auto peer = accept_one(server);
  send_all(client.get(), bytes, false);
  (void)::shutdown(client.get(), SD_SEND);
  DWORD timeout = 2000;
  (void)::setsockopt(peer.get(), SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char *>(&timeout),
                     sizeof(timeout));
  std::array<char, 8> received{};
  const int amount =
      ::recv(peer.get(), received.data(), static_cast<int>(received.size()), 0);
  return amount == 0 ||
         (amount == SOCKET_ERROR &&
          (::WSAGetLastError() == WSAECONNRESET ||
           ::WSAGetLastError() == WSAECONNABORTED));
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    winsock_session winsock;
    const auto controller = fixture::controller_argument(
        argc, argv, L"crtsys_wfp_kernel_tcp_content_filter_controller.exe");
    auto ipv4 = make_listener(AF_INET);
    auto ipv6 = make_listener(AF_INET6, ipv4.port);
    fixture::state_directory state(L"kernel-tcp-content-filter");
    fixture::controller_process policy(controller, ipv4.port, state.path());
    policy.wait_ready();

    const std::string ordinary = make_record(
        crtsys::examples::wfp::content_filter::classification::ordinary,
        1001, "ordinary body may contain BLOCKME without changing policy");
    const std::string restricted = make_record(
        crtsys::examples::wfp::content_filter::classification::restricted,
        2001, "the typed classification blocks this body");
    std::string malformed = ordinary;
    malformed[0] = 'X';
    if (!exchange_two(ipv4, ordinary, ordinary) ||
        !exchange_two(ipv6, ordinary, ordinary) ||
        !exchange(ipv4, restricted, false) ||
        !exchange(ipv6, restricted, false) ||
        !exchange(ipv4, malformed, false) ||
        !exchange(ipv6, malformed, false))
      throw std::runtime_error("kernel TCP traffic verdict is incorrect");
    std::string oversized_prefix(4, '\0');
    const auto oversized =
        static_cast<std::uint32_t>(contract::maximum_record_size + 1);
    oversized_prefix[0] = static_cast<char>((oversized >> 24) & 0xff);
    oversized_prefix[1] = static_cast<char>((oversized >> 16) & 0xff);
    oversized_prefix[2] = static_cast<char>((oversized >> 8) & 0xff);
    oversized_prefix[3] = static_cast<char>(oversized & 0xff);
    if (!reject_wire(ipv4, oversized_prefix) ||
        !reject_wire(ipv6, std::string_view("\0\0", 2)))
      throw std::runtime_error("kernel TCP malformed framing was not blocked");

    policy.request_stop();
    policy.wait();
    const auto stats = fixture::read_stats(policy.stats_file());
    if (fixture::require_stat(stats, "after.inspected") <
            fixture::require_stat(stats, "before.inspected") + 8 ||
        fixture::require_stat(stats, "after.permitted") <
            fixture::require_stat(stats, "before.permitted") + 4 ||
        fixture::require_stat(stats, "after.blocked") <
            fixture::require_stat(stats, "before.blocked") + 6 ||
        fixture::require_stat(stats, "after.malformed") <
            fixture::require_stat(stats, "before.malformed") + 4 ||
        fixture::require_stat(stats, "after.failed") !=
            fixture::require_stat(stats, "before.failed"))
      throw std::runtime_error("kernel TCP controller statistics are wrong");
    if (!exchange(ipv4, restricted, true) ||
        !exchange(ipv6, restricted, true))
      throw std::runtime_error("kernel TCP policy removal was not observed");
    std::wcout
        << L"Kernel TCP content-filter acceptance PASS: "
           L"IPv4/IPv6 same-flow framing, typed record policy, "
           L"malformed fail-close, fragmentation, outbound pass-through, "
           L"statistics, cleanup.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Kernel TCP content-filter acceptance failed: "
              << error.what() << '\n';
    return 1;
  }
}
