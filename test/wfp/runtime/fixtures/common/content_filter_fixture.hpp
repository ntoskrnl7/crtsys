#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace crtsys::test::wfp::content_filter_fixture {

inline std::wstring quote(std::wstring_view value) {
  std::wstring result = L"\"";
  std::size_t backslashes = 0;
  for (const wchar_t character : value) {
    if (character == L'\\') {
      ++backslashes;
      continue;
    }
    if (character == L'\"') {
      result.append(backslashes * 2 + 1, L'\\');
      result.push_back(character);
      backslashes = 0;
      continue;
    }
    result.append(backslashes, L'\\');
    backslashes = 0;
    result.push_back(character);
  }
  result.append(backslashes * 2, L'\\');
  result.push_back(L'\"');
  return result;
}

inline std::filesystem::path sibling_executable(std::wstring_view name) {
  std::vector<wchar_t> path(32768);
  const DWORD length = ::GetModuleFileNameW(
      nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length == path.size())
    throw std::system_error(::GetLastError(), std::system_category(),
                            "GetModuleFileNameW(fixture)");
  return std::filesystem::path(std::wstring(path.data(), length))
      .parent_path() /
      std::wstring(name);
}

inline std::filesystem::path controller_argument(
    int argc, wchar_t **argv, std::wstring_view default_name) {
  if (argc == 1)
    return sibling_executable(default_name);
  if (argc == 3 && std::wstring_view(argv[1]) == L"--controller")
    return std::filesystem::absolute(argv[2]);
  throw std::invalid_argument("usage: acceptance [--controller <path>]");
}

class state_directory {
public:
  explicit state_directory(std::wstring_view label) {
    const auto root = std::filesystem::temp_directory_path();
    for (unsigned attempt = 0; attempt != 32; ++attempt) {
      path_ = root /
              (std::wstring(L"crtsys-") + std::wstring(label) + L"-" +
               std::to_wstring(::GetCurrentProcessId()) + L"-" +
               std::to_wstring(::GetTickCount64()) + L"-" +
               std::to_wstring(attempt));
      std::error_code error;
      if (std::filesystem::create_directory(path_, error))
        return;
      if (error && error != std::errc::file_exists)
        throw std::filesystem::filesystem_error(
            "create content-filter fixture directory", path_, error);
    }
    throw std::runtime_error("cannot allocate content-filter fixture state");
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
  controller_process(const std::filesystem::path &executable,
                     std::uint16_t port,
                     const std::filesystem::path &state,
                     std::size_t expected_requests = 0,
                     std::wstring_view behavior = L"normal")
      : ready_(state / "ready"), stop_(state / "stop"),
        stats_(state / "stats") {
    if (!std::filesystem::is_regular_file(executable))
      throw std::invalid_argument("controller executable does not exist");
    std::wstring command =
        quote(executable.wstring()) + L" --port " + std::to_wstring(port) +
        L" --ready-file " + quote(ready_.wstring()) + L" --stop-file " +
        quote(stop_.wstring()) + L" --stats-file " + quote(stats_.wstring()) +
        L" --duration-ms 60000 --behavior " + quote(behavior);
    if (expected_requests != 0)
      command += L" --expected-requests " +
                 std::to_wstring(expected_requests);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr,
                          executable.parent_path().c_str(), &startup,
                          &process))
      throw std::system_error(::GetLastError(), std::system_category(),
                              "CreateProcessW(content-filter controller)");
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

  void wait_ready(std::uint32_t timeout_ms = 10'000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (!std::filesystem::exists(ready_)) {
      if (::WaitForSingleObject(process_, 0) == WAIT_OBJECT_0)
        throw std::runtime_error(
            "content-filter controller exited before ready");
      if (std::chrono::steady_clock::now() >= deadline)
        throw std::runtime_error(
            "content-filter controller ready timeout");
      ::Sleep(20);
    }
  }

  void request_stop() const {
    std::ofstream output(stop_, std::ios::binary | std::ios::trunc);
    if (!output)
      throw std::runtime_error("cannot signal content-filter controller stop");
    output << "stop\n";
  }

  void wait(std::uint32_t timeout_ms = 15'000) {
    const DWORD waited = ::WaitForSingleObject(process_, timeout_ms);
    if (waited != WAIT_OBJECT_0)
      throw std::runtime_error("content-filter controller exit timeout");
    DWORD exit_code = ERROR_GEN_FAILURE;
    if (!::GetExitCodeProcess(process_, &exit_code))
      throw std::system_error(::GetLastError(), std::system_category(),
                              "GetExitCodeProcess(content-filter controller)");
    completed_ = true;
    if (exit_code != 0)
      throw std::runtime_error("content-filter controller reported failure");
  }

  const std::filesystem::path &stats_file() const noexcept { return stats_; }

private:
  std::filesystem::path ready_;
  std::filesystem::path stop_;
  std::filesystem::path stats_;
  HANDLE process_ = nullptr;
  HANDLE thread_ = nullptr;
  bool completed_ = false;
};

inline std::unordered_map<std::string, std::uint64_t>
read_stats(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("content-filter controller stats are missing");
  std::unordered_map<std::string, std::uint64_t> result;
  std::string line;
  while (std::getline(input, line)) {
    const std::size_t separator = line.find('=');
    if (separator == std::string::npos || separator == 0 ||
        separator + 1 == line.size())
      throw std::runtime_error("malformed content-filter controller stats");
    std::size_t consumed = 0;
    const auto value = std::stoull(line.substr(separator + 1), &consumed, 10);
    if (consumed != line.size() - separator - 1)
      throw std::runtime_error("invalid content-filter controller statistic");
    if (!result.emplace(line.substr(0, separator), value).second)
      throw std::runtime_error("duplicate content-filter controller statistic");
  }
  return result;
}

inline std::uint64_t require_stat(
    const std::unordered_map<std::string, std::uint64_t> &stats,
    std::string_view name) {
  const auto found = stats.find(std::string(name));
  if (found == stats.end())
    throw std::runtime_error("required controller statistic is missing");
  return found->second;
}

} // namespace crtsys::test::wfp::content_filter_fixture
