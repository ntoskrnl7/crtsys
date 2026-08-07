#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include "controller_process.hpp"

namespace {

class winsock_session {
public:
  winsock_session() {
    WSADATA data{};
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result)
      throw std::system_error(result, std::system_category(), "WSAStartup");
  }
  ~winsock_session() { WSACleanup(); }
};

class socket_owner {
public:
  explicit socket_owner(SOCKET value = INVALID_SOCKET) noexcept : value_(value) {}
  socket_owner(const socket_owner &) = delete;
  socket_owner &operator=(const socket_owner &) = delete;
  socket_owner(socket_owner &&other) noexcept
      : value_(std::exchange(other.value_, INVALID_SOCKET)) {}
  socket_owner &operator=(socket_owner &&other) noexcept {
    if (this != &other) {
      close();
      value_ = std::exchange(other.value_, INVALID_SOCKET);
    }
    return *this;
  }
  ~socket_owner() { close(); }
  SOCKET get() const noexcept { return value_; }
  void close() noexcept {
    if (value_ != INVALID_SOCKET) {
      closesocket(value_);
      value_ = INVALID_SOCKET;
    }
  }
private:
  SOCKET value_;
};

struct listener {
  socket_owner socket;
  std::uint16_t port;
  int family;
};

[[noreturn]] void throw_socket(const char *operation) {
  throw std::system_error(WSAGetLastError(), std::system_category(), operation);
}

listener make_listener(int family) {
  socket_owner value(socket(family, SOCK_STREAM, IPPROTO_TCP));
  if (value.get() == INVALID_SOCKET)
    throw_socket("socket(listener)");
  std::uint16_t port = 0;
  if (family == AF_INET) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(value.get(), reinterpret_cast<const sockaddr *>(&address),
             sizeof(address)) == SOCKET_ERROR)
      throw_socket("bind(v4)");
    int size = sizeof(address);
    if (getsockname(value.get(), reinterpret_cast<sockaddr *>(&address),
                    &size) == SOCKET_ERROR)
      throw_socket("getsockname(v4)");
    port = ntohs(address.sin_port);
  } else {
    DWORD v6_only = 1;
    if (setsockopt(value.get(), IPPROTO_IPV6, IPV6_V6ONLY,
                   reinterpret_cast<const char *>(&v6_only),
                   sizeof(v6_only)) == SOCKET_ERROR)
      throw_socket("setsockopt(v6only)");
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    if (bind(value.get(), reinterpret_cast<const sockaddr *>(&address),
             sizeof(address)) == SOCKET_ERROR)
      throw_socket("bind(v6)");
    int size = sizeof(address);
    if (getsockname(value.get(), reinterpret_cast<sockaddr *>(&address),
                    &size) == SOCKET_ERROR)
      throw_socket("getsockname(v6)");
    port = ntohs(address.sin6_port);
  }
  if (listen(value.get(), 4) == SOCKET_ERROR)
    throw_socket("listen");
  return {std::move(value), port, family};
}

void send_all(SOCKET socket_value, std::string_view value) {
  std::size_t offset = 0;
  while (offset != value.size()) {
    const int sent = send(socket_value, value.data() + offset,
                          static_cast<int>(value.size() - offset), 0);
    if (sent == SOCKET_ERROR)
      throw_socket("send");
    offset += static_cast<std::size_t>(sent);
  }
}

std::string receive_to_eof(SOCKET socket_value) {
  std::string result;
  std::array<char, 4096> buffer{};
  for (;;) {
    const int read = recv(socket_value, buffer.data(),
                          static_cast<int>(buffer.size()), 0);
    if (read == 0)
      return result;
    if (read == SOCKET_ERROR)
      throw_socket("recv");
    result.append(buffer.data(), static_cast<std::size_t>(read));
  }
}

socket_owner connect_loopback(int family, std::uint16_t port) {
  socket_owner value(socket(family, SOCK_STREAM, IPPROTO_TCP));
  if (value.get() == INVALID_SOCKET)
    throw_socket("socket(client)");
  if (family == AF_INET) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (connect(value.get(), reinterpret_cast<const sockaddr *>(&address),
                sizeof(address)) == SOCKET_ERROR)
      throw_socket("connect(v4)");
  } else {
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = htons(port);
    if (connect(value.get(), reinterpret_cast<const sockaddr *>(&address),
                sizeof(address)) == SOCKET_ERROR)
      throw_socket("connect(v6)");
  }
  return value;
}

