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

#include "kmdf_bus_ntl_ioctl.hpp"

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

std::vector<wchar_t> find_interface_path(const GUID &interface_class) {
  device_info_set devices{
      SetupDiGetClassDevsW(&interface_class, nullptr, nullptr,
                           DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)};
  if (!devices)
    return {};

  SP_DEVICE_INTERFACE_DATA interface_data{};
  interface_data.cbSize = sizeof(interface_data);
  if (!SetupDiEnumDeviceInterfaces(devices.get(), nullptr, &interface_class, 0,
                                   &interface_data))
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

std::vector<wchar_t> wait_for_interface(const GUID &interface_class,
                                        bool present) {
  for (unsigned attempt = 0; attempt != 150; ++attempt) {
    auto path = find_interface_path(interface_class);
    if (present ? !path.empty() : path.empty())
      return path;
    Sleep(100);
  }
  return {};
}

ntl::unique_handle open_interface(const std::vector<wchar_t> &path) {
  if (path.empty())
    return {};
  return ntl::unique_handle{
      CreateFileW(path.data(), GENERIC_READ | GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL, nullptr)};
}

bool manage_child(HANDLE bus, kmdf_bus_ntl_sample::child_action action,
                  std::uint32_t serial_number) {
  kmdf_bus_ntl_sample::child_action_request input{action, serial_number};
  kmdf_bus_ntl_sample::child_action_reply output{};
  DWORD bytes_returned = 0;
  if (!DeviceIoControl(bus, kmdf_bus_ntl_sample::child_action_ioctl_code,
                       &input, sizeof(input), &output, sizeof(output),
                       &bytes_returned, nullptr)) {
    std::fwprintf(stderr, L"bus DeviceIoControl failed: %lu\n", GetLastError());
    return false;
  }
  if (bytes_returned != sizeof(output) ||
      output.serial_number != serial_number || output.operation_status < 0) {
    std::fwprintf(stderr,
                  L"bus operation failed: bytes=%lu serial=%u status=0x%08x\n",
                  bytes_returned, output.serial_number,
                  static_cast<unsigned>(output.operation_status));
    return false;
  }
  return true;
}

bool query_child(std::uint32_t expected_serial) {
  const auto path = wait_for_interface(
      kmdf_bus_ntl_sample::child_device_interface_guid, true);
  if (path.empty()) {
    std::fwprintf(stderr, L"Child function interface did not arrive.\n");
    return false;
  }

  ntl::unique_handle child = open_interface(path);
  if (!child) {
    std::fwprintf(stderr, L"CreateFileW(%ls) failed: %lu\n", path.data(),
                  GetLastError());
    return false;
  }

  kmdf_bus_ntl_sample::query_reply reply{};
  DWORD bytes_returned = 0;
  if (!DeviceIoControl(child.get(), kmdf_bus_ntl_sample::query_ioctl_code,
                       nullptr, 0, &reply, sizeof(reply), &bytes_returned,
                       nullptr)) {
    std::fwprintf(stderr, L"child DeviceIoControl failed: %lu\n",
                  GetLastError());
    return false;
  }

  if (bytes_returned != sizeof(reply) ||
      reply.serial_number != expected_serial || reply.request_count == 0 ||
      reply.server_irql != 0 ||
      reply.pdo_events.resource_requirements_queries == 0 ||
      reply.pdo_events.resource_queries == 0) {
    std::fwprintf(stderr,
                  L"unexpected child reply: bytes=%lu serial=%u count=%u "
                  L"irql=%u requirements=%u resources=%u\n",
                  bytes_returned, reply.serial_number, reply.request_count,
                  reply.server_irql,
                  reply.pdo_events.resource_requirements_queries,
                  reply.pdo_events.resource_queries);
    return false;
  }

  std::wprintf(L"child interface: %ls\n", path.data());
  std::printf("child %u: count=%u irql=%u message=%s\n", reply.serial_number,
              reply.request_count, reply.server_irql, reply.message);
  std::printf("PDO events: requirements=%u resources=%u lock=%u wake=%u/%u\n",
              reply.pdo_events.resource_requirements_queries,
              reply.pdo_events.resource_queries, reply.pdo_events.lock_changes,
              reply.pdo_events.wake_enables, reply.pdo_events.wake_disables);
  return true;
}

bool wait_for_child_removal() {
  for (unsigned attempt = 0; attempt != 150; ++attempt) {
    if (find_interface_path(kmdf_bus_ntl_sample::child_device_interface_guid)
            .empty())
      return true;
    Sleep(100);
  }
  std::fwprintf(stderr, L"Child function interface did not disappear.\n");
  return false;
}

} // namespace

int wmain() {
  const auto bus_path =
      wait_for_interface(kmdf_bus_ntl_sample::bus_interface_guid, true);
  if (bus_path.empty()) {
    std::fwprintf(stderr, L"Bus management interface was not found.\n");
    return 1;
  }

  ntl::unique_handle bus = open_interface(bus_path);
  if (!bus) {
    std::fwprintf(stderr, L"CreateFileW(%ls) failed: %lu\n", bus_path.data(),
                  GetLastError());
    return 1;
  }

  if (!manage_child(bus.get(), kmdf_bus_ntl_sample::child_action::plug_in, 1) ||
      !query_child(1))
    return 1;

  if (!manage_child(bus.get(), kmdf_bus_ntl_sample::child_action::mark_missing,
                    1) ||
      !wait_for_child_removal())
    return 1;

  if (!manage_child(bus.get(), kmdf_bus_ntl_sample::child_action::plug_in, 2) ||
      !query_child(2))
    return 1;

  if (!manage_child(bus.get(), kmdf_bus_ntl_sample::child_action::eject, 2) ||
      !wait_for_child_removal())
    return 1;

  std::printf("NTL KMDF bus lifecycle and typed query interface: PASS\n");
  return 0;
}
