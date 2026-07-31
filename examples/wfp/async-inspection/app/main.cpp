#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <ntl/wfp/all>

#include "async_inspection_contract.hpp"

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
      listen(socket.get(), 4) == SOCKET_ERROR)
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

struct connect_result {
  int error;
  std::chrono::milliseconds elapsed;
};

connect_result connect_once(std::uint16_t port, int family) {
  socket_owner client(::socket(family, SOCK_STREAM, IPPROTO_TCP));
  if (client.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "socket(client)");

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
  const auto start = std::chrono::steady_clock::now();
  const int result = connect(
      client.get(), reinterpret_cast<const sockaddr *>(&storage), address_size);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  return {result == 0 ? ERROR_SUCCESS : WSAGetLastError(), elapsed};
}

template <class Layer>
void install_filter(ntl::wfp::policy_transaction &transaction,
                    const ntl::wfp::sublayer_ref &sublayer,
                    const ntl::wfp::callout_ref<Layer> &callout,
                    ntl::wfp::filter_key<Layer> key, const wchar_t *name,
                    std::uint16_t port, std::uint64_t context) {
  ntl::wfp::filter_builder<Layer> filter(
      key, name, ntl::wfp::callout_unavailable::block);
  filter.protocol_equal(IPPROTO_TCP).remote_port_equal(port).context(context);
  transaction.add_filter(sublayer, callout, filter);
}

void install_policy(ntl::wfp::policy_session &session,
                    const listener &permit_v4, const listener &block_v4,
                    const listener &permit_v6, const listener &block_v6) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_async_inspection::provider_key,
         L"crtsys NTL WFP async-inspection provider",
         L"Dynamic ALE authorization inspection provider"});
    const auto sublayer = transaction.add_sublayer(
        provider, {wfp_async_inspection::sublayer_key,
                   L"crtsys NTL WFP async-inspection sublayer",
                   L"Contains permit and block authorization rules", 0x7300});
    const auto callout_v4 =
        transaction.add_callout<wfp_async_inspection::layer_v4>(
            provider,
            {wfp_async_inspection::callout_key_v4,
             L"Pend selected IPv4 TCP authorizations",
             L"Completes initial ALE authorization on a worker thread"});
    const auto callout_v6 =
        transaction.add_callout<wfp_async_inspection::layer_v6>(
            provider,
            {wfp_async_inspection::callout_key_v6,
             L"Pend selected IPv6 TCP authorizations",
             L"Completes initial ALE authorization on a worker thread"});

    install_filter(transaction, sublayer, callout_v4,
                   wfp_async_inspection::permit_filter_key_v4,
                   L"Asynchronously permit the selected IPv4 TCP port",
                   permit_v4.port, wfp_async_inspection::permit_context);
    install_filter(transaction, sublayer, callout_v4,
                   wfp_async_inspection::block_filter_key_v4,
                   L"Asynchronously block the selected IPv4 TCP port",
                   block_v4.port, wfp_async_inspection::block_context);
    install_filter(transaction, sublayer, callout_v6,
                   wfp_async_inspection::permit_filter_key_v6,
                   L"Asynchronously permit the selected IPv6 TCP port",
                   permit_v6.port, wfp_async_inspection::permit_context);
    install_filter(transaction, sublayer, callout_v6,
                   wfp_async_inspection::block_filter_key_v6,
                   L"Asynchronously block the selected IPv6 TCP port",
                   block_v6.port, wfp_async_inspection::block_context);
  });
}

