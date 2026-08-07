#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
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
  ~socket_owner() { if (value_ != INVALID_SOCKET) closesocket(value_); }
  SOCKET get() const noexcept { return value_; }
private:
  SOCKET value_;
};

struct listener { socket_owner socket; std::uint16_t port; int family; };

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
  if (listen(value.get(), 64) == SOCKET_ERROR)
    throw_socket("listen");
  return {std::move(value), port, family};
}

struct connect_result { int error; std::chrono::milliseconds elapsed; };

connect_result connect_once(int family, std::uint16_t port) {
  socket_owner client(socket(family, SOCK_STREAM, IPPROTO_TCP));
  if (client.get() == INVALID_SOCKET)
    throw_socket("socket(client)");
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
  const auto start = std::chrono::steady_clock::now();
  const int result = connect(client.get(),
      reinterpret_cast<const sockaddr *>(&storage), size);
  return {result == 0 ? ERROR_SUCCESS : WSAGetLastError(),
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - start)};
}

std::size_t parse_count(const wchar_t *value) {
  wchar_t *end = nullptr;
  const unsigned long parsed = std::wcstoul(value, &end, 10);
  if (!value[0] || !end || *end || parsed == 0 || parsed > 4096)
    throw std::invalid_argument("connection count must be 1..4096");
  return static_cast<std::size_t>(parsed);
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc != 3 && argc != 6)
      throw std::invalid_argument(
          "usage: crtsys_wfp_async_inspection_acceptance.exe "
          "<controller.exe> <ipc-directory> "
          "[--unload-race <connection-count> <driver-service>]");
    const bool race =
        argc == 6 && std::wstring_view(argv[3]) == L"--unload-race";
    if (argc == 6 && !race)
      throw std::invalid_argument("unknown acceptance mode");
    const std::size_t count = race ? parse_count(argv[4]) : 0;
    winsock_session winsock;
    auto permit_v4 = make_listener(AF_INET);
    auto block_v4 = make_listener(AF_INET);
    auto permit_v6 = make_listener(AF_INET6);
    auto block_v6 = make_listener(AF_INET6);
    const auto ipc = std::filesystem::absolute(argv[2]);
    std::vector<std::wstring> controller_arguments{
        race ? L"--unload-race" : L"--serve",
        std::to_wstring(permit_v4.port), std::to_wstring(block_v4.port),
        std::to_wstring(permit_v6.port), std::to_wstring(block_v6.port)};
    if (race)
      controller_arguments.emplace_back(argv[5]);
    controller_arguments.emplace_back(ipc.wstring());
    crtsys::wfp_test::controller_process controller(
        argv[1], std::move(controller_arguments), ipc);
    controller.wait_ready();
    if (!race) {
      const auto allowed4 = connect_once(AF_INET, permit_v4.port);
      const auto denied4 = connect_once(AF_INET, block_v4.port);
      const auto allowed6 = connect_once(AF_INET6, permit_v6.port);
      const auto denied6 = connect_once(AF_INET6, block_v6.port);
      if (allowed4.error || allowed6.error ||
          denied4.error != WSAEACCES || denied6.error != WSAEACCES)
        throw std::runtime_error("async permit/block decisions mismatched");
      if (allowed4.elapsed < std::chrono::milliseconds(50) ||
          allowed6.elapsed < std::chrono::milliseconds(50) ||
          denied4.elapsed < std::chrono::milliseconds(50) ||
          denied6.elapsed < std::chrono::milliseconds(50))
        throw std::runtime_error("async decisions completed synchronously");
      controller.stop();
      if (connect_once(AF_INET, block_v4.port).error != ERROR_SUCCESS ||
          connect_once(AF_INET6, block_v6.port).error != ERROR_SUCCESS)
        throw std::runtime_error("policy removal did not restore connects");
      if (controller.stats().find("state=stopped") == std::string::npos)
        throw std::runtime_error("controller final stats are missing");
      std::cout
          << "NTL WFP async-inspection ok: permit=delayed-success, "
             "block=delayed-10013, restored=success, ipv4=pass, "
             "ipv6=pass, deferred=verified, cleanup=restored\n";
    } else {
      std::atomic<bool> start{false};
      std::atomic<std::size_t> ready{0};
      std::atomic<std::size_t> denied{0};
      std::atomic<std::size_t> unexpected{0};
      std::vector<std::thread> workers;
      workers.reserve(count);
      for (std::size_t index = 0; index != count; ++index) {
        workers.emplace_back([&, index] {
          ready.fetch_add(1, std::memory_order_release);
          while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();
          const auto result =
              (index & 1) == 0
                  ? connect_once(AF_INET, block_v4.port)
                  : connect_once(AF_INET6, block_v6.port);
          if (result.error == WSAEACCES)
            denied.fetch_add(1, std::memory_order_relaxed);
          else
            unexpected.fetch_add(1, std::memory_order_relaxed);
        });
      }
      while (ready.load(std::memory_order_acquire) != count)
        std::this_thread::yield();
      start.store(true, std::memory_order_release);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      std::cout
          << "NTL WFP async-inspection unload-race ready: pending="
          << count << std::endl;
      bool workers_joined = false;
      bool driver_stopped = false;
      try {
        controller.command(L"stop-driver");
        controller.wait_file(L"driver.stopped");
        driver_stopped = true;
        for (auto &worker : workers)
          worker.join();
        workers_joined = true;
        if (unexpected.load(std::memory_order_relaxed) != 0 ||
            denied.load(std::memory_order_relaxed) != count)
          throw std::runtime_error(
              "unload-race connections did not remain fail closed");

        controller.command(L"start-driver");
        controller.wait_file(L"driver.started");
        driver_stopped = false;
      } catch (...) {
        if (!workers_joined) {
          for (auto &worker : workers) {
            if (worker.joinable())
              worker.join();
          }
        }
        if (driver_stopped) {
          try {
            controller.command(L"start-driver");
            controller.wait_file(L"driver.started");
          } catch (...) {
          }
        }
        throw;
      }
      const auto reloaded_permit_v4 = connect_once(AF_INET, permit_v4.port);
      const auto reloaded_permit_v6 = connect_once(AF_INET6, permit_v6.port);
      const auto reloaded_block_v4 = connect_once(AF_INET, block_v4.port);
      const auto reloaded_block_v6 = connect_once(AF_INET6, block_v6.port);
      if (reloaded_permit_v4.error != ERROR_SUCCESS ||
          reloaded_permit_v6.error != ERROR_SUCCESS ||
          reloaded_block_v4.error != WSAEACCES ||
          reloaded_block_v6.error != WSAEACCES ||
          reloaded_permit_v4.elapsed < std::chrono::milliseconds(50) ||
          reloaded_permit_v6.elapsed < std::chrono::milliseconds(50) ||
          reloaded_block_v4.elapsed < std::chrono::milliseconds(50) ||
          reloaded_block_v6.elapsed < std::chrono::milliseconds(50))
        throw std::runtime_error(
            "async-inspection did not recover after driver reload");

      controller.command(L"release-policy");
      controller.wait_file(L"policy.released");
      if (connect_once(AF_INET, block_v4.port).error != ERROR_SUCCESS ||
          connect_once(AF_INET6, block_v6.port).error != ERROR_SUCCESS)
        throw std::runtime_error(
            "async-inspection policy cleanup did not restore connects");
      controller.stop();
      if (controller.stats().find("state=stopped") == std::string::npos)
        throw std::runtime_error("controller final stats are missing");
      std::cout
          << "NTL WFP async-inspection unload-race ok: connections="
          << count << ", denied="
          << denied.load(std::memory_order_relaxed)
          << ", drain=bounded, cancel=fail-closed, reload=verified, "
             "cleanup=restored\n";
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "async-inspection acceptance failed: "
              << error.what() << '\n';
    return 1;
  }
}
