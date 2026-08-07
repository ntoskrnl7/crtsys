#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdint>
#include <system_error>
#include <utility>

namespace crtsys::wfp_sample {

[[noreturn]] inline void throw_socket(const char *operation) {
  throw std::system_error(
      ::WSAGetLastError(), std::system_category(), operation);
}

[[noreturn]] inline void throw_windows(const char *operation) {
  throw std::system_error(
      static_cast<int>(::GetLastError()),
      std::system_category(), operation);
}

class winsock_session {
public:
  winsock_session() {
    WSADATA data{};
    const int status = ::WSAStartup(MAKEWORD(2, 2), &data);
    if (status != 0)
      throw std::system_error(
          status, std::system_category(), "WSAStartup");
  }
  winsock_session(const winsock_session &) = delete;
  winsock_session &operator=(const winsock_session &) = delete;
  ~winsock_session() { (void)::WSACleanup(); }
};

class socket_owner {
public:
  explicit socket_owner(SOCKET value = INVALID_SOCKET) noexcept
      : value_(value) {}
  socket_owner(const socket_owner &) = delete;
  socket_owner &operator=(const socket_owner &) = delete;
  socket_owner(socket_owner &&other) noexcept
      : value_(std::exchange(other.value_, INVALID_SOCKET)) {}
  socket_owner &operator=(socket_owner &&other) noexcept {
    if (this != &other) {
      reset();
      value_ = std::exchange(other.value_, INVALID_SOCKET);
    }
    return *this;
  }
  ~socket_owner() { reset(); }

  SOCKET get() const noexcept { return value_; }
  SOCKET release() noexcept {
    return std::exchange(value_, INVALID_SOCKET);
  }
  void reset() noexcept {
    if (value_ != INVALID_SOCKET)
      (void)::closesocket(value_);
    value_ = INVALID_SOCKET;
  }

private:
  SOCKET value_ = INVALID_SOCKET;
};

struct listener {
  socket_owner socket;
  std::uint16_t port = 0;
};

inline listener make_listener() {
  socket_owner socket(::WSASocketW(
      AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
      WSA_FLAG_OVERLAPPED));
  if (socket.get() == INVALID_SOCKET)
    throw_socket("WSASocketW(listener)");

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (::bind(socket.get(),
             reinterpret_cast<const sockaddr *>(&address),
             sizeof(address)) == SOCKET_ERROR ||
      ::listen(socket.get(), SOMAXCONN) == SOCKET_ERROR)
    throw_socket("bind/listen");
  int size = sizeof(address);
  if (::getsockname(socket.get(),
                    reinterpret_cast<sockaddr *>(&address),
                    &size) == SOCKET_ERROR)
    throw_socket("getsockname");
  return {std::move(socket), ntohs(address.sin_port)};
}

inline listener make_ipv6_listener(std::uint16_t requested_port = 0) {
  socket_owner socket(::WSASocketW(
      AF_INET6, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
      WSA_FLAG_OVERLAPPED));
  if (socket.get() == INVALID_SOCKET)
    throw_socket("WSASocketW(IPv6 listener)");

  DWORD ipv6_only = 1;
  if (::setsockopt(
          socket.get(), IPPROTO_IPV6, IPV6_V6ONLY,
          reinterpret_cast<const char *>(&ipv6_only),
          sizeof(ipv6_only)) == SOCKET_ERROR)
    throw_socket("setsockopt(IPV6_V6ONLY)");

  sockaddr_in6 address{};
  address.sin6_family = AF_INET6;
  address.sin6_addr = in6addr_loopback;
  address.sin6_port = htons(requested_port);
  if (::bind(socket.get(),
             reinterpret_cast<const sockaddr *>(&address),
             sizeof(address)) == SOCKET_ERROR ||
      ::listen(socket.get(), SOMAXCONN) == SOCKET_ERROR)
    throw_socket("bind/listen(IPv6)");
  int size = sizeof(address);
  if (::getsockname(socket.get(),
                    reinterpret_cast<sockaddr *>(&address),
                    &size) == SOCKET_ERROR)
    throw_socket("getsockname(IPv6)");
  return {std::move(socket), ntohs(address.sin6_port)};
}

inline socket_owner accept_one(const listener &value) {
  socket_owner accepted(
      ::accept(value.socket.get(), nullptr, nullptr));
  if (accepted.get() == INVALID_SOCKET)
    throw_socket("accept");
  return accepted;
}

inline bool has_pending_connection(const listener &value) {
  fd_set readable;
  FD_ZERO(&readable);
  FD_SET(value.socket.get(), &readable);
  timeval timeout{};
  timeout.tv_usec = 250000;
  const int selected =
      ::select(0, &readable, nullptr, nullptr, &timeout);
  if (selected == SOCKET_ERROR)
    throw_socket("select(proxy listener)");
  return selected != 0;
}

} // namespace crtsys::wfp_sample
