#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <ntl/wfp/all>

#include "flow_monitor_contract.hpp"

namespace {

class handle_owner {
public:
  explicit handle_owner(HANDLE value = INVALID_HANDLE_VALUE) noexcept
      : value_(value) {}
  handle_owner(const handle_owner &) = delete;
  handle_owner &operator=(const handle_owner &) = delete;
  ~handle_owner() {
    if (value_ != INVALID_HANDLE_VALUE)
      CloseHandle(value_);
  }
  HANDLE get() const noexcept { return value_; }

private:
  HANDLE value_ = INVALID_HANDLE_VALUE;
};

class winsock_session {
public:
  winsock_session() {
    WSADATA data{};
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0)
      throw std::system_error(result, std::system_category(), "WSAStartup");
  }
  ~winsock_session() { WSACleanup(); }
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
        closesocket(value_);
      value_ = std::exchange(other.value_, INVALID_SOCKET);
    }
    return *this;
  }
  ~socket_owner() {
    if (value_ != INVALID_SOCKET)
      closesocket(value_);
  }
  SOCKET get() const noexcept { return value_; }

private:
  SOCKET value_ = INVALID_SOCKET;
};

struct listener {
  socket_owner socket;
  std::uint16_t port;
  int family;
};

listener make_listener(int family) {
  socket_owner socket(::socket(family, SOCK_STREAM, IPPROTO_TCP));
  if (socket.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "socket(listener)");
  sockaddr_storage storage{};
  int address_size = 0;
  if (family == AF_INET) {
    auto &address = reinterpret_cast<sockaddr_in &>(storage);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address_size = sizeof(address);
  } else if (family == AF_INET6) {
    DWORD v6_only = 1;
    if (setsockopt(socket.get(), IPPROTO_IPV6, IPV6_V6ONLY,
                   reinterpret_cast<const char *>(&v6_only),
                   sizeof(v6_only)) == SOCKET_ERROR)
      throw std::system_error(WSAGetLastError(), std::system_category(),
                              "IPV6_V6ONLY");
    auto &address = reinterpret_cast<sockaddr_in6 &>(storage);
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address_size = sizeof(address);
  } else {
    throw std::invalid_argument("unsupported listener family");
  }
  if (bind(socket.get(), reinterpret_cast<const sockaddr *>(&storage),
           address_size) == SOCKET_ERROR ||
      listen(socket.get(), SOMAXCONN) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "bind/listen");
  int size = sizeof(storage);
  if (getsockname(socket.get(), reinterpret_cast<sockaddr *>(&storage),
                  &size) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "getsockname");
  const auto port =
      family == AF_INET
          ? ntohs(reinterpret_cast<const sockaddr_in &>(storage).sin_port)
          : ntohs(reinterpret_cast<const sockaddr_in6 &>(storage).sin6_port);
  return {std::move(socket), port, family};
}

wfp_flow_monitor::monitor_stats query_stats(HANDLE device) {
  wfp_flow_monitor::monitor_stats stats{};
  DWORD bytes = 0;
  if (!DeviceIoControl(device, wfp_flow_monitor::query_stats_ioctl, nullptr, 0,
                       &stats, sizeof(stats), &bytes, nullptr) ||
      bytes != sizeof(stats))
    throw std::system_error(GetLastError(), std::system_category(),
                            "DeviceIoControl(query stats)");
  return stats;
}

void exchange_tcp(const listener &server, const std::string &payload) {
  socket_owner client(::socket(server.family, SOCK_STREAM, IPPROTO_TCP));
  if (client.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "socket(client)");
  sockaddr_storage storage{};
  int address_size = 0;
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
  if (connect(client.get(), reinterpret_cast<const sockaddr *>(&storage),
              address_size) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "connect");

  std::exception_ptr server_error;
  std::thread receiver([&] {
    try {
      socket_owner accepted(accept(server.socket.get(), nullptr, nullptr));
      if (accepted.get() == INVALID_SOCKET)
        throw std::system_error(WSAGetLastError(), std::system_category(),
                                "accept");
      std::array<char, 512> buffer{};
      std::size_t total = 0;
      while (total < payload.size()) {
        const int received = recv(accepted.get(), buffer.data(),
                                  static_cast<int>(buffer.size()), 0);
        if (received <= 0)
          break;
        total += static_cast<std::size_t>(received);
      }
      if (total != payload.size())
        throw std::runtime_error("server received an unexpected byte count");
    } catch (...) {
      server_error = std::current_exception();
    }
  });

  std::size_t sent = 0;
  while (sent < payload.size()) {
    const int result = send(client.get(), payload.data() + sent,
                            static_cast<int>(payload.size() - sent), 0);
    if (result == SOCKET_ERROR) {
      const int error = WSAGetLastError();
      (void)shutdown(client.get(), SD_BOTH);
      receiver.join();
      throw std::system_error(error, std::system_category(), "send");
    }
    sent += static_cast<std::size_t>(result);
  }
  shutdown(client.get(), SD_SEND);
  receiver.join();
  if (server_error)
    std::rethrow_exception(server_error);
}