int connect_error(int family, std::uint16_t port) {
  socket_owner value(socket(family, SOCK_STREAM, IPPROTO_TCP));
  if (value.get() == INVALID_SOCKET)
    throw_socket("socket(client probe)");
  sockaddr_storage storage{};
  int size = 0;
  if (family == AF_INET) {
    auto &address = *reinterpret_cast<sockaddr_in *>(&storage);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    size = sizeof(address);
  } else {
    auto &address = *reinterpret_cast<sockaddr_in6 *>(&storage);
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = htons(port);
    size = sizeof(address);
  }
  if (connect(value.get(), reinterpret_cast<const sockaddr *>(&storage),
              size) == 0)
    return ERROR_SUCCESS;
  return WSAGetLastError();
}

bool listener_has_pending_connection(const listener &value) {
  fd_set readable;
  FD_ZERO(&readable);
  FD_SET(value.socket.get(), &readable);
  timeval timeout{};
  timeout.tv_usec = 250000;
  const int selected = select(0, &readable, nullptr, nullptr, &timeout);
  if (selected == SOCKET_ERROR)
    throw_socket("select(origin listener)");
  return selected != 0;
}

std::uint64_t stat_value(
    std::string_view stats, std::string_view name) {
  const std::string prefix = std::string(name) + "=";
  const auto begin = stats.find(prefix);
  if (begin == std::string_view::npos)
    throw std::runtime_error("proxy service stat is missing");
  const auto value_begin = begin + prefix.size();
  const auto value_end = stats.find('\n', value_begin);
  return std::stoull(std::string(stats.substr(
      value_begin, value_end - value_begin)));
}

void echo_once(const listener &server, std::string &received) {
  socket_owner peer(accept(server.socket.get(), nullptr, nullptr));
  if (peer.get() == INVALID_SOCKET)
    throw_socket("accept(origin)");
  received = receive_to_eof(peer.get());
  send_all(peer.get(), "echo:" + received);
  if (shutdown(peer.get(), SD_SEND) == SOCKET_ERROR)
    throw_socket("shutdown(origin)");
}

std::string exchange(int family, std::uint16_t port, std::string_view payload) {
  auto client = connect_loopback(family, port);
  send_all(client.get(), payload);
  if (shutdown(client.get(), SD_SEND) == SOCKET_ERROR)
    throw_socket("shutdown(client)");
  return receive_to_eof(client.get());
}

void run_exchange_pair(listener &v4, listener &v6,
                       std::string_view payload_v4,
                       std::string_view payload_v6) {
  std::string received_v4;
  std::string received_v6;
  std::exception_ptr error_v4;
  std::exception_ptr error_v6;
  std::thread origin_v4([&] {
    try {
      echo_once(v4, received_v4);
    } catch (...) {
      error_v4 = std::current_exception();
    }
  });
  std::thread origin_v6([&] {
    try {
      echo_once(v6, received_v6);
    } catch (...) {
      error_v6 = std::current_exception();
    }
  });
  std::string response_v4;
  std::string response_v6;
  try {
    try {
      response_v4 = exchange(AF_INET, v4.port, payload_v4);
    } catch (const std::exception &error) {
      throw std::runtime_error(
          std::string("IPv4 client: ") + error.what());
    }
    try {
      response_v6 = exchange(AF_INET6, v6.port, payload_v6);
    } catch (const std::exception &error) {
      throw std::runtime_error(
          std::string("IPv6 client: ") + error.what());
    }
  } catch (...) {
    v4.socket.close();
    v6.socket.close();
    origin_v4.join();
    origin_v6.join();
    throw;
  }
  origin_v4.join();
  origin_v6.join();
  if (error_v4)
    std::rethrow_exception(error_v4);
  if (error_v6)
    std::rethrow_exception(error_v6);
  if (received_v4 != payload_v4 || received_v6 != payload_v6 ||
      response_v4 != "echo:" + std::string(payload_v4) ||
      response_v6 != "echo:" + std::string(payload_v6))
    throw std::runtime_error("redirected echo evidence mismatched");
}

