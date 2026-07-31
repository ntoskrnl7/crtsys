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
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include <ntl/net/io/async_socket>
#include <ntl/wfp/all>

#include "connect_redirect_contract.hpp"
#include "coroutine_task.hpp"

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
      : value_(std::exchange(other.value_, INVALID_SOCKET)) {}
  socket_owner &operator=(socket_owner &&other) noexcept {
    if (this != &other) {
      reset();
      value_ = std::exchange(other.value_, INVALID_SOCKET);
    }
    return *this;
  }
  ~socket_owner() { reset(); }
  SOCKET get() const noexcept { return value_; }
  SOCKET release() noexcept {
    return std::exchange(value_, INVALID_SOCKET);
  }
  void close() noexcept { reset(); }

private:
  void reset() noexcept {
    if (value_ != INVALID_SOCKET) {
      (void)::closesocket(value_);
      value_ = INVALID_SOCKET;
    }
  }
  SOCKET value_ = INVALID_SOCKET;
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

socket_owner connect_loopback(
    int family, std::uint16_t port) {
  socket_owner result(::WSASocketW(
      family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
      WSA_FLAG_OVERLAPPED));
  if (result.get() == INVALID_SOCKET)
    throw_socket("WSASocketW(client)");

  if (family == AF_INET) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::connect(
            result.get(),
            reinterpret_cast<const sockaddr *>(&address),
            sizeof(address)) == SOCKET_ERROR)
      throw_socket("connect(IPv4 loopback)");
  } else {
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = htons(port);
    if (::connect(
            result.get(),
            reinterpret_cast<const sockaddr *>(&address),
            sizeof(address)) == SOCKET_ERROR)
      throw_socket("connect(IPv6 loopback)");
  }
  return result;
}

std::string receive_to_eof(SOCKET socket) {
  std::string result;
  std::array<char, 4096> buffer{};
  for (;;) {
    const int read =
        ::recv(socket, buffer.data(),
               static_cast<int>(buffer.size()), 0);
    if (read == 0)
      return result;
    if (read == SOCKET_ERROR)
      throw_socket("recv");
    result.append(buffer.data(), static_cast<std::size_t>(read));
  }
}

void send_all(SOCKET socket, std::string_view value) {
  std::size_t sent = 0;
  while (sent != value.size()) {
    const int chunk = ::send(
        socket, value.data() + sent,
        static_cast<int>(value.size() - sent), 0);
    if (chunk == SOCKET_ERROR)
      throw_socket("send");
    sent += static_cast<std::size_t>(chunk);
  }
}

