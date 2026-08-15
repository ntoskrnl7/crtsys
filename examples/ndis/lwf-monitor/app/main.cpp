#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winioctl.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <thread>

#include "lwf_monitor_contract.hpp"

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

class socket_owner {
public:
  explicit socket_owner(SOCKET value = INVALID_SOCKET) noexcept
      : value_(value) {}
  socket_owner(const socket_owner &) = delete;
  socket_owner &operator=(const socket_owner &) = delete;
  ~socket_owner() {
    if (value_ != INVALID_SOCKET)
      closesocket(value_);
  }
  SOCKET get() const noexcept { return value_; }

private:
  SOCKET value_ = INVALID_SOCKET;
};

class winsock_session {
public:
  winsock_session() {
    WSADATA data{};
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0)
      throw std::system_error(result, std::system_category(),
                              "WSAStartup");
  }
  ~winsock_session() { WSACleanup(); }
};

class icmp_handle_owner {
public:
  explicit icmp_handle_owner(HANDLE value = INVALID_HANDLE_VALUE) noexcept
      : value_(value) {}
  icmp_handle_owner(const icmp_handle_owner &) = delete;
  icmp_handle_owner &operator=(const icmp_handle_owner &) = delete;
  ~icmp_handle_owner() {
    if (value_ != INVALID_HANDLE_VALUE)
      IcmpCloseHandle(value_);
  }
  HANDLE get() const noexcept { return value_; }

private:
  HANDLE value_ = INVALID_HANDLE_VALUE;
};

ndis_lwf_monitor::monitor_stats query_stats(HANDLE device) {
  ndis_lwf_monitor::monitor_stats stats{};
  DWORD bytes = 0;
  if (!DeviceIoControl(
          device, ndis_lwf_monitor::query_stats_ioctl, nullptr, 0,
          &stats, sizeof(stats), &bytes, nullptr) ||
      bytes != sizeof(stats))
    throw std::system_error(GetLastError(), std::system_category(),
                            "DeviceIoControl(query stats)");
  return stats;
}

void generate_outbound_traffic() {
  socket_owner socket(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
  if (socket.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "socket");
  sockaddr_in destination{};
  destination.sin_family = AF_INET;
  destination.sin_port = htons(9);
  if (InetPtonW(AF_INET, L"192.0.2.1", &destination.sin_addr) != 1)
    throw std::runtime_error("InetPtonW failed");
  constexpr std::array<char, 96> payload{};
  for (unsigned int attempt = 0; attempt != 8; ++attempt) {
    const int result = sendto(
        socket.get(), payload.data(), static_cast<int>(payload.size()), 0,
        reinterpret_cast<const sockaddr *>(&destination),
        sizeof(destination));
    if (result == SOCKET_ERROR) {
      const int error = WSAGetLastError();
      if (error != WSAEHOSTUNREACH && error != WSAENETUNREACH)
        throw std::system_error(error, std::system_category(), "sendto");
    }
  }
}

void generate_receive_traffic() {
  in_addr route_destination{};
  if (InetPtonW(AF_INET, L"192.0.2.1", &route_destination) != 1)
    throw std::runtime_error("InetPtonW(receive route probe) failed");

  MIB_IPFORWARDROW route{};
  const DWORD route_result =
      GetBestRoute(route_destination.s_addr, INADDR_ANY, &route);
  if (route_result != NO_ERROR)
    throw std::system_error(static_cast<int>(route_result),
                            std::system_category(),
                            "GetBestRoute(receive probe)");
  if (route.dwForwardNextHop == htonl(INADDR_ANY))
    throw std::runtime_error(
        "the selected route has no external IPv4 next hop");

  icmp_handle_owner icmp(IcmpCreateFile());
  if (icmp.get() == INVALID_HANDLE_VALUE)
    throw std::system_error(GetLastError(), std::system_category(),
                            "IcmpCreateFile");

  std::array<char, 128> payload{};
  using reply_buffer =
      std::array<std::byte,
                 sizeof(ICMP_ECHO_REPLY) + payload.size() + 8>;
  alignas(ICMP_ECHO_REPLY) reply_buffer reply{};
  for (unsigned int attempt = 0; attempt != 4; ++attempt) {
    const DWORD replies = IcmpSendEcho(
        icmp.get(), route.dwForwardNextHop, payload.data(),
        static_cast<WORD>(payload.size()), nullptr, reply.data(),
        static_cast<DWORD>(reply.size()), 3000);
    if (replies == 0)
      throw std::system_error(GetLastError(), std::system_category(),
                              "IcmpSendEcho(default gateway)");
    const auto *echo = reinterpret_cast<const ICMP_ECHO_REPLY *>(reply.data());
    if (echo->Status != IP_SUCCESS)
      throw std::system_error(static_cast<int>(echo->Status),
                              std::system_category(),
                              "ICMP echo reply(default gateway)");
  }
}

} // namespace

