#include "operation_log_sample.hpp"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace {

using crtsys_minifilter_operation_log_sample::operation;
using crtsys_minifilter_operation_log_sample::phase;

struct unique_handle {
  HANDLE value = INVALID_HANDLE_VALUE;
  ~unique_handle() {
    if (value != INVALID_HANDLE_VALUE)
      CloseHandle(value);
  }
};

std::wstring_view name_of(operation value) noexcept {
  switch (value) {
  case operation::create:
    return L"create";
  case operation::read:
    return L"read";
  case operation::write:
    return L"write";
  case operation::cleanup:
    return L"cleanup";
  case operation::close:
    return L"close";
  }
  return L"unknown";
}

std::wstring_view name_of(phase value) noexcept {
  return value == phase::post ? L"post" : L"pre";
}

} // namespace

int wmain() {
  using namespace crtsys_minifilter_operation_log_sample;

  try {
    auto client = connect();
    if (client.invoke(reset_method) != 1) {
      std::cerr << "the driver rejected the queue reset\n";
      return 1;
    }

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        L"crtsys_operation_log_sample.ntlspy";
    {
      unique_handle file{CreateFileW(
          path.c_str(), GENERIC_READ | GENERIC_WRITE,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
      if (file.value == INVALID_HANDLE_VALUE) {
        std::cerr << "CreateFileW failed: " << GetLastError() << '\n';
        return 1;
      }

      const std::array<std::uint8_t, 8> input{0x43, 0x52, 0x54, 0x53,
                                              1,    2,    3,    4};
      DWORD transferred = 0;
      if (!WriteFile(file.value, input.data(),
                     static_cast<DWORD>(input.size()), &transferred, nullptr) ||
          transferred != input.size()) {
        std::cerr << "WriteFile failed: " << GetLastError() << '\n';
        return 1;
      }

      LARGE_INTEGER beginning{};
      if (!SetFilePointerEx(file.value, beginning, nullptr, FILE_BEGIN)) {
        std::cerr << "SetFilePointerEx failed: " << GetLastError() << '\n';
        return 1;
      }

      std::array<std::uint8_t, 8> output{};
      if (!ReadFile(file.value, output.data(),
                    static_cast<DWORD>(output.size()), &transferred, nullptr) ||
          transferred != output.size() || output != input) {
        std::cerr << "ReadFile failed: " << GetLastError() << '\n';
        return 1;
      }
    }

    constexpr std::uint32_t required_operations = (1u << 4) - 1;
    constexpr std::uint32_t close_operation =
        1u << (static_cast<std::uint32_t>(operation::close) - 1);
    std::uint32_t operations_seen = 0;
    std::uint32_t record_count = 0;
    std::uint64_t dropped = 0;
    for (;;) {
      const record_batch batch = client.invoke(read_method);
      if (batch.count > batch.records.size()) {
        std::cerr << "the driver returned an invalid batch\n";
        return 1;
      }

      dropped = batch.dropped;
      for (std::size_t index = 0; index != batch.count; ++index) {
        const record &value = batch.records[index];
        operations_seen |=
            1u << (static_cast<std::uint32_t>(value.operation_id) - 1);
        ++record_count;
        std::wcout << L'#' << value.sequence << L' '
                   << name_of(value.operation_id) << L'/'
                   << name_of(value.phase_id) << L" status=0x" << std::hex
                   << std::uppercase
                   << static_cast<std::uint32_t>(value.status) << std::dec
                   << L" information=" << value.information << L'\n';
      }
      if (batch.remaining == 0)
        break;
    }

    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    if (record_count == 0 ||
        (operations_seen & required_operations) != required_operations) {
      std::cerr << "create/read/write/cleanup were not all observed\n";
      return 1;
    }

    std::cout << "operation log sample passed: records=" << record_count
              << " dropped=" << dropped
              << " close_observed="
              << ((operations_seen & close_operation) != 0 ? "yes" : "no")
              << '\n';
    return 0;
  } catch (const ntl::flt::communication_error &failure) {
    std::cerr << "minifilter communication failed: 0x" << std::hex
              << std::uppercase << static_cast<unsigned long>(failure.result())
              << '\n';
  } catch (const std::exception &failure) {
    std::cerr << "operation log sample failed: " << failure.what() << '\n';
  }
  return 1;
}
