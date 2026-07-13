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

#include "ntl_sample_ioctl.hpp"

namespace {

constexpr wchar_t kDevicePathPrefix[] = L"\\\\.\\";

void print_last_error(const wchar_t *operation) {
  std::fwprintf(stderr, L"%ls failed: GetLastError()=%lu\n", operation,
                GetLastError());
}

std::uint32_t parse_value(int argc, wchar_t **argv) {
  if (argc < 2) {
    return 41;
  }

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

  wchar_t device_path[128]{};
  const int written =
      std::swprintf(device_path, sizeof(device_path) / sizeof(device_path[0]),
                    L"%ls%ls", kDevicePathPrefix, ntl_sample::device_name);
  if (written <= 0 ||
      static_cast<std::size_t>(written) >=
          sizeof(device_path) / sizeof(device_path[0])) {
    std::fwprintf(stderr, L"device path is too long\n");
    return 2;
  }

  ntl::unique_handle device{CreateFileW(device_path, GENERIC_READ | GENERIC_WRITE,
                                        0, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr)};
  if (!device) {
    print_last_error(L"CreateFileW");
    std::fwprintf(stderr, L"device path: %ls\n", device_path);
    return 1;
  }

  ntl_sample::ping_request request{value};
  ntl_sample::ping_reply reply{};
  DWORD bytes_returned = 0;

  const BOOL ok =
      DeviceIoControl(device.get(), ntl_sample::ping_ioctl_code, &request,
                      sizeof(request), &reply, sizeof(reply), &bytes_returned,
                      nullptr);
  if (!ok) {
    print_last_error(L"DeviceIoControl");
    return 1;
  }

  const std::uint32_t expected_checksum =
      request.value + reply.sequence + reply.configured_flags;
  if (bytes_returned != sizeof(reply) || reply.value != request.value + 1 ||
      reply.sequence == 0 || reply.checksum != expected_checksum) {
    std::fwprintf(stderr,
                  L"unexpected reply: bytes=%lu value=%u sequence=%u flags=%u "
                  L"checksum=%u expected_checksum=%u\n",
                  bytes_returned, reply.value, reply.sequence,
                  reply.configured_flags, reply.checksum, expected_checksum);
    return 1;
  }

  std::wprintf(L"ping ok: request=%u reply=%u sequence=%u flags=%u checksum=%u\n",
               request.value, reply.value, reply.sequence,
               reply.configured_flags, reply.checksum);
  return 0;
}
