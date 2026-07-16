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

bool begin_overlapped_ioctl(HANDLE device, unsigned long code, void *input,
                            DWORD input_size, void *output, DWORD output_size,
                            OVERLAPPED &overlapped) {
  DWORD ignored = 0;
  if (DeviceIoControl(device, code, input, input_size, output, output_size,
                      &ignored, &overlapped))
    return true;
  return GetLastError() == ERROR_IO_PENDING;
}

bool finish_overlapped_ioctl(HANDLE device, OVERLAPPED &overlapped,
                             DWORD &bytes_returned, DWORD timeout_ms = 5000) {
  if (GetOverlappedResult(device, &overlapped, &bytes_returned, FALSE))
    return true;
  if (GetLastError() != ERROR_IO_INCOMPLETE)
    return false;

  const DWORD wait_result = WaitForSingleObject(overlapped.hEvent, timeout_ms);
  if (wait_result != WAIT_OBJECT_0) {
    const DWORD error =
        wait_result == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
    (void)CancelIoEx(device, &overlapped);
    (void)WaitForSingleObject(overlapped.hEvent, 1000);
    SetLastError(error);
    return false;
  }

  return GetOverlappedResult(device, &overlapped, &bytes_returned, FALSE) !=
         FALSE;
}

void report_stage(const char *stage) {
  std::printf("NTL KMDF stage: %s\n", stage);
  std::fflush(stdout);
}

bool query_manual_queue_stats(HANDLE device,
                              kmdf_ntl_sample::manual_queue_stats &stats) {
  ntl::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  if (!event)
    return false;

  OVERLAPPED overlapped{};
  overlapped.hEvent = event.get();
  DWORD bytes_returned = 0;
  return begin_overlapped_ioctl(
             device, kmdf_ntl_sample::manual_stats_ioctl_code, nullptr, 0,
             &stats, sizeof(stats), overlapped) &&
         finish_overlapped_ioctl(device, overlapped, bytes_returned) &&
         bytes_returned == sizeof(stats);
}

