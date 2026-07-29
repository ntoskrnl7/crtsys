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
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <ntl/net/inspection/core>
#include <ntl/rpc/client>
#include <ntl/rpc/coroutine>
#include <ntl/wfp/all>

#include "coroutine_task.hpp"
#include "udp_content_filter_contract.hpp"

namespace {

using layer = wfp_udp_content_filter::layer;

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
  std::uint16_t port = 0;
};

struct udp_sender {
  socket_owner socket;
};

udp_receiver make_receiver() {
  socket_owner socket(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
  if (socket.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "socket(receiver)");

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (bind(socket.get(), reinterpret_cast<const sockaddr *>(&address),
           sizeof(address)) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "bind(receiver)");

  int size = sizeof(address);
  if (getsockname(socket.get(), reinterpret_cast<sockaddr *>(&address),
                  &size) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "getsockname(receiver)");
  return {std::move(socket), ntohs(address.sin_port)};
}

udp_sender make_sender(std::uint16_t port) {
  socket_owner sender(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
  if (sender.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "socket(sender)");

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (connect(sender.get(), reinterpret_cast<const sockaddr *>(&address),
              sizeof(address)) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "connect(sender)");
  return {std::move(sender)};
}

void send_datagram(udp_sender &sender, std::string_view payload) {
  const int length = static_cast<int>(payload.size());
  if (send(sender.socket.get(), payload.data(), length, 0) != length)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "send(datagram)");
}

void send_datagram(std::uint16_t port, std::string_view payload) {
  auto sender = make_sender(port);
  send_datagram(sender, payload);
}

std::string receive_datagram(SOCKET socket, DWORD timeout_ms) {
  if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char *>(&timeout_ms),
                 sizeof(timeout_ms)) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "setsockopt(SO_RCVTIMEO)");

  std::array<char, 512> buffer{};
  const int received =
      recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
  if (received == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "recv(datagram)");
  return std::string(buffer.data(), static_cast<std::size_t>(received));
}

bool has_no_datagram(SOCKET socket) {
  DWORD timeout_ms = 250;
  if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char *>(&timeout_ms),
                 sizeof(timeout_ms)) == SOCKET_ERROR)
    return false;
  std::array<char, 16> buffer{};
  const int result =
      recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
  return result == SOCKET_ERROR && WSAGetLastError() == WSAETIMEDOUT;
}

ntl::rpc::client open_policy_client() {
  ntl::rpc::client client(wfp_udp_content_filter::endpoint_name);
  if (!client)
    throw std::runtime_error(
        "could not open UDP content-filter RPC endpoint");

  ntl::rpc::contract_requirements requirements;
  requirements
      .contract_version(wfp_udp_content_filter::contract_version)
      .transport_features(
          ntl::rpc::transport_features::asynchronous_calls |
          ntl::rpc::transport_features::client_sessions |
          ntl::rpc::transport_features::reliable_notifications)
      .capabilities(wfp_udp_content_filter::capabilities::current)
      .notification(wfp_udp_content_filter::inspection_requests)
      .method(wfp_udp_content_filter::submit_verdict)
      .method(wfp_udp_content_filter::query_stats);
  (void)client.require_contract(requirements);
  (void)client.start_session();
  client.subscribe(wfp_udp_content_filter::inspection_requests);
  return client;
}

ntl::net::inspection::verdict
inspect_request(const wfp_udp_content_request &request) {
  if (request.payload.size() >
      wfp_udp_content_filter::maximum_payload_size)
    return ntl::net::inspection::verdict::block;
  const auto bytes = std::as_bytes(std::span(
      request.payload.data(), request.payload.size()));
  ntl::net::inspection::context metadata{
      ntl::net::inspection::content_kind::opaque,
      ntl::net::inspection::direction::outbound,
      request.id,
      request.source_port,
      request.destination_port,
  };
  const ntl::net::inspection::udp_datagram_view datagram(
      metadata, ntl::net::inspection::content_view(bytes));
  return ntl::net::inspection::evaluate(
      [](const ntl::net::inspection::udp_datagram_view &value) {
        const auto blocked = value.payload().contains("BLOCKME");
        if (!blocked)
          return ntl::net::inspection::verdict::block;
        return *blocked ? ntl::net::inspection::verdict::block
                        : ntl::net::inspection::verdict::permit;
      },
      datagram);
}

