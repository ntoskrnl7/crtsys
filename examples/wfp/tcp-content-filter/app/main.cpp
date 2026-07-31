#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
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
#include "tcp_content_filter_contract.hpp"

namespace {

using flow_layer_v4 = wfp_tcp_content_filter::flow_layer_v4;
using flow_layer_v6 = wfp_tcp_content_filter::flow_layer_v6;
using stream_layer_v4 = wfp_tcp_content_filter::stream_layer_v4;
using stream_layer_v6 = wfp_tcp_content_filter::stream_layer_v6;

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

struct tcp_listener {
  socket_owner socket;
  int family = AF_UNSPEC;
  std::uint16_t port = 0;
};

tcp_listener make_listener(int family = AF_INET,
                           std::uint16_t requested_port = 0) {
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
  return {std::move(socket), family, port};
}

socket_owner connect_tcp(int family, std::uint16_t port) {
  socket_owner socket(::socket(family, SOCK_STREAM, IPPROTO_TCP));
  if (socket.get() == INVALID_SOCKET)
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
  if (connect(socket.get(), reinterpret_cast<const sockaddr *>(&storage),
              address_size) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "connect");
  return socket;
}

socket_owner accept_tcp(const tcp_listener &listener) {
  socket_owner accepted(accept(listener.socket.get(), nullptr, nullptr));
  if (accepted.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "accept");
  return accepted;
}

std::string frame_message(std::string_view content) {
  if (content.size() > (std::numeric_limits<std::uint32_t>::max)())
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

void send_all(SOCKET socket, std::string_view bytes) {
  std::size_t offset = 0;
  while (offset != bytes.size()) {
    const std::size_t remaining = bytes.size() - offset;
    const int chunk = static_cast<int>((
        std::min)(remaining,
                  static_cast<std::size_t>((std::numeric_limits<int>::max)())));
    const int sent = send(socket, bytes.data() + offset, chunk, 0);
    if (sent == SOCKET_ERROR)
      throw std::system_error(WSAGetLastError(), std::system_category(),
                              "send");
    if (sent == 0)
      throw std::runtime_error("TCP send made no progress");
    offset += static_cast<std::size_t>(sent);
  }
}

std::string receive_exactly(SOCKET socket, std::size_t size, DWORD timeout_ms) {
  if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char *>(&timeout_ms),
                 sizeof(timeout_ms)) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "setsockopt(SO_RCVTIMEO)");
  std::string bytes(size, '\0');
  std::size_t offset = 0;
  while (offset != size) {
    const int received = recv(
        socket, bytes.data() + offset,
        static_cast<int>(
            (std::min)(size - offset, static_cast<std::size_t>(
                                          (std::numeric_limits<int>::max)()))),
        0);
    if (received == SOCKET_ERROR)
      throw std::system_error(WSAGetLastError(), std::system_category(),
                              "recv");
    if (received == 0)
      throw std::runtime_error("TCP stream closed before a full frame");
    offset += static_cast<std::size_t>(received);
  }
  return bytes;
}

std::string exchange_message(const tcp_listener &listener,
                             std::string_view content, bool split_prefix) {
  auto client = connect_tcp(listener.family, listener.port);
  auto server = accept_tcp(listener);
  const std::string frame = frame_message(content);
  if (split_prefix && frame.size() > 2) {
    send_all(client.get(), std::string_view(frame).substr(0, 2));
    ::Sleep(20);
    send_all(client.get(), std::string_view(frame).substr(2));
  } else {
    send_all(client.get(), frame);
  }
  return receive_exactly(server.get(), frame.size(), 3000);
}

bool receive_flow_reset(SOCKET socket, DWORD timeout_ms) {
  if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char *>(&timeout_ms),
                 sizeof(timeout_ms)) == SOCKET_ERROR)
    return false;
  std::array<char, 16> bytes{};
  const int received =
      recv(socket, bytes.data(), static_cast<int>(bytes.size()), 0);
  if (received == 0)
    return true;
  if (received != SOCKET_ERROR)
    return false;
  const int error = WSAGetLastError();
  return error == WSAECONNRESET || error == WSAECONNABORTED;
}

bool blocked_message(const tcp_listener &listener, std::string_view content) {
  auto client = connect_tcp(listener.family, listener.port);
  auto server = accept_tcp(listener);
  send_all(client.get(), frame_message(content));
  return receive_flow_reset(server.get(), 3000);
}

