#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>

#include <ntl/net/io/async_socket>
#include <ntl/net/user/structured_concurrency>
#include <ntl/wfp/all>

#include "connect_redirect_contract.hpp"
#include "connect_redirect_policy.hpp"
#include "controller_lifecycle.hpp"

namespace {

class winsock_session {
public:
  winsock_session() {
    WSADATA data{};
    const int result = ::WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0)
      throw std::system_error(result, std::system_category(),
                              "WSAStartup");
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
      : value_(other.release()) {}
  socket_owner &operator=(socket_owner &&other) noexcept {
    if (this != &other) {
      reset();
      value_.store(other.release(), std::memory_order_release);
    }
    return *this;
  }
  ~socket_owner() { reset(); }
  SOCKET get() const noexcept {
    return value_.load(std::memory_order_acquire);
  }
  SOCKET release() noexcept {
    return value_.exchange(
        INVALID_SOCKET, std::memory_order_acq_rel);
  }
  void close() noexcept { reset(); }

private:
  void reset() noexcept {
    const SOCKET value = value_.exchange(
        INVALID_SOCKET, std::memory_order_acq_rel);
    if (value != INVALID_SOCKET)
      (void)::closesocket(value);
  }
  std::atomic<SOCKET> value_ = INVALID_SOCKET;
};

struct listener {
  socket_owner socket;
  std::uint16_t port = 0;
  int family = AF_UNSPEC;
};

[[noreturn]] void throw_socket(const char *operation) {
  throw std::system_error(::WSAGetLastError(),
                          std::system_category(), operation);
}

listener make_listener(int family) {
  socket_owner socket(::WSASocketW(
      family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
      WSA_FLAG_OVERLAPPED));
  if (socket.get() == INVALID_SOCKET)
    throw_socket("WSASocketW(listener)");

  std::uint16_t port = 0;
  if (family == AF_INET) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(
            socket.get(),
            reinterpret_cast<const sockaddr *>(&address),
            sizeof(address)) == SOCKET_ERROR)
      throw_socket("bind(IPv4 listener)");
    int size = sizeof(address);
    if (::getsockname(
            socket.get(),
            reinterpret_cast<sockaddr *>(&address),
            &size) == SOCKET_ERROR)
      throw_socket("getsockname(IPv4 listener)");
    port = ntohs(address.sin_port);
  } else if (family == AF_INET6) {
    DWORD v6_only = 1;
    if (::setsockopt(
            socket.get(), IPPROTO_IPV6, IPV6_V6ONLY,
            reinterpret_cast<const char *>(&v6_only),
            sizeof(v6_only)) == SOCKET_ERROR)
      throw_socket("setsockopt(IPV6_V6ONLY)");
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    if (::bind(
            socket.get(),
            reinterpret_cast<const sockaddr *>(&address),
            sizeof(address)) == SOCKET_ERROR)
      throw_socket("bind(IPv6 listener)");
    int size = sizeof(address);
    if (::getsockname(
            socket.get(),
            reinterpret_cast<sockaddr *>(&address),
            &size) == SOCKET_ERROR)
      throw_socket("getsockname(IPv6 listener)");
    port = ntohs(address.sin6_port);
  } else {
    throw std::invalid_argument("unsupported listener family");
  }
  if (::listen(socket.get(), 4) == SOCKET_ERROR)
    throw_socket("listen");
  return {std::move(socket), port, family};
}

ntl::net::user::task<std::uint64_t>
relay(ntl::net::async_socket &source, ntl::net::async_socket &destination) {
  std::array<std::byte, 64 * 1024> buffer{};
  std::uint64_t total = 0;
  for (;;) {
    const std::size_t read = co_await source.read_some_borrowed(buffer);
    if (read == 0) {
      if (::shutdown(destination.borrowed_native_handle(), SD_SEND) ==
              SOCKET_ERROR &&
          ::WSAGetLastError() != WSAENOTCONN)
        throw_socket("shutdown(relay destination)");
      co_return total;
    }
    const std::size_t written = co_await destination.write_all(
        std::span<const std::byte>(buffer.data(), read));
    if (written != read)
      throw std::runtime_error("coroutine relay completed short");
    total += written;
  }
}

