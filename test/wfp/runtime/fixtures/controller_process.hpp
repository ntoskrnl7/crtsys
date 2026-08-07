#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace crtsys::wfp_test {

inline std::wstring quote_argument(std::wstring_view value) {
  std::wstring result = L"\"";
  for (const wchar_t character : value) {
    if (character == L'\"')
      result.push_back(L'\\');
    result.push_back(character);
  }
  result.push_back(L'\"');
  return result;
}

class controller_process {
public:
  controller_process(
      const std::filesystem::path &executable,
      std::vector<std::wstring> arguments,
      const std::filesystem::path &ipc_directory)
      : ipc_(std::filesystem::absolute(ipc_directory)) {
    std::filesystem::create_directories(ipc_);
    remove(L"controller.ready");
    remove(L"stop.request");
    remove(L"controller.stats");
    remove(L"controller.error");
    remove(L"release-policy");
    remove(L"policy.released");
    remove(L"stop-driver");
    remove(L"driver.stopped");
    remove(L"start-driver");
    remove(L"driver.started");
    remove(L"enable-block");
    remove(L"block.ready");
    remove(L"disable-block");
    remove(L"recovered.ready");

    std::wstring command = quote_argument(
        std::filesystem::absolute(executable).wstring());
    for (const auto &argument : arguments) {
      command.push_back(L' ');
      command += quote_argument(argument);
    }
    std::vector<wchar_t> mutable_command(
        command.begin(), command.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (!::CreateProcessW(
            nullptr, mutable_command.data(), nullptr, nullptr,
            FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
            &startup, &process_))
      throw std::system_error(
          ::GetLastError(), std::system_category(),
          "CreateProcessW(controller)");
  }

  controller_process(const controller_process &) = delete;
  controller_process &operator=(const controller_process &) = delete;

  ~controller_process() {
    if (process_.hProcess) {
      if (::WaitForSingleObject(process_.hProcess, 0) == WAIT_TIMEOUT)
        ::TerminateProcess(process_.hProcess, ERROR_CANCELLED);
      ::CloseHandle(process_.hThread);
      ::CloseHandle(process_.hProcess);
    }
  }

  void wait_ready(
      std::chrono::seconds timeout = std::chrono::seconds(30)) {
    wait_file(L"controller.ready", timeout);
  }

  void wait_file(
      std::wstring_view name,
      std::chrono::seconds timeout = std::chrono::seconds(30)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const auto path = ipc_ / std::filesystem::path(name);
    for (;;) {
      std::error_code error;
      if (std::filesystem::exists(path, error))
        return;
      if (error)
        throw std::system_error(error, "query controller IPC file");
      if (::WaitForSingleObject(process_.hProcess, 0) == WAIT_OBJECT_0)
        throw std::runtime_error("controller exited before IPC signal");
      if (std::chrono::steady_clock::now() >= deadline)
        throw std::runtime_error("controller IPC wait timed out");
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
  }

  void command(std::wstring_view name) const {
    std::ofstream stream(
        ipc_ / std::filesystem::path(name),
        std::ios::binary | std::ios::trunc);
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream << "command\n";
  }

  void stop() {
    command(L"stop.request");
    wait_exit();
  }

  void terminate_for_acceptance() {
    if (!::TerminateProcess(process_.hProcess, ERROR_PROCESS_ABORTED))
      throw std::system_error(
          ::GetLastError(), std::system_category(),
          "TerminateProcess(controller)");
    if (::WaitForSingleObject(process_.hProcess, 30000) != WAIT_OBJECT_0)
      throw std::runtime_error("terminated controller did not exit");
  }

  void wait_exit(
      std::chrono::seconds timeout = std::chrono::seconds(30)) {
    const DWORD waited = ::WaitForSingleObject(
        process_.hProcess,
        static_cast<DWORD>(timeout.count() * 1000));
    if (waited != WAIT_OBJECT_0)
      throw std::runtime_error("controller did not exit in time");
    DWORD code = 0;
    if (!::GetExitCodeProcess(process_.hProcess, &code))
      throw std::system_error(
          ::GetLastError(), std::system_category(),
          "GetExitCodeProcess(controller)");
    if (code != ERROR_SUCCESS) {
      std::ifstream stream(ipc_ / L"controller.error", std::ios::binary);
      const std::string detail = stream
                                     ? std::string(
                                           std::istreambuf_iterator<char>(stream),
                                           std::istreambuf_iterator<char>())
                                     : std::string{};
      throw std::runtime_error(
          detail.empty() ? "controller returned failure"
                         : "controller returned failure: " + detail);
    }
  }

  std::string stats() const {
    std::ifstream stream(ipc_ / L"controller.stats", std::ios::binary);
    if (!stream)
      throw std::runtime_error("controller stats are missing");
    return {std::istreambuf_iterator<char>(stream), {}};
  }

private:
  void remove(std::wstring_view name) noexcept {
    std::error_code ignored;
    (void)std::filesystem::remove(
        ipc_ / std::filesystem::path(name), ignored);
  }

  std::filesystem::path ipc_;
  PROCESS_INFORMATION process_{};
};

} // namespace crtsys::wfp_test
