#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <ntl/wfp/all>

#include "async_inspection_contract.hpp"

namespace {

using layer = wfp_async_inspection::layer;

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
      listen(socket.get(), 4) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "bind/listen");

  int size = sizeof(address);
  if (getsockname(socket.get(), reinterpret_cast<sockaddr *>(&address),
                  &size) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "getsockname");
  return {std::move(socket), ntohs(address.sin_port)};
}

struct connect_result {
  int error;
  std::chrono::milliseconds elapsed;
};

connect_result connect_once(std::uint16_t port) {
  socket_owner client(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (client.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "socket(client)");

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  const auto start = std::chrono::steady_clock::now();
  const int result =
      connect(client.get(), reinterpret_cast<const sockaddr *>(&address),
              sizeof(address));
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  return {result == 0 ? ERROR_SUCCESS : WSAGetLastError(), elapsed};
}

void install_filter(ntl::wfp::policy_transaction &transaction,
                    const ntl::wfp::sublayer_ref &sublayer,
                    const ntl::wfp::callout_ref<layer> &callout,
                    ntl::wfp::filter_key<layer> key,
                    const wchar_t *name, std::uint16_t port,
                    std::uint64_t context) {
  ntl::wfp::filter_builder<layer> filter(key, name);
  filter.protocol_equal(IPPROTO_TCP)
      .remote_port_equal(port)
      .context(context);
  transaction.add_filter(sublayer, callout, filter);
}

void install_policy(ntl::wfp::dynamic_session &session,
                    std::uint16_t permit_port,
                    std::uint16_t block_port) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_async_inspection::provider_key,
         L"crtsys NTL WFP async-inspection provider",
         L"Dynamic ALE authorization inspection provider"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {wfp_async_inspection::sublayer_key,
         L"crtsys NTL WFP async-inspection sublayer",
         L"Contains permit and block authorization rules", 0x7300});
    const auto callout = transaction.add_callout<layer>(
        provider,
        {wfp_async_inspection::callout_key,
         L"Pend selected TCP authorizations",
         L"Completes initial ALE authorization on a worker thread"});

    install_filter(
        transaction, sublayer, callout,
        wfp_async_inspection::permit_filter_key,
        L"Asynchronously permit the selected TCP port", permit_port,
        wfp_async_inspection::permit_context);
    install_filter(
        transaction, sublayer, callout,
        wfp_async_inspection::block_filter_key,
        L"Asynchronously block the selected TCP port", block_port,
        wfp_async_inspection::block_context);
  });
}

} // namespace

int wmain() {
  try {
    winsock_session winsock;
    auto permitted = make_listener();
    auto blocked = make_listener();

    std::wcout << L"[1/5] Permit port: " << permitted.port
               << L", block port: " << blocked.port << L".\n";
    {
      std::wcout << L"[2/5] Installing two dynamic ALE authorization "
                    L"rules.\n";
      ntl::wfp::dynamic_session policy(
          L"crtsys ntl::wfp async-inspection sample");
      install_policy(policy, permitted.port, blocked.port);

      std::wcout << L"[3/5] Connecting to the permit port through the "
                    L"worker-thread decision.\n";
      const auto allowed = connect_once(permitted.port);
      if (allowed.error != ERROR_SUCCESS ||
          allowed.elapsed < std::chrono::milliseconds(50)) {
        std::wcerr << L"Async permit was not observed: error="
                   << allowed.error << L", elapsed="
                   << allowed.elapsed.count() << L" ms\n";
        return 2;
      }

      std::wcout << L"[4/5] Connecting to the block port through the "
                    L"same pend/reauthorize path.\n";
      const auto denied = connect_once(blocked.port);
      if (denied.error != WSAEACCES ||
          denied.elapsed < std::chrono::milliseconds(50)) {
        std::wcerr << L"Async block was not observed: error="
                   << denied.error << L", elapsed="
                   << denied.elapsed.count() << L" ms\n";
        return 3;
      }
    }

    std::wcout << L"[5/5] Policy removed; the formerly blocked port must "
                  L"connect now.\n";
    const auto restored = connect_once(blocked.port);
    if (restored.error != ERROR_SUCCESS) {
      std::wcerr << L"Connection did not recover: " << restored.error
                 << L'\n';
      return 4;
    }

    std::wcout << L"NTL WFP async-inspection ok: permit=delayed-success, "
                  L"block=delayed-10013, restored=success\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NTL WFP async-inspection failed: " << error.what()
              << '\n';
    return 1;
  }
}
