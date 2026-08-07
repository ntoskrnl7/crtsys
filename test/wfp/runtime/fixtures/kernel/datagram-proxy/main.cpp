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
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "datagram_proxy_contract.hpp"
#include "runtime_controller_fixture.hpp"

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

struct udp_receiver {
  socket_owner socket;
  int family = AF_UNSPEC;
  std::uint16_t port = 0;
};

struct received_datagram {
  std::string payload;
  sockaddr_storage peer{};
  int peer_size = 0;
};

udp_receiver make_receiver(int family, std::uint16_t requested_port = 0) {
  socket_owner socket(::socket(family, SOCK_DGRAM, IPPROTO_UDP));
  if (socket.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "socket(receiver)");
  sockaddr_storage storage{};
  int address_size = 0;
  if (family == AF_INET) {
    auto &address = reinterpret_cast<sockaddr_in &>(storage);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(requested_port);
    address_size = sizeof(address);
  } else {
    DWORD v6_only = 1;
    if (::setsockopt(socket.get(), IPPROTO_IPV6, IPV6_V6ONLY,
                     reinterpret_cast<const char *>(&v6_only),
                     sizeof(v6_only)) == SOCKET_ERROR)
      throw std::system_error(::WSAGetLastError(), std::system_category(),
                              "IPV6_V6ONLY");
    auto &address = reinterpret_cast<sockaddr_in6 &>(storage);
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = htons(requested_port);
    address_size = sizeof(address);
  }
  if (::bind(socket.get(), reinterpret_cast<const sockaddr *>(&storage),
             address_size) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "bind(receiver)");
  int size = sizeof(storage);
  if (::getsockname(socket.get(), reinterpret_cast<sockaddr *>(&storage),
                    &size) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "getsockname(receiver)");
  const auto port =
      family == AF_INET
          ? ntohs(reinterpret_cast<const sockaddr_in &>(storage).sin_port)
          : ntohs(reinterpret_cast<const sockaddr_in6 &>(storage).sin6_port);
  return {std::move(socket), family, port};
}

void send_datagram(int family, std::uint16_t port, std::string_view payload) {
  socket_owner sender(::socket(family, SOCK_DGRAM, IPPROTO_UDP));
  if (sender.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "socket(sender)");
  sockaddr_storage storage{};
  int address_size = 0;
  if (family == AF_INET) {
    auto &address = reinterpret_cast<sockaddr_in &>(storage);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    address_size = sizeof(address);
  } else {
    auto &address = reinterpret_cast<sockaddr_in6 &>(storage);
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = htons(port);
    address_size = sizeof(address);
  }
  if (::connect(sender.get(), reinterpret_cast<const sockaddr *>(&storage),
                address_size) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "connect(datagram)");
  const int sent = ::send(sender.get(), payload.data(),
                          static_cast<int>(payload.size()), 0);
  if (sent != static_cast<int>(payload.size()))
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "send(datagram)");
}

socket_owner make_connected_sender(int family, std::uint16_t port) {
  socket_owner sender(::socket(family, SOCK_DGRAM, IPPROTO_UDP));
  if (sender.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "socket(sender)");
  sockaddr_storage storage{};
  int address_size = 0;
  if (family == AF_INET) {
    auto &address = reinterpret_cast<sockaddr_in &>(storage);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    address_size = sizeof(address);
  } else {
    auto &address = reinterpret_cast<sockaddr_in6 &>(storage);
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = htons(port);
    address_size = sizeof(address);
  }
  if (::connect(sender.get(), reinterpret_cast<const sockaddr *>(&storage),
                address_size) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "connect(sender)");
  return sender;
}

void send_connected(SOCKET socket, std::string_view payload) {
  const int sent =
      ::send(socket, payload.data(), static_cast<int>(payload.size()), 0);
  if (sent != static_cast<int>(payload.size()))
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "send(connected datagram)");
}

received_datagram receive_datagram_from(SOCKET socket, DWORD timeout_ms,
                                        const char *operation) {
  if (::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&timeout_ms),
                   sizeof(timeout_ms)) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "setsockopt(SO_RCVTIMEO)");
  std::array<char, 256> buffer{};
  received_datagram result{};
  result.peer_size = sizeof(result.peer);
  const int received = ::recvfrom(
      socket, buffer.data(), static_cast<int>(buffer.size()), 0,
      reinterpret_cast<sockaddr *>(&result.peer), &result.peer_size);
  if (received == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            operation);
  result.payload.assign(buffer.data(), static_cast<std::size_t>(received));
  return result;
}

