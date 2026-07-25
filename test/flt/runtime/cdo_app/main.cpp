#include <Windows.h>
#include <fltUser.h>
#include <winioctl.h>

#include "../cdo_shared/cdo_runtime.hpp"

#include <cstdint>
#include <iostream>

namespace {
using namespace crtsys_flt_cdo_runtime;

struct unique_handle {
  HANDLE value = INVALID_HANDLE_VALUE;

  explicit unique_handle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept
      : value(handle) {}
  unique_handle(const unique_handle &) = delete;
  unique_handle &operator=(const unique_handle &) = delete;
  ~unique_handle() { reset(); }

  explicit operator bool() const noexcept {
    return value != INVALID_HANDLE_VALUE;
  }

  void reset() noexcept {
    if (value != INVALID_HANDLE_VALUE) {
      CloseHandle(value);
      value = INVALID_HANDLE_VALUE;
    }
  }
};

unique_handle open_control_device() {
  return unique_handle{CreateFileW(
      user_device_path, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
}

bool enable_load_driver_privilege() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(),
                        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
    return false;

  LUID privilege{};
  if (!LookupPrivilegeValueW(nullptr, L"SeLoadDriverPrivilege",
                             &privilege)) {
    CloseHandle(token);
    return false;
  }

  TOKEN_PRIVILEGES privileges{};
  privileges.PrivilegeCount = 1;
  privileges.Privileges[0].Luid = privilege;
  privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  SetLastError(ERROR_SUCCESS);
  const BOOL adjusted = AdjustTokenPrivileges(
      token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr);
  const DWORD error = GetLastError();
  CloseHandle(token);
  return adjusted && error != ERROR_NOT_ALL_ASSIGNED;
}

bool ping(HANDLE device, std::uint32_t value, ping_reply &reply) {
  ping_request request{value};
  DWORD returned = 0;
  return DeviceIoControl(device, ping_ioctl, &request, sizeof(request),
                         &reply, sizeof(reply), &returned, nullptr) &&
         returned == sizeof(reply) && reply.value == value + 1;
}

} // namespace

int wmain() {
  if (!enable_load_driver_privilege()) {
    std::cerr << "enabling SeLoadDriverPrivilege failed: "
              << GetLastError() << '\n';
    return 1;
  }

  unique_handle first = open_control_device();
  if (!first) {
    std::cerr << "opening minifilter CDO failed: " << GetLastError() << '\n';
    return 1;
  }

  unique_handle concurrent = open_control_device();
  if (concurrent) {
    std::cerr << "exclusive CDO policy accepted a concurrent open\n";
    return 1;
  }
  const DWORD concurrent_open_error = GetLastError();

  ping_reply first_reply{};
  if (!ping(first.value, 41, first_reply)) {
    std::cerr << "first CDO ping failed: " << GetLastError() << '\n';
    return 1;
  }

  const HRESULT unload_while_open = FilterUnload(filter_name);
  if (SUCCEEDED(unload_while_open)) {
    std::cerr << "optional minifilter unload succeeded while CDO was open\n";
    return 1;
  }

  ping_reply after_veto_reply{};
  if (!ping(first.value, 99, after_veto_reply)) {
    std::cerr << "CDO stopped dispatching after unload veto: "
              << GetLastError() << '\n';
    return 1;
  }
  if (after_veto_reply.unload_veto_count != 1) {
    std::cerr << "optional unload did not reach the minifilter CDO veto\n";
    return 1;
  }
  first.reset();

  unique_handle reopened = open_control_device();
  if (!reopened) {
    std::cerr << "CDO did not reopen after cleanup/close: "
              << GetLastError() << '\n';
    return 1;
  }

  ping_reply reopened_reply{};
  if (!ping(reopened.value, 7, reopened_reply)) {
    std::cerr << "reopened CDO ping failed: " << GetLastError() << '\n';
    return 1;
  }
  reopened.reset();

  if (first_reply.sequence != 1 || after_veto_reply.sequence != 2 ||
      reopened_reply.sequence != 3 || reopened_reply.create_count != 2 ||
      reopened_reply.ioctl_count != 3 ||
      reopened_reply.unload_veto_count != 1) {
    std::cerr << "unexpected CDO lifecycle counters\n";
    return 1;
  }

  std::cout << "cdo_integration=PASS concurrent_open_error="
            << concurrent_open_error << " unload_veto=0x" << std::hex
            << std::uppercase
            << static_cast<unsigned long>(unload_while_open)
            << std::dec << " creates=" << reopened_reply.create_count
            << " ioctls=" << reopened_reply.ioctl_count << '\n';
  return 0;
}
