#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#include "controller_process.hpp"

namespace {

class winsock_session {
public:
  winsock_session() {
    WSADATA data{};
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result)
      throw std::system_error(result, std::system_category(), "WSAStartup");
  }
  ~winsock_session() { WSACleanup(); }
};

class socket_owner {
public:
  explicit socket_owner(SOCKET value = INVALID_SOCKET) noexcept : value_(value) {}
  socket_owner(const socket_owner &) = delete;
  socket_owner &operator=(const socket_owner &) = delete;
  socket_owner(socket_owner &&other) noexcept
      : value_(std::exchange(other.value_, INVALID_SOCKET)) {}
  ~socket_owner() { if (value_ != INVALID_SOCKET) closesocket(value_); }
  SOCKET get() const noexcept { return value_; }
private:
  SOCKET value_;
};

struct listener { socket_owner socket; std::uint16_t port; };

[[noreturn]] void throw_socket(const char *operation) {
  throw std::system_error(WSAGetLastError(), std::system_category(), operation);
}

listener make_listener() {
  socket_owner value(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (value.get() == INVALID_SOCKET)
    throw_socket("socket(listener)");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(value.get(), reinterpret_cast<const sockaddr *>(&address),
           sizeof(address)) == SOCKET_ERROR)
    throw_socket("bind(listener)");
  int size = sizeof(address);
  if (getsockname(value.get(), reinterpret_cast<sockaddr *>(&address),
                  &size) == SOCKET_ERROR)
    throw_socket("getsockname(listener)");
  if (listen(value.get(), 32) == SOCKET_ERROR)
    throw_socket("listen");
  return {std::move(value), ntohs(address.sin_port)};
}

int connect_once(std::uint16_t port) {
  socket_owner client(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (client.get() == INVALID_SOCKET)
    throw_socket("socket(client)");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (connect(client.get(), reinterpret_cast<const sockaddr *>(&address),
              sizeof(address)) == 0)
    return ERROR_SUCCESS;
  return WSAGetLastError();
}

std::filesystem::path executable_path() {
  std::array<wchar_t, 32768> buffer{};
  const DWORD length = GetModuleFileNameW(
      nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (!length || length == buffer.size())
    throw std::system_error(GetLastError(), std::system_category(),
                            "GetModuleFileNameW");
  return std::filesystem::path(buffer.data(), buffer.data() + length);
}

void run_one_shot(
    const std::filesystem::path &controller,
    std::wstring_view mode, std::uint16_t port,
    const std::filesystem::path &ipc) {
  crtsys::wfp_test::controller_process child(
      controller,
      {std::wstring(mode), std::to_wstring(port), ipc.wstring()}, ipc);
  child.wait_ready();
  child.stop();
  if (child.stats().find("state=") == std::string::npos)
    throw std::runtime_error("persistent controller stats are missing");
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc != 3 && argc != 4)
      throw std::invalid_argument(
          "usage: crtsys_wfp_ale_connect_block_acceptance.exe "
          "<controller.exe> <ipc-directory> "
          "[--persistent-lifecycle|--arbitration|--crash-recovery]");
    winsock_session winsock;
    auto server = make_listener();
    const auto controller = std::filesystem::absolute(argv[1]);
    const auto ipc = std::filesystem::absolute(argv[2]);
    const std::wstring_view mode = argc == 4 ? argv[3] : L"--serve";
    if (mode == L"--serve") {
      crtsys::wfp_test::controller_process child(
          controller,
          {L"--serve", std::to_wstring(server.port), ipc.wstring()}, ipc);
      child.wait_ready();
      if (connect_once(server.port) != WSAEACCES)
        throw std::runtime_error("ALE block policy did not deny connect");
      child.stop();
      if (connect_once(server.port) != ERROR_SUCCESS)
        throw std::runtime_error("connect did not recover after policy stop");
      std::cout
          << "NTL WFP ale-connect-block ok: blocked_error=10013, "
             "restored_connect=success, port="
          << server.port << "\n";
    } else if (mode == L"--persistent-lifecycle") {
      bool persistent_graph_may_exist = false;
      try {
        persistent_graph_may_exist = true;
        run_one_shot(controller, L"--persistent-install", server.port, ipc);
        if (connect_once(server.port) != WSAEACCES)
          throw std::runtime_error("persistent filter did not survive close");
        run_one_shot(controller, L"--persistent-check", server.port, ipc);
        run_one_shot(controller, L"--persistent-migrate", server.port, ipc);
        run_one_shot(controller, L"--persistent-rollback", server.port, ipc);
        run_one_shot(controller, L"--persistent-recover", server.port, ipc);
        if (connect_once(server.port) != WSAEACCES)
          throw std::runtime_error("recovered persistent graph did not block");
        run_one_shot(
            controller, L"--persistent-uninstall", server.port, ipc);
        persistent_graph_may_exist = false;
        if (connect_once(server.port) != ERROR_SUCCESS)
          throw std::runtime_error("connect did not recover after uninstall");
      } catch (...) {
        if (persistent_graph_may_exist) {
          try {
            run_one_shot(
                controller, L"--persistent-uninstall", server.port, ipc);
          } catch (...) {
          }
        }
        throw;
      }
      std::cout
          << "NTL WFP persistent lifecycle ok: boot-time=block, "
             "reconcile=atomic, migrate=2, rollback=1, recover=1, "
             "session-close=retained, uninstall=clean, port="
          << server.port << "\n";
    } else if (mode == L"--arbitration") {
      crtsys::wfp_test::controller_process child(
          controller,
          {L"--arbitration", std::to_wstring(server.port),
           executable_path().wstring(), ipc.wstring()}, ipc);
      child.wait_ready();
      if (connect_once(server.port) != ERROR_SUCCESS)
        throw std::runtime_error("high permit provider did not allow");
      child.command(L"enable-block");
      child.wait_file(L"block.ready");
      if (connect_once(server.port) != WSAEACCES)
        throw std::runtime_error("lower block provider did not veto");
      child.command(L"disable-block");
      child.wait_file(L"recovered.ready");
      if (connect_once(server.port) != ERROR_SUCCESS)
        throw std::runtime_error("connect did not recover after block removal");
      child.stop();
      std::cout
          << "NTL WFP provider-arbitration ok: high-permit=installed, "
             "lower-block=veto, block-removal=recovered, port="
          << server.port << "\n";
    } else if (mode == L"--crash-recovery") {
      crtsys::wfp_test::controller_process child(
          controller,
          {L"--serve", std::to_wstring(server.port), ipc.wstring()}, ipc);
      child.wait_ready();
      if (connect_once(server.port) != WSAEACCES)
        throw std::runtime_error("held policy did not block before crash");
      child.terminate_for_acceptance();
      bool recovered = false;
      for (std::size_t attempt = 0; attempt != 100; ++attempt) {
        if (connect_once(server.port) == ERROR_SUCCESS) {
          recovered = true;
          break;
        }
        Sleep(50);
      }
      if (!recovered)
        throw std::runtime_error(
            "dynamic policy did not disappear after controller crash");
      std::cout
          << "NTL WFP policy-process crash recovery ok: held=blocked, "
             "killed=cleanup, restarted-probe=permitted\n";
    } else {
      throw std::invalid_argument("unknown ALE acceptance mode");
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "ALE connect-block acceptance failed: "
              << error.what() << '\n';
    return 1;
  }
}
