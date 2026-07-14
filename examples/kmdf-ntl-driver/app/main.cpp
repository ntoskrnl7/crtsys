#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winioctl.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

#include <ntl/handle>

#include "kmdf_ntl_ioctl.hpp"

namespace {

constexpr wchar_t device_path[] = L"\\\\.\\CrtSysKmdfNtlSample";

std::uint32_t parse_value(int argc, wchar_t **argv) {
  if (argc < 2)
    return 36;

  wchar_t *end = nullptr;
  const unsigned long parsed = std::wcstoul(argv[1], &end, 0);
  if (end == argv[1] || *end != L'\0') {
    std::fwprintf(stderr, L"invalid value: %ls\n", argv[1]);
    std::exit(2);
  }
  return static_cast<std::uint32_t>(parsed);
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  const std::uint32_t value = parse_value(argc, argv);
  ntl::unique_handle device{CreateFileW(
      device_path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, nullptr)};
  if (!device) {
    std::fwprintf(stderr, L"CreateFileW failed: %lu\n", GetLastError());
    return 1;
  }

  kmdf_ntl_sample::transform_request request{value};
  kmdf_ntl_sample::transform_reply reply{};
  DWORD bytes_returned = 0;
  if (!DeviceIoControl(device.get(), kmdf_ntl_sample::transform_ioctl_code,
                       &request, sizeof(request), &reply, sizeof(reply),
                       &bytes_returned, nullptr)) {
    std::fwprintf(stderr, L"DeviceIoControl failed: %lu\n", GetLastError());
    return 1;
  }

  const std::uint32_t expected = value + 6;
  if (bytes_returned != sizeof(reply) || reply.value != expected ||
      reply.server_irql != 0) {
    std::fwprintf(stderr,
                  L"unexpected reply: bytes=%lu value=%u irql=%u\n",
                  bytes_returned, reply.value, reply.server_irql);
    return 1;
  }

  std::printf("NTL KMDF ok: value=%u result=%u server_irql=%u message=%s\n",
              value, reply.value, reply.server_irql, reply.message);
  return 0;
}