std::string receive_datagram(SOCKET socket, DWORD timeout_ms,
                             const char *operation) {
  return receive_datagram_from(socket, timeout_ms, operation).payload;
}

void send_reply(SOCKET socket, const received_datagram &request,
                std::string_view payload) {
  const int sent = ::sendto(
      socket, payload.data(), static_cast<int>(payload.size()), 0,
      reinterpret_cast<const sockaddr *>(&request.peer), request.peer_size);
  if (sent != static_cast<int>(payload.size()))
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "sendto(proxy reply)");
}

bool has_no_datagram(SOCKET socket) {
  DWORD timeout_ms = 200;
  if (::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&timeout_ms),
                   sizeof(timeout_ms)) == SOCKET_ERROR)
    return false;
  char byte = 0;
  const int result = ::recv(socket, &byte, 1, 0);
  return result == SOCKET_ERROR && ::WSAGetLastError() == WSAETIMEDOUT;
}

std::filesystem::path parse_controller(int argc, wchar_t **argv) {
  if (argc == 1)
    return fixture::sibling_executable(
        L"crtsys_wfp_datagram_proxy_controller.exe");
  if (argc == 3 && std::wstring_view(argv[1]) == L"--controller")
    return std::filesystem::absolute(argv[2]);
  throw std::invalid_argument("usage: acceptance [--controller <path>]");
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    winsock_session winsock;
    const auto controller = parse_controller(argc, argv);
    auto original_v4 = make_receiver(AF_INET);
    auto proxy_v4 = make_receiver(AF_INET);
    auto original_v6 = make_receiver(AF_INET6, original_v4.port);
    auto proxy_v6 = make_receiver(AF_INET6, proxy_v4.port);
    fixture::state_directory state(L"kernel-datagram-proxy");
    fixture::controller_process policy(
        controller, state.path(),
        {{L"--original-port", std::to_wstring(original_v4.port)},
         {L"--proxy-port", std::to_wstring(proxy_v4.port)},
         {L"--application", fixture::current_executable().wstring()}});
    policy.wait_ready();
    constexpr std::string_view payload = "ntl-datagram-proxy";
    constexpr std::string_view reply = "ntl-datagram-proxy-reply";
    auto sender_v4 = make_connected_sender(AF_INET, original_v4.port);
    auto sender_v6 = make_connected_sender(AF_INET6, original_v6.port);
    send_connected(sender_v4.get(), payload);
    send_connected(sender_v6.get(), payload);
    const auto request_v4 = receive_datagram_from(
        proxy_v4.socket.get(), 2000, "recv(proxy IPv4 request)");
    const auto request_v6 = receive_datagram_from(
        proxy_v6.socket.get(), 2000, "recv(proxy IPv6 request)");
    if (request_v4.payload != payload || request_v6.payload != payload ||
        !has_no_datagram(original_v4.socket.get()) ||
        !has_no_datagram(original_v6.socket.get()))
      throw std::runtime_error("UDP redirection was not exclusive");
    send_reply(proxy_v4.socket.get(), request_v4, reply);
    send_reply(proxy_v6.socket.get(), request_v6, reply);
    ::Sleep(100);
    if (receive_datagram(sender_v4.get(), 2000,
                         "recv(restored IPv4 reply)") != reply ||
        receive_datagram(sender_v6.get(), 2000,
                         "recv(restored IPv6 reply)") != reply)
      throw std::runtime_error("UDP reply tuple was not restored");

    policy.request_stop();
    policy.wait();
    const auto stats = fixture::read_stats(policy.stats_file());
    if (fixture::require_stat(stats, "policy.original_port") !=
            original_v4.port ||
        fixture::require_stat(stats, "policy.proxy_port") != proxy_v4.port ||
        fixture::require_stat(stats, "policy.ipv4") != 1 ||
        fixture::require_stat(stats, "policy.ipv6") != 1)
      throw std::runtime_error("datagram-proxy controller stats are wrong");

    send_datagram(AF_INET, original_v4.port, payload);
    send_datagram(AF_INET6, original_v6.port, payload);
    if (receive_datagram(original_v4.socket.get(), 2000,
                         "recv(restored IPv4 destination)") != payload ||
        receive_datagram(original_v6.socket.get(), 2000,
                         "recv(restored IPv6 destination)") != payload)
      throw std::runtime_error("UDP destination was not restored");

    std::wcout << L"Kernel datagram-proxy acceptance PASS: redirected and "
                  L"restored bidirectional IPv4/IPv6 UDP tuples, then "
                  L"restored both destinations after policy removal.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Kernel datagram-proxy acceptance failed: " << error.what()
              << '\n';
    return 1;
  }
}