ntl::rpc::client open_policy_client() {
  ntl::rpc::client client(wfp_tcp_content_filter::endpoint_name);
  if (!client)
    throw std::runtime_error("could not open TCP content-filter RPC endpoint");

  ntl::rpc::contract_requirements requirements;
  requirements.contract_version(wfp_tcp_content_filter::contract_version)
      .transport_features(ntl::rpc::transport_features::asynchronous_calls |
                          ntl::rpc::transport_features::client_sessions |
                          ntl::rpc::transport_features::reliable_notifications)
      .capabilities(wfp_tcp_content_filter::capabilities::current)
      .notification(wfp_tcp_content_filter::inspection_requests)
      .method(wfp_tcp_content_filter::submit_verdict)
      .method(wfp_tcp_content_filter::query_stats);
  (void)client.require_contract(requirements);
  (void)client.start_session();
  client.subscribe(wfp_tcp_content_filter::inspection_requests);
  return client;
}

ntl::net::inspection::verdict
inspect_request(const wfp_tcp_content_request &request) {
  const auto bytes =
      std::as_bytes(std::span(request.frame.data(), request.frame.size()));
  if (request.content_offset !=
          wfp_tcp_content_filter::sample_u32_be_prefix_size ||
      request.frame.size() <
          wfp_tcp_content_filter::sample_u32_be_prefix_size ||
      request.frame.size() > wfp_tcp_content_filter::maximum_frame_size ||
      request.content_size !=
          request.frame.size() -
              wfp_tcp_content_filter::sample_u32_be_prefix_size)
    return ntl::net::inspection::verdict::drop_flow;

  const std::uint32_t encoded_size =
      (static_cast<std::uint32_t>(request.frame[0]) << 24) |
      (static_cast<std::uint32_t>(request.frame[1]) << 16) |
      (static_cast<std::uint32_t>(request.frame[2]) << 8) |
      static_cast<std::uint32_t>(request.frame[3]);
  if (encoded_size != request.content_size)
    return ntl::net::inspection::verdict::drop_flow;

  ntl::net::inspection::context metadata{
      ntl::net::inspection::content_kind::opaque,
      ntl::net::inspection::direction::inbound,
      request.id,
      request.source_port,
      request.destination_port,
  };
  const ntl::net::inspection::content_view frame(bytes);
  auto content = frame.subview(request.content_offset, request.content_size);
  if (!content)
    return ntl::net::inspection::verdict::drop_flow;
  const ntl::net::inspection::tcp_message_view message(metadata, frame,
                                                       *content);
  return ntl::net::inspection::evaluate(
      [](const ntl::net::inspection::tcp_message_view &value) {
        const auto blocked = value.content().contains("BLOCKME");
        if (!blocked)
          return ntl::net::inspection::verdict::drop_flow;
        return *blocked ? ntl::net::inspection::verdict::drop_flow
                        : ntl::net::inspection::verdict::permit;
      },
      message);
}

struct policy_counts {
  std::uint32_t permitted = 0;
  std::uint32_t blocked = 0;
};

wfp_tcp_content_filter_app::coroutine_task<policy_counts>
run_policy(ntl::rpc::client &client, std::size_t request_count) {
  policy_counts counts;
  for (std::size_t index = 0; index != request_count; ++index) {
    auto delivery = co_await client.receive_reliable_async(
        wfp_tcp_content_filter::inspection_requests);
    const auto verdict = inspect_request(delivery.payload());
    const auto wire = verdict == ntl::net::inspection::verdict::permit
                          ? wfp_tcp_content_filter::wire_verdict::permit
                          : wfp_tcp_content_filter::wire_verdict::block;
    const std::int32_t result =
        client.invoke(wfp_tcp_content_filter::submit_verdict,
                      delivery.payload().id, static_cast<std::uint8_t>(wire));
    if (result != STATUS_SUCCESS)
      throw std::runtime_error(
          "submit TCP content verdict failed with NTSTATUS " +
          std::to_string(result));
    client.acknowledge(wfp_tcp_content_filter::inspection_requests, delivery);
    if (wire == wfp_tcp_content_filter::wire_verdict::permit)
      ++counts.permitted;
    else
      ++counts.blocked;
  }
  co_return counts;
}

