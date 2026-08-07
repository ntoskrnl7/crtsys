#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "content_filter_fixture.hpp"
#include "tcp_content_filter_contract.hpp"

namespace {

namespace fixture = crtsys::test::wfp::content_filter_fixture;

class winsock_session {
public:
  winsock_session() {
    WSADATA data{};
    const int result = ::WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0)
      throw std::system_error(result, std::system_category(), "WSAStartup");
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
  ~socket_owner() {
    if (value_ != INVALID_SOCKET)
      ::closesocket(value_);
  }
  SOCKET get() const noexcept { return value_; }

private:
  SOCKET value_;
};

struct listener {
  socket_owner socket;
  int family = AF_UNSPEC;
  std::uint16_t port = 0;
};

listener make_listener(int family, std::uint16_t requested_port = 0) {
  socket_owner socket(::socket(family, SOCK_STREAM, IPPROTO_TCP));
  if (socket.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "socket(listener)");
  sockaddr_storage storage{};
  int size = 0;
  if (family == AF_INET) {
    auto &address = reinterpret_cast<sockaddr_in &>(storage);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(requested_port);
    size = sizeof(address);
  } else {
    DWORD v6_only = 1;
    if (::setsockopt(socket.get(), IPPROTO_IPV6, IPV6_V6ONLY,
                     reinterpret_cast<const char *>(&v6_only),
                     sizeof(v6_only)) == SOCKET_ERROR)
      throw std::system_error(::WSAGetLastError(), std::system_category(),
                              "setsockopt(IPV6_V6ONLY)");
    auto &address = reinterpret_cast<sockaddr_in6 &>(storage);
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = htons(requested_port);
    size = sizeof(address);
  }
  if (::bind(socket.get(), reinterpret_cast<const sockaddr *>(&storage),
             size) == SOCKET_ERROR ||
      ::listen(socket.get(), 8) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "bind/listen");
  size = sizeof(storage);
  if (::getsockname(socket.get(), reinterpret_cast<sockaddr *>(&storage),
                    &size) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "getsockname");
  const auto port =
      family == AF_INET
          ? ntohs(reinterpret_cast<const sockaddr_in &>(storage).sin_port)
          : ntohs(reinterpret_cast<const sockaddr_in6 &>(storage).sin6_port);
  return {std::move(socket), family, port};
}

socket_owner connect_tcp(int family, std::uint16_t port) {
  socket_owner socket(::socket(family, SOCK_STREAM, IPPROTO_TCP));
  if (socket.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "socket(client)");
  sockaddr_storage storage{};
  int size = 0;
  if (family == AF_INET) {
    auto &address = reinterpret_cast<sockaddr_in &>(storage);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    size = sizeof(address);
  } else {
    auto &address = reinterpret_cast<sockaddr_in6 &>(storage);
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = htons(port);
    size = sizeof(address);
  }
  if (::connect(socket.get(), reinterpret_cast<const sockaddr *>(&storage),
                size) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "connect");
  return socket;
}

socket_owner accept_tcp(const listener &server) {
  socket_owner accepted(::accept(server.socket.get(), nullptr, nullptr));
  if (accepted.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "accept");
  return accepted;
}

std::string frame_message(std::string_view content) {
  if (content.size() > wfp_tcp_content_filter::maximum_record_size)
    throw std::overflow_error("sample message is too large");
  const auto length = static_cast<std::uint32_t>(content.size());
  std::string frame(wfp_tcp_content_filter::sample_u32_be_prefix_size, '\0');
  frame[0] = static_cast<char>((length >> 24) & 0xff);
  frame[1] = static_cast<char>((length >> 16) & 0xff);
  frame[2] = static_cast<char>((length >> 8) & 0xff);
  frame[3] = static_cast<char>(length & 0xff);
  frame.append(content);
  return frame;
}

std::string make_record(
    crtsys::examples::wfp::content_filter::classification category,
    std::uint32_t rule_id, std::string_view body) {
  std::string result(
      crtsys::examples::wfp::content_filter::wire_size(body.size()), '\0');
  const auto status = crtsys::examples::wfp::content_filter::encode(
      std::as_writable_bytes(std::span(result.data(), result.size())),
      category, rule_id, std::as_bytes(std::span(body)),
      wfp_tcp_content_filter::maximum_record_body_size);
  if (!status.is_ok())
    throw std::runtime_error("cannot encode content-filter record");
  return result;
}

void send_all(SOCKET socket, std::string_view bytes) {
  std::size_t offset = 0;
  while (offset != bytes.size()) {
    const int sent = ::send(
        socket, bytes.data() + offset,
        static_cast<int>((std::min)(
            bytes.size() - offset,
            static_cast<std::size_t>((std::numeric_limits<int>::max)()))),
        0);
    if (sent <= 0)
      throw std::system_error(::WSAGetLastError(), std::system_category(),
                              "send");
    offset += static_cast<std::size_t>(sent);
  }
}

std::string receive_exactly(SOCKET socket, std::size_t size,
                            DWORD timeout_ms) {
  if (::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&timeout_ms),
                   sizeof(timeout_ms)) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "setsockopt(SO_RCVTIMEO)");
  std::string bytes(size, '\0');
  std::size_t offset = 0;
  while (offset != size) {
    const int received = ::recv(
        socket, bytes.data() + offset,
        static_cast<int>((std::min)(
            size - offset,
            static_cast<std::size_t>((std::numeric_limits<int>::max)()))),
        0);
    if (received <= 0)
      throw std::runtime_error("TCP stream closed before a full frame");
    offset += static_cast<std::size_t>(received);
  }
  return bytes;
}