struct load_result {
  std::chrono::milliseconds elapsed{};
  std::chrono::microseconds p50{};
  std::chrono::microseconds p95{};
  std::chrono::microseconds maximum{};
};

load_result exchange_tcp_load(
    const listener &server, const std::string &payload,
    std::size_t connection_count, std::size_t concurrency) {
  if (connection_count == 0 || concurrency == 0 ||
      concurrency > connection_count)
    throw std::invalid_argument("invalid flow-monitor load dimensions");

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
        timeout.tv_usec = 100000;
        const int selected =
            select(0, &readable, nullptr, nullptr, &timeout);
        if (selected == SOCKET_ERROR)
          throw std::system_error(
              WSAGetLastError(), std::system_category(), "select");
        if (selected == 0)
          continue;

        socket_owner accepted(
            accept(server.socket.get(), nullptr, nullptr));
        if (accepted.get() == INVALID_SOCKET)
          throw std::system_error(
              WSAGetLastError(), std::system_category(), "accept");
        std::array<char, 512> buffer{};
        std::size_t total = 0;
        while (total < payload.size()) {
          const int received = recv(
              accepted.get(), buffer.data(),
              static_cast<int>((std::min)(
                  buffer.size(), payload.size() - total)),
              0);
          if (received <= 0)
            throw std::runtime_error(
                "load server received an incomplete payload");
          total += static_cast<std::size_t>(received);
        }
        const char acknowledgement = 'A';
        if (send(accepted.get(), &acknowledgement, 1, 0) != 1)
          throw std::system_error(
              WSAGetLastError(), std::system_category(),
              "load server acknowledgement");
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
          const auto connection_started =
              std::chrono::steady_clock::now();
          sockaddr_storage storage{};
          int address_size = 0;
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
          socket_owner client;
          int connect_error = WSAEADDRINUSE;
          for (unsigned attempt = 0; attempt != 256; ++attempt) {
            socket_owner candidate(
                ::socket(server.family, SOCK_STREAM, IPPROTO_TCP));
            if (candidate.get() == INVALID_SOCKET)
              throw std::system_error(
                  WSAGetLastError(), std::system_category(),
                  "socket(client)");
            // The load server acknowledges the complete payload before this
            // abortive close, so the test does not consume one TIME_WAIT
            // entry per synthetic flow.
            linger abortive_close{};
            abortive_close.l_onoff = 1;
            abortive_close.l_linger = 0;
            if (setsockopt(
                    candidate.get(), SOL_SOCKET, SO_LINGER,
                    reinterpret_cast<const char *>(&abortive_close),
                    sizeof(abortive_close)) == SOCKET_ERROR)
              throw std::system_error(
                  WSAGetLastError(), std::system_category(), "SO_LINGER");
            if (connect(
                    candidate.get(),
                    reinterpret_cast<const sockaddr *>(&storage),
                    address_size) != SOCKET_ERROR) {
              client = std::move(candidate);
              connect_error = ERROR_SUCCESS;
              break;
            }
            connect_error = WSAGetLastError();
            if (connect_error != WSAEADDRINUSE &&
                connect_error != WSAEADDRNOTAVAIL)
              break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
          if (connect_error != ERROR_SUCCESS)
            throw std::system_error(
                connect_error, std::system_category(), "connect");
          std::size_t sent = 0;
          while (sent != payload.size()) {
            const int result = send(
                client.get(), payload.data() + sent,
                static_cast<int>(payload.size() - sent), 0);
            if (result == SOCKET_ERROR)
              throw std::system_error(
                  WSAGetLastError(), std::system_category(), "send");
            sent += static_cast<std::size_t>(result);
          }
          char acknowledgement = 0;
          if (recv(client.get(), &acknowledgement, 1, 0) != 1 ||
              acknowledgement != 'A')
            throw std::runtime_error(
                "load client did not receive the server acknowledgement");
          latencies[index] =
              std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now() -
                  connection_started);
        }
      } catch (...) {
        capture_failure(std::current_exception());
      }
    });
  }
  for (auto &worker : workers)
    worker.join();
  receiver.join();
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started);
  if (failure)
    std::rethrow_exception(failure);

  std::sort(latencies.begin(), latencies.end());
  return {
      elapsed,
      latencies[(latencies.size() - 1) / 2],
      latencies[
          ((latencies.size() - 1) * 95) / 100],
      latencies.back(),
  };
}