int wmain() {
  try {
    winsock_session winsock;
    handle_owner device(CreateFileW(
        ndis_lwf_monitor::user_device_path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
        nullptr));
    if (device.get() == INVALID_HANDLE_VALUE)
      throw std::system_error(GetLastError(), std::system_category(),
                              "CreateFile(NDIS LWF monitor)");

    const auto before = query_stats(device.get());
    if (before.modules_attached == 0 || before.restarts == 0)
      throw std::runtime_error(
          "the LWF is registered but is not attached and running");
    if (before.oid_requests == 0)
      throw std::runtime_error(
          "the LWF did not observe adapter OID initialization");

    generate_receive_traffic();
    generate_outbound_traffic();
    ndis_lwf_monitor::monitor_stats after{};
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
      after = query_stats(device.get());
      if (after.send_lists > before.send_lists &&
          after.send_bytes > before.send_bytes &&
          after.send_completions > before.send_completions &&
          after.receive_lists > before.receive_lists &&
          after.receive_bytes > before.receive_bytes &&
          after.metadata_preserved > before.metadata_preserved &&
          after.metadata_restored > before.metadata_restored)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    } while (std::chrono::steady_clock::now() < deadline);

    if (after.send_lists <= before.send_lists ||
        after.send_bytes <= before.send_bytes)
      throw std::runtime_error(
          "outbound NBL traffic was not observed by the LWF");
    if (after.send_completions <= before.send_completions)
      throw std::runtime_error(
          "outbound NBL completion was not observed by the LWF");
    if (after.receive_lists <= before.receive_lists ||
        after.receive_bytes <= before.receive_bytes)
      throw std::runtime_error(
          "inbound NBL traffic was not observed by the LWF");
    if (after.metadata_preserved <= before.metadata_preserved ||
        after.metadata_restored <= before.metadata_restored)
      throw std::runtime_error(
          "NBL metadata preservation/restoration was not observed");

    std::wcout
        << L"NTL NDIS LWF monitor ok: attached="
        << after.modules_attached << L", restarts=" << after.restarts
        << L", pauses=" << after.pauses
        << L", sends=" << (after.send_lists - before.send_lists)
        << L", send-bytes=" << (after.send_bytes - before.send_bytes)
        << L", receives=" << (after.receive_lists - before.receive_lists)
        << L", checksum-metadata="
        << (after.checksum_metadata - before.checksum_metadata)
        << L", lso="
        << (after.large_send_metadata - before.large_send_metadata)
        << L", rsc="
        << (after.receive_coalescing_metadata -
            before.receive_coalescing_metadata)
        << L", vlan=" << (after.vlan_metadata - before.vlan_metadata)
        << L", hash="
        << (after.receive_hash_metadata - before.receive_hash_metadata)
        << L", metadata-preserved="
        << (after.metadata_preserved - before.metadata_preserved)
        << L", metadata-restored="
        << (after.metadata_restored - before.metadata_restored)
        << L", oid-requests=" << after.oid_requests
        << L", oid-completions=" << after.oid_completions
        << L", oid-cancellations=" << after.oid_cancellations
        << L", direct-oid-requests=" << after.direct_oid_requests
        << L", direct-oid-completions=" << after.direct_oid_completions
        << L", direct-oid-cancellations="
        << after.direct_oid_cancellations
        << L", status-indications=" << after.status_indications
        << L", device-pnp-events=" << after.device_pnp_events
        << L", net-pnp-events=" << after.net_pnp_events
        << L", immediate-receive-returns="
        << after.immediate_receive_returns
        << L'\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NTL NDIS LWF monitor failed: " << error.what() << '\n';
    return 1;
  }
}