ntl::net::user::task<std::tuple<std::uint64_t, std::uint64_t>>
relay_bidirectionally(ntl::net::async_socket &client_side,
                      ntl::net::async_socket &server_side) {
  co_return co_await ntl::net::user::when_all_cancel_on_error(
      relay(client_side, server_side), relay(server_side, client_side),
      [&client_side, &server_side]() noexcept {
        (void)client_side.cancel();
        (void)server_side.cancel();
      });
}

struct proxy_result {
  std::uint64_t upstream = 0;
  std::uint64_t downstream = 0;
  std::uint16_t original_port = 0;
  int original_family = AF_UNSPEC;
};

proxy_result run_proxy_once(const listener &proxy) {
  socket_owner accepted(
      ::accept(proxy.socket.get(), nullptr, nullptr));
  if (accepted.get() == INVALID_SOCKET)
    throw_socket("accept(proxy)");

  auto redirected =
      ntl::wfp::redirected_connection::capture(accepted.get());
  const auto &destination =
      redirected.original_destination_ref();
  if (destination.ss_family != AF_INET &&
      destination.ss_family != AF_INET6)
    throw std::runtime_error(
        "sample expected an IPv4 or IPv6 original destination");

  socket_owner outbound(redirected.connect_original());
  ntl::net::io_completion_context context;
  ntl::net::async_socket client_side(
      context, accepted.release());
  ntl::net::async_socket server_side(
      context, outbound.release());

  proxy_result result{};
  std::tie(result.upstream, result.downstream) =
      ntl::net::user::sync_wait(
          relay_bidirectionally(client_side, server_side));
  result.original_family = destination.ss_family;
  result.original_port =
      destination.ss_family == AF_INET
          ? ntohs(
                reinterpret_cast<const sockaddr_in *>(
                    &destination)
                    ->sin_port)
          : ntohs(
                reinterpret_cast<const sockaddr_in6 *>(
                    &destination)
                    ->sin6_port);
  context.wait_for_idle();
  return result;
}

void install_policy(ntl::wfp::policy_session &session,
                    std::uint16_t original_port_v4,
                    std::uint16_t proxy_port_v4,
                    std::uint16_t original_port_v6,
                    std::uint16_t proxy_port_v6) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_connect_redirect::provider_key,
         L"crtsys NTL WFP connect-redirect provider",
         L"Dynamic provider for the local coroutine TCP proxy"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {wfp_connect_redirect::sublayer_key,
         L"crtsys NTL WFP connect-redirect sublayer",
         L"Local TCP proxy redirection policy", 0x7800});
    const auto callout =
        transaction.add_callout<wfp_connect_redirect::layer>(
            provider,
            {wfp_connect_redirect::callout_key,
             L"Redirect selected TCP connects to the local proxy",
             L"Typed ALE_CONNECT_REDIRECT_V4 callout"});

    auto filter = crtsys::examples::wfp::connect_redirect::make_filter<
        wfp_connect_redirect::layer>(
        wfp_connect_redirect::filter_key,
        L"Redirect the selected IPv4 destination port",
        ::GetCurrentProcessId(), proxy_port_v4, original_port_v4);
    transaction.add_connect_redirect_filter(
        sublayer, callout, filter);

    const auto callout_v6 =
        transaction.add_callout<
            wfp_connect_redirect::layer_v6>(
            provider,
            {wfp_connect_redirect::callout_key_v6,
             L"Redirect selected IPv6 TCP connects to the local proxy",
             L"Typed ALE_CONNECT_REDIRECT_V6 callout"});
    auto filter_v6 = crtsys::examples::wfp::connect_redirect::make_filter<
        wfp_connect_redirect::layer_v6>(
        wfp_connect_redirect::filter_key_v6,
        L"Redirect the selected IPv6 destination port",
        ::GetCurrentProcessId(), proxy_port_v6, original_port_v6);
    transaction.add_connect_redirect_filter(
        sublayer, callout_v6, filter_v6);
  });
}

