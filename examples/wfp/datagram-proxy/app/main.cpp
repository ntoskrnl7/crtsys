#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <ntl/wfp/all>

#include "datagram_proxy_contract.hpp"

namespace {

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

struct udp_receiver {
  socket_owner socket;
  int family;
  std::uint16_t port;
};

udp_receiver make_receiver(int family, std::uint16_t requested_port = 0) {
  socket_owner socket(::socket(family, SOCK_DGRAM, IPPROTO_UDP));
  if (socket.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
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
    auto &address = reinterpret_cast<sockaddr_in6 &>(storage);
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = htons(requested_port);
    address_size = sizeof(address);
  }
  if (bind(socket.get(), reinterpret_cast<const sockaddr *>(&storage),
           address_size) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(), "bind");

  int size = sizeof(storage);
  if (getsockname(socket.get(), reinterpret_cast<sockaddr *>(&storage),
                  &size) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "getsockname");
  const auto port =
      family == AF_INET
          ? ntohs(reinterpret_cast<const sockaddr_in &>(storage).sin_port)
          : ntohs(reinterpret_cast<const sockaddr_in6 &>(storage).sin6_port);
  return {std::move(socket), family, port};
}

void send_datagram(int family, std::uint16_t port, const char *text) {
  socket_owner sender(::socket(family, SOCK_DGRAM, IPPROTO_UDP));
  if (sender.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
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
  if (connect(sender.get(), reinterpret_cast<const sockaddr *>(&storage),
              address_size) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "connect(udp)");

  const int length = static_cast<int>(std::strlen(text));
  if (send(sender.get(), text, length, 0) != length)
    throw std::system_error(WSAGetLastError(), std::system_category(), "send");
}

std::string receive_datagram(SOCKET socket, DWORD timeout_ms) {
  if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char *>(&timeout_ms),
                 sizeof(timeout_ms)) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "setsockopt(SO_RCVTIMEO)");

  std::array<char, 256> buffer{};
  const int received =
      recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
  if (received == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(), "recv");
  return std::string(buffer.data(), static_cast<std::size_t>(received));
}

bool has_no_datagram(SOCKET socket) {
  DWORD timeout_ms = 150;
  if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char *>(&timeout_ms),
                 sizeof(timeout_ms)) == SOCKET_ERROR)
    return false;
  char byte = 0;
  const int result = recv(socket, &byte, 1, 0);
  return result == SOCKET_ERROR && WSAGetLastError() == WSAETIMEDOUT;
}

void install_policy(ntl::wfp::policy_session &session,
                    std::uint16_t original_port, std::uint16_t proxy_port) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_datagram_proxy::provider_key,
         L"crtsys NTL WFP datagram-proxy provider",
         L"Dynamic provider for one outbound IPv4 UDP redirect"});
    const auto sublayer = transaction.add_sublayer(
        provider, {wfp_datagram_proxy::sublayer_key,
                   L"crtsys NTL WFP datagram-proxy sublayer",
                   L"Removed atomically when the controller closes", 0x7200});

    const auto flow_callout_v4 =
        transaction.add_callout<wfp_datagram_proxy::flow_layer_v4>(
            provider,
            {wfp_datagram_proxy::flow_callout_key_v4,
             L"Remember matching IPv4 UDP flows",
             L"Associates typed proxy state at ALE_FLOW_ESTABLISHED_V4"});
    const auto datagram_callout_v4 =
        transaction.add_callout<wfp_datagram_proxy::datagram_layer_v4>(
            provider,
            {wfp_datagram_proxy::datagram_callout_key_v4,
             L"Redirect matching IPv4 UDP datagrams",
             L"Clone, modify, inject, then absorb the original IPv4 datagram"});
    const auto flow_callout_v6 =
        transaction.add_callout<wfp_datagram_proxy::flow_layer_v6>(
            provider,
            {wfp_datagram_proxy::flow_callout_key_v6,
             L"Remember matching IPv6 UDP flows",
             L"Associates typed proxy state at ALE_FLOW_ESTABLISHED_V6"});
    const auto datagram_callout_v6 =
        transaction.add_callout<wfp_datagram_proxy::datagram_layer_v6>(
            provider,
            {wfp_datagram_proxy::datagram_callout_key_v6,
             L"Redirect matching IPv6 UDP datagrams",
             L"Clone, modify, inject, then absorb the original IPv6 datagram"});

    ntl::wfp::inspection_filter_builder<wfp_datagram_proxy::flow_layer_v4>
        flow_filter_v4(wfp_datagram_proxy::flow_filter_key_v4,
                       L"Remember the selected outbound IPv4 UDP flow");
    flow_filter_v4.protocol_equal(IPPROTO_UDP)
        .direction_equal(FWP_DIRECTION_OUTBOUND)
        .remote_port_equal(original_port)
        .context(proxy_port);
    transaction.add_inspection_filter(sublayer, flow_callout_v4,
                                      flow_filter_v4);

    ntl::wfp::packet_filter_builder<wfp_datagram_proxy::datagram_layer_v4>
        datagram_filter_v4(wfp_datagram_proxy::datagram_filter_key_v4,
                           L"Redirect the selected IPv4 UDP destination port",
                           ntl::wfp::callout_unavailable::permit);
    datagram_filter_v4.protocol_equal(IPPROTO_UDP)
        .direction_equal(FWP_DIRECTION_OUTBOUND)
        .remote_port_equal(original_port);
    transaction.add_packet_filter(sublayer, datagram_callout_v4,
                                  datagram_filter_v4);

    ntl::wfp::inspection_filter_builder<wfp_datagram_proxy::flow_layer_v6>
        flow_filter_v6(wfp_datagram_proxy::flow_filter_key_v6,
                       L"Remember the selected outbound IPv6 UDP flow");
    flow_filter_v6.protocol_equal(IPPROTO_UDP)
        .direction_equal(FWP_DIRECTION_OUTBOUND)
        .remote_port_equal(original_port)
        .context(proxy_port);
    transaction.add_inspection_filter(sublayer, flow_callout_v6,
                                      flow_filter_v6);

    ntl::wfp::packet_filter_builder<wfp_datagram_proxy::datagram_layer_v6>
        datagram_filter_v6(wfp_datagram_proxy::datagram_filter_key_v6,
                           L"Redirect the selected IPv6 UDP destination port",
                           ntl::wfp::callout_unavailable::permit);
    datagram_filter_v6.protocol_equal(IPPROTO_UDP)
        .direction_equal(FWP_DIRECTION_OUTBOUND)
        .remote_port_equal(original_port);
    transaction.add_packet_filter(sublayer, datagram_callout_v6,
                                  datagram_filter_v6);
  });
}

} // namespace