struct policy_counts {
  std::uint32_t permitted = 0;
  std::uint32_t blocked = 0;
};

wfp_udp_content_filter_app::coroutine_task<policy_counts>
run_policy(ntl::rpc::client &client, std::size_t request_count) {
  policy_counts counts;
  for (std::size_t index = 0; index != request_count; ++index) {
    auto delivery = co_await client.receive_reliable_async(
        wfp_udp_content_filter::inspection_requests);
    const auto verdict = inspect_request(delivery.payload());
    const auto wire =
        verdict == ntl::net::inspection::verdict::permit
            ? wfp_udp_content_filter::wire_verdict::permit
            : wfp_udp_content_filter::wire_verdict::block;
    const std::int32_t result = client.invoke(
        wfp_udp_content_filter::submit_verdict, delivery.payload().id,
        static_cast<std::uint8_t>(wire));
    if (result != STATUS_SUCCESS)
      throw std::runtime_error(
          "submit UDP content verdict failed with NTSTATUS " +
          std::to_string(result));
    client.acknowledge(
        wfp_udp_content_filter::inspection_requests, delivery);
    if (wire == wfp_udp_content_filter::wire_verdict::permit)
      ++counts.permitted;
    else
      ++counts.blocked;
  }
  co_return counts;
}

void install_policy(ntl::wfp::dynamic_session &session,
                    std::uint16_t destination_port) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_udp_content_filter::provider_key,
         L"crtsys NTL WFP UDP content-filter provider",
         L"Dynamic provider for complete outbound UDP decisions"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {wfp_udp_content_filter::sublayer_key,
         L"crtsys NTL WFP UDP content-filter sublayer",
         L"Fail-closed user-mode UDP datagram inspection", 0x7600});
    const auto callout = transaction.add_callout<layer>(
        provider,
        {wfp_udp_content_filter::callout_key,
         L"Inspect complete outbound UDP datagrams",
         L"Clones and absorbs a datagram until a typed verdict reinjects it"});

    ntl::wfp::packet_filter_builder<layer> filter(
        wfp_udp_content_filter::filter_key,
        L"Inspect UDP datagrams sent to the selected port");
    filter.protocol_equal(IPPROTO_UDP)
        .direction_equal(FWP_DIRECTION_OUTBOUND)
        .remote_port_equal(destination_port);
    transaction.add_packet_filter(sublayer, callout, filter);
  });
}