wfp_connect_redirect_app::coroutine_task<std::uint64_t>
relay(ntl::net::async_socket &source,
      ntl::net::async_socket &destination) {
  std::array<std::byte, 64 * 1024> buffer{};
  std::uint64_t total = 0;
  for (;;) {
    const std::size_t read = co_await source.read_some(buffer);
    if (read == 0) {
      if (::shutdown(destination.native_handle(), SD_SEND) ==
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
      redirected.original_destination();
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

  auto upstream = relay(client_side, server_side);
  auto downstream = relay(server_side, client_side);
  proxy_result result{};
  try {
    result.upstream = upstream.get();
  } catch (...) {
    const auto failure = std::current_exception();
    (void)client_side.cancel();
    (void)server_side.cancel();
    try {
      (void)downstream.get();
    } catch (...) {
    }
    context.wait_for_idle();
    std::rethrow_exception(failure);
  }
  result.downstream = downstream.get();
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

std::string run_echo_once(const listener &server,
                          std::string *received) {
  socket_owner accepted(
      ::accept(server.socket.get(), nullptr, nullptr));
  if (accepted.get() == INVALID_SOCKET)
    throw_socket("accept(origin)");
  *received = receive_to_eof(accepted.get());
  const std::string reply = "echo:" + *received;
  send_all(accepted.get(), reply);
  if (::shutdown(accepted.get(), SD_SEND) == SOCKET_ERROR)
    throw_socket("shutdown(origin)");
  return reply;
}

std::string exchange(
    int family, std::uint16_t port,
    std::string_view payload) {
  auto client = connect_loopback(family, port);
  send_all(client.get(), payload);
  if (::shutdown(client.get(), SD_SEND) == SOCKET_ERROR)
    throw_socket("shutdown(client)");
  return receive_to_eof(client.get());
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

    ntl::wfp::connect_redirect_filter_builder<
        wfp_connect_redirect::layer>
        filter(
            wfp_connect_redirect::filter_key,
            L"Redirect the selected destination port",
            {::GetCurrentProcessId(), proxy_port_v4},
            ntl::wfp::callout_unavailable::permit);
    filter.description(
              L"PID and proxy port are encoded by the typed builder")
        .protocol_equal(IPPROTO_TCP)
        .remote_port_equal(original_port_v4);
    transaction.add_connect_redirect_filter(
        sublayer, callout, filter);

    const auto callout_v6 =
        transaction.add_callout<
            wfp_connect_redirect::layer_v6>(
            provider,
            {wfp_connect_redirect::callout_key_v6,
             L"Redirect selected IPv6 TCP connects to the local proxy",
             L"Typed ALE_CONNECT_REDIRECT_V6 callout"});
    ntl::wfp::connect_redirect_filter_builder<
        wfp_connect_redirect::layer_v6>
        filter_v6(
            wfp_connect_redirect::filter_key_v6,
            L"Redirect the selected IPv6 destination port",
            {::GetCurrentProcessId(), proxy_port_v6},
            ntl::wfp::callout_unavailable::permit);
    filter_v6.description(
                 L"IPv6 PID and proxy port use the typed builder")
        .protocol_equal(IPPROTO_TCP)
        .remote_port_equal(original_port_v6);
    transaction.add_connect_redirect_filter(
        sublayer, callout_v6, filter_v6);
  });
}

bool listener_has_pending_connection(const listener &value) {
  fd_set readable;
  FD_ZERO(&readable);
  FD_SET(value.socket.get(), &readable);
  timeval timeout{};
  timeout.tv_sec = 0;
  timeout.tv_usec = 250000;
  const int selected =
      ::select(0, &readable, nullptr, nullptr, &timeout);
  if (selected == SOCKET_ERROR)
    throw_socket("select(proxy listener)");
  return selected != 0;
}

struct redirected_proof {
  proxy_result proxy;
  std::string origin_received;
  std::string expected_reply;
  std::string actual_reply;
};

redirected_proof run_redirected_exchange(
    listener &origin, listener &proxy,
    std::string_view payload) {
  redirected_proof result;
  std::exception_ptr proxy_error;
  std::exception_ptr origin_error;
  std::thread proxy_thread([&] {
    try {
      result.proxy = run_proxy_once(proxy);
    } catch (...) {
      proxy_error = std::current_exception();
    }
  });
  std::thread origin_thread([&] {
    try {
      result.expected_reply =
          run_echo_once(origin, &result.origin_received);
    } catch (...) {
      origin_error = std::current_exception();
    }
  });

  try {
    result.actual_reply =
        exchange(origin.family, origin.port, payload);
  } catch (...) {
    proxy.socket.close();
    origin.socket.close();
    proxy_thread.join();
    origin_thread.join();
    throw;
  }
  proxy_thread.join();
  origin_thread.join();
  if (proxy_error)
    std::rethrow_exception(proxy_error);
  if (origin_error)
    std::rethrow_exception(origin_error);
  return result;
}

void validate_redirected_exchange(
    const redirected_proof &proof,
    const listener &origin,
    std::string_view payload) {
  if (proof.proxy.original_family != origin.family ||
      proof.proxy.original_port != origin.port ||
      proof.origin_received != payload ||
      proof.actual_reply != proof.expected_reply ||
      proof.proxy.upstream != payload.size() ||
      proof.proxy.downstream !=
          proof.expected_reply.size())
    throw std::runtime_error(
        "redirected dual-stack exchange proof did not match");
}

void prove_direct_exchange(listener &origin) {
  constexpr std::string_view payload = "policy-removed";
  std::string received;
  std::string expected;
  std::exception_ptr server_error;
  std::thread server([&] {
    try {
      expected = run_echo_once(origin, &received);
    } catch (...) {
      server_error = std::current_exception();
    }
  });
  std::string actual;
  try {
    actual = exchange(origin.family, origin.port, payload);
  } catch (...) {
    origin.socket.close();
    server.join();
    throw;
  }
  server.join();
  if (server_error)
    std::rethrow_exception(server_error);
  if (received != payload || actual != expected)
    throw std::runtime_error(
        "direct dual-stack exchange after policy removal failed");
}

} // namespace

int wmain() {
  try {
    std::wcout << std::unitbuf;
    std::cerr << std::unitbuf;
    winsock_session winsock;
    auto origin_v4 = make_listener(AF_INET);
    auto proxy_v4 = make_listener(AF_INET);
    auto origin_v6 = make_listener(AF_INET6);
    auto proxy_v6 = make_listener(AF_INET6);
    constexpr std::string_view payload =
        "ntl-connect-redirect-coroutine";

    std::wcout
        << L"[1/5] IPv4 origin/proxy=" << origin_v4.port
        << L"/" << proxy_v4.port
        << L", IPv6 origin/proxy=" << origin_v6.port
        << L"/" << proxy_v6.port << L".\n";

    redirected_proof proof_v4;
    redirected_proof proof_v6;
    {
      auto policy = ntl::wfp::policy_session::ephemeral(
          L"crtsys ntl::wfp connect-redirect sample");
      install_policy(
          policy, origin_v4.port, proxy_v4.port,
          origin_v6.port, proxy_v6.port);
      std::wcout
          << L"[2/5] Typed IPv4 and IPv6 ALE connect "
             L"redirects installed.\n";

      proof_v4 = run_redirected_exchange(
          origin_v4, proxy_v4, payload);
      proof_v6 = run_redirected_exchange(
          origin_v6, proxy_v6, payload);
      std::wcout
          << L"[3/5] Both proxies captured WFP context and "
             L"relayed both directions with co_await.\n";
    }

    validate_redirected_exchange(
        proof_v4, origin_v4, payload);
    validate_redirected_exchange(
        proof_v6, origin_v6, payload);
    prove_direct_exchange(origin_v4);
    prove_direct_exchange(origin_v6);
    std::wcout
        << L"[4/5] Ephemeral policy removed; both families are "
           L"direct.\n";

    if (listener_has_pending_connection(proxy_v4) ||
        listener_has_pending_connection(proxy_v6))
      throw std::runtime_error(
          "proxy received a connection after policy removal");
    std::wcout
        << L"[5/5] No connection reached either proxy after "
           L"removal.\n";
    std::wcout
        << L"NTL WFP connect-redirect ok: IPv4="
        << origin_v4.port << L"->" << proxy_v4.port
        << L", IPv6=" << origin_v6.port << L"->"
        << proxy_v6.port
        << L", coroutine_up=" << proof_v4.proxy.upstream +
                                      proof_v6.proxy.upstream
        << L", coroutine_down="
        << proof_v4.proxy.downstream +
               proof_v6.proxy.downstream
        << L", restored=direct\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NTL WFP connect-redirect failed: "
              << error.what() << '\n';
    return 1;
  }
}