int run_unload_race(
    const listener &permitted_v4, const listener &blocked_v4,
    const listener &permitted_v6, const listener &blocked_v6,
    std::size_t connection_count) {
  if (connection_count == 0 || connection_count > 4096)
    throw std::invalid_argument(
        "unload-race connection count must be 1..4096");
  auto policy = ntl::wfp::policy_session::ephemeral(
      L"crtsys ntl::wfp async-inspection unload race");
  install_policy(
      policy, permitted_v4, blocked_v4, permitted_v6, blocked_v6);

  std::atomic<bool> start{false};
  std::atomic<std::size_t> ready{0};
  std::atomic<std::size_t> denied{0};
  std::atomic<std::size_t> unexpected{0};
  std::vector<std::thread> workers;
  workers.reserve(connection_count);
  for (std::size_t index = 0; index != connection_count; ++index) {
    workers.emplace_back([&, index] {
      ready.fetch_add(1, std::memory_order_release);
      while (!start.load(std::memory_order_acquire))
        std::this_thread::yield();
      const auto result =
          (index & 1) == 0
              ? connect_once(blocked_v4.port, AF_INET)
              : connect_once(blocked_v6.port, AF_INET6);
      if (result.error == WSAEACCES)
        denied.fetch_add(1, std::memory_order_relaxed);
      else
        unexpected.fetch_add(1, std::memory_order_relaxed);
    });
  }
  while (ready.load(std::memory_order_acquire) != connection_count)
    std::this_thread::yield();
  start.store(true, std::memory_order_release);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  std::wcout << L"NTL WFP async-inspection unload-race ready: pending="
             << connection_count << std::endl;
  for (auto &worker : workers)
    worker.join();
  if (unexpected.load(std::memory_order_relaxed) != 0 ||
      denied.load(std::memory_order_relaxed) != connection_count)
    throw std::runtime_error(
        "unload-race connections did not remain fail-closed");
  std::wcout
      << L"NTL WFP async-inspection unload-race ok: connections="
      << connection_count
      << L", denied=" << denied.load(std::memory_order_relaxed)
      << L", drain=bounded\n";
  return 0;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    const bool unload_race =
        argc > 1 && std::wstring_view(argv[1]) == L"--unload-race";
    const std::size_t unload_race_connections =
        unload_race && argc > 2
            ? static_cast<std::size_t>(std::stoull(argv[2]))
            : 512;
    winsock_session winsock;
    auto permitted_v4 = make_listener(AF_INET);
    auto blocked_v4 = make_listener(AF_INET);
    auto permitted_v6 = make_listener(AF_INET6);
    auto blocked_v6 = make_listener(AF_INET6);

    if (unload_race)
      return run_unload_race(
          permitted_v4, blocked_v4, permitted_v6, blocked_v6,
          unload_race_connections);

    std::wcout << L"[1/5] IPv4 permit/block ports: " << permitted_v4.port
               << L"/" << blocked_v4.port << L"; IPv6 permit/block ports: "
               << permitted_v6.port << L"/" << blocked_v6.port << L".\n";
    {
      std::wcout << L"[2/5] Installing two dynamic ALE authorization "
                    L"rules.\n";
      auto policy = ntl::wfp::policy_session::ephemeral(
          L"crtsys ntl::wfp async-inspection sample");
      install_policy(policy, permitted_v4, blocked_v4, permitted_v6,
                     blocked_v6);

      std::wcout << L"[3/5] Connecting to the permit port through the "
                    L"worker-thread decision.\n";
      const auto allowed_v4 = connect_once(permitted_v4.port, AF_INET);
      const auto allowed_v6 = connect_once(permitted_v6.port, AF_INET6);
      if (allowed_v4.error != ERROR_SUCCESS ||
          allowed_v6.error != ERROR_SUCCESS ||
          allowed_v4.elapsed < std::chrono::milliseconds(50) ||
          allowed_v6.elapsed < std::chrono::milliseconds(50)) {
        std::wcerr << L"Async permit was not observed: error="
                   << allowed_v4.error << L"/" << allowed_v6.error
                   << L", elapsed=" << allowed_v4.elapsed.count() << L"/"
                   << allowed_v6.elapsed.count() << L" ms\n";
        return 2;
      }

      std::wcout << L"[4/5] Connecting to the block port through the "
                    L"same pend/reauthorize path.\n";
      const auto denied_v4 = connect_once(blocked_v4.port, AF_INET);
      const auto denied_v6 = connect_once(blocked_v6.port, AF_INET6);
      if (denied_v4.error != WSAEACCES || denied_v6.error != WSAEACCES ||
          denied_v4.elapsed < std::chrono::milliseconds(50) ||
          denied_v6.elapsed < std::chrono::milliseconds(50)) {
        std::wcerr << L"Async block was not observed: error=" << denied_v4.error
                   << L"/" << denied_v6.error << L", elapsed="
                   << denied_v4.elapsed.count() << L"/"
                   << denied_v6.elapsed.count() << L" ms\n";
        return 3;
      }
    }

    std::wcout << L"[5/5] Policy removed; the formerly blocked port must "
                  L"connect now.\n";
    const auto restored_v4 = connect_once(blocked_v4.port, AF_INET);
    const auto restored_v6 = connect_once(blocked_v6.port, AF_INET6);
    if (restored_v4.error != ERROR_SUCCESS ||
        restored_v6.error != ERROR_SUCCESS) {
      std::wcerr << L"Connection did not recover: " << restored_v4.error << L"/"
                 << restored_v6.error << L'\n';
      return 4;
    }

    std::wcout << L"NTL WFP async-inspection ok: permit=delayed-success, "
                  L"block=delayed-10013, restored=success, "
                  L"ipv4=pass, ipv6=pass\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NTL WFP async-inspection failed: " << error.what() << '\n';
    return 1;
  }
}