std::uint16_t parse_port(const wchar_t *value) {
  wchar_t *end = nullptr;
  const unsigned long parsed = std::wcstoul(value, &end, 10);
  if (!value[0] || !end || *end || parsed == 0 || parsed > 65535)
    throw std::invalid_argument("port must be in 1..65535");
  return static_cast<std::uint16_t>(parsed);
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    const bool unavailable_proxy =
        argc == 5 &&
        std::wstring_view(argv[1]) == L"--unavailable-proxy";
    if (argc != 4 && !unavailable_proxy)
      throw std::invalid_argument(
          "usage: crtsys_wfp_connect_redirect_proxy_service.exe "
          "[--unavailable-proxy] <origin-port-v4> <origin-port-v6> "
          "<ipc-directory>");
    winsock_session winsock;
    const int first_port = unavailable_proxy ? 2 : 1;
    const auto original_v4 = parse_port(argv[first_port]);
    const auto original_v6 = parse_port(argv[first_port + 1]);
    crtsys::wfp_sample::controller_lifecycle lifecycle(
        argv[first_port + 2]);
    auto proxy_v4 = make_listener(AF_INET);
    auto proxy_v6 = make_listener(AF_INET6);

    if (unavailable_proxy) {
      const auto unavailable_v4 = proxy_v4.port;
      const auto unavailable_v6 = proxy_v6.port;
      proxy_v4.socket.close();
      proxy_v6.socket.close();
      {
        auto policy = ntl::wfp::policy_session::ephemeral(
            L"crtsys connect-redirect unavailable proxy controller");
        install_policy(
            policy, original_v4, unavailable_v4,
            original_v6, unavailable_v6);
        std::ostringstream ready;
        ready << "state=unavailable-proxy\n"
              << "origin_v4=" << original_v4 << "\n"
              << "origin_v6=" << original_v6 << "\n"
              << "proxy_v4=" << unavailable_v4 << "\n"
              << "proxy_v6=" << unavailable_v6 << "\n";
        lifecycle.publish_ready(ready.str());
        lifecycle.wait_for_stop();
      }
      lifecycle.publish_stats(
          "state=stopped\nunavailable_proxy=closed\n");
      return 0;
    }

    proxy_result result_v4{};
    proxy_result result_v6{};
    std::exception_ptr failure_v4;
    std::exception_ptr failure_v6;
    std::atomic<bool> stopping = false;
    std::thread worker_v4([&]() {
      try {
        result_v4 = run_proxy_once(proxy_v4);
      } catch (...) {
        if (!stopping.load(std::memory_order_acquire))
          failure_v4 = std::current_exception();
      }
    });
    std::thread worker_v6([&]() {
      try {
        result_v6 = run_proxy_once(proxy_v6);
      } catch (...) {
        if (!stopping.load(std::memory_order_acquire))
          failure_v6 = std::current_exception();
      }
    });

    {
      auto policy = ntl::wfp::policy_session::ephemeral(
          L"crtsys connect-redirect proxy service");
      install_policy(
          policy, original_v4, proxy_v4.port,
          original_v6, proxy_v6.port);
      std::ostringstream ready;
      ready << "state=ready\norigin_v4=" << original_v4
            << "\norigin_v6=" << original_v6
            << "\nproxy_v4=" << proxy_v4.port
            << "\nproxy_v6=" << proxy_v6.port << "\n";
      lifecycle.publish_ready(ready.str());
      lifecycle.wait_for_stop();
      stopping.store(true, std::memory_order_release);
      proxy_v4.socket.close();
      proxy_v6.socket.close();
    }

    worker_v4.join();
    worker_v6.join();
    if (failure_v4)
      std::rethrow_exception(failure_v4);
    if (failure_v6)
      std::rethrow_exception(failure_v6);
    std::ostringstream stats;
    stats << "state=stopped\n"
          << "v4_upstream=" << result_v4.upstream << "\n"
          << "v4_downstream=" << result_v4.downstream << "\n"
          << "v4_original_family=" << result_v4.original_family << "\n"
          << "v4_original_port=" << result_v4.original_port << "\n"
          << "v6_upstream=" << result_v6.upstream << "\n"
          << "v6_downstream=" << result_v6.downstream << "\n"
          << "v6_original_family=" << result_v6.original_family << "\n"
          << "v6_original_port=" << result_v6.original_port << "\n";
    lifecycle.publish_stats(stats.str());
    return 0;
  } catch (const std::exception &error) {
    const bool unavailable_proxy =
        argc == 5 &&
        std::wstring_view(argv[1]) == L"--unavailable-proxy";
    const int ipc_index = unavailable_proxy ? 4 : 3;
    if ((argc == 4 || unavailable_proxy) && argv[ipc_index]) {
      std::ofstream failure(
          std::filesystem::path(argv[ipc_index]) / L"controller.error",
          std::ios::binary | std::ios::trunc);
      if (failure)
        failure << error.what() << '\n';
    }
    std::cerr << "connect-redirect proxy service failed: "
              << error.what() << '\n';
    return 1;
  }
}