int wmain() {
  try {
    winsock_session winsock;
    auto original_v4 = make_receiver(AF_INET);
    auto proxy_v4 = make_receiver(AF_INET);
    auto original_v6 = make_receiver(AF_INET6, original_v4.port);
    auto proxy_v6 = make_receiver(AF_INET6, proxy_v4.port);
    constexpr char payload[] = "ntl-datagram-proxy";

    std::wcout << L"[1/5] Dual-stack original UDP port: " << original_v4.port
               << L", proxy UDP port: " << proxy_v4.port << L".\n";
    {
      std::wcout << L"[2/5] Installing flow and datagram callouts.\n";
      auto policy = ntl::wfp::policy_session::ephemeral(
          L"crtsys ntl::wfp datagram-proxy sample");
      install_policy(policy, original_v4.port, proxy_v4.port);

      std::wcout << L"[3/5] Sending to the original port. The proxy socket "
                    L"must receive it instead for IPv4 and IPv6.\n";
      send_datagram(AF_INET, original_v4.port, payload);
      send_datagram(AF_INET6, original_v6.port, payload);
      const auto redirected_v4 = receive_datagram(proxy_v4.socket.get(), 2000);
      const auto redirected_v6 = receive_datagram(proxy_v6.socket.get(), 2000);
      if (redirected_v4 != payload || redirected_v6 != payload ||
          !has_no_datagram(original_v4.socket.get()) ||
          !has_no_datagram(original_v6.socket.get())) {
        std::wcerr << L"UDP redirect was not exclusive.\n";
        return 3;
      }
      std::wcout << L"[4/5] Redirect observed; closing the ephemeral policy.\n";
    }

    std::wcout << L"[5/5] Sending again. The original socket must receive "
                  L"the datagram now for IPv4 and IPv6.\n";
    send_datagram(AF_INET, original_v4.port, payload);
    send_datagram(AF_INET6, original_v6.port, payload);
    const auto restored_v4 = receive_datagram(original_v4.socket.get(), 2000);
    const auto restored_v6 = receive_datagram(original_v6.socket.get(), 2000);
    if (restored_v4 != payload || restored_v6 != payload) {
      std::wcerr << L"UDP destination was not restored.\n";
      return 4;
    }

    std::wcout << L"NTL WFP datagram-proxy ok: redirected_port="
               << proxy_v4.port << L", restored_port=" << original_v4.port
               << L", ipv4=pass, ipv6=pass\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NTL WFP datagram-proxy failed: " << error.what() << '\n';
    return 1;
  }
}
