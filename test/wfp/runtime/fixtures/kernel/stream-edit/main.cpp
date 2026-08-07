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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <ntl/net/framing>
#include <ntl/net/io/async_framed_stream>
#include <ntl/net/io/async_socket>

#include "coroutine_task.hpp"
#include "runtime_controller_fixture.hpp"
#include "stream_edit_contract.hpp"

namespace {

namespace fixture = crtsys::test::wfp::runtime_fixture;
inline constexpr std::string_view inline_token = "BLOCKME";
inline constexpr std::string_view inline_replacement = "REDACT!";
inline constexpr std::string_view oob_token = "OOBBLOCK";
inline constexpr std::string_view oob_replacement = "[OOB-REDACTED]";

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
  SOCKET release() noexcept {
    return std::exchange(value_, INVALID_SOCKET);
  }

private:
  SOCKET value_ = INVALID_SOCKET;
};

struct listener {
  socket_owner socket;
  std::uint16_t port = 0;
};

listener make_listener() {
  socket_owner socket(::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr,
                                   0, WSA_FLAG_OVERLAPPED));
  if (socket.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "socket(listener)");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::bind(socket.get(), reinterpret_cast<const sockaddr *>(&address),
             sizeof(address)) == SOCKET_ERROR ||
      ::listen(socket.get(), 4) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "bind/listen");
  int size = sizeof(address);
  if (::getsockname(socket.get(), reinterpret_cast<sockaddr *>(&address),
                    &size) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "getsockname");
  return {std::move(socket), ntohs(address.sin_port)};
}

socket_owner connect_client(std::uint16_t port) {
  socket_owner client(::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr,
                                   0, WSA_FLAG_OVERLAPPED));
  if (client.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "WSASocket(client)");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (::connect(client.get(), reinterpret_cast<const sockaddr *>(&address),
                sizeof(address)) == SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "connect");
  return client;
}

wfp_stream_edit_fixture::coroutine_task<std::string>
receive_exactly(ntl::net::async_socket &socket, std::size_t size) {
  std::string received(size, '\0');
  const std::size_t count = co_await socket.read_exactly_borrowed(
      std::as_writable_bytes(std::span(received)));
  if (count != size)
    throw std::runtime_error("co_await read_exactly completed short");
  co_return received;
}

wfp_stream_edit_fixture::coroutine_task<std::size_t>
write_part(ntl::net::async_socket &socket, const std::string &part) {
  co_return co_await socket.write_all(
      std::as_bytes(std::span<const char>(part.data(), part.size())));
}

wfp_stream_edit_fixture::coroutine_task<std::size_t>
wait_for_one_byte(ntl::net::async_socket &socket) {
  std::array<std::byte, 1> byte{};
  co_return co_await socket.read_some_borrowed(byte);
}

wfp_stream_edit_fixture::coroutine_task<std::vector<std::string>>
receive_framed_messages(
    ntl::net::async_framed_stream<ntl::net::framing::u32_be_length_prefix>
        &stream,
    std::size_t count) {
  std::vector<std::string> messages;
  messages.reserve(count);
  for (std::size_t index = 0; index != count; ++index) {
    auto message = co_await stream.read_frame();
    const auto content = message.content();
    messages.emplace_back(reinterpret_cast<const char *>(content.data()),
                          content.size());
  }
  co_return messages;
}

std::string frame_message(std::string_view payload) {
  if (payload.size() > (std::numeric_limits<std::uint32_t>::max)())
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
  socket_owner accepted(::accept(server.socket.get(), nullptr, nullptr));
  if (accepted.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "accept(framed contract)");
  ntl::net::async_socket async_client(context, client.release());
  ntl::net::async_socket async_server(context, accepted.release());
  ntl::net::async_framed_stream framed(
      std::move(async_server), ntl::net::framing::u32_be_length_prefix{1024},
      ntl::net::framing::frame_limits{1028}, 64);
  auto receive = receive_framed_messages(framed, 2);
  const std::string first = frame_message("dynamic-length-one");
  const std::string second = frame_message("BLOCKME-in-message");
  if (write_part(async_client, first.substr(0, 2)).get() != 2 ||
      write_part(async_client, first.substr(2) + second).get() !=
          first.size() - 2 + second.size())
    throw std::runtime_error("framed coroutine write was short");
  const auto messages = receive.get();
  if (messages.size() != 2 || messages[0] != "dynamic-length-one" ||
      messages[1] != "BLOCKME-in-message")
    throw std::runtime_error("framed coroutine changed message boundaries");
  context.wait_for_idle();
}

