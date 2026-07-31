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

#include "bind_redirect_contract.hpp"

namespace {

class winsock_session {
public:
  winsock_session() {
    WSADATA data{};
    const int result = ::WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0)
      throw std::system_error(
          result, std::system_category(), "WSAStartup");
  }
  ~winsock_session() { ::WSACleanup(); }
};

class socket_owner {
public:
  explicit socket_owner(
      SOCKET value = INVALID_SOCKET) noexcept
      : value_(value) {}
  socket_owner(const socket_owner &) = delete;
  socket_owner &operator=(const socket_owner &) = delete;
  socket_owner(socket_owner &&other) noexcept
      : value_(std::exchange(
            other.value_, INVALID_SOCKET)) {}
  socket_owner &operator=(socket_owner &&other) noexcept {
    if (this != &other) {
      reset();
      value_ = std::exchange(
          other.value_, INVALID_SOCKET);
    }
    return *this;
  }
  ~socket_owner() { reset(); }

  SOCKET get() const noexcept { return value_; }

private:
  void reset() noexcept {
    if (value_ != INVALID_SOCKET) {
      (void)::closesocket(value_);
      value_ = INVALID_SOCKET;
    }
  }
  SOCKET value_ = INVALID_SOCKET;
};

[[noreturn]] void throw_socket(const char *operation) {
  throw std::system_error(
      ::WSAGetLastError(), std::system_category(), operation);
}

socket_owner create_udp_socket(int family) {
  socket_owner result(
      ::socket(family, SOCK_DGRAM, IPPROTO_UDP));
  if (result.get() == INVALID_SOCKET)
    throw_socket("socket(UDP)");

  BOOL exclusive = TRUE;
  if (::setsockopt(
          result.get(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
          reinterpret_cast<const char *>(&exclusive),
          sizeof(exclusive)) == SOCKET_ERROR)
    throw_socket("setsockopt(SO_EXCLUSIVEADDRUSE)");
  if (family == AF_INET6) {
    DWORD v6_only = 1;
    if (::setsockopt(
            result.get(), IPPROTO_IPV6, IPV6_V6ONLY,
            reinterpret_cast<const char *>(&v6_only),
            sizeof(v6_only)) == SOCKET_ERROR)
      throw_socket("setsockopt(IPV6_V6ONLY)");
  }
  return result;
}

std::uint16_t bind_loopback(
    socket_owner &socket, int family) {
  if (family == AF_INET) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(
            socket.get(),
            reinterpret_cast<const sockaddr *>(&address),
            sizeof(address)) == SOCKET_ERROR)
      throw_socket("bind(IPv4)");
    int size = sizeof(address);
    if (::getsockname(
            socket.get(),
            reinterpret_cast<sockaddr *>(&address),
            &size) == SOCKET_ERROR)
      throw_socket("getsockname(IPv4)");
    return ntohs(address.sin_port);
  }

  sockaddr_in6 address{};
  address.sin6_family = AF_INET6;
  address.sin6_addr = in6addr_loopback;
  address.sin6_port = 0;
  if (::bind(
          socket.get(),
          reinterpret_cast<const sockaddr *>(&address),
          sizeof(address)) == SOCKET_ERROR)
    throw_socket("bind(IPv6)");
  int size = sizeof(address);
  if (::getsockname(
          socket.get(),
          reinterpret_cast<sockaddr *>(&address),
          &size) == SOCKET_ERROR)
    throw_socket("getsockname(IPv6)");
  return ntohs(address.sin6_port);
}

