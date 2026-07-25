#include <Windows.h>
#include <fltUser.h>
#include <winioctl.h>

#include "../shared/control_device_sample.hpp"

#include <cstdint>
#include <iostream>

namespace {
using namespace crtsys_minifilter_control_device_sample;

struct unique_handle {
  HANDLE value = INVALID_HANDLE_VALUE;
  ~unique_handle() {
    if (value != INVALID_HANDLE_VALUE)
      CloseHandle(value);
  }
};

bool enable_load_driver_privilege() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(),
                        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
    return false;

  LUID id{};
  const BOOL found =
      LookupPrivilegeValueW(nullptr, L"SeLoadDriverPrivilege", &id);
  if (!found) {
    CloseHandle(token);
    return false;
  }

  TOKEN_PRIVILEGES privileges{};
  privileges.PrivilegeCount = 1;
  privileges.Privileges[0].Luid = id;
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
         returned == sizeof(reply);
}

} // namespace

int wmain() {
  if (!enable_load_driver_privilege()) {
    std::cerr << "SeLoadDriverPrivilege is required\n";
    return 1;
  }

  unique_handle device{CreateFileW(
      user_device_path, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
  if (device.value == INVALID_HANDLE_VALUE) {
    std::cerr << "CreateFile failed: " << GetLastError() << '\n';
    return 1;
  }

  ping_reply first{};
  if (!ping(device.value, 41, first) || first.value != 42) {
    std::cerr << "first ping failed: " << GetLastError() << '\n';
    return 1;
  }

  const HRESULT unload_result = FilterUnload(filter_name);
  if (SUCCEEDED(unload_result)) {
    std::cerr << "the filter unloaded while its control device was open\n";
    return 1;
  }

  ping_reply second{};
  if (!ping(device.value, 99, second) || second.value != 100 ||
      second.unload_vetoes != 1) {
    std::cerr << "dispatch after unload veto failed\n";
    return 1;
  }

  std::cout << "control-device PASS: sequences " << first.sequence << ", "
            << second.sequence << "; unload veto observed\n";
  return 0;
}
