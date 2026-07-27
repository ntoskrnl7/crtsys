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

#include "kmdf_reference_ioctl.hpp"

namespace {

class device_info_set {
public:
  explicit device_info_set(HDEVINFO value) noexcept : value_(value) {}
  device_info_set(const device_info_set &) = delete;
  device_info_set &operator=(const device_info_set &) = delete;
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
      &kmdf_reference::device_interface_guid, nullptr, nullptr,
      DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)};
  if (!devices)
    return {};

  SP_DEVICE_INTERFACE_DATA interface_data{};
  interface_data.cbSize = sizeof(interface_data);
  if (!SetupDiEnumDeviceInterfaces(
          devices.get(), nullptr, &kmdf_reference::device_interface_guid,
          0, &interface_data))
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
  return {detail->DevicePath, detail->DevicePath + length + 1};
}

ntl::unique_handle open_device(const wchar_t *path, bool overlapped = false) {
  return ntl::unique_handle{
      CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL |
                      (overlapped ? FILE_FLAG_OVERLAPPED : 0),
                  nullptr)};
}

bool valid_reply(const kmdf_reference::status_reply &reply) {
  return kmdf_reference::valid_header(
             reply.header, sizeof(kmdf_reference::status_reply)) &&
         reply.server_irql == 0 &&
         (reply.flags & (kmdf_reference::hardware_prepared |
                         kmdf_reference::device_in_d0)) ==
             (kmdf_reference::hardware_prepared |
              kmdf_reference::device_in_d0);
}

bool query(HANDLE device, kmdf_reference::status_reply &reply) {
  kmdf_reference::query_request request{
      kmdf_reference::make_header(
          sizeof(kmdf_reference::query_request))};
  DWORD bytes = 0;
  return DeviceIoControl(device, kmdf_reference::query_ioctl, &request,
                         sizeof(request), &reply, sizeof(reply), &bytes,
                         nullptr) &&
         bytes == sizeof(reply) && valid_reply(reply);
}

bool operate(HANDLE device, std::uint32_t value, std::uint32_t delay_ms,
             kmdf_reference::status_reply &reply) {
  kmdf_reference::operation_request request{
      kmdf_reference::make_header(
          sizeof(kmdf_reference::operation_request)),
      value,
      delay_ms};
  DWORD bytes = 0;
  return DeviceIoControl(device, kmdf_reference::operation_ioctl, &request,
                         sizeof(request), &reply, sizeof(reply), &bytes,
                         nullptr) &&
         bytes == sizeof(reply) && valid_reply(reply);
}

bool verify_cancellation(const wchar_t *path) {
  auto device = open_device(path, true);
  if (!device)
    return false;

  kmdf_reference::operation_request request{
      kmdf_reference::make_header(
          sizeof(kmdf_reference::operation_request)),
      99,
      kmdf_reference::maximum_delay_ms};
  kmdf_reference::status_reply reply{};
  ntl::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  if (!event)
    return false;

  OVERLAPPED overlapped{};
  overlapped.hEvent = event.get();
  DWORD bytes = 0;
  if (DeviceIoControl(device.get(), kmdf_reference::operation_ioctl, &request,
                      sizeof(request), &reply, sizeof(reply), &bytes,
                      &overlapped)) {
    SetLastError(ERROR_INVALID_DATA);
    return false;
  }
  if (GetLastError() != ERROR_IO_PENDING)
    return false;
  if (!CancelIoEx(device.get(), &overlapped))
    return false;
  if (GetOverlappedResult(device.get(), &overlapped, &bytes, TRUE)) {
    SetLastError(ERROR_INVALID_DATA);
    return false;
  }
  return GetLastError() == ERROR_OPERATION_ABORTED;
}

} // namespace

int wmain() {
  const auto path = find_device_interface_path();
  if (path.empty()) {
    std::fwprintf(stderr, L"Reference device interface not found: %lu\n",
                  GetLastError());
    return 1;
  }

  auto first = open_device(path.data());
  auto second = open_device(path.data());
  if (!first || !second) {
    std::fwprintf(stderr, L"CreateFileW failed: %lu\n", GetLastError());
    return 1;
  }

  kmdf_reference::status_reply initial{};
  if (!query(first.get(), initial) || initial.session_id == 0 ||
      initial.open_handles < 2 || initial.prepare_count == 0 ||
      initial.d0_entry_count == 0) {
    std::fwprintf(stderr, L"initial contract query failed: %lu\n",
                  GetLastError());
    return 1;
  }

  kmdf_reference::status_reply operation{};
  if (!operate(first.get(), 17, 10, operation) ||
      operation.value != 52 || operation.sequence == 0 ||
      operation.session_id != initial.session_id ||
      operation.completed_requests != initial.completed_requests + 1) {
    std::fwprintf(stderr, L"operation validation failed: %lu\n",
                  GetLastError());
    return 1;
  }

  if (!verify_cancellation(path.data())) {
    std::fwprintf(stderr, L"cancellation validation failed: %lu\n",
                  GetLastError());
    return 1;
  }

  second.reset();
  kmdf_reference::status_reply final{};
  bool observed_close = false;
  for (unsigned attempt = 0; attempt != 100; ++attempt) {
    if (!query(first.get(), final)) {
      std::fwprintf(stderr, L"final contract query failed: %lu\n",
                    GetLastError());
      return 1;
    }
    if (final.open_handles == 1) {
      observed_close = true;
      break;
    }
    Sleep(10);
  }

  if (!observed_close ||
      final.completed_requests != initial.completed_requests + 1 ||
      final.canceled_requests != initial.canceled_requests + 1) {
    std::fprintf(
        stderr,
        "final counters invalid: open=%u complete=%u cancel=%u\n",
        final.open_handles, final.completed_requests,
        final.canceled_requests);
    return 1;
  }

  std::printf(
      "NTL KMDF reference ok: abi=%u session=%u sequence=%u value=%u "
      "open=%u prepare=%u d0=%u complete=%u cancel=%u irql=%u\n",
      final.header.version, final.session_id, operation.sequence,
      operation.value, final.open_handles, final.prepare_count,
      final.d0_entry_count, final.completed_requests,
      final.canceled_requests, final.server_irql);
  return 0;
}
