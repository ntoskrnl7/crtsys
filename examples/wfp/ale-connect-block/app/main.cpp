#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <ntl/wfp/all>

#include "ale_connect_block_contract.hpp"

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

socket_owner make_listener(std::uint16_t port) {
  socket_owner listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (listener.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "socket(listener)");

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (bind(listener.get(), reinterpret_cast<const sockaddr *>(&address),
           sizeof(address)) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(), "bind");
  if (listen(listener.get(), 4) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "listen");
  return listener;
}

int connect_once(std::uint16_t port) {
  socket_owner client(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (client.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "socket(client)");

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (connect(client.get(), reinterpret_cast<const sockaddr *>(&address),
              sizeof(address)) == 0)
    return ERROR_SUCCESS;
  return WSAGetLastError();
}

void install_policy(ntl::wfp::dynamic_session &session,
                    std::uint16_t port) {
  using layer = ntl::wfp::layers::ale_auth_connect_v4;

  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_ale_connect_block::provider_key,
         L"crtsys NTL WFP ALE connect-block provider",
         L"Dynamic provider for the ALE connect-block sample"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {wfp_ale_connect_block::sublayer_key,
         L"crtsys NTL WFP ALE connect-block sublayer",
         L"Dynamic sublayer removed with its engine session", 0x7100});
    const auto callout = transaction.add_callout<layer>(
        provider,
        {wfp_ale_connect_block::callout_key,
         L"crtsys NTL WFP ALE connect-block callout",
         L"Typed ALE_AUTH_CONNECT_V4 terminating callout"});

    ntl::wfp::filter_builder<layer> filter(
        wfp_ale_connect_block::filter_key,
        L"Block the selected loopback TCP port");
    filter.description(L"Observable ALE_AUTH_CONNECT_V4 enforcement")
        .protocol_equal(IPPROTO_TCP)
        .remote_port_equal(port);
    transaction.add_filter(sublayer, callout, filter);
  });
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    const auto port =
        argc > 1
            ? static_cast<std::uint16_t>(std::stoul(argv[1]))
            : wfp_ale_connect_block::default_port;

    std::wcout << L"[1/5] Starting a loopback TCP listener on port "
               << port << L".\n";
    winsock_session winsock;
    auto listener = make_listener(port);

    int blocked_error = ERROR_SUCCESS;
    {
      std::wcout
          << L"[2/5] Installing a dynamic WFP rule: outbound IPv4 TCP "
             L"connects to this port must call the kernel driver.\n";
      ntl::wfp::dynamic_session policy(
          L"crtsys ntl::wfp ALE connect-block sample");
      install_policy(policy, port);
      std::wcout
          << L"[3/5] Connecting while the rule exists. The driver should "
             L"return block.\n";
      blocked_error = connect_once(port);
      if (blocked_error == ERROR_SUCCESS) {
        std::wcerr << L"WFP policy did not block TCP port " << port << L'\n';
        return 2;
      }
      if (blocked_error != WSAEACCES) {
        std::wcerr << L"The connection failed, but not because WFP denied "
                      L"it. Winsock error="
                   << blocked_error << L'\n';
        return 4;
      }
      std::wcout << L"      Expected result: WSAEACCES (10013).\n"
                    L"[4/5] Closing the dynamic WFP session. Its provider, "
                    L"sublayer, callout, and filter are removed now.\n";
    }

    std::wcout << L"[5/5] Connecting again without the rule. This connection "
                  L"should succeed.\n";
    const int restored_error = connect_once(port);
    if (restored_error != ERROR_SUCCESS) {
      std::wcerr << L"TCP remained blocked after dynamic session close: "
                 << restored_error << L'\n';
      return 3;
    }

    std::wcout << L"NTL WFP ale-connect-block ok: blocked_error="
               << blocked_error
               << L", restored_connect=success, port=" << port << L'\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NTL WFP ale-connect-block failed: " << error.what()
              << '\n';
    return 1;
  }
}
