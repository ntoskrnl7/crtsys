#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// clang-format off: SetupAPI requires the Windows SDK base declarations.
#include <windows.h>
#include <setupapi.h>
#include <winioctl.h>
// clang-format on

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <utility>
#include <vector>

#include <ntl/handle>

#include "kmdf_usb_ntl_ioctl.hpp"

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
  device_info_set devices{
      SetupDiGetClassDevsW(&kmdf_usb_ntl_sample::device_interface_guid, nullptr,
                           nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)};
  if (!devices)
    return {};

  SP_DEVICE_INTERFACE_DATA interface_data{};
  interface_data.cbSize = sizeof(interface_data);
  if (!SetupDiEnumDeviceInterfaces(devices.get(), nullptr,
                                   &kmdf_usb_ntl_sample::device_interface_guid,
                                   0, &interface_data))
    return {};

  DWORD required_size = 0;
  (void)SetupDiGetDeviceInterfaceDetailW(devices.get(), &interface_data,
                                         nullptr, 0, &required_size, nullptr);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
      required_size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
    return {};

  std::vector<std::byte> storage(required_size);
  auto *detail =
      reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(storage.data());
  detail->cbSize = sizeof(*detail);
  if (!SetupDiGetDeviceInterfaceDetailW(devices.get(), &interface_data, detail,
                                        required_size, nullptr, nullptr))
    return {};

  const std::size_t length = std::wcslen(detail->DevicePath);
  return std::vector<wchar_t>(detail->DevicePath,
                              detail->DevicePath + length + 1);
}

} // namespace

int wmain() {
  const auto path = find_device_interface_path();
  if (path.empty()) {
    std::fwprintf(stderr, L"USB device interface was not found (error %lu).\n",
                  GetLastError());
    return 1;
  }

  ntl::unique_handle device{
      CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
  if (!device) {
    std::fwprintf(stderr, L"CreateFileW(%ls) failed: %lu\n", path.data(),
                  GetLastError());
    return 1;
  }

  kmdf_usb_ntl_sample::query_reply reply{};
  DWORD bytes_returned = 0;
  if (!DeviceIoControl(device.get(), kmdf_usb_ntl_sample::query_ioctl_code,
                       nullptr, 0, &reply, sizeof(reply), &bytes_returned,
                       nullptr)) {
    std::fwprintf(stderr, L"DeviceIoControl failed: %lu\n", GetLastError());
    return 1;
  }

  if (bytes_returned != sizeof(reply)) {
    std::fwprintf(stderr, L"unexpected reply size: %lu\n", bytes_returned);
    return 1;
  }

  std::printf("USB %04x:%04x interfaces=%u pipes=%u in=0x%02x/%u "
              "out=0x%02x/%u reads=%u\n",
              reply.vendor_id, reply.product_id, reply.interface_count,
              reply.configured_pipe_count, reply.input_endpoint,
              reply.input_packet_size, reply.output_endpoint,
              reply.output_packet_size, reply.continuous_read_count);
  return 0;
}
