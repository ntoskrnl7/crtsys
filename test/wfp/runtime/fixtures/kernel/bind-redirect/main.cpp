#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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
  socket_owner result(::socket(family, SOCK_DGRAM, IPPROTO_UDP));
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

std::uint16_t bind_loopback(socket_owner &socket, int family) {
  if (family == AF_INET) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(socket.get(), reinterpret_cast<const sockaddr *>(&address),
               sizeof(address)) == SOCKET_ERROR)
      throw_socket("bind(IPv4)");
    int size = sizeof(address);
    if (::getsockname(socket.get(), reinterpret_cast<sockaddr *>(&address),
                      &size) == SOCKET_ERROR)
      throw_socket("getsockname(IPv4)");
    return ntohs(address.sin_port);
  }

  sockaddr_in6 address{};
  address.sin6_family = AF_INET6;
  address.sin6_addr = in6addr_loopback;
  address.sin6_port = 0;
  if (::bind(socket.get(), reinterpret_cast<const sockaddr *>(&address),
             sizeof(address)) == SOCKET_ERROR)
    throw_socket("bind(IPv6)");
  int size = sizeof(address);
  if (::getsockname(socket.get(), reinterpret_cast<sockaddr *>(&address),
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
    address.sin_port = htons(wfp_bind_redirect::redirected_port_v4);
    if (::bind(socket.get(), reinterpret_cast<const sockaddr *>(&address),
               sizeof(address)) == SOCKET_ERROR)
      throw_socket("sample IPv4 target port is unavailable");
  }
  {
    auto socket = create_udp_socket(AF_INET6);
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = htons(wfp_bind_redirect::redirected_port_v6);
    if (::bind(socket.get(), reinterpret_cast<const sockaddr *>(&address),
               sizeof(address)) == SOCKET_ERROR)
      throw_socket("sample IPv6 target port is unavailable");
  }
}

std::wstring quote(std::wstring_view value) {
  std::wstring result = L"\"";
  for (const wchar_t character : value) {
    if (character == L'\"')
      result.push_back(L'\\');
    result.push_back(character);
  }
  result.push_back(L'\"');
  return result;
}

std::filesystem::path executable_path() {
  std::vector<wchar_t> buffer(32768);
  const DWORD length = ::GetModuleFileNameW(
      nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 || length == buffer.size())
    throw std::system_error(
        ::GetLastError(), std::system_category(), "GetModuleFileNameW");
  return std::filesystem::path(std::wstring(buffer.data(), length));
}

class state_directory {
public:
  state_directory() {
    const auto root = std::filesystem::temp_directory_path();
    for (unsigned attempt = 0; attempt != 32; ++attempt) {
      path_ = root /
              (L"crtsys-bind-redirect-" +
               std::to_wstring(::GetCurrentProcessId()) + L"-" +
               std::to_wstring(::GetTickCount64()) + L"-" +
               std::to_wstring(attempt));
      std::error_code error;
      if (std::filesystem::create_directory(path_, error))
        return;
      if (error && error != std::errc::file_exists)
        throw std::filesystem::filesystem_error(
            "create bind-redirect fixture directory", path_, error);
    }
    throw std::runtime_error("cannot create bind-redirect fixture state");
  }
  state_directory(const state_directory &) = delete;
  state_directory &operator=(const state_directory &) = delete;
  ~state_directory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  const std::filesystem::path &path() const noexcept { return path_; }

private:
  std::filesystem::path path_;
};

