#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "content_filter_fixture.hpp"
#include "udp_content_filter_contract.hpp"

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

struct receiver {
  socket_owner socket;
  int family = AF_UNSPEC;
  std::uint16_t port = 0;
};

receiver make_receiver(int family, std::uint16_t requested_port = 0) {
  socket_owner socket(::socket(family, SOCK_DGRAM, IPPROTO_UDP));
  if (socket.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "socket(receiver)");
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
             size) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "bind(receiver)");
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

socket_owner send_datagram(int family, std::uint16_t port,
                           std::string_view payload) {
  socket_owner socket(::socket(family, SOCK_DGRAM, IPPROTO_UDP));
  if (socket.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "socket(sender)");
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
                size) == SOCKET_ERROR ||
      ::send(socket.get(), payload.data(), static_cast<int>(payload.size()),
             0) != static_cast<int>(payload.size()))
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "connect/send(datagram)");
  return socket;
}

std::optional<std::string> receive_datagram(SOCKET socket, DWORD timeout_ms) {
  if (::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&timeout_ms),
                   sizeof(timeout_ms)) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "setsockopt(SO_RCVTIMEO)");
  std::array<char, 8192> buffer{};
  const int received =
      ::recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
  if (received == SOCKET_ERROR) {
    const int error = ::WSAGetLastError();
    if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK)
      return std::nullopt;
    throw std::system_error(error, std::system_category(), "recv(datagram)");
  }
  return std::string(buffer.data(), static_cast<std::size_t>(received));
}

std::string describe_datagram(const std::optional<std::string> &value,
                              std::string_view expected) {
  if (!value)
    return "missing";
  if (*value == expected)
    return "expected";
  return "unexpected(size=" + std::to_string(value->size()) + ")";
}

std::string make_record(
    crtsys::examples::wfp::content_filter::classification category,
    std::uint32_t rule_id, std::string_view body) {
  std::string result(
      crtsys::examples::wfp::content_filter::wire_size(body.size()), '\0');
  const auto status = crtsys::examples::wfp::content_filter::encode(
      std::as_writable_bytes(std::span(result.data(), result.size())),
      category, rule_id, std::as_bytes(std::span(body)),
      wfp_udp_content_filter::maximum_record_body_size);
  if (!status.is_ok())
    throw std::runtime_error("cannot encode content-filter record");
  return result;
}

std::filesystem::path policy_service_path(int argc, wchar_t **argv,
                                          bool &failure) {
  failure = false;
  std::filesystem::path service = fixture::sibling_executable(
      L"crtsys_wfp_udp_content_filter_policy_service.exe");
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
  auto ipv4 = make_receiver(AF_INET);
  auto ipv6 = make_receiver(AF_INET6, ipv4.port);
  fixture::state_directory state(L"user-udp-content-filter");
  fixture::controller_process policy(service, ipv4.port, state.path(), 6);
  policy.wait_ready();
  const std::string allowed = make_record(
      crtsys::examples::wfp::content_filter::classification::ordinary,
      1001, "ordinary UDP body may contain BLOCKME");
  const std::string blocked = make_record(
      crtsys::examples::wfp::content_filter::classification::restricted,
      2001, "typed restricted classification blocks this datagram");
  std::string malformed = allowed;
  malformed[0] = 'X';
  std::vector<socket_owner> senders;
  senders.reserve(6);
  senders.push_back(send_datagram(AF_INET, ipv4.port, allowed));
  senders.push_back(send_datagram(AF_INET, ipv4.port, blocked));
  senders.push_back(send_datagram(AF_INET, ipv4.port, malformed));
  senders.push_back(send_datagram(AF_INET6, ipv6.port, allowed));
  senders.push_back(send_datagram(AF_INET6, ipv6.port, blocked));
  senders.push_back(send_datagram(AF_INET6, ipv6.port, malformed));
  const auto delivered_v4 = receive_datagram(ipv4.socket.get(), 3000);
  const auto delivered_v6 = receive_datagram(ipv6.socket.get(), 3000);
  const auto extra_v4 = receive_datagram(ipv4.socket.get(), 500);
  const auto extra_v6 = receive_datagram(ipv6.socket.get(), 500);
  const bool traffic_correct =
      delivered_v4 && *delivered_v4 == allowed && delivered_v6 &&
      *delivered_v6 == allowed && !extra_v4 && !extra_v6;
  policy.request_stop();
  policy.wait();
  const auto stats = fixture::read_stats(policy.stats_file());
  if (!traffic_correct)
    throw std::runtime_error(
        "user UDP traffic verdict is incorrect: ipv4=" +
        describe_datagram(delivered_v4, allowed) +
        ", ipv6=" + describe_datagram(delivered_v6, allowed) +
        ", extra_ipv4=" + describe_datagram(extra_v4, allowed) +
        ", extra_ipv6=" + describe_datagram(extra_v6, allowed) +
        ", queued=" + std::to_string(fixture::require_stat(stats, "after.queued")) +
        ", permitted=" + std::to_string(fixture::require_stat(stats, "after.permitted")) +
        ", blocked=" + std::to_string(fixture::require_stat(stats, "after.blocked")) +
        ", malformed=" + std::to_string(fixture::require_stat(stats, "after.malformed")) +
        ", failed=" + std::to_string(fixture::require_stat(stats, "after.failed")) +
        ", injection_completion_failures=" +
        std::to_string(fixture::require_stat(
            stats, "after.injection_completion_failures")) +
        ", last_injection_status=" +
        std::to_string(fixture::require_stat(
            stats, "after.last_injection_status")));
  if (fixture::require_stat(stats, "after.queued") <
          fixture::require_stat(stats, "before.queued") + 6 ||
      fixture::require_stat(stats, "after.permitted") <
          fixture::require_stat(stats, "before.permitted") + 2 ||
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
      fixture::require_stat(stats, "policy.permitted") != 2 ||
      fixture::require_stat(stats, "policy.blocked") != 2 ||
      fixture::require_stat(stats, "policy.malformed") != 2)
    throw std::runtime_error("user UDP policy-service statistics are wrong");
  send_datagram(AF_INET, ipv4.port, blocked);
  send_datagram(AF_INET6, ipv6.port, blocked);
  const auto restored_v4 = receive_datagram(ipv4.socket.get(), 2000);
  const auto restored_v6 = receive_datagram(ipv6.socket.get(), 2000);
  if (!restored_v4 || *restored_v4 != blocked || !restored_v6 ||
      *restored_v6 != blocked)
    throw std::runtime_error("user UDP policy removal was not observed");
  std::wcout
      << L"NTL WFP UDP content-filter acceptance PASS: complete-udp=6, "
         L"permit=2, policy-block=2, malformed=2, structured-record=used, "
         L"coroutine=used, ipv4=pass, ipv6=pass, restored=success.\n";
}

