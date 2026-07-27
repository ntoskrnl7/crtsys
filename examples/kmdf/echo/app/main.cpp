#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <setupapi.h>
#include <winioctl.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <utility>
#include <vector>

#include <ntl/handle>

#include "kmdf_echo_ntl_ioctl.hpp"

namespace {

class device_info_set {
public:
  explicit device_info_set(HDEVINFO value) noexcept : value_(value) {}
  device_info_set(const device_info_set &) = delete;
  device_info_set &operator=(const device_info_set &) = delete;
  device_info_set(device_info_set &&other) noexcept
      : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
  ~device_info_set() {
    if (value_ != INVALID_HANDLE_VALUE)
      SetupDiDestroyDeviceInfoList(value_);
  }

  HDEVINFO get() const noexcept { return value_; }
  explicit operator bool() const noexcept {
    return value_ != INVALID_HANDLE_VALUE;
  }

private:
  HDEVINFO value_;
};

std::vector<wchar_t> find_device_interface_path() {
  device_info_set devices{SetupDiGetClassDevsW(
      &kmdf_echo_ntl_sample::device_interface_guid, nullptr, nullptr,
      DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)};
  if (!devices)
    return {};

  SP_DEVICE_INTERFACE_DATA interface_data{};
  interface_data.cbSize = sizeof(interface_data);
  if (!SetupDiEnumDeviceInterfaces(
          devices.get(), nullptr,
          &kmdf_echo_ntl_sample::device_interface_guid, 0, &interface_data))
    return {};

  DWORD required_size = 0;
  (void)SetupDiGetDeviceInterfaceDetailW(
      devices.get(), &interface_data, nullptr, 0, &required_size, nullptr);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
      required_size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
    return {};

  std::vector<std::byte> storage(required_size);
  auto *detail =
      reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(storage.data());
  detail->cbSize = sizeof(*detail);
  if (!SetupDiGetDeviceInterfaceDetailW(
          devices.get(), &interface_data, detail, required_size, nullptr,
          nullptr))
    return {};

  const std::size_t length = std::wcslen(detail->DevicePath);
  return std::vector<wchar_t>(detail->DevicePath,
                              detail->DevicePath + length + 1);
}

bool issue_echo(HANDLE device, std::uint32_t value, std::uint32_t delay_ms,
                kmdf_echo_ntl_sample::echo_reply &reply) {
  const kmdf_echo_ntl_sample::echo_request request{value, delay_ms};
  DWORD bytes_returned = 0;
  return DeviceIoControl(device, kmdf_echo_ntl_sample::echo_ioctl_code,
                         const_cast<kmdf_echo_ntl_sample::echo_request *>(
                             &request),
                         sizeof(request), &reply, sizeof(reply),
                         &bytes_returned, nullptr) &&
         bytes_returned == sizeof(reply);
}

bool verify_cancellation(const wchar_t *path) {
  ntl::unique_handle device{
      CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr)};
  if (!device)
    return false;

  kmdf_echo_ntl_sample::echo_request request{99, 30'000};
  kmdf_echo_ntl_sample::echo_reply reply{};
  OVERLAPPED overlapped{};
  overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!overlapped.hEvent)
    return false;
  ntl::unique_handle event{overlapped.hEvent};

  DWORD bytes_returned = 0;
  if (DeviceIoControl(device.get(), kmdf_echo_ntl_sample::echo_ioctl_code,
                      &request, sizeof(request), &reply, sizeof(reply),
                      &bytes_returned, &overlapped)) {
    std::fwprintf(stderr, L"cancel request completed unexpectedly.\n");
    return false;
  }
  if (GetLastError() != ERROR_IO_PENDING) {
    std::fwprintf(stderr, L"pending echo failed: %lu\n", GetLastError());
    return false;
  }

  if (!CancelIoEx(device.get(), &overlapped)) {
    std::fwprintf(stderr, L"CancelIoEx failed: %lu\n", GetLastError());
    return false;
  }
  if (GetOverlappedResult(device.get(), &overlapped, &bytes_returned, TRUE)) {
    std::fwprintf(stderr, L"canceled echo reported success.\n");
    return false;
  }
  return GetLastError() == ERROR_OPERATION_ABORTED;
}

} // namespace

int wmain() {
  const auto path = find_device_interface_path();
  if (path.empty()) {
    std::fwprintf(stderr,
                  L"Echo device interface was not found (error %lu).\n",
                  GetLastError());
    return 1;
  }

  ntl::unique_handle device{
      CreateFileW(path.data(), GENERIC_READ | GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL, nullptr)};
  if (!device) {
    std::fwprintf(stderr, L"CreateFileW(%ls) failed: %lu\n", path.data(),
                  GetLastError());
    return 1;
  }

  kmdf_echo_ntl_sample::echo_reply first{};
  if (!issue_echo(device.get(), 42, 25, first) || first.value != 42 ||
      first.completed_requests == 0 ||
      first.server_irql != 0) {
    std::fwprintf(stderr, L"first echo validation failed: %lu\n",
                  GetLastError());
    return 1;
  }

  if (!verify_cancellation(path.data())) {
    std::fwprintf(stderr, L"echo cancellation validation failed.\n");
    return 1;
  }

  kmdf_echo_ntl_sample::echo_reply final{};
  // CancelIoEx may cancel in the framework queue before EvtIoDeviceControl
  // dispatches the request, so the driver callback counter is monotonic but
  // is not required to advance for every user-observed cancellation.
  if (!issue_echo(device.get(), 7, 10, final) || final.value != 7 ||
      final.completed_requests != first.completed_requests + 1 ||
      final.canceled_requests < first.canceled_requests ||
      final.server_irql != 0) {
    std::fprintf(
        stderr,
        "final echo mismatch: value=%u complete=%u cancel=%u irql=%u\n",
        final.value, final.completed_requests, final.canceled_requests,
        final.server_irql);
    return 1;
  }

  std::printf(
      "NTL KMDF echo ok: value=%u complete=%u cancel=%u irql=%u "
      "message=%s\n",
      final.value, final.completed_requests, final.canceled_requests,
      final.server_irql, final.message);
  return 0;
}