void install_policy(ntl::wfp::policy_session &session,
                    const listener &server_v4, const listener &server_v6) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_flow_monitor::provider_key,
         L"crtsys NTL WFP flow-monitor provider",
         L"Dynamic provider for selected outbound TCP flow telemetry"});
    const auto sublayer = transaction.add_sublayer(
        provider, {wfp_flow_monitor::sublayer_key,
                   L"crtsys NTL WFP flow-monitor sublayer",
                   L"Observation-only flow and stream filters", 0x7400});
    const auto flow_callout_v4 =
        transaction.add_callout<wfp_flow_monitor::flow_layer_v4>(
            provider, {wfp_flow_monitor::flow_callout_key_v4,
                       L"Attach IPv4 TCP monitor flow state",
                       L"Associates typed context at ALE_FLOW_ESTABLISHED_V4"});
    const auto stream_callout_v4 =
        transaction.add_callout<wfp_flow_monitor::stream_layer_v4>(
            provider, {wfp_flow_monitor::stream_callout_key_v4,
                       L"Observe IPv4 TCP stream bytes",
                       L"Counts stream indications without changing traffic"});
    const auto flow_callout_v6 =
        transaction.add_callout<wfp_flow_monitor::flow_layer_v6>(
            provider, {wfp_flow_monitor::flow_callout_key_v6,
                       L"Attach IPv6 TCP monitor flow state",
                       L"Associates typed context at ALE_FLOW_ESTABLISHED_V6"});
    const auto stream_callout_v6 =
        transaction.add_callout<wfp_flow_monitor::stream_layer_v6>(
            provider, {wfp_flow_monitor::stream_callout_key_v6,
                       L"Observe IPv6 TCP stream bytes",
                       L"Counts stream indications without changing traffic"});

    ntl::wfp::inspection_filter_builder<wfp_flow_monitor::flow_layer_v4>
        flow_filter_v4(wfp_flow_monitor::flow_filter_key_v4,
                       L"Observe the selected outbound IPv4 TCP flow");
    flow_filter_v4.protocol_equal(IPPROTO_TCP)
        .direction_equal(FWP_DIRECTION_OUTBOUND)
        .remote_port_equal(server_v4.port);
    transaction.add_inspection_filter(sublayer, flow_callout_v4,
                                      flow_filter_v4);

    ntl::wfp::inspection_filter_builder<wfp_flow_monitor::stream_layer_v4>
        stream_filter_v4(wfp_flow_monitor::stream_filter_key_v4,
                         L"Count bytes on the selected IPv4 TCP stream");
    stream_filter_v4.remote_port_equal(server_v4.port);
    transaction.add_inspection_filter(sublayer, stream_callout_v4,
                                      stream_filter_v4);

    ntl::wfp::inspection_filter_builder<wfp_flow_monitor::flow_layer_v6>
        flow_filter_v6(wfp_flow_monitor::flow_filter_key_v6,
                       L"Observe the selected outbound IPv6 TCP flow");
    flow_filter_v6.protocol_equal(IPPROTO_TCP)
        .direction_equal(FWP_DIRECTION_OUTBOUND)
        .remote_port_equal(server_v6.port);
    transaction.add_inspection_filter(sublayer, flow_callout_v6,
                                      flow_filter_v6);

    ntl::wfp::inspection_filter_builder<wfp_flow_monitor::stream_layer_v6>
        stream_filter_v6(wfp_flow_monitor::stream_filter_key_v6,
                         L"Count bytes on the selected IPv6 TCP stream");
    stream_filter_v6.remote_port_equal(server_v6.port);
    transaction.add_inspection_filter(sublayer, stream_callout_v6,
                                      stream_filter_v6);
  });
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    const bool load_test =
        argc > 1 && std::wstring_view(argv[1]) == L"--load-self-test";
    const std::size_t connection_count =
        load_test && argc > 2
            ? static_cast<std::size_t>(std::stoull(argv[2]))
            : 1;
    const std::size_t concurrency =
        load_test && argc > 3
            ? static_cast<std::size_t>(std::stoull(argv[3]))
            : 1;
    if (connection_count == 0 || connection_count > 100000 ||
        concurrency == 0 || concurrency > 1024 ||
        concurrency > connection_count)
      throw std::invalid_argument(
          "load self-test expects 1..100000 flows and 1..1024 workers");

    winsock_session winsock;
    handle_owner device(CreateFileW(wfp_flow_monitor::user_device_path,
                                    GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, 0, nullptr));
    if (device.get() == INVALID_HANDLE_VALUE)
      throw std::system_error(GetLastError(), std::system_category(),
                              "CreateFile(flow monitor)");

    auto server_v4 = make_listener(AF_INET);
    auto server_v6 = make_listener(AF_INET6);
    const std::string payload(192, 'M');
    const auto before = query_stats(device.get());

    std::wcout << L"[1/5] Monitoring outbound IPv4/IPv6 TCP ports "
               << server_v4.port << L"/" << server_v6.port << L".\n";
    {
      std::wcout << L"[2/5] Installing observation-only flow and stream "
                    L"filters.\n";
      auto policy = ntl::wfp::policy_session::ephemeral(
          L"crtsys ntl::wfp flow-monitor sample");
      install_policy(policy, server_v4, server_v6);

      std::wcout << L"[3/5] Sending " << payload.size() << L" TCP bytes.\n";
      load_result load_v4{};
      load_result load_v6{};
      if (load_test) {
        load_v4 = exchange_tcp_load(
            server_v4, payload, connection_count, concurrency);
        load_v6 = exchange_tcp_load(
            server_v6, payload, connection_count, concurrency);
      } else {
        exchange_tcp(server_v4, payload);
        exchange_tcp(server_v6, payload);
      }

      std::wcout << L"[4/5] Reading driver telemetry through the typed "
                    L"IOCTL.\n";
      wfp_flow_monitor::monitor_stats current{};
      const auto deadline =
          std::chrono::steady_clock::now() +
          (load_test ? std::chrono::seconds(30)
                     : std::chrono::seconds(3));
      const auto expected_flows = connection_count * 2;
      const auto expected_bytes =
          payload.size() * expected_flows;
      do {
        current = query_stats(device.get());
        if (current.flows_started >=
                before.flows_started + expected_flows &&
            current.stream_indications > before.stream_indications &&
            current.stream_bytes >= before.stream_bytes + expected_bytes)
          break;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      } while (std::chrono::steady_clock::now() < deadline);

      if (current.flows_started <
              before.flows_started + expected_flows ||
          current.stream_indications <= before.stream_indications ||
          current.stream_bytes < before.stream_bytes + expected_bytes) {
        std::wcerr << L"Expected flow telemetry was not observed.\n";
        return 2;
      }
      if (load_test) {
        const auto elapsed = load_v4.elapsed + load_v6.elapsed;
        const auto flows_per_second =
            elapsed.count() == 0
                ? expected_flows * 1000
                : expected_flows * 1000 /
                      static_cast<std::size_t>(elapsed.count());
        std::wcout
            << L"NTL WFP flow-monitor load ok: flows="
            << expected_flows
            << L", concurrency=" << concurrency
            << L", elapsed-ms=" << elapsed.count()
            << L", flows-per-second=" << flows_per_second
            << L", ipv4-p95-us=" << load_v4.p95.count()
            << L", ipv6-p95-us=" << load_v6.p95.count()
            << L", ipv4-max-us=" << load_v4.maximum.count()
            << L", ipv6-max-us=" << load_v6.maximum.count()
            << L'\n';
      }
    }

    std::wcout << L"[5/5] Policy removed; traffic was never modified.\n";
    const auto after = query_stats(device.get());
    std::wcout << L"NTL WFP flow-monitor ok: flows="
               << (after.flows_started - before.flows_started)
               << L", indications="
               << (after.stream_indications - before.stream_indications)
               << L", bytes=" << (after.stream_bytes - before.stream_bytes)
               << L", missed=" << (after.missed_bytes - before.missed_bytes)
               << L", ipv4=pass, ipv6=pass\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NTL WFP flow-monitor failed: " << error.what() << '\n';
    return 1;
  }
}