void install_policy(ntl::wfp::policy_session &session,
                    std::uint16_t destination_port) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_tcp_content_filter::provider_key,
         L"crtsys NTL WFP TCP content-filter provider",
         L"Ephemeral provider for framed inbound TCP inspection"});
    const auto sublayer = transaction.add_sublayer(
        provider, {wfp_tcp_content_filter::sublayer_key,
                   L"crtsys NTL WFP TCP content-filter sublayer",
                   L"Fail-closed user-mode TCP message inspection", 0x7600});
    const auto flow_callout_v4 = transaction.add_callout<flow_layer_v4>(
        provider, {wfp_tcp_content_filter::flow_callout_key_v4,
                   L"Attach inbound IPv4 TCP inspection state",
                   L"Associates bounded state with the selected server flow"});
    const auto stream_callout_v4 = transaction.add_callout<stream_layer_v4>(
        provider,
        {wfp_tcp_content_filter::stream_callout_key_v4,
         L"Inspect complete IPv4 sample-protocol TCP messages",
         L"Defers IPv4 inbound data until a user-mode verdict resumes it"});
    const auto flow_callout_v6 = transaction.add_callout<flow_layer_v6>(
        provider, {wfp_tcp_content_filter::flow_callout_key_v6,
                   L"Attach inbound IPv6 TCP inspection state",
                   L"Associates bounded state with the selected server flow"});
    const auto stream_callout_v6 = transaction.add_callout<stream_layer_v6>(
        provider,
        {wfp_tcp_content_filter::stream_callout_key_v6,
         L"Inspect complete IPv6 sample-protocol TCP messages",
         L"Defers IPv6 inbound data until a user-mode verdict resumes it"});

    ntl::wfp::inspection_filter_builder<flow_layer_v4> flow_filter_v4(
        wfp_tcp_content_filter::flow_filter_key_v4,
        L"Attach state to inbound IPv4 TCP flows for the selected server");
    flow_filter_v4.protocol_equal(IPPROTO_TCP)
        .direction_equal(FWP_DIRECTION_INBOUND)
        .local_port_equal(destination_port);
    transaction.add_inspection_filter(sublayer, flow_callout_v4,
                                      flow_filter_v4);

    ntl::wfp::stream_control_filter_builder<stream_layer_v4> stream_filter_v4(
        wfp_tcp_content_filter::stream_filter_key_v4,
        L"Defer framed IPv4 TCP application messages for a typed verdict",
        ntl::wfp::callout_unavailable::block);
    stream_filter_v4.local_port_equal(destination_port);
    transaction.add_stream_control_filter(sublayer, stream_callout_v4,
                                          stream_filter_v4);

    ntl::wfp::inspection_filter_builder<flow_layer_v6> flow_filter_v6(
        wfp_tcp_content_filter::flow_filter_key_v6,
        L"Attach state to inbound IPv6 TCP flows for the selected server");
    flow_filter_v6.protocol_equal(IPPROTO_TCP)
        .direction_equal(FWP_DIRECTION_INBOUND)
        .local_port_equal(destination_port);
    transaction.add_inspection_filter(sublayer, flow_callout_v6,
                                      flow_filter_v6);

    ntl::wfp::stream_control_filter_builder<stream_layer_v6> stream_filter_v6(
        wfp_tcp_content_filter::stream_filter_key_v6,
        L"Defer framed IPv6 TCP application messages for a typed verdict",
        ntl::wfp::callout_unavailable::block);
    stream_filter_v6.local_port_equal(destination_port);
    transaction.add_stream_control_filter(sublayer, stream_callout_v6,
                                          stream_filter_v6);
  });
}