void verify_target_ports_available() {
  {
    auto socket = create_udp_socket(AF_INET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port =
        htons(wfp_bind_redirect::redirected_port_v4);
    if (::bind(
            socket.get(),
            reinterpret_cast<const sockaddr *>(&address),
            sizeof(address)) == SOCKET_ERROR)
      throw_socket("sample IPv4 target port is unavailable");
  }
  {
    auto socket = create_udp_socket(AF_INET6);
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port =
        htons(wfp_bind_redirect::redirected_port_v6);
    if (::bind(
            socket.get(),
            reinterpret_cast<const sockaddr *>(&address),
            sizeof(address)) == SOCKET_ERROR)
      throw_socket("sample IPv6 target port is unavailable");
  }
}

void install_policy(ntl::wfp::policy_session &session) {
  const auto application =
      ntl::wfp::application_id::current_process();
  session.install(
      [&](ntl::wfp::policy_transaction &transaction) {
        const auto provider = transaction.add_provider(
            {wfp_bind_redirect::provider_key,
             L"crtsys NTL WFP bind-redirect provider",
             L"Dynamic dual-stack UDP bind policy"});
        const auto sublayer = transaction.add_sublayer(
            provider,
            {wfp_bind_redirect::sublayer_key,
             L"crtsys NTL WFP bind-redirect sublayer",
             L"Application-scoped bind redirection", 0x7810});

        const auto callout_v4 =
            transaction.add_callout<
                wfp_bind_redirect::layer_v4>(
                provider,
                {wfp_bind_redirect::callout_key_v4,
                 L"Redirect selected IPv4 UDP binds",
                 L"Typed ALE_BIND_REDIRECT_V4 callout"});
        ntl::wfp::bind_redirect_filter_builder<
            wfp_bind_redirect::layer_v4>
            filter_v4(
                wfp_bind_redirect::filter_key_v4,
                L"Redirect current process IPv4 UDP binds",
                wfp_bind_redirect::selector_v4,
                ntl::wfp::callout_unavailable::permit);
        filter_v4.application_equal(application)
            .protocol_equal(IPPROTO_UDP);
        transaction.add_bind_redirect_filter(
            sublayer, callout_v4, filter_v4);

        const auto callout_v6 =
            transaction.add_callout<
                wfp_bind_redirect::layer_v6>(
                provider,
                {wfp_bind_redirect::callout_key_v6,
                 L"Redirect selected IPv6 UDP binds",
                 L"Typed ALE_BIND_REDIRECT_V6 callout"});
        ntl::wfp::bind_redirect_filter_builder<
            wfp_bind_redirect::layer_v6>
            filter_v6(
                wfp_bind_redirect::filter_key_v6,
                L"Redirect current process IPv6 UDP binds",
                wfp_bind_redirect::selector_v6,
                ntl::wfp::callout_unavailable::permit);
        filter_v6.application_equal(application)
            .protocol_equal(IPPROTO_UDP);
        transaction.add_bind_redirect_filter(
            sublayer, callout_v6, filter_v6);
      });
}

} // namespace

int wmain() {
  try {
    winsock_session winsock;
    verify_target_ports_available();

    socket_owner redirected_v4;
    socket_owner redirected_v6;
    std::uint16_t redirected_port_v4 = 0;
    std::uint16_t redirected_port_v6 = 0;
    {
      auto policy = ntl::wfp::policy_session::ephemeral(
          L"crtsys ntl::wfp bind-redirect sample");
      install_policy(policy);

      redirected_v4 = create_udp_socket(AF_INET);
      redirected_v6 = create_udp_socket(AF_INET6);
      redirected_port_v4 =
          bind_loopback(redirected_v4, AF_INET);
      redirected_port_v6 =
          bind_loopback(redirected_v6, AF_INET6);
      if (redirected_port_v4 !=
              wfp_bind_redirect::redirected_port_v4 ||
          redirected_port_v6 !=
              wfp_bind_redirect::redirected_port_v6)
        throw std::runtime_error(
            "typed bind redirect did not select both target ports");
    }

    auto direct_v4 = create_udp_socket(AF_INET);
    auto direct_v6 = create_udp_socket(AF_INET6);
    const std::uint16_t direct_port_v4 =
        bind_loopback(direct_v4, AF_INET);
    const std::uint16_t direct_port_v6 =
        bind_loopback(direct_v6, AF_INET6);
    if (direct_port_v4 == redirected_port_v4 ||
        direct_port_v6 == redirected_port_v6)
      throw std::runtime_error(
          "bind remained redirected after ephemeral policy removal");

    std::wcout
        << L"NTL WFP bind-redirect ok: IPv4="
        << redirected_port_v4 << L", IPv6="
        << redirected_port_v6
        << L", restored=ephemeral\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NTL WFP bind-redirect failed: "
              << error.what() << '\n';
    return 1;
  }
}