void validate_fail_closed_paths() {
  auto client = open_policy_client();
  auto receiver = make_receiver();
  const auto before =
      client.invoke(wfp_udp_content_filter::query_stats);
  std::uint64_t first_request_id = 0;

  {
    ntl::wfp::dynamic_session policy(
        L"crtsys ntl::wfp UDP content-filter failure self-test");
    install_policy(policy, receiver.port);

    for (std::size_t index = 0;
         index !=
             wfp_udp_content_filter::maximum_pending_requests + 1;
         ++index) {
      send_datagram(
          receiver.port, "TIMEOUT:" + std::to_string(index));
    }

    const auto first = client.receive_reliable(
        wfp_udp_content_filter::inspection_requests);
    first_request_id = first.payload().id;
    const auto malformed = client.invoke(
        wfp_udp_content_filter::submit_verdict, first_request_id,
        std::uint8_t{0});
    if (malformed != STATUS_INVALID_PARAMETER)
      throw std::runtime_error(
          "driver accepted a malformed UDP verdict");

    wfp_udp_content_filter_stats after{};
    bool expired = false;
    for (std::size_t attempt = 0; attempt != 80; ++attempt) {
      ::Sleep(50);
      after = client.invoke(wfp_udp_content_filter::query_stats);
      if (after.timed_out >=
              before.timed_out +
                  wfp_udp_content_filter::maximum_pending_requests &&
          after.failed >= before.failed + 1) {
        expired = true;
        break;
      }
    }
    if (!expired)
      throw std::runtime_error(
          "UDP timeout or pending-limit proof did not complete");

    const auto late = client.invoke(
        wfp_udp_content_filter::submit_verdict, first_request_id,
        static_cast<std::uint8_t>(
            wfp_udp_content_filter::wire_verdict::permit));
    if (late != STATUS_NOT_FOUND)
      throw std::runtime_error(
          "driver accepted a late UDP permit after timeout");
    client.acknowledge(
        wfp_udp_content_filter::inspection_requests, first);
    if (!has_no_datagram(receiver.socket.get()))
      throw std::runtime_error(
          "a timed-out or over-quota datagram reached the receiver");
  }

  constexpr std::string_view restored = "TIMEOUT:restored";
  send_datagram(receiver.port, restored);
  if (receive_datagram(receiver.socket.get(), 2000) != restored)
    throw std::runtime_error(
        "failure self-test did not restore UDP after policy removal");
  client.close_session();
  std::wcout
      << L"NTL WFP UDP content-filter fail-closed self-test ok: "
         L"malformed=blocked, timeout=blocked, quota=bounded, "
         L"late-permit=rejected\n";
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    winsock_session winsock;
    if (argc == 2 &&
        _wcsicmp(argv[1], L"--failure-self-test") == 0) {
      validate_fail_closed_paths();
      return 0;
    }

    auto client = open_policy_client();
    auto receiver = make_receiver();
    const auto before =
        client.invoke(wfp_udp_content_filter::query_stats);
    constexpr std::string_view allowed =
        "ALLOW:ntl-udp-content-filter";
    constexpr std::string_view blocked =
        "BLOCKME:ntl-udp-content-filter";

    std::wcout << L"[1/5] Inspecting outbound UDP port "
               << receiver.port << L".\n";
    {
      ntl::wfp::dynamic_session policy(
          L"crtsys ntl::wfp UDP content-filter sample");
      install_policy(policy, receiver.port);
      auto policy_run = run_policy(client, 2);
      auto sender = make_sender(receiver.port);

      std::wcout
          << L"[2/5] Sending one allowed and one blocked datagram.\n";
      send_datagram(sender, allowed);
      send_datagram(sender, blocked);
      if (receive_datagram(receiver.socket.get(), 3000) != allowed ||
          !has_no_datagram(receiver.socket.get())) {
        std::wcerr
            << L"Typed UDP policy did not exclusively permit the "
               L"allowed datagram.\n";
        return 2;
      }

      const auto counts = policy_run.get();
      if (counts.permitted != 1 || counts.blocked != 1) {
        std::wcerr
            << L"Coroutine policy did not return two typed verdicts.\n";
        return 3;
      }
      std::wcout
          << L"[3/5] UDP permit/reinject and block both succeeded.\n";

      const auto after =
          client.invoke(wfp_udp_content_filter::query_stats);
      if (after.queued < before.queued + 2 ||
          after.permitted < before.permitted + 1 ||
          after.blocked < before.blocked + 1 ||
          after.timed_out != before.timed_out ||
          after.failed != before.failed) {
        std::wcerr
            << L"Driver statistics did not prove both verdicts.\n";
        return 4;
      }
    }

    std::wcout
        << L"[4/5] Dynamic WFP policy removed; sending BLOCKME again.\n";
    send_datagram(receiver.port, blocked);
    if (receive_datagram(receiver.socket.get(), 2000) != blocked) {
      std::wcerr
          << L"Policy removal did not restore ordinary UDP delivery.\n";
      return 5;
    }
    std::wcout << L"[5/5] Ordinary UDP delivery restored.\n";
    client.unsubscribe(wfp_udp_content_filter::inspection_requests);
    client.close_session();

    std::wcout
        << L"NTL WFP UDP content-filter ok: complete-udp=2, "
           L"permit=1, block=1, coroutine=used, restored=success\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NTL WFP UDP content-filter failed: "
              << error.what() << '\n';
    return 1;
  }
}
