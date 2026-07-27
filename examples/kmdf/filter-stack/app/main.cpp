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

#include "kmdf_filter_stack_ntl_ioctl.hpp"

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
      &kmdf_filter_stack_ntl_sample::device_interface_guid, nullptr, nullptr,
      DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)};
  if (!devices)
    return {};

  SP_DEVICE_INTERFACE_DATA interface_data{};
  interface_data.cbSize = sizeof(interface_data);
  if (!SetupDiEnumDeviceInterfaces(
          devices.get(), nullptr,
          &kmdf_filter_stack_ntl_sample::device_interface_guid, 0,
          &interface_data))
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

} // namespace

int wmain() {
  const auto path = find_device_interface_path();
  if (path.empty()) {
    std::fwprintf(stderr,
                  L"Filter-stack interface was not found (error %lu).\n",
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

  constexpr std::uint32_t input_value = 31;
  kmdf_filter_stack_ntl_sample::query_request request{input_value};
  kmdf_filter_stack_ntl_sample::query_reply reply{};
  DWORD bytes_returned = 0;
  if (!DeviceIoControl(
          device.get(), kmdf_filter_stack_ntl_sample::query_ioctl_code,
          &request, sizeof(request), &reply, sizeof(reply), &bytes_returned,
          nullptr)) {
    std::fwprintf(stderr, L"DeviceIoControl failed: %lu\n", GetLastError());
    return 1;
  }

  const std::uint32_t expected_layers =
      kmdf_filter_stack_ntl_sample::target_layer |
      kmdf_filter_stack_ntl_sample::filter_layer;
  if (bytes_returned != sizeof(reply) || reply.value != input_value + 11 ||
      reply.layers != expected_layers || reply.target_requests == 0 ||
      reply.filter_completions == 0 || reply.prepare_count == 0 ||
      reply.d0_count == 0 || reply.server_irql != 0) {
    std::fprintf(
        stderr,
        "filter-stack mismatch: bytes=%lu value=%u layers=0x%x "
        "target=%u filter=%u prepare=%u d0=%u irql=%u\n",
        bytes_returned, reply.value, reply.layers, reply.target_requests,
        reply.filter_completions, reply.prepare_count, reply.d0_count,
        reply.server_irql);
    return 1;
  }

  std::printf(
      "NTL KMDF filter stack ok: value=%u layers=0x%x target=%u "
      "filter=%u prepare=%u d0=%u irql=%u message=%s\n",
      reply.value, reply.layers, reply.target_requests,
      reply.filter_completions, reply.prepare_count, reply.d0_count,
      reply.server_irql, reply.message);
  return 0;
}