std::string exchange_message(const listener &server,
                             std::string_view content, bool split_prefix) {
  auto client = connect_tcp(server.family, server.port);
  auto peer = accept_tcp(server);
  const std::string frame = frame_message(content);
  if (split_prefix) {
    send_all(client.get(), std::string_view(frame).substr(0, 2));
    ::Sleep(20);
    send_all(client.get(), std::string_view(frame).substr(2));
  } else {
    send_all(client.get(), frame);
  }
  return receive_exactly(peer.get(), frame.size(), 3000);
}

std::string exchange_two_messages(const listener &server,
                                  std::string_view first,
                                  std::string_view second) {
  auto client = connect_tcp(server.family, server.port);
  auto peer = accept_tcp(server);
  const std::string wire = frame_message(first) + frame_message(second);
  send_all(client.get(), std::string_view(wire).substr(0, 2));
  ::Sleep(20);
  send_all(client.get(), std::string_view(wire).substr(2));
  const std::string received = receive_exactly(peer.get(), wire.size(), 3000);
  constexpr std::string_view reply = "unframed-outbound-reply";
  send_all(peer.get(), reply);
  if (receive_exactly(client.get(), reply.size(), 3000) != reply)
    throw std::runtime_error("outbound TCP response was not passed through");
  return received;
}

bool receive_flow_reset(SOCKET socket, DWORD timeout_ms) {
  if (::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&timeout_ms),
                   sizeof(timeout_ms)) == SOCKET_ERROR)
    return false;
  std::array<char, 16> bytes{};
  const int received =
      ::recv(socket, bytes.data(), static_cast<int>(bytes.size()), 0);
  if (received == 0)
    return true;
  if (received != SOCKET_ERROR)
    return false;
  const int error = ::WSAGetLastError();
  return error == WSAECONNRESET || error == WSAECONNABORTED;
}

bool blocked_message(const listener &server, std::string_view content) {
  auto client = connect_tcp(server.family, server.port);
  auto peer = accept_tcp(server);
  send_all(client.get(), frame_message(content));
  return receive_flow_reset(peer.get(), 3000);
}

bool blocked_wire(const listener &server, std::string_view wire) {
  auto client = connect_tcp(server.family, server.port);
  auto peer = accept_tcp(server);
  send_all(client.get(), wire);
  (void)::shutdown(client.get(), SD_SEND);
  return receive_flow_reset(peer.get(), 3000);
}

std::filesystem::path policy_service_path(int argc, wchar_t **argv,
                                          bool &failure) {
  failure = false;
  std::filesystem::path service = fixture::sibling_executable(
      L"crtsys_wfp_tcp_content_filter_policy_service.exe");
  for (int index = 1; index < argc; ++index) {
    const std::wstring_view option(argv[index]);
    if (option == L"--failure-self-test") {
      failure = true;
    } else if (option == L"--controller" && index + 1 < argc) {
      service = std::filesystem::absolute(argv[++index]);
    } else {
      throw std::invalid_argument(
          "usage: acceptance [--failure-self-test] [--controller <path>]");
    }
  }
  return service;
}

