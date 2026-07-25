#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <Windows.h>
#include <fltUser.h>

#define NTL_USER_MODE
#include <ntl/flt/communication_client>

#include "../delete_shared/delete_runtime.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

namespace fs = std::filesystem;
using namespace crtsys_flt_delete_runtime_test;

class unique_handle {
public:
  unique_handle() noexcept = default;
  explicit unique_handle(HANDLE value) noexcept : value_(value) {}

  unique_handle(const unique_handle &) = delete;
  unique_handle &operator=(const unique_handle &) = delete;

  unique_handle(unique_handle &&other) noexcept
      : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}

  unique_handle &operator=(unique_handle &&other) noexcept {
    if (this != &other) {
      reset();
      value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
    }
    return *this;
  }

  ~unique_handle() { reset(); }

  HANDLE get() const noexcept { return value_; }

  void reset() noexcept {
    if (value_ != INVALID_HANDLE_VALUE) {
      CloseHandle(value_);
      value_ = INVALID_HANDLE_VALUE;
    }
  }

private:
  HANDLE value_ = INVALID_HANDLE_VALUE;
};

[[noreturn]] void fail(std::string_view operation,
                       DWORD error = GetLastError()) {
  std::ostringstream message;
  message << operation << " failed: error=" << error;
  throw std::runtime_error(message.str());
}

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string(message));
}

fs::path relative_path(std::wstring_view value) {
  while (!value.empty() && (value.front() == L'\\' || value.front() == L'/'))
    value.remove_prefix(1);
  return fs::path(value);
}

bool path_exists(const fs::path &path) noexcept {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES)
    return true;
  const DWORD error = GetLastError();
  if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
    return false;
  return false;
}

void write_text(const fs::path &path, std::string_view contents) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    fail("open seed file");
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!output.good())
    fail("write seed file");
}

unique_handle open_delete(const fs::path &path) {
  HANDLE handle =
      CreateFileW(path.c_str(), GENERIC_READ | DELETE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    fail("CreateFileW(delete access)");
  return unique_handle(handle);
}

void set_legacy_disposition(HANDLE handle, bool delete_requested) {
  FILE_DISPOSITION_INFO disposition{};
  disposition.DeleteFile = delete_requested ? TRUE : FALSE;
  if (!SetFileInformationByHandle(handle, FileDispositionInfo, &disposition,
                                  sizeof(disposition))) {
    fail(delete_requested ? "set legacy delete disposition"
                          : "clear legacy delete disposition");
  }
}

void set_extended_disposition(HANDLE handle, DWORD flags) {
  FILE_DISPOSITION_INFO_EX disposition{};
  disposition.Flags = flags;
  if (!SetFileInformationByHandle(handle, FileDispositionInfoEx, &disposition,
                                  sizeof(disposition))) {
    fail("set extended delete disposition");
  }
}

void verify_legacy_clear(const fs::path &path) {
  write_text(path, "legacy clear\n");
  auto handle = open_delete(path);
  set_legacy_disposition(handle.get(), true);
  set_legacy_disposition(handle.get(), false);
  handle.reset();
  require(path_exists(path), "legacy disposition clear did not preserve file");
}

void verify_on_close_clear(const fs::path &path) {
  HANDLE raw =
      CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, CREATE_ALWAYS,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
  if (raw == INVALID_HANDLE_VALUE)
    fail("CreateFileW(delete-on-close to clear)");
  unique_handle handle(raw);
  set_extended_disposition(handle.get(), FILE_DISPOSITION_FLAG_ON_CLOSE);
  handle.reset();
  require(path_exists(path),
          "extended on-close disposition clear did not preserve file");
}

void verify_readonly_extended_delete(const fs::path &path) {
  write_text(path, "readonly extended delete\n");
  if (!SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY))
    fail("SetFileAttributesW(readonly)");

  auto handle = open_delete(path);
  set_extended_disposition(
      handle.get(),
      FILE_DISPOSITION_FLAG_DELETE |
          FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
          FILE_DISPOSITION_FLAG_FORCE_IMAGE_SECTION_CHECK |
          FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE);
  handle.reset();
  require(!path_exists(path),
          "extended ignore-readonly disposition did not delete file");
}

