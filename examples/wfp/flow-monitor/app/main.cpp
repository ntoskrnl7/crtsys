#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winioctl.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

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
};

listener make_listener() {
  socket_owner socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (socket.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "socket(listener)");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (bind(socket.get(), reinterpret_cast<const sockaddr *>(&address),
           sizeof(address)) == SOCKET_ERROR ||
      listen(socket.get(), 2) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "bind/listen");
  int size = sizeof(address);
  if (getsockname(socket.get(), reinterpret_cast<sockaddr *>(&address),
                  &size) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "getsockname");
  return {std::move(socket), ntohs(address.sin_port)};
}

wfp_flow_monitor::monitor_stats query_stats(HANDLE device) {
  wfp_flow_monitor::monitor_stats stats{};
  DWORD bytes = 0;
  if (!DeviceIoControl(
          device, wfp_flow_monitor::query_stats_ioctl, nullptr, 0,
          &stats, sizeof(stats), &bytes, nullptr) ||
      bytes != sizeof(stats))
    throw std::system_error(GetLastError(), std::system_category(),
                            "DeviceIoControl(query stats)");
  return stats;
}

void exchange_tcp(const listener &server, const std::string &payload) {
  socket_owner client(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (client.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "socket(client)");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(server.port);
  if (connect(client.get(), reinterpret_cast<const sockaddr *>(&address),
              sizeof(address)) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "connect");

  std::exception_ptr server_error;
  std::thread receiver([&] {
    try {
      socket_owner accepted(
          accept(server.socket.get(), nullptr, nullptr));
      if (accepted.get() == INVALID_SOCKET)
        throw std::system_error(WSAGetLastError(), std::system_category(),
                                "accept");
      std::array<char, 512> buffer{};
      std::size_t total = 0;
      while (total < payload.size()) {
        const int received =
            recv(accepted.get(), buffer.data(),
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
    const int result = send(
        client.get(), payload.data() + sent,
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

void install_policy(ntl::wfp::dynamic_session &session,
                    std::uint16_t port) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_flow_monitor::provider_key,
         L"crtsys NTL WFP flow-monitor provider",
         L"Dynamic provider for selected outbound TCP flow telemetry"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {wfp_flow_monitor::sublayer_key,
         L"crtsys NTL WFP flow-monitor sublayer",
         L"Observation-only flow and stream filters", 0x7400});
    const auto flow_callout =
        transaction.add_callout<wfp_flow_monitor::flow_layer>(
            provider,
            {wfp_flow_monitor::flow_callout_key,
             L"Attach TCP monitor flow state",
             L"Associates typed context at ALE_FLOW_ESTABLISHED_V4"});
    const auto stream_callout =
        transaction.add_callout<wfp_flow_monitor::stream_layer>(
            provider,
            {wfp_flow_monitor::stream_callout_key,
             L"Observe TCP stream bytes",
             L"Counts stream indications without changing traffic"});

    ntl::wfp::inspection_filter_builder<
        wfp_flow_monitor::flow_layer>
        flow_filter(wfp_flow_monitor::flow_filter_key,
                    L"Observe the selected outbound TCP flow");
    flow_filter.protocol_equal(IPPROTO_TCP)
        .direction_equal(FWP_DIRECTION_OUTBOUND)
        .remote_port_equal(port);
    transaction.add_inspection_filter(
        sublayer, flow_callout, flow_filter);

    ntl::wfp::inspection_filter_builder<
        wfp_flow_monitor::stream_layer>
        stream_filter(wfp_flow_monitor::stream_filter_key,
                      L"Count bytes on the selected TCP stream");
    stream_filter.remote_port_equal(port);
    transaction.add_inspection_filter(
        sublayer, stream_callout, stream_filter);
  });
}

} // namespace

int wmain() {
  try {
    winsock_session winsock;
    handle_owner device(CreateFileW(
        wfp_flow_monitor::user_device_path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
        nullptr));
    if (device.get() == INVALID_HANDLE_VALUE)
      throw std::system_error(GetLastError(), std::system_category(),
                              "CreateFile(flow monitor)");

    auto server = make_listener();
    const std::string payload(192, 'M');
    const auto before = query_stats(device.get());

    std::wcout << L"[1/5] Monitoring outbound TCP port " << server.port
               << L".\n";
    {
      std::wcout << L"[2/5] Installing observation-only flow and stream "
                    L"filters.\n";
      ntl::wfp::dynamic_session policy(
          L"crtsys ntl::wfp flow-monitor sample");
      install_policy(policy, server.port);

      std::wcout << L"[3/5] Sending " << payload.size()
                 << L" TCP bytes.\n";
      exchange_tcp(server, payload);

      std::wcout << L"[4/5] Reading driver telemetry through the typed "
                    L"IOCTL.\n";
      wfp_flow_monitor::monitor_stats current{};
      const auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::seconds(3);
      do {
        current = query_stats(device.get());
        if (current.flows_started > before.flows_started &&
            current.stream_indications > before.stream_indications &&
            current.stream_bytes >= before.stream_bytes + payload.size())
          break;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      } while (std::chrono::steady_clock::now() < deadline);

      if (current.flows_started <= before.flows_started ||
          current.stream_indications <= before.stream_indications ||
          current.stream_bytes < before.stream_bytes + payload.size()) {
        std::wcerr << L"Expected flow telemetry was not observed.\n";
        return 2;
      }
    }

    std::wcout << L"[5/5] Policy removed; traffic was never modified.\n";
    const auto after = query_stats(device.get());
    std::wcout << L"NTL WFP flow-monitor ok: flows="
               << (after.flows_started - before.flows_started)
               << L", indications="
               << (after.stream_indications - before.stream_indications)
               << L", bytes=" << (after.stream_bytes - before.stream_bytes)
               << L", missed="
               << (after.missed_bytes - before.missed_bytes) << L'\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NTL WFP flow-monitor failed: " << error.what()
              << '\n';
    return 1;
  }
}
