#include "mini_spy_runtime.hpp"

#include "../shared/runtime_test.hpp"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace crtsys_flt_runtime_test {
namespace {

bool exercise_file(const std::filesystem::path &path, std::string &failure) {
  HANDLE file = CreateFileW(
      path.c_str(), GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    failure = "MiniSpy CreateFileW failed: " +
              std::to_string(GetLastError());
    return false;
  }

  const std::array<unsigned char, 32> input{
      0x43, 0x52, 0x54, 0x53, 1, 2, 3, 4};
  DWORD written = 0;
  if (!WriteFile(file, input.data(), static_cast<DWORD>(input.size()),
                 &written, nullptr) ||
      written != input.size()) {
    const DWORD error = GetLastError();
    CloseHandle(file);
    failure = "MiniSpy WriteFile failed: " + std::to_string(error);
    return false;
  }

  LARGE_INTEGER beginning{};
  if (!SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN)) {
    const DWORD error = GetLastError();
    CloseHandle(file);
    failure = "MiniSpy SetFilePointerEx failed: " +
              std::to_string(error);
    return false;
  }

  std::array<unsigned char, 32> output{};
  DWORD read = 0;
  if (!ReadFile(file, output.data(), static_cast<DWORD>(output.size()), &read,
                nullptr) ||
      read != output.size() || output != input) {
    const DWORD error = GetLastError();
    CloseHandle(file);
    failure = "MiniSpy ReadFile failed: " + std::to_string(error);
    return false;
  }
  if (!CloseHandle(file)) {
    failure = "MiniSpy CloseHandle failed: " +
              std::to_string(GetLastError());
    return false;
  }
  return true;
}

} // namespace

bool run_mini_spy_runtime_tests(ntl::flt::communication_client &client,
                                const std::filesystem::path &root,
                                std::string &failure) {
  namespace fs = std::filesystem;
  try {
    if (client.invoke(mini_spy_reset_method) != 1) {
      failure = "MiniSpy reset was rejected";
      return false;
    }

    std::vector<fs::path> paths;
    for (std::uint32_t index = 0; index != 24; ++index) {
      wchar_t suffix[16]{};
      (void)swprintf_s(suffix, L"%02u.tmp", index);
      const fs::path path =
          root / (std::wstring(mini_spy_file_prefix) + suffix);
      paths.push_back(path);
      if (!exercise_file(path, failure))
        return false;
    }

    std::uint64_t last_sequence = 0;
    std::uint64_t dropped = 0;
    std::uint32_t operation_mask = 0;
    std::uint32_t records = 0;
    for (;;) {
      const mini_spy_batch batch = client.invoke(mini_spy_read_method);
      if (batch.count > batch.records.size()) {
        failure = "MiniSpy returned an oversized batch";
        return false;
      }
      dropped = batch.dropped;
      for (std::size_t index = 0; index != batch.count; ++index) {
        const auto &record = batch.records[index];
        if (record.sequence <= last_sequence) {
          failure = "MiniSpy sequence order is not strictly increasing";
          return false;
        }
        last_sequence = record.sequence;
        if (record.operation <
                static_cast<std::uint32_t>(mini_spy_operation::create) ||
            record.operation >
                static_cast<std::uint32_t>(mini_spy_operation::close)) {
          failure = "MiniSpy returned an unknown operation";
          return false;
        }
        operation_mask |= 1u << (record.operation - 1);
        ++records;
      }
      if (batch.remaining == 0)
        break;
    }

    constexpr std::uint32_t every_operation_mask = (1u << 5) - 1;
    if (dropped == 0 || records == 0 ||
        operation_mask != every_operation_mask) {
      failure = "MiniSpy did not prove bounded overflow and five-operation "
                "coverage: dropped=" +
                std::to_string(dropped) + " records=" +
                std::to_string(records) + " operation_mask=0x";
      char mask[16]{};
      (void)sprintf_s(mask, "%x", operation_mask);
      failure += mask;
      return false;
    }

    if (client.invoke(mini_spy_reset_method) != 1) {
      failure = "MiniSpy second reset was rejected";
      return false;
    }
    if (!exercise_file(paths.front(), failure))
      return false;
    const mini_spy_batch closed =
        client.invoke(mini_spy_close_for_unload_method);
    if (closed.closed == 0 || closed.discarded_on_close == 0) {
      failure = "MiniSpy unload preparation did not discard pending records";
      return false;
    }
    const mini_spy_batch after_close = client.invoke(mini_spy_read_method);
    if (after_close.closed == 0 || after_close.count != 0 ||
        after_close.remaining != 0) {
      failure = "MiniSpy accepted or retained records after close";
      return false;
    }

    std::error_code ignored;
    for (const auto &path : paths)
      fs::remove(path, ignored);
    std::printf("minispy_logging=PASS operations=5 records=%u dropped=%llu "
                "unload_discarded=%u\n",
                records, static_cast<unsigned long long>(dropped),
                closed.discarded_on_close);
    return true;
  } catch (const std::exception &error) {
    failure = error.what();
    return false;
  }
}

} // namespace crtsys_flt_runtime_test
