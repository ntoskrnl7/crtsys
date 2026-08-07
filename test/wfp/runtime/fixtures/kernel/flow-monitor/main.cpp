#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

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
  socket_owner &operator=(socket_owner &&other) noexcept {
    if (this != &other) {
      if (value_ != INVALID_SOCKET)
        ::closesocket(value_);
      value_ = std::exchange(other.value_, INVALID_SOCKET);
    }
    return *this;
  }
  ~socket_owner() {
    if (value_ != INVALID_SOCKET)
      ::closesocket(value_);
  }
  SOCKET get() const noexcept { return value_; }

private:
  SOCKET value_ = INVALID_SOCKET;
};

struct listener {
  socket_owner socket;
  std::uint16_t port = 0;
  int family = AF_UNSPEC;
};

listener make_listener(int family) {
  socket_owner socket(::socket(family, SOCK_STREAM, IPPROTO_TCP));
  if (socket.get() == INVALID_SOCKET)
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
    if (::setsockopt(socket.get(), IPPROTO_IPV6, IPV6_V6ONLY,
                     reinterpret_cast<const char *>(&v6_only),
                     sizeof(v6_only)) == SOCKET_ERROR)
      throw std::system_error(::WSAGetLastError(), std::system_category(),
                              "IPV6_V6ONLY");
    auto &address = reinterpret_cast<sockaddr_in6 &>(storage);
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address_size = sizeof(address);
  }
  if (::bind(socket.get(), reinterpret_cast<const sockaddr *>(&storage),
             address_size) == SOCKET_ERROR ||
      ::listen(socket.get(), SOMAXCONN) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "bind/listen");
  int size = sizeof(storage);
  if (::getsockname(socket.get(), reinterpret_cast<sockaddr *>(&storage),
                    &size) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "getsockname");
  const auto port =
      family == AF_INET
          ? ntohs(reinterpret_cast<const sockaddr_in &>(storage).sin_port)
          : ntohs(reinterpret_cast<const sockaddr_in6 &>(storage).sin6_port);
  return {std::move(socket), port, family};
}

sockaddr_storage loopback_address(const listener &server,
                                  int &address_size) {
  sockaddr_storage storage{};
  if (server.family == AF_INET) {
    auto &address = reinterpret_cast<sockaddr_in &>(storage);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(server.port);
    address_size = sizeof(address);
  } else {
    auto &address = reinterpret_cast<sockaddr_in6 &>(storage);
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = htons(server.port);
    address_size = sizeof(address);
  }
  return storage;
}

void set_socket_timeout(SOCKET socket, DWORD timeout_ms) {
  if (::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&timeout_ms),
                   sizeof(timeout_ms)) == SOCKET_ERROR ||
      ::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char *>(&timeout_ms),
                   sizeof(timeout_ms)) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "setsockopt(socket timeout)");
}

struct load_result {
  std::chrono::milliseconds elapsed{};
  std::chrono::microseconds p50{};
  std::chrono::microseconds p95{};
  std::chrono::microseconds maximum{};
};