void run_exchange_stage(std::string_view stage, listener &v4, listener &v6,
                        std::string_view payload_v4,
                        std::string_view payload_v6) {
  try {
    run_exchange_pair(v4, v6, payload_v4, payload_v6);
  } catch (const std::exception &error) {
    throw std::runtime_error(
        std::string(stage) + " exchange failed: " + error.what());
  }
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument(
          "usage: crtsys_wfp_connect_redirect_acceptance.exe "
          "<proxy-service.exe> <ipc-directory>");
    winsock_session winsock;
    auto origin_v4 = make_listener(AF_INET);
    auto origin_v6 = make_listener(AF_INET6);
    crtsys::wfp_test::controller_process service(
        argv[1],
        {std::to_wstring(origin_v4.port), std::to_wstring(origin_v6.port),
         std::filesystem::absolute(argv[2]).wstring()},
        argv[2]);
    service.wait_ready();
    try {
      run_exchange_stage(
          "redirected", origin_v4, origin_v6,
          "redirect-v4", "redirect-v6");
    } catch (...) {
      // Let the proxy join both relay workers and publish its concrete
      // failure before propagating the client-side symptom.
      try {
        service.stop();
      } catch (...) {
      }
      throw;
    }
    service.stop();
    const auto stats = service.stats();
    const auto v4_up = stat_value(stats, "v4_upstream");
    const auto v4_down = stat_value(stats, "v4_downstream");
    const auto v6_up = stat_value(stats, "v6_upstream");
    const auto v6_down = stat_value(stats, "v6_downstream");
    if (stat_value(stats, "v4_original_family") != AF_INET ||
        stat_value(stats, "v4_original_port") != origin_v4.port ||
        stat_value(stats, "v6_original_family") != AF_INET6 ||
        stat_value(stats, "v6_original_port") != origin_v6.port ||
        !v4_up || !v4_down || !v6_up || !v6_down)
      throw std::runtime_error(
          "original destination or coroutine relay evidence mismatched");

    run_exchange_stage(
        "policy-removal", origin_v4, origin_v6,
        "direct-v4", "direct-v6");

    crtsys::wfp_test::controller_process unavailable(
        argv[1],
        {L"--unavailable-proxy", std::to_wstring(origin_v4.port),
         std::to_wstring(origin_v6.port),
         std::filesystem::absolute(argv[2]).wstring()},
        argv[2]);
    unavailable.wait_ready();
    const int unavailable_v4 = connect_error(AF_INET, origin_v4.port);
    const int unavailable_v6 = connect_error(AF_INET6, origin_v6.port);
    if (unavailable_v4 == ERROR_SUCCESS || unavailable_v6 == ERROR_SUCCESS)
      throw std::runtime_error(
          "unavailable proxy did not fail closed for both families");
    if (listener_has_pending_connection(origin_v4) ||
        listener_has_pending_connection(origin_v6))
      throw std::runtime_error(
          "unavailable proxy traffic bypassed to the origin");
    unavailable.stop();
    if (unavailable.stats().find("unavailable_proxy=closed") ==
        std::string::npos)
      throw std::runtime_error("unavailable proxy mode did not stop cleanly");

    run_exchange_stage(
        "failure-restoration", origin_v4, origin_v6,
        "failure-restored-v4", "failure-restored-v6");
    std::cout
        << "NTL WFP connect-redirect ok: IPv4=" << origin_v4.port
        << ", IPv6=" << origin_v6.port
        << ", coroutine_up=" << v4_up + v6_up
        << ", coroutine_down=" << v4_down + v6_down
        << ", restored=direct, unavailable_proxy=blocked"
           ", origin_bypass=none, failure_restored=IPv4/IPv6\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "user connect-redirect acceptance failed: "
              << error.what();
    if (argc == 3) {
      std::ifstream failure(
          std::filesystem::absolute(argv[2]) / L"controller.error",
          std::ios::binary);
      if (failure) {
        const std::string detail{
            std::istreambuf_iterator<char>(failure), {}};
        if (!detail.empty())
          std::cerr << "; proxy service: " << detail;
      }
    }
    std::cerr << '\n';
    return 1;
  }
}
