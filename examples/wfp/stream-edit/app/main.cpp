#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <ntl/net/io/async_framed_stream>
#include <ntl/net/io/async_socket>
#include <ntl/net/framing>
#include <ntl/wfp/all>

#include "coroutine_task.hpp"
#include "stream_edit_contract.hpp"

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
  SOCKET release() noexcept {
    return std::exchange(value_, INVALID_SOCKET);
  }

private:
  SOCKET value_ = INVALID_SOCKET;
};

struct listener {
  socket_owner socket;
  std::uint16_t port;
};

listener make_listener() {
  socket_owner socket(::WSASocketW(
      AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
      WSA_FLAG_OVERLAPPED));
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

socket_owner connect_client(std::uint16_t port) {
  socket_owner client(::WSASocketW(
      AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
      WSA_FLAG_OVERLAPPED));
  if (client.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "WSASocket(client)");

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (connect(client.get(), reinterpret_cast<const sockaddr *>(&address),
              sizeof(address)) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "connect");
  return client;
}

wfp_stream_edit_app::coroutine_task<std::string>
receive_exactly(ntl::net::async_socket &socket,
                std::size_t size) {
  std::string received(size, '\0');
  const std::size_t count = co_await socket.read_exactly(
      std::as_writable_bytes(std::span(received)));
  if (count != size)
    throw std::runtime_error("co_await read_exactly completed short");
  co_return received;
}

wfp_stream_edit_app::coroutine_task<std::size_t>
write_part(ntl::net::async_socket &socket,
           const std::string &part) {
  co_return co_await socket.write_all(std::as_bytes(
      std::span<const char>(part.data(), part.size())));
}

wfp_stream_edit_app::coroutine_task<std::size_t>
wait_for_one_byte(ntl::net::async_socket &socket) {
  std::array<std::byte, 1> byte{};
  co_return co_await socket.read_some(byte);
}

wfp_stream_edit_app::coroutine_task<std::vector<std::string>>
receive_framed_messages(
    ntl::net::async_framed_stream<
        ntl::net::framing::u32_be_length_prefix> &stream,
    std::size_t count) {
  std::vector<std::string> messages;
  messages.reserve(count);
  for (std::size_t index = 0; index != count; ++index) {
    auto message = co_await stream.read_frame();
    const auto content = message.content();
    messages.emplace_back(
        reinterpret_cast<const char *>(content.data()), content.size());
  }
  co_return messages;
}

std::string frame_message(std::string_view payload) {
  if (payload.size() >
      (std::numeric_limits<std::uint32_t>::max)())
    throw std::overflow_error("framed message is too large");
  const auto size = static_cast<std::uint32_t>(payload.size());
  std::string frame(4, '\0');
  frame[0] = static_cast<char>((size >> 24) & 0xff);
  frame[1] = static_cast<char>((size >> 16) & 0xff);
  frame[2] = static_cast<char>((size >> 8) & 0xff);
  frame[3] = static_cast<char>(size & 0xff);
  frame.append(payload);
  return frame;
}

void validate_framed_coroutine() {
  auto server = make_listener();
  ntl::net::io_completion_context context;
  auto client = connect_client(server.port);
  socket_owner accepted(
      accept(server.socket.get(), nullptr, nullptr));
  if (accepted.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "accept(framed test)");

  ntl::net::async_socket async_client(context, client.release());
  ntl::net::async_socket async_server(context, accepted.release());
  ntl::net::async_framed_stream framed(
      async_server, ntl::net::framing::u32_be_length_prefix{1024},
      ntl::net::framing::frame_limits{1028}, 64);
  auto receive = receive_framed_messages(framed, 2);

  const std::string first = frame_message("dynamic-length-one");
  const std::string second = frame_message("BLOCKME-in-message");
  auto prefix = write_part(async_client, first.substr(0, 2));
  if (prefix.get() != 2)
    throw std::runtime_error("fragmented frame prefix write was short");
  auto coalesced =
      write_part(async_client, first.substr(2) + second);
  if (coalesced.get() != first.size() - 2 + second.size())
    throw std::runtime_error("coalesced frame write was short");

  const auto messages = receive.get();
  if (messages.size() != 2 ||
      messages[0] != "dynamic-length-one" ||
      messages[1] != "BLOCKME-in-message")
    throw std::runtime_error(
        "framed coroutine did not preserve complete messages");
  context.wait_for_idle();
}

std::string exchange_tcp(const listener &server,
                          const std::vector<std::string> &parts) {
  ntl::net::io_completion_context context;
  auto client = connect_client(server.port);
  socket_owner accepted(
      accept(server.socket.get(), nullptr, nullptr));
  if (accepted.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "accept");

  ntl::net::async_socket async_client(
      context, client.release());
  ntl::net::async_socket async_server(
      context, accepted.release());
  std::size_t expected_size = 0;
  for (const auto &part : parts) {
    if (part.size() >
        (std::numeric_limits<std::size_t>::max)() - expected_size)
      throw std::overflow_error("loopback message length overflow");
    expected_size += part.size();
  }
  auto receiver = receive_exactly(async_server, expected_size);

  for (const auto &part : parts) {
    auto write = write_part(async_client, part);
    if (write.get() != part.size())
      throw std::runtime_error("co_await write_all completed short");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (shutdown(async_client.native_handle(), SD_SEND) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "shutdown(SD_SEND)");

  auto received = receiver.get();
  context.wait_for_idle();
  return received;
}

std::string receive_from_server(
    const listener &server, const std::vector<std::string> &parts,
    std::size_t expected_size) {
  ntl::net::io_completion_context context;
  auto client = connect_client(server.port);
  socket_owner accepted(
      accept(server.socket.get(), nullptr, nullptr));
  if (accepted.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "accept(server-send)");

  ntl::net::async_socket async_client(
      context, client.release());
  ntl::net::async_socket async_server(
      context, accepted.release());
  auto receiver = receive_exactly(async_client, expected_size);
  for (const auto &part : parts) {
    auto write = write_part(async_server, part);
    if (write.get() != part.size())
      throw std::runtime_error(
          "server co_await write_all completed short");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (shutdown(async_server.native_handle(), SD_SEND) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "shutdown(server SD_SEND)");

  auto received = receiver.get();
  context.wait_for_idle();
  return received;
}

void validate_coroutine_cancellation() {
  auto server = make_listener();
  ntl::net::io_completion_context context;
  auto client = connect_client(server.port);
  socket_owner accepted(
      accept(server.socket.get(), nullptr, nullptr));
  if (accepted.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "accept(cancel test)");

  ntl::net::async_socket async_client(
      context, client.release());
  ntl::net::async_socket async_server(
      context, accepted.release());
  auto pending = wait_for_one_byte(async_server);
  if (!async_server.cancel())
    throw std::runtime_error("CancelIoEx did not accept socket read");

  try {
    (void)pending.get();
  } catch (const std::system_error &error) {
    if (error.code().value() == ERROR_OPERATION_ABORTED) {
      context.wait_for_idle();
      return;
    }
    throw;
  }
  throw std::runtime_error("cancelled socket read unexpectedly succeeded");
}

void validate_coroutine_eof() {
  auto server = make_listener();
  ntl::net::io_completion_context context;
  auto client = connect_client(server.port);
  socket_owner accepted(
      accept(server.socket.get(), nullptr, nullptr));
  if (accepted.get() == INVALID_SOCKET)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "accept(EOF test)");

  ntl::net::async_socket async_client(
      context, client.release());
  ntl::net::async_socket async_server(
      context, accepted.release());
  auto pending = receive_exactly(async_server, 2);
  const std::string one_byte = "x";
  auto write = write_part(async_client, one_byte);
  if (write.get() != one_byte.size())
    throw std::runtime_error("EOF fixture write completed short");
  if (shutdown(async_client.native_handle(), SD_SEND) == SOCKET_ERROR)
    throw std::system_error(WSAGetLastError(), std::system_category(),
                            "shutdown(EOF test)");

  try {
    (void)pending.get();
  } catch (const std::system_error &error) {
    if (error.code().value() == ERROR_HANDLE_EOF) {
      context.wait_for_idle();
      return;
    }
    throw;
  }
  throw std::runtime_error("incomplete read_exactly ignored clean EOF");
}

void install_policy(ntl::wfp::policy_session &session,
                    std::uint16_t port) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_stream_edit::provider_key,
         L"crtsys NTL WFP stream-edit provider",
         L"Dynamic provider for one outbound TCP token replacement"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {wfp_stream_edit::sublayer_key,
         L"crtsys NTL WFP stream-edit sublayer",
         L"Flow association and stream-control filters", 0x7500});
    const auto flow_callout =
        transaction.add_callout<wfp_stream_edit::flow_layer>(
            provider,
            {wfp_stream_edit::flow_callout_key,
             L"Attach stream editor flow state",
             L"Associates typed state at ALE_FLOW_ESTABLISHED_V4"});
    const auto stream_callout =
        transaction.add_callout<wfp_stream_edit::stream_layer>(
            provider,
            {wfp_stream_edit::stream_callout_key,
             L"Replace the selected outbound token",
             L"Buffers boundaries, injects replacement, and blocks source "
             L"bytes"});

    ntl::wfp::inspection_filter_builder<
        wfp_stream_edit::flow_layer>
        flow_filter(wfp_stream_edit::flow_filter_key,
                    L"Attach state to the selected outbound TCP flow");
    flow_filter.protocol_equal(IPPROTO_TCP)
        .direction_equal(FWP_DIRECTION_OUTBOUND)
        .remote_port_equal(port);
    transaction.add_inspection_filter(
        sublayer, flow_callout, flow_filter);

    ntl::wfp::stream_control_filter_builder<
        wfp_stream_edit::stream_layer>
        stream_filter(wfp_stream_edit::stream_filter_key,
                      L"Replace BLOCKME with REDACT!",
                      ntl::wfp::callout_unavailable::permit);
    stream_filter.remote_port_equal(port);
    transaction.add_stream_control_filter(
        sublayer, stream_callout, stream_filter);
  });
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    winsock_session winsock;
    if (argc == 2 &&
        _wcsicmp(argv[1], L"--coroutine-self-test") == 0) {
      auto listener = make_listener();
      const std::string expected = "fragmented-coroutine-io";
      const auto received = exchange_tcp(
          listener, {"fragmented-", "coroutine-", "io"});
      if (received != expected)
        throw std::runtime_error(
            "coroutine loopback exchange returned unexpected bytes");
      validate_coroutine_cancellation();
      validate_coroutine_eof();
      validate_framed_coroutine();
      std::wcout
          << L"NTL WFP stream-edit coroutine self-test ok: "
             L"IOCP read/write/cancel/EOF, framed split/coalesced messages\n";
      return 0;
    }

    auto server = make_listener();
    const std::vector<std::string> split_message{
        "before-BLO", "CKME-after"};
    const std::string original = "before-BLOCKME-after";
    const std::string edited = "before-REDACT!-after";
    const std::string oob_original = wfp_stream_edit::oob_token;
    const std::string oob_edited =
        wfp_stream_edit::oob_replacement;
    const std::string oob_passthrough = "OOBPASS";

    std::wcout << L"[1/8] Editing TCP port " << server.port
               << L".\n";
    {
      std::wcout << L"[2/8] Installing flow and stream-control rules.\n";
      auto policy = ntl::wfp::policy_session::ephemeral(
          L"crtsys ntl::wfp stream-edit sample");
      install_policy(policy, server.port);

      std::wcout << L"[3/8] Sending BLOCKME with co_await write_all "
                    L"across two operations.\n";
      const auto result = exchange_tcp(server, split_message);
      if (result != edited) {
        std::cerr << "Unexpected edited stream: " << result << '\n';
        return 2;
      }
      std::wcout << L"[4/8] Server received the equal-length replacement "
                    L"REDACT!.\n";

      std::wcout << L"[5/8] Exercising deferred OOB clone/continue.\n";
      const auto cloned = receive_from_server(
          server, {oob_passthrough}, oob_passthrough.size());
      if (cloned != oob_passthrough) {
        std::cerr << "OOB clone changed pass-through bytes: "
                  << cloned << '\n';
        return 3;
      }

      std::wcout << L"[6/8] Exercising split-boundary variable-length "
                    L"OOB replacement.\n";
      const auto replaced = receive_from_server(
          server, {"OOB", "BLOCK"}, oob_edited.size());
      if (replaced != oob_edited) {
        std::cerr << "Unexpected OOB replacement: "
                  << replaced << '\n';
        return 4;
      }
    }

    std::wcout << L"[7/8] Policy removed; inline token must pass.\n";
    const auto restored = exchange_tcp(server, {original});
    if (restored != original) {
      std::cerr << "Original stream was not restored: " << restored
                << '\n';
      return 5;
    }
    std::wcout << L"[8/8] Policy removed; OOB token must pass.\n";
    const auto oob_restored = receive_from_server(
        server, {oob_original}, oob_original.size());
    if (oob_restored != oob_original) {
      std::cerr << "Original OOB stream was not restored: "
                << oob_restored << '\n';
      return 6;
    }

    std::wcout << L"NTL WFP stream-edit ok: BLOCKME->REDACT!, "
                  L"oob-clone=continued, oob-variable-replacement=handled, "
                  L"busy-threshold=bounded, restored=original\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NTL WFP stream-edit failed: " << error.what()
              << '\n';
    return 1;
  }
}