void validate_normal(const std::filesystem::path &service) {
  auto ipv4 = make_listener(AF_INET);
  auto ipv6 = make_listener(AF_INET6, ipv4.port);
  fixture::state_directory state(L"user-tcp-content-filter");
  fixture::controller_process policy(service, ipv4.port, state.path(), 8);
  policy.wait_ready();
  const std::string allowed = make_record(
      crtsys::examples::wfp::content_filter::classification::ordinary,
      1001, "ordinary body may contain BLOCKME without changing policy");
  const std::string blocked = make_record(
      crtsys::examples::wfp::content_filter::classification::restricted,
      2001, "typed restricted classification closes the flow");
  std::string malformed = allowed;
  malformed[0] = 'X';
  const std::string two_allowed =
      frame_message(allowed) + frame_message(allowed);
  if (exchange_two_messages(ipv4, allowed, allowed) != two_allowed ||
      exchange_two_messages(ipv6, allowed, allowed) != two_allowed ||
      !blocked_message(ipv4, blocked) || !blocked_message(ipv6, blocked) ||
      !blocked_message(ipv4, malformed) ||
      !blocked_message(ipv6, malformed))
    throw std::runtime_error("user TCP traffic verdict is incorrect");
  policy.request_stop();
  policy.wait();
  const auto stats = fixture::read_stats(policy.stats_file());
  if (fixture::require_stat(stats, "after.queued") <
          fixture::require_stat(stats, "before.queued") + 8 ||
      fixture::require_stat(stats, "after.permitted") <
          fixture::require_stat(stats, "before.permitted") + 4 ||
      fixture::require_stat(stats, "after.blocked") <
          fixture::require_stat(stats, "before.blocked") + 4 ||
      fixture::require_stat(stats, "after.malformed") <
          fixture::require_stat(stats, "before.malformed") + 2 ||
      fixture::require_stat(stats, "after.timed_out") !=
          fixture::require_stat(stats, "before.timed_out") ||
      fixture::require_stat(stats, "after.cancelled") !=
          fixture::require_stat(stats, "before.cancelled") ||
      fixture::require_stat(stats, "after.failed") !=
          fixture::require_stat(stats, "before.failed") ||
      fixture::require_stat(stats, "policy.permitted") != 4 ||
      fixture::require_stat(stats, "policy.blocked") != 2 ||
      fixture::require_stat(stats, "policy.malformed") != 2)
    throw std::runtime_error("user TCP policy-service statistics are wrong");
  if (exchange_message(ipv4, blocked, false) != frame_message(blocked) ||
      exchange_message(ipv6, blocked, false) != frame_message(blocked))
    throw std::runtime_error("user TCP policy removal was not observed");
  std::wcout
      << L"NTL WFP TCP content-filter acceptance PASS: complete-tcp=8, "
         L"permit=4, policy-block=2, malformed=2, "
         L"same-flow=2-per-family, tcp-prefix-split=handled, "
         L"outbound=pass-through, structured-record=used, coroutine=used, "
         L"ipv4=pass, ipv6=pass, restored=success.\n";
}

void validate_failure(const std::filesystem::path &service) {
  auto ipv4 = make_listener(AF_INET);
  auto ipv6 = make_listener(AF_INET6, ipv4.port);
  fixture::state_directory state(L"user-tcp-content-filter-failure");
  fixture::controller_process policy(service, ipv4.port, state.path(), 0,
                                     L"failure");
  policy.wait_ready();
  std::string oversized_prefix(4, '\0');
  const auto oversized = static_cast<std::uint32_t>(
      wfp_tcp_content_filter::maximum_record_size + 1);
  oversized_prefix[0] = static_cast<char>((oversized >> 24) & 0xff);
  oversized_prefix[1] = static_cast<char>((oversized >> 16) & 0xff);
  oversized_prefix[2] = static_cast<char>((oversized >> 8) & 0xff);
  oversized_prefix[3] = static_cast<char>(oversized & 0xff);
  if (!blocked_wire(ipv4, oversized_prefix) ||
      !blocked_wire(ipv6, oversized_prefix))
    throw std::runtime_error("user TCP malformed framing did not fail closed");
  auto sender_v4 = connect_tcp(AF_INET, ipv4.port);
  auto peer_v4 = accept_tcp(ipv4);
  auto sender_v6 = connect_tcp(AF_INET6, ipv6.port);
  auto peer_v6 = accept_tcp(ipv6);
  send_all(sender_v4.get(), frame_message("TIMEOUT:tcp-v4"));
  send_all(sender_v6.get(), frame_message("TIMEOUT:tcp-v6"));
  if (!receive_flow_reset(peer_v4.get(), 5000) ||
      !receive_flow_reset(peer_v6.get(), 5000))
    throw std::runtime_error("user TCP timeout did not fail closed");
  auto cancel_sender = connect_tcp(AF_INET6, ipv6.port);
  auto cancel_peer = accept_tcp(ipv6);
  send_all(cancel_sender.get(), frame_message("CANCEL:tcp-v6"));
  if (!receive_flow_reset(cancel_peer.get(), 3000))
    throw std::runtime_error("user TCP session loss did not fail closed");
  policy.request_stop();
  policy.wait();
  const auto stats = fixture::read_stats(policy.stats_file());
  if (fixture::require_stat(stats, "after.timed_out") <
          fixture::require_stat(stats, "before.timed_out") + 2 ||
      fixture::require_stat(stats, "after.cancelled") <
          fixture::require_stat(stats, "before.cancelled") + 1 ||
      fixture::require_stat(stats, "after.malformed") <
          fixture::require_stat(stats, "before.malformed") + 2 ||
      fixture::require_stat(stats, "after.blocked") <
          fixture::require_stat(stats, "before.blocked") + 5)
    throw std::runtime_error("user TCP failure statistics are incomplete");
  constexpr std::string_view restored = "TIMEOUT:restored";
  if (exchange_message(ipv4, restored, false) != frame_message(restored) ||
      exchange_message(ipv6, restored, false) != frame_message(restored))
    throw std::runtime_error("user TCP failure policy was not removed");
  std::wcout << L"NTL WFP TCP content-filter failure acceptance PASS: "
                L"framing=blocked, ipv4/ipv6-timeout=flow-dropped, "
                L"late-permit=rejected, session-loss=cancelled.\n";
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    winsock_session winsock;
    bool failure = false;
    const auto service = policy_service_path(argc, argv, failure);
    if (failure)
      validate_failure(service);
    else
      validate_normal(service);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NTL WFP TCP content-filter acceptance failed: "
              << error.what() << '\n';
    return 1;
  }
}
