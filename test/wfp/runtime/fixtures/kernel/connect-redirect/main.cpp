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
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

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

struct invocation {
  std::filesystem::path controller;
  std::filesystem::path ipc_directory;
  bool remove_ipc_directory = false;
};

std::filesystem::path sibling_controller() {
  std::vector<wchar_t> module(32768);
  const DWORD length = GetModuleFileNameW(
      nullptr, module.data(), static_cast<DWORD>(module.size()));
  if (!length || length == static_cast<DWORD>(module.size()))
    throw std::system_error(
        GetLastError(), std::system_category(), "GetModuleFileNameW");
  return std::filesystem::path(module.data(), module.data() + length)
             .parent_path() /
         L"crtsys_wfp_kernel_connect_redirect_controller.exe";
}

invocation parse_invocation(int argc, wchar_t **argv) {
  if (argc == 1) {
    return {
        sibling_controller(),
        std::filesystem::temp_directory_path() /
            (L"crtsys-wfp-kernel-connect-" +
             std::to_wstring(GetCurrentProcessId())),
        true};
  }
  if (argc == 3)
    return {std::filesystem::absolute(argv[1]),
            std::filesystem::absolute(argv[2]), false};
  throw std::invalid_argument(
      "usage: acceptance [<controller.exe> <ipc-directory>]");
}

class temporary_directory_cleanup {
public:
  temporary_directory_cleanup(std::filesystem::path path,
                              bool enabled) noexcept
      : path_(std::move(path)), enabled_(enabled) {}
  temporary_directory_cleanup(const temporary_directory_cleanup &) = delete;
  temporary_directory_cleanup &
  operator=(const temporary_directory_cleanup &) = delete;
  ~temporary_directory_cleanup() {
    if (!enabled_)
      return;
    std::error_code ignored;
    (void)std::filesystem::remove_all(path_, ignored);
  }

private:
  std::filesystem::path path_;
  bool enabled_ = false;
};

[[noreturn]] void throw_socket(const char *operation) {
  throw std::system_error(WSAGetLastError(), std::system_category(), operation);
}

listener make_listener(int family, std::uint16_t requested_port = 0) {
  socket_owner value(socket(family, SOCK_STREAM, IPPROTO_TCP));
  if (value.get() == INVALID_SOCKET)
    throw_socket("socket(listener)");
  const BOOL reuse = TRUE;
  if (setsockopt(value.get(), SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char *>(&reuse), sizeof(reuse)) ==
      SOCKET_ERROR)
    throw_socket("setsockopt(reuseaddr)");
  std::uint16_t port = 0;
  if (family == AF_INET) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(requested_port);
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
    address.sin6_port = htons(requested_port);
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

bool connection_closed_error(int value) noexcept {
  return value == WSAECONNREFUSED || value == WSAECONNRESET ||
         value == WSAECONNABORTED || value == WSAENETRESET ||
         value == WSAESHUTDOWN || value == WSAENOTCONN;
}

bool exchange_failed_closed(int family, std::uint16_t port) {
  try {
    auto client = connect_loopback(family, port);
    const DWORD receive_timeout_ms = 5'000;
    if (setsockopt(
            client.get(), SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char *>(&receive_timeout_ms),
            sizeof(receive_timeout_ms)) == SOCKET_ERROR)
      throw_socket("setsockopt(fail-closed receive timeout)");
    send_all(client.get(), "origin-unavailable-probe");
    if (shutdown(client.get(), SD_SEND) == SOCKET_ERROR) {
      const int error = WSAGetLastError();
      if (connection_closed_error(error))
        return true;
      throw std::system_error(
          error, std::system_category(), "shutdown(fail-closed probe)");
    }
    return receive_to_eof(client.get()).empty();
  } catch (const std::system_error &error) {
    if (connection_closed_error(error.code().value()))
      return true;
    throw;
  }
}

std::uint64_t stat_value(
    std::string_view stats, std::string_view name) {
  const std::string prefix = std::string(name) + "=";
  const auto begin = stats.find(prefix);
  if (begin == std::string_view::npos)
    throw std::runtime_error("kernel controller stat is missing");
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
    response_v4 = exchange(AF_INET, v4.port, payload_v4);
    response_v6 = exchange(AF_INET6, v6.port, payload_v6);
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

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    const auto arguments = parse_invocation(argc, argv);
    temporary_directory_cleanup cleanup(
        arguments.ipc_directory, arguments.remove_ipc_directory);
    winsock_session winsock;
    auto origin_v4 = make_listener(AF_INET);
    auto origin_v6 = make_listener(AF_INET6, origin_v4.port);
    crtsys::wfp_test::controller_process controller(
        arguments.controller,
        {std::to_wstring(origin_v4.port),
         arguments.ipc_directory.wstring()},
        arguments.ipc_directory);
    controller.wait_ready();
    const auto before = controller.stats();
    run_exchange_pair(origin_v4, origin_v6, "redirect-v4", "redirect-v6");

    const auto original_port = origin_v4.port;
    origin_v4.socket.close();
    origin_v6.socket.close();
    const bool failed_v4 = exchange_failed_closed(AF_INET, original_port);
    const bool failed_v6 = exchange_failed_closed(AF_INET6, original_port);
    if (!failed_v4 || !failed_v6)
      throw std::runtime_error(
          "kernel proxy did not fail closed when origin was unavailable");

    controller.stop();
    const auto after = controller.stats();
    const auto accepted = stat_value(after, "accepted") -
                          stat_value(before, "accepted");
    const auto records = stat_value(after, "redirect_records") -
                         stat_value(before, "redirect_records");
    const auto completed = stat_value(after, "completed") -
                           stat_value(before, "completed");
    const auto failed = stat_value(after, "failed") -
                        stat_value(before, "failed");
    const auto to_origin = stat_value(after, "bytes_to_origin") -
                           stat_value(before, "bytes_to_origin");
    const auto to_client = stat_value(after, "bytes_to_client") -
                           stat_value(before, "bytes_to_client");
    if (accepted != 4 || records != 4 || completed != 2 || failed != 2 ||
        !to_origin || !to_client)
      throw std::runtime_error(
          "kernel redirect record/counter evidence mismatched");

    auto restored_v4 = make_listener(AF_INET, original_port);
    auto restored_v6 = make_listener(AF_INET6, original_port);
    run_exchange_pair(
        restored_v4, restored_v6,
        "failure-restored-v4", "failure-restored-v6");
    std::cout
        << "Kernel connect-redirect PASS: IPv4/IPv6 ALE redirect, "
           "original-destination WSK relay, redirect records="
        << records << ", counters accepted=" << accepted
        << " completed=" << completed
        << ", restored=IPv4/IPv6, failed_origin=blocked, failed_counter=2"
           ", failure_restored=IPv4/IPv6, cleanup\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "kernel connect-redirect acceptance failed: "
              << error.what() << '\n';
    return 1;
  }
}