class controller_process {
public:
  controller_process(const std::filesystem::path &controller,
                     const std::filesystem::path &application,
                     const std::filesystem::path &state)
      : ready_(state / L"ready"), stop_(state / L"stop") {
    if (!std::filesystem::is_regular_file(controller))
      throw std::invalid_argument("bind-redirect controller was not found");
    std::wstring command =
        quote(controller.wstring()) + L" --application " +
        quote(application.wstring()) + L" --ready-file " +
        quote(ready_.wstring()) + L" --stop-file " +
        quote(stop_.wstring()) + L" --duration-ms 60000";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr,
                          controller.parent_path().c_str(), &startup,
                          &process))
      throw std::system_error(
          ::GetLastError(), std::system_category(),
          "CreateProcessW(bind-redirect controller)");
    process_ = process.hProcess;
    thread_ = process.hThread;
  }
  controller_process(const controller_process &) = delete;
  controller_process &operator=(const controller_process &) = delete;
  ~controller_process() {
    if (process_ && !completed_) {
      (void)::TerminateProcess(process_, ERROR_CANCELLED);
      (void)::WaitForSingleObject(process_, 5000);
    }
    if (thread_)
      ::CloseHandle(thread_);
    if (process_)
      ::CloseHandle(process_);
  }

  void wait_ready() const {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(10);
    while (!std::filesystem::exists(ready_)) {
      if (::WaitForSingleObject(process_, 0) == WAIT_OBJECT_0)
        throw std::runtime_error(
            "bind-redirect controller exited before ready");
      if (std::chrono::steady_clock::now() >= deadline)
        throw std::runtime_error("bind-redirect controller ready timeout");
      ::Sleep(20);
    }
  }

  void stop_and_wait() {
    {
      std::ofstream output(stop_, std::ios::binary | std::ios::trunc);
      if (!output)
        throw std::runtime_error("cannot signal bind-redirect controller");
      output << "stop\n";
    }
    if (::WaitForSingleObject(process_, 15000) != WAIT_OBJECT_0)
      throw std::runtime_error("bind-redirect controller exit timeout");
    DWORD exit_code = ERROR_GEN_FAILURE;
    if (!::GetExitCodeProcess(process_, &exit_code))
      throw std::system_error(
          ::GetLastError(), std::system_category(),
          "GetExitCodeProcess(bind-redirect controller)");
    completed_ = true;
    if (exit_code != 0)
      throw std::runtime_error("bind-redirect controller failed");
  }

private:
  std::filesystem::path ready_;
  std::filesystem::path stop_;
  HANDLE process_ = nullptr;
  HANDLE thread_ = nullptr;
  bool completed_ = false;
};

std::filesystem::path controller_path(int argc, wchar_t **argv) {
  if (argc == 1)
    return executable_path().parent_path() /
           L"crtsys_wfp_bind_redirect_controller.exe";
  if (argc == 3 && std::wstring_view(argv[1]) == L"--controller")
    return std::filesystem::absolute(argv[2]);
  throw std::invalid_argument(
      "usage: crtsys_wfp_bind_redirect_acceptance.exe "
      "[--controller <path>]");
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    winsock_session winsock;
    verify_target_ports_available();
    const auto application = executable_path();
    state_directory state;

    std::uint16_t redirected_port_v4 = 0;
    std::uint16_t redirected_port_v6 = 0;
    socket_owner redirected_v4;
    socket_owner redirected_v6;
    {
      controller_process controller(
          controller_path(argc, argv), application, state.path());
      controller.wait_ready();

      redirected_v4 = create_udp_socket(AF_INET);
      redirected_v6 = create_udp_socket(AF_INET6);
      redirected_port_v4 = bind_loopback(redirected_v4, AF_INET);
      redirected_port_v6 = bind_loopback(redirected_v6, AF_INET6);
      if (redirected_port_v4 != wfp_bind_redirect::redirected_port_v4 ||
          redirected_port_v6 != wfp_bind_redirect::redirected_port_v6)
        throw std::runtime_error(
            "typed bind redirect did not select both target ports");
      controller.stop_and_wait();
    }

    auto direct_v4 = create_udp_socket(AF_INET);
    auto direct_v6 = create_udp_socket(AF_INET6);
    const std::uint16_t direct_port_v4 = bind_loopback(direct_v4, AF_INET);
    const std::uint16_t direct_port_v6 = bind_loopback(direct_v6, AF_INET6);
    if (direct_port_v4 == redirected_port_v4 ||
        direct_port_v6 == redirected_port_v6)
      throw std::runtime_error(
          "bind remained redirected after controller policy removal");

    std::wcout << L"NTL WFP bind-redirect ok: IPv4="
               << redirected_port_v4 << L", IPv6="
               << redirected_port_v6
               << L", restored=controller-exit\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NTL WFP bind-redirect acceptance failed: "
              << error.what() << '\n';
    return 1;
  }
}