bool wait_until_queued(HANDLE device, std::uint32_t expected) {
  for (unsigned attempt = 0; attempt != 100; ++attempt) {
    kmdf_ntl_sample::manual_queue_stats stats{};
    if (!query_manual_queue_stats(device, stats))
      return false;
    if (stats.queued_requests >= expected)
      return true;
    Sleep(10);
  }
  return false;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);

  const std::uint32_t value = parse_value(argc, argv);
  report_stage("open control device");
  ntl::unique_handle control_device{CreateFileW(
      device_path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr)};
  if (!control_device) {
    std::fwprintf(stderr, L"control CreateFileW failed: %lu\n", GetLastError());
    return 1;
  }

  report_stage("open operation device");
  ntl::unique_handle operation_device{CreateFileW(
      device_path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr)};
  if (!operation_device) {
    std::fwprintf(stderr, L"operation CreateFileW failed: %lu\n",
                  GetLastError());
    return 1;
  }

  report_stage("transform ioctl");
  kmdf_ntl_sample::transform_request request{value};
  kmdf_ntl_sample::transform_reply reply{};
  ntl::unique_handle transform_event{
      CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  if (!transform_event) {
    std::fwprintf(stderr, L"CreateEventW failed: %lu\n", GetLastError());
    return 1;
  }

  OVERLAPPED transform_overlapped{};
  transform_overlapped.hEvent = transform_event.get();
  DWORD bytes_returned = 0;
  if (!begin_overlapped_ioctl(
          control_device.get(), kmdf_ntl_sample::transform_ioctl_code, &request,
          sizeof(request), &reply, sizeof(reply), transform_overlapped) ||
      !finish_overlapped_ioctl(control_device.get(), transform_overlapped,
                               bytes_returned)) {
    std::fwprintf(stderr, L"DeviceIoControl failed: %lu\n", GetLastError());
    return 1;
  }

  const std::uint32_t expected = value + 6;
  if (bytes_returned != sizeof(reply) || reply.value != expected ||
      reply.server_irql != 0) {
    std::fwprintf(stderr, L"unexpected reply: bytes=%lu value=%u irql=%u\n",
                  bytes_returned, reply.value, reply.server_irql);
    return 1;
  }

  std::printf("NTL KMDF ok: value=%u result=%u server_irql=%u message=%s\n",
              value, reply.value, reply.server_irql, reply.message);

  report_stage("manual queue baseline");
  kmdf_ntl_sample::manual_queue_stats initial_stats{};
  if (!query_manual_queue_stats(control_device.get(), initial_stats)) {
    std::fwprintf(stderr, L"manual queue stats failed: %lu\n", GetLastError());
    return 1;
  }

  ntl::unique_handle wait_event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  ntl::unique_handle release_event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  if (!wait_event || !release_event) {
    std::fwprintf(stderr, L"CreateEventW failed: %lu\n", GetLastError());
    return 1;
  }

  kmdf_ntl_sample::manual_wait_request wait_request{value};
  kmdf_ntl_sample::manual_wait_reply wait_reply{};
  OVERLAPPED wait_overlapped{};
  wait_overlapped.hEvent = wait_event.get();
  report_stage("queue manual wait");
  if (!begin_overlapped_ioctl(operation_device.get(),
                              kmdf_ntl_sample::manual_wait_ioctl_code,
                              &wait_request, sizeof(wait_request), &wait_reply,
                              sizeof(wait_reply), wait_overlapped) ||
      !wait_until_queued(control_device.get(), 1)) {
    std::fwprintf(stderr, L"manual wait did not enter the queue: %lu\n",
                  GetLastError());
    return 1;
  }

  OVERLAPPED release_overlapped{};
  release_overlapped.hEvent = release_event.get();
  report_stage("release manual wait");
  if (!begin_overlapped_ioctl(operation_device.get(),
                              kmdf_ntl_sample::manual_release_ioctl_code,
                              nullptr, 0, nullptr, 0, release_overlapped)) {
    std::fwprintf(stderr, L"manual release failed to start: %lu\n",
                  GetLastError());
    return 1;
  }

  DWORD release_bytes = 0;
  DWORD wait_bytes = 0;
  if (!finish_overlapped_ioctl(operation_device.get(), release_overlapped,
                               release_bytes) ||
      !finish_overlapped_ioctl(operation_device.get(), wait_overlapped,
                               wait_bytes) ||
      release_bytes != 0 || wait_bytes != sizeof(wait_reply) ||
      wait_reply.value != value + 1000 || wait_reply.server_irql != 0) {
    std::fwprintf(stderr,
                  L"manual queue completion mismatch: error=%lu "
                  L"release_bytes=%lu wait_bytes=%lu value=%u irql=%u\n",
                  GetLastError(), release_bytes, wait_bytes, wait_reply.value,
                  wait_reply.server_irql);
    return 1;
  }

  ntl::unique_handle cancel_event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  if (!cancel_event) {
    std::fwprintf(stderr, L"CreateEventW failed: %lu\n", GetLastError());
    return 1;
  }

  kmdf_ntl_sample::manual_wait_request cancel_request{value + 1};
  kmdf_ntl_sample::manual_wait_reply cancel_reply{};
  OVERLAPPED cancel_overlapped{};
  cancel_overlapped.hEvent = cancel_event.get();
  report_stage("queue cancellable wait");
  if (!begin_overlapped_ioctl(
          operation_device.get(), kmdf_ntl_sample::manual_wait_ioctl_code,
          &cancel_request, sizeof(cancel_request), &cancel_reply,
          sizeof(cancel_reply), cancel_overlapped) ||
      !wait_until_queued(control_device.get(), 1)) {
    std::fwprintf(stderr, L"cancel request did not enter the queue: %lu\n",
                  GetLastError());
    return 1;
  }

  if (!CancelIoEx(operation_device.get(), &cancel_overlapped)) {
    std::fwprintf(stderr, L"CancelIoEx failed: %lu\n", GetLastError());
    return 1;
  }

  DWORD cancel_bytes = 0;
  if (finish_overlapped_ioctl(operation_device.get(), cancel_overlapped,
                              cancel_bytes) ||
      GetLastError() != ERROR_OPERATION_ABORTED) {
    std::fwprintf(stderr, L"canceled request completed unexpectedly: %lu\n",
                  GetLastError());
    return 1;
  }

  kmdf_ntl_sample::manual_queue_stats final_stats{};
  report_stage("manual queue final stats");
  if (!query_manual_queue_stats(control_device.get(), final_stats) ||
      final_stats.queued_requests != 0 ||
      final_stats.released_requests != initial_stats.released_requests + 1 ||
      final_stats.canceled_requests != initial_stats.canceled_requests + 1 ||
      final_stats.forward_progress_requests <=
          initial_stats.forward_progress_requests) {
    std::fwprintf(stderr,
                  L"manual queue stats mismatch: queued=%u released=%u "
                  L"canceled=%u forward_progress=%u\n",
                  final_stats.queued_requests, final_stats.released_requests,
                  final_stats.canceled_requests,
                  final_stats.forward_progress_requests);
    return 1;
  }

  std::printf("NTL KMDF manual queue ok: released=%u canceled=%u\n",
              final_stats.released_requests, final_stats.canceled_requests);
  return 0;
}