void verify_create_delete_on_close(const fs::path &path) {
  HANDLE raw =
      CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, CREATE_ALWAYS,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
  if (raw == INVALID_HANDLE_VALUE)
    fail("CreateFileW(FILE_FLAG_DELETE_ON_CLOSE)");
  unique_handle handle(raw);
  handle.reset();
  require(!path_exists(path), "FILE_DELETE_ON_CLOSE did not delete file");
}

fs::path stream_path(const fs::path &base) {
  std::wstring value = base.wstring();
  value.push_back(L':');
  value.append(stream_name);
  return fs::path(std::move(value));
}

void verify_stream_only_delete(const fs::path &base) {
  write_text(base, "base survives stream deletion\n");
  const fs::path stream = stream_path(base);
  write_text(stream, "named stream\n");
  auto handle = open_delete(stream);
  set_legacy_disposition(handle.get(), true);
  handle.reset();
  require(path_exists(base), "deleting an ADS also deleted its base file");
  require(!path_exists(stream), "named stream disposition did not delete ADS");
}

void verify_base_delete(const fs::path &base) {
  write_text(base, "delete the complete file\n");
  write_text(stream_path(base), "base-owned stream\n");
  auto handle = open_delete(base);
  set_legacy_disposition(handle.get(), true);
  handle.reset();
  require(!path_exists(base), "base disposition did not delete complete file");
}

void verify_pending_delete(const fs::path &path) {
  write_text(path, "existing handle remains usable\n");
  auto keeper = open_delete(path);
  auto deleter = open_delete(path);
  set_legacy_disposition(deleter.get(), true);

  std::array<char, 8> buffer{};
  DWORD bytes_read = 0;
  require(ReadFile(keeper.get(), buffer.data(),
                   static_cast<DWORD>(buffer.size()), &bytes_read, nullptr) !=
              FALSE &&
              bytes_read != 0,
          "an existing handle stopped working while deletion was pending");

  deleter.reset();
  keeper.reset();
  require(!path_exists(path),
          "pending deletion did not complete after the last handle closed");
}

void verify_racing_dispositions(ntl::flt::communication_client &client,
                                const fs::path &path) {
  write_text(path, "racing disposition\n");
  auto first = open_delete(path);
  auto second = open_delete(path);
  require(client.invoke(arm_race_gate) == 1,
          "the driver rejected the race-gate request");

  std::array<DWORD, 2> errors{};
  std::thread set_delete([&] {
    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = TRUE;
    if (!SetFileInformationByHandle(first.get(), FileDispositionInfo,
                                    &disposition, sizeof(disposition))) {
      errors[0] = GetLastError();
    }
  });
  std::thread clear_delete([&] {
    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = FALSE;
    if (!SetFileInformationByHandle(second.get(), FileDispositionInfo,
                                    &disposition, sizeof(disposition))) {
      errors[1] = GetLastError();
    }
  });
  set_delete.join();
  clear_delete.join();
  if (errors[0] != ERROR_SUCCESS)
    fail("racing delete disposition", errors[0]);
  if (errors[1] != ERROR_SUCCESS)
    fail("racing clear disposition", errors[1]);

  set_legacy_disposition(first.get(), true);
  second.reset();
  first.reset();
  require(!path_exists(path), "final disposition did not delete raced file");
}