std::string exchange_tcp(const listener &server,
                         const std::vector<std::string> &parts) {
  ntl::net::io_completion_context context;
  auto client = connect_client(server.port);
  socket_owner accepted(::accept(server.socket.get(), nullptr, nullptr));
  if (accepted.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "accept");
  ntl::net::async_socket async_client(context, client.release());
  ntl::net::async_socket async_server(context, accepted.release());
  std::size_t expected_size = 0;
  for (const auto &part : parts)
    expected_size += part.size();
  auto receiver = receive_exactly(async_server, expected_size);
  for (const auto &part : parts) {
    if (write_part(async_client, part).get() != part.size())
      throw std::runtime_error("co_await write_all completed short");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (::shutdown(async_client.borrowed_native_handle(), SD_SEND) ==
      SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "shutdown(client)");
  auto received = receiver.get();
  context.wait_for_idle();
  return received;
}

std::string receive_from_server(const listener &server,
                                const std::vector<std::string> &parts,
                                std::size_t expected_size) {
  ntl::net::io_completion_context context;
  auto client = connect_client(server.port);
  socket_owner accepted(::accept(server.socket.get(), nullptr, nullptr));
  if (accepted.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "accept(server send)");
  ntl::net::async_socket async_client(context, client.release());
  ntl::net::async_socket async_server(context, accepted.release());
  auto receiver = receive_exactly(async_client, expected_size);
  for (const auto &part : parts) {
    if (write_part(async_server, part).get() != part.size())
      throw std::runtime_error("server co_await write_all completed short");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (::shutdown(async_server.borrowed_native_handle(), SD_SEND) ==
      SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "shutdown(server)");
  auto received = receiver.get();
  context.wait_for_idle();
  return received;
}

void validate_coroutine_cancellation() {
  auto server = make_listener();
  ntl::net::io_completion_context context;
  auto client = connect_client(server.port);
  socket_owner accepted(::accept(server.socket.get(), nullptr, nullptr));
  if (accepted.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "accept(cancel contract)");
  ntl::net::async_socket async_client(context, client.release());
  ntl::net::async_socket async_server(context, accepted.release());
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
  socket_owner accepted(::accept(server.socket.get(), nullptr, nullptr));
  if (accepted.get() == INVALID_SOCKET)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "accept(EOF contract)");
  ntl::net::async_socket async_client(context, client.release());
  ntl::net::async_socket async_server(context, accepted.release());
  auto pending = receive_exactly(async_server, 2);
  if (write_part(async_client, "x").get() != 1)
    throw std::runtime_error("EOF fixture write completed short");
  if (::shutdown(async_client.borrowed_native_handle(), SD_SEND) ==
      SOCKET_ERROR)
    throw std::system_error(::WSAGetLastError(), std::system_category(),
                            "shutdown(EOF contract)");
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

void run_coroutine_contract() {
  wfp_stream_edit::oob_pending_budget budget;
  for (std::size_t index = 0;
       index != wfp_stream_edit::maximum_oob_pending; ++index) {
    if (!budget.try_acquire())
      throw std::runtime_error("OOB pending budget rejected early");
  }
  if (budget.try_acquire() ||
      budget.pending() != wfp_stream_edit::maximum_oob_pending ||
      budget.rejections() != 1)
    throw std::runtime_error("OOB pending budget did not apply backpressure");
  for (std::size_t index = 0;
       index != wfp_stream_edit::maximum_oob_pending; ++index) {
    if (!budget.release())
      throw std::runtime_error("OOB pending budget released early");
  }
  if (budget.release() || budget.pending() != 0)
    throw std::runtime_error("OOB pending budget underflowed");

  auto listener = make_listener();
  if (exchange_tcp(listener, {"fragmented-", "coroutine-", "io"}) !=
      "fragmented-coroutine-io")
    throw std::runtime_error("coroutine loopback exchange changed bytes");
  validate_coroutine_cancellation();
  validate_coroutine_eof();
  validate_framed_coroutine();
  std::wcout << L"Kernel stream-edit coroutine contract PASS: IOCP read/"
                L"write/cancel/EOF, split/coalesced framing, and bounded "
                L"OOB backpressure.\n";
}

std::filesystem::path parse_controller(int argc, wchar_t **argv) {
  if (argc == 1)
    return fixture::sibling_executable(
        L"crtsys_wfp_stream_edit_controller.exe");
  if (argc == 3 && std::wstring_view(argv[1]) == L"--controller")
    return std::filesystem::absolute(argv[2]);
  throw std::invalid_argument(
      "usage: acceptance [--controller <path>] | --coroutine-contract");
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    winsock_session winsock;
    if (argc == 2 &&
        std::wstring_view(argv[1]) == L"--coroutine-contract") {
      run_coroutine_contract();
      return 0;
    }
    const auto controller = parse_controller(argc, argv);
    auto server = make_listener();
    fixture::state_directory state(L"kernel-stream-edit");
    fixture::controller_process policy(
        controller, state.path(), {{L"--port", std::to_wstring(server.port)}});
    policy.wait_ready();

    const std::string original = "before-" + std::string(inline_token) +
                                 "-after";
    const std::string edited = "before-" + std::string(inline_replacement) +
                               "-after";
    if (exchange_tcp(server, {"before-BLO", "CKME-after"}) != edited)
      throw std::runtime_error("inline split-boundary replacement failed");
    constexpr std::string_view passthrough = "OOBPASS";
    if (receive_from_server(server, {std::string(passthrough)},
                            passthrough.size()) != passthrough)
      throw std::runtime_error("OOB pass-through clone changed bytes");
    if (receive_from_server(server, {"OOB", "BLOCK"},
                            oob_replacement.size()) != oob_replacement)
      throw std::runtime_error("OOB variable-length replacement failed");

    policy.request_stop();
    policy.wait();
    const auto stats = fixture::read_stats(policy.stats_file());
    if (fixture::require_stat(stats, "policy.port") != server.port ||
        fixture::require_stat(stats, "policy.inline_edit") != 1 ||
        fixture::require_stat(stats, "policy.oob_edit") != 1)
      throw std::runtime_error("stream-edit controller stats are wrong");
    if (exchange_tcp(server, {original}) != original ||
        receive_from_server(server, {std::string(oob_token)},
                            oob_token.size()) != oob_token)
      throw std::runtime_error("stream bytes were not restored after stop");

    std::wcout << L"Kernel stream-edit acceptance PASS: inline and OOB "
                  L"replacement, pass-through clone, and policy cleanup.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Kernel stream-edit acceptance failed: " << error.what()
              << '\n';
    return 1;
  }
}