load_result exchange_load(const listener &server, std::string_view payload,
                          std::size_t connection_count,
                          std::size_t concurrency) {
  std::vector<std::chrono::microseconds> latencies(connection_count);
  std::atomic<std::size_t> next_connection{0};
  std::atomic<bool> failed{false};
  std::mutex failure_mutex;
  std::exception_ptr failure;
  const auto capture_failure = [&](std::exception_ptr value) {
    bool expected = false;
    if (failed.compare_exchange_strong(expected, true)) {
      std::lock_guard lock(failure_mutex);
      failure = std::move(value);
    }
  };

  std::thread receiver([&] {
    try {
      std::size_t accepted_count = 0;
      while (accepted_count != connection_count) {
        if (failed.load(std::memory_order_acquire))
          return;
        fd_set readable{};
        FD_ZERO(&readable);
        FD_SET(server.socket.get(), &readable);
        timeval timeout{};
        timeout.tv_usec = 100'000;
        const int selected = ::select(0, &readable, nullptr, nullptr, &timeout);
        if (selected == SOCKET_ERROR)
          throw std::system_error(::WSAGetLastError(), std::system_category(),
                                  "select(load listener)");
        if (selected == 0)
          continue;
        socket_owner accepted(::accept(server.socket.get(), nullptr, nullptr));
        if (accepted.get() == INVALID_SOCKET)
          throw std::system_error(::WSAGetLastError(), std::system_category(),
                                  "accept(load)");
        set_socket_timeout(accepted.get(), 10'000);
        std::array<char, 512> buffer{};
        std::size_t total = 0;
        while (total != payload.size()) {
          const int received = ::recv(
              accepted.get(), buffer.data(),
              static_cast<int>((std::min)(buffer.size(),
                                          payload.size() - total)),
              0);
          if (received <= 0)
            throw std::runtime_error("load server received a short payload");
          total += static_cast<std::size_t>(received);
        }
        const char acknowledgement = 'A';
        if (::send(accepted.get(), &acknowledgement, 1, 0) != 1)
          throw std::system_error(::WSAGetLastError(), std::system_category(),
                                  "send(load acknowledgement)");
        ++accepted_count;
      }
    } catch (...) {
      capture_failure(std::current_exception());
    }
  });

  const auto started = std::chrono::steady_clock::now();
  std::vector<std::thread> workers;
  workers.reserve(concurrency);
  for (std::size_t worker = 0; worker != concurrency; ++worker) {
    workers.emplace_back([&] {
      try {
        for (;;) {
          const auto index =
              next_connection.fetch_add(1, std::memory_order_relaxed);
          if (index >= connection_count ||
              failed.load(std::memory_order_acquire))
            return;
          const auto connection_started = std::chrono::steady_clock::now();
          int address_size = 0;
          const auto address = loopback_address(server, address_size);
          socket_owner client;
          int connect_error = WSAEADDRINUSE;
          for (unsigned attempt = 0; attempt != 256; ++attempt) {
            socket_owner candidate(
                ::socket(server.family, SOCK_STREAM, IPPROTO_TCP));
            if (candidate.get() == INVALID_SOCKET)
              throw std::system_error(::WSAGetLastError(),
                                      std::system_category(),
                                      "socket(load client)");
            set_socket_timeout(candidate.get(), 10'000);
            linger abortive_close{};
            abortive_close.l_onoff = 1;
            if (::setsockopt(candidate.get(), SOL_SOCKET, SO_LINGER,
                             reinterpret_cast<const char *>(&abortive_close),
                             sizeof(abortive_close)) == SOCKET_ERROR)
              throw std::system_error(::WSAGetLastError(),
                                      std::system_category(), "SO_LINGER");
            if (::connect(candidate.get(),
                          reinterpret_cast<const sockaddr *>(&address),
                          address_size) != SOCKET_ERROR) {
              client = std::move(candidate);
              connect_error = ERROR_SUCCESS;
              break;
            }
            connect_error = ::WSAGetLastError();
            if (connect_error != WSAEADDRINUSE &&
                connect_error != WSAEADDRNOTAVAIL)
              break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
          if (connect_error != ERROR_SUCCESS)
            throw std::system_error(connect_error, std::system_category(),
                                    "connect(load client)");
          std::size_t sent = 0;
          while (sent != payload.size()) {
            const int amount = ::send(
                client.get(), payload.data() + sent,
                static_cast<int>(payload.size() - sent), 0);
            if (amount <= 0)
              throw std::system_error(::WSAGetLastError(),
                                      std::system_category(),
                                      "send(load payload)");
            sent += static_cast<std::size_t>(amount);
          }
          char acknowledgement = 0;
          if (::recv(client.get(), &acknowledgement, 1, 0) != 1 ||
              acknowledgement != 'A')
            throw std::runtime_error("load acknowledgement was not received");
          latencies[index] =
              std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now() - connection_started);
        }
      } catch (...) {
        capture_failure(std::current_exception());
      }
    });
  }
  for (auto &worker : workers)
    worker.join();
  receiver.join();
  if (failure)
    std::rethrow_exception(failure);
  std::sort(latencies.begin(), latencies.end());
  return {std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - started),
          latencies[(latencies.size() - 1) / 2],
          latencies[((latencies.size() - 1) * 95) / 100], latencies.back()};
}

struct options {
  std::filesystem::path controller = fixture::sibling_executable(
      L"crtsys_wfp_flow_monitor_controller.exe");
  std::size_t flows = 1;
  std::size_t concurrency = 1;
};

options parse_options(int argc, wchar_t **argv) {
  options result;
  for (int index = 1; index < argc; ++index) {
    const std::wstring_view option(argv[index]);
    if (option == L"--controller" && index + 1 < argc) {
      result.controller = std::filesystem::absolute(argv[++index]);
    } else if (option == L"--load-test" && index + 2 < argc) {
      result.flows = static_cast<std::size_t>(std::stoull(argv[++index]));
      result.concurrency =
          static_cast<std::size_t>(std::stoull(argv[++index]));
    } else {
      throw std::invalid_argument(
          "usage: acceptance [--controller <path>] "
          "[--load-test <flows-per-family> <concurrency>]");
    }
  }
  if (result.flows == 0 || result.flows > 100'000 ||
      result.concurrency == 0 || result.concurrency > 1024 ||
      result.concurrency > result.flows)
    throw std::invalid_argument("invalid flow-monitor load dimensions");
  return result;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    const auto options = parse_options(argc, argv);
    winsock_session winsock;
    auto server_v4 = make_listener(AF_INET);
    auto server_v6 = make_listener(AF_INET6);
    fixture::state_directory state(L"kernel-flow-monitor");
    fixture::controller_process controller(
        options.controller, state.path(),
        {{L"--ipv4-port", std::to_wstring(server_v4.port)},
         {L"--ipv6-port", std::to_wstring(server_v6.port)}},
        180'000);
    controller.wait_ready();

    const std::string payload(192, 'M');
    const auto load_v4 = exchange_load(server_v4, payload, options.flows,
                                       options.concurrency);
    const auto load_v6 = exchange_load(server_v6, payload, options.flows,
                                       options.concurrency);
    controller.request_stop();
    controller.wait(30'000);

    const auto stats = fixture::read_stats(controller.stats_file());
    const auto expected_flows = options.flows * 2;
    const auto expected_bytes = payload.size() * expected_flows;
    if (fixture::require_stat(stats, "policy.ipv4_port") != server_v4.port ||
        fixture::require_stat(stats, "policy.ipv6_port") != server_v6.port ||
        fixture::require_stat(stats, "after.flows_started") <
            fixture::require_stat(stats, "before.flows_started") +
                expected_flows ||
        fixture::require_stat(stats, "after.stream_indications") <=
            fixture::require_stat(stats, "before.stream_indications") ||
        fixture::require_stat(stats, "after.stream_bytes") <
            fixture::require_stat(stats, "before.stream_bytes") +
                expected_bytes)
      throw std::runtime_error("flow-monitor telemetry is incomplete");

    const auto elapsed = load_v4.elapsed + load_v6.elapsed;
    std::wcout << L"Kernel flow-monitor acceptance PASS: flows="
               << expected_flows << L", bytes=" << expected_bytes
               << L", concurrency=" << options.concurrency
               << L", elapsed-ms=" << elapsed.count()
               << L", ipv4-p95-us=" << load_v4.p95.count()
               << L", ipv6-p95-us=" << load_v6.p95.count() << L".\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Kernel flow-monitor acceptance failed: " << error.what()
              << '\n';
    return 1;
  }
}