void validate_failure(const std::filesystem::path &service) {
  auto ipv4 = make_receiver(AF_INET);
  auto ipv6 = make_receiver(AF_INET6, ipv4.port);
  fixture::state_directory state(L"user-udp-content-filter-failure");
  fixture::controller_process policy(service, ipv4.port, state.path(), 0,
                                     L"failure");
  policy.wait_ready();
  for (std::size_t index = 0;
       index != wfp_udp_content_filter::maximum_pending_requests + 1;
       ++index) {
    const int family = (index & 1u) == 0 ? AF_INET : AF_INET6;
    send_datagram(family, ipv4.port,
                  "TIMEOUT:" + std::to_string(index));
  }
  if (receive_datagram(ipv4.socket.get(), 4000) ||
      receive_datagram(ipv6.socket.get(), 1000))
    throw std::runtime_error("timed-out UDP datagram reached the receiver");
  send_datagram(AF_INET6, ipv6.port, "CANCEL:udp-v6");
  if (receive_datagram(ipv6.socket.get(), 3000))
    throw std::runtime_error("cancelled UDP datagram reached the receiver");
  policy.request_stop();
  policy.wait();
  const auto stats = fixture::read_stats(policy.stats_file());
  if (fixture::require_stat(stats, "after.timed_out") <
          fixture::require_stat(stats, "before.timed_out") +
              wfp_udp_content_filter::maximum_pending_requests ||
      fixture::require_stat(stats, "after.cancelled") <
          fixture::require_stat(stats, "before.cancelled") + 1 ||
      fixture::require_stat(stats, "after.failed") <
          fixture::require_stat(stats, "before.failed") + 1 ||
      fixture::require_stat(stats, "after.blocked") <
          fixture::require_stat(stats, "before.blocked") +
              wfp_udp_content_filter::maximum_pending_requests + 2)
    throw std::runtime_error("user UDP failure statistics are incomplete");
  constexpr std::string_view restored = "TIMEOUT:restored";
  send_datagram(AF_INET, ipv4.port, restored);
  send_datagram(AF_INET6, ipv6.port, restored);
  const auto restored_v4 = receive_datagram(ipv4.socket.get(), 2000);
  const auto restored_v6 = receive_datagram(ipv6.socket.get(), 2000);
  if (!restored_v4 || *restored_v4 != restored || !restored_v6 ||
      *restored_v6 != restored)
    throw std::runtime_error("user UDP failure policy was not removed");
  std::wcout << L"NTL WFP UDP content-filter failure acceptance PASS: "
                L"malformed=blocked, ipv4/ipv6-timeout=blocked, "
                L"quota=bounded, late-permit=rejected, "
                L"session-loss=cancelled.\n";
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
    std::cerr << "NTL WFP UDP content-filter acceptance failed: "
              << error.what() << '\n';
    return 1;
  }
}