void validate_fail_closed_paths() {
  auto client = open_policy_client();
  auto listener = make_listener();
  const auto before = client.invoke(wfp_tcp_content_filter::query_stats);
  std::uint64_t request_id = 0;

  {
    auto policy = ntl::wfp::policy_session::ephemeral(
        L"crtsys ntl::wfp TCP content-filter failure self-test");
    install_policy(policy, listener.port);

    auto sender = connect_tcp(listener.family, listener.port);
    auto receiver = accept_tcp(listener);
    send_all(sender.get(), frame_message("TIMEOUT:tcp"));

    const auto delivery =
        client.receive_reliable(wfp_tcp_content_filter::inspection_requests);
    request_id = delivery.payload().id;
    const auto malformed = client.invoke(wfp_tcp_content_filter::submit_verdict,
                                         request_id, std::uint8_t{0});
    if (malformed != STATUS_INVALID_PARAMETER)
      throw std::runtime_error("driver accepted a malformed TCP verdict");
    if (!receive_flow_reset(receiver.get(), 4000))
      throw std::runtime_error("timed-out TCP message did not fail closed");

    wfp_tcp_content_filter_stats after{};
    bool accounted = false;
    for (std::size_t attempt = 0; attempt != 20; ++attempt) {
      after = client.invoke(wfp_tcp_content_filter::query_stats);
      if (after.timed_out >= before.timed_out + 1 &&
          after.blocked >= before.blocked + 1) {
        accounted = true;
        break;
      }
      ::Sleep(25);
    }
    if (!accounted)
      throw std::runtime_error(
          "TCP timeout statistics did not prove fail-closed handling");

    const auto late =
        client.invoke(wfp_tcp_content_filter::submit_verdict, request_id,
                      static_cast<std::uint8_t>(
                          wfp_tcp_content_filter::wire_verdict::permit));
    if (late != STATUS_NOT_FOUND)
      throw std::runtime_error(
          "driver accepted a late TCP permit after timeout");
    client.acknowledge(wfp_tcp_content_filter::inspection_requests, delivery);
  }

  constexpr std::string_view restored = "TIMEOUT:restored";
  if (exchange_message(listener, restored, false) != frame_message(restored))
    throw std::runtime_error(
        "failure self-test did not restore TCP after policy removal");
  client.close_session();
  std::wcout << L"NTL WFP TCP content-filter fail-closed self-test ok: "
                L"malformed=blocked, timeout=flow-dropped, "
                L"late-permit=rejected\n";
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    winsock_session winsock;
    if (argc == 2 && _wcsicmp(argv[1], L"--failure-self-test") == 0) {
      validate_fail_closed_paths();
      return 0;
    }

    auto client = open_policy_client();
    auto listener_v4 = make_listener(AF_INET);
    auto listener_v6 = make_listener(AF_INET6, listener_v4.port);
    const auto before = client.invoke(wfp_tcp_content_filter::query_stats);
    constexpr std::string_view allowed = "ALLOW:ntl-tcp-content-filter";
    constexpr std::string_view blocked = "BLOCKME:ntl-tcp-content-filter";

    std::wcout << L"[1/6] Inspecting inbound sample-protocol TCP port "
               << listener_v4.port << L" on IPv4 and IPv6.\n";
    {
      auto policy = ntl::wfp::policy_session::ephemeral(
          L"crtsys ntl::wfp TCP content-filter sample");
      install_policy(policy, listener_v4.port);
      auto policy_run = run_policy(client, 4);

      std::wcout << L"[2/6] Sending an allowed message with its sample "
                    L"length prefix split across two writes.\n";
      if (exchange_message(listener_v4, allowed, true) !=
              frame_message(allowed) ||
          exchange_message(listener_v6, allowed, true) !=
              frame_message(allowed)) {
        std::wcerr << L"Permitted TCP frame was not delivered unchanged.\n";
        return 2;
      }

      std::wcout << L"[3/6] Sending BLOCKME as one complete application "
                    L"message; the flow must be dropped.\n";
      if (!blocked_message(listener_v4, blocked) ||
          !blocked_message(listener_v6, blocked)) {
        std::wcerr << L"Blocked TCP content did not close the flow.\n";
        return 3;
      }

      const auto counts = policy_run.get();
      if (counts.permitted != 2 || counts.blocked != 2) {
        std::wcerr << L"Coroutine policy did not return two typed verdicts.\n";
        return 4;
      }
      std::wcout
          << L"[4/6] Both complete messages were decided in user mode.\n";

      const auto after = client.invoke(wfp_tcp_content_filter::query_stats);
      if (after.queued < before.queued + 4 ||
          after.permitted < before.permitted + 2 ||
          after.blocked < before.blocked + 2 ||
          after.timed_out != before.timed_out ||
          after.failed != before.failed) {
        std::wcerr << L"Driver statistics did not prove both verdicts.\n";
        return 5;
      }
    }

    std::wcout
        << L"[5/6] Ephemeral WFP policy removed; sending BLOCKME again.\n";
    if (exchange_message(listener_v4, blocked, false) !=
            frame_message(blocked) ||
        exchange_message(listener_v6, blocked, false) !=
            frame_message(blocked)) {
      std::wcerr << L"Policy removal did not restore ordinary TCP delivery.\n";
      return 6;
    }
    std::wcout << L"[6/6] Ordinary TCP delivery restored.\n";
    client.unsubscribe(wfp_tcp_content_filter::inspection_requests);
    client.close_session();

    std::wcout << L"NTL WFP TCP content-filter ok: complete-tcp=4, "
                  L"permit=2, block=2, tcp-prefix-split=handled, "
                  L"coroutine=used, ipv4=pass, ipv6=pass, restored=success\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NTL WFP TCP content-filter failed: " << error.what() << '\n';
    return 1;
  }
}