observations wait_for_observations(ntl::flt::communication_client &client) {
  observations observed{};
  for (unsigned attempt = 0; attempt != 100; ++attempt) {
    observed = client.invoke(query_observations);
    if (observed.disposition_races >= 1 &&
        observed.race_gate_arrivals >= 2 &&
        observed.stream_deletions >= 1 && observed.file_deletions >= 5 &&
        observed.completion_states_created ==
            observed.completion_states_destroyed) {
      return observed;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return observed;
}

void print_observations(const observations &value) {
  std::cout << "last_cleanup_status=0x" << std::hex << std::uppercase
            << static_cast<std::uint32_t>(value.last_cleanup_status)
            << std::dec << std::nouppercase
            << " create_delete_on_close=" << value.create_delete_on_close
            << " legacy=" << value.legacy_requests
            << " extended=" << value.extended_requests
            << " delete=" << value.delete_requests
            << " clear=" << value.clear_requests
            << " on_close=" << value.on_close_requests
            << " posix=" << value.posix_requests
            << " force_image=" << value.force_image_section_requests
            << " ignore_readonly=" << value.ignore_readonly_requests
            << " set_success=" << value.set_information_successes
            << " set_failure=" << value.set_information_failures
            << " races=" << value.disposition_races
            << " race_arrivals=" << value.race_gate_arrivals
            << " cleanup_checks=" << value.cleanup_checks
            << " cleanup_present=" << value.cleanup_present
            << " file_deletions=" << value.file_deletions
            << " stream_deletions=" << value.stream_deletions
            << " completion_states=" << value.completion_states_created << '/'
            << value.completion_states_destroyed
            << " stream_contexts=" << value.stream_contexts_created << '/'
            << value.stream_contexts_destroyed << '\n';
}

void require_observations(const observations &value) {
  require(value.create_delete_on_close >= 1,
          "driver missed FILE_DELETE_ON_CLOSE create");
  require(value.legacy_requests >= 8,
          "driver missed legacy disposition operations");
  require(value.extended_requests >= 2,
          "driver missed extended disposition operations");
  require(value.delete_requests >= 1 && value.clear_requests >= 1,
          "driver did not distinguish set and clear requests");
  require(value.on_close_requests >= 1,
          "driver missed FILE_DISPOSITION_ON_CLOSE");
  require(value.posix_requests >= 1 &&
              value.force_image_section_requests >= 1 &&
              value.ignore_readonly_requests >= 1,
          "driver missed one or more extended disposition flags");
  require(value.set_information_failures == 0,
          "a tracked set-information operation failed");
  require(value.disposition_races >= 1 && value.race_gate_arrivals >= 2,
          "driver did not detect the forced disposition race");
  require(value.cleanup_checks >= 1 && value.cleanup_present >= 2,
          "driver did not perform successful post-cleanup state queries");
  require(value.file_deletions >= 5,
          "driver did not confirm all expected whole-file deletions");
  require(value.stream_deletions >= 1,
          "driver did not distinguish named-stream deletion");
  require(value.completion_states_created ==
              value.completion_states_destroyed,
          "typed completion-state ownership did not balance");
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  if (argc > 2) {
    std::cerr
        << "usage: crtsys_flt_delete_runtime_test_app [volume-root]\n";
    return 2;
  }

  try {
    fs::path root =
        argc == 2 ? fs::path(argv[1]) : fs::current_path().root_path();
    root = root.root_path();
    if (root.empty())
      throw std::runtime_error("a volume root such as C:\\ is required");

    const fs::path directory = root / relative_path(test_directory_name);
    std::error_code error;
    fs::remove_all(directory, error);
    error.clear();
    if (!fs::create_directories(directory, error) || error) {
      throw std::runtime_error("failed to create test directory: " +
                               error.message());
    }

    auto client = ntl::flt::communication_client::connect(port_name);
    require(client.invoke(reset_observations) == 1,
            "the driver rejected the counter reset");

    verify_legacy_clear(directory / legacy_clear_name);
    verify_on_close_clear(directory / on_close_clear_name);
    verify_readonly_extended_delete(directory / readonly_name);
    verify_create_delete_on_close(directory / delete_on_close_name);
    verify_stream_only_delete(directory / stream_base_name);
    verify_base_delete(directory / base_delete_name);
    verify_pending_delete(directory / pending_name);
    verify_racing_dispositions(client, directory / race_name);

    const observations observed = wait_for_observations(client);
    print_observations(observed);
    require_observations(observed);

    fs::remove_all(directory, error);
    if (error)
      throw std::runtime_error("failed to clean test directory: " +
                               error.message());

    std::cout << "NTL delete runtime test PASS\n";
    return 0;
  } catch (const std::exception &failure) {
    std::cerr << "NTL delete runtime test FAIL: " << failure.what() << '\n';
    return 1;
  }
}
