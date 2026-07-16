#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winioctl.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <thread>
#include <vector>

#include <ntl/handle>

#include "ioctl.hpp"

namespace {

constexpr wchar_t device_path[] = L"\\\\.\\CrtSysKmdfVerifierStress";

struct options {
  unsigned iterations = 64;
  unsigned workers = 4;
};

unsigned parse_argument(int argc, wchar_t **argv, int index, unsigned fallback,
                        unsigned maximum, const wchar_t *name) {
  if (argc <= index)
    return fallback;
  wchar_t *end = nullptr;
  const unsigned long parsed = std::wcstoul(argv[index], &end, 0);
  if (end == argv[index] || *end != L'\0' || parsed == 0 ||
      parsed > maximum) {
    std::fwprintf(stderr, L"invalid %ls: %ls (expected 1..%u)\n", name,
                  argv[index], maximum);
    std::exit(2);
  }
  return static_cast<unsigned>(parsed);
}

ntl::unique_handle open_device() {
  return ntl::unique_handle{CreateFileW(
      device_path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr)};
}

bool begin_ioctl(HANDLE device, unsigned long code, void *input,
                 DWORD input_size, void *output, DWORD output_size,
                 OVERLAPPED &overlapped) {
  DWORD ignored = 0;
  if (DeviceIoControl(device, code, input, input_size, output, output_size,
                      &ignored, &overlapped))
    return true;
  return GetLastError() == ERROR_IO_PENDING;
}

bool finish_ioctl(HANDLE device, OVERLAPPED &overlapped,
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

bool issue_transform(HANDLE device, std::uint32_t value) {
  ntl::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  if (!event)
    return false;

  kmdf_verifier_stress::transform_request request{value};
  kmdf_verifier_stress::transform_reply reply{};
  OVERLAPPED overlapped{};
  overlapped.hEvent = event.get();
  DWORD bytes = 0;
  if (!begin_ioctl(device, kmdf_verifier_stress::transform_ioctl, &request,
                   sizeof(request), &reply, sizeof(reply), overlapped) ||
      !finish_ioctl(device, overlapped, bytes))
    return false;
  if (bytes != sizeof(reply) || reply.value != value + 6 ||
      reply.server_irql != 0) {
    SetLastError(ERROR_INVALID_DATA);
    return false;
  }
  return true;
}

bool query_stats(HANDLE device, kmdf_verifier_stress::queue_stats &stats) {
  ntl::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  if (!event)
    return false;
  OVERLAPPED overlapped{};
  overlapped.hEvent = event.get();
  DWORD bytes = 0;
  return begin_ioctl(device, kmdf_verifier_stress::stats_ioctl, nullptr, 0,
                     &stats, sizeof(stats), overlapped) &&
         finish_ioctl(device, overlapped, bytes) && bytes == sizeof(stats);
}

bool wait_until_queued(HANDLE device) {
  for (unsigned attempt = 0; attempt != 200; ++attempt) {
    kmdf_verifier_stress::queue_stats stats{};
    if (!query_stats(device, stats))
      return false;
    if (stats.queued_requests == 1)
      return true;
    Sleep(10);
  }
  SetLastError(ERROR_TIMEOUT);
  return false;
}

struct async_result {
  bool succeeded = false;
  DWORD error = ERROR_SUCCESS;
  DWORD bytes = 0;
};

bool run_release_cancel_race(HANDLE control, HANDLE operation,
                             std::uint32_t value, unsigned iteration) {
  kmdf_verifier_stress::queue_stats before{};
  if (!query_stats(control, before))
    return false;

  ntl::unique_handle wait_event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  ntl::unique_handle release_event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  if (!wait_event || !release_event)
    return false;

  kmdf_verifier_stress::wait_request request{value};
  kmdf_verifier_stress::wait_reply reply{};
  OVERLAPPED wait_overlapped{};
  wait_overlapped.hEvent = wait_event.get();
  if (!begin_ioctl(operation, kmdf_verifier_stress::wait_ioctl, &request,
                   sizeof(request), &reply, sizeof(reply), wait_overlapped) ||
      !wait_until_queued(control))
    return false;

  std::atomic<bool> start{false};
  async_result release{};
  std::thread releaser([&] {
    while (!start.load(std::memory_order_acquire))
      std::this_thread::yield();

    OVERLAPPED overlapped{};
    overlapped.hEvent = release_event.get();
    if (!begin_ioctl(operation, kmdf_verifier_stress::release_ioctl, nullptr,
                     0, nullptr, 0, overlapped)) {
      release.error = GetLastError();
      return;
    }
    release.succeeded = finish_ioctl(operation, overlapped, release.bytes);
    release.error = release.succeeded ? ERROR_SUCCESS : GetLastError();
  });

  start.store(true, std::memory_order_release);
  if ((iteration & 1u) != 0)
    (void)SwitchToThread();
  const bool cancel_started =
      CancelIoEx(operation, &wait_overlapped) != FALSE;
  const DWORD cancel_error = cancel_started ? ERROR_SUCCESS : GetLastError();
  releaser.join();

  DWORD wait_bytes = 0;
  const bool wait_succeeded =
      finish_ioctl(operation, wait_overlapped, wait_bytes);
  const DWORD wait_error = wait_succeeded ? ERROR_SUCCESS : GetLastError();

  kmdf_verifier_stress::queue_stats after{};
  if (!query_stats(control, after))
    return false;
  const std::uint32_t released =
      after.released_requests - before.released_requests;
  const std::uint32_t canceled =
      after.canceled_requests - before.canceled_requests;

  const bool exactly_once = after.queued_requests == 0 &&
                            released + canceled == 1;
  const bool release_won =
      wait_succeeded && wait_bytes == sizeof(reply) &&
      reply.value == value + 1000 && reply.server_irql == 0 && released == 1 &&
      canceled == 0 && release.succeeded && release.bytes == 0;
  const bool cancel_won =
      !wait_succeeded && wait_error == ERROR_OPERATION_ABORTED &&
      released == 0 && canceled == 1;
  const bool cancel_result_valid =
      cancel_started || cancel_error == ERROR_NOT_FOUND;

  if (exactly_once && (release_won || cancel_won) && cancel_result_valid)
    return true;

  std::fwprintf(
      stderr,
      L"race %u failed: wait_ok=%u wait_error=%lu wait_bytes=%lu "
      L"release_ok=%u release_error=%lu release_bytes=%lu cancel_ok=%u "
      L"cancel_error=%lu queued=%u released=%u canceled=%u\n",
      iteration, wait_succeeded ? 1u : 0u, wait_error, wait_bytes,
      release.succeeded ? 1u : 0u, release.error, release.bytes,
      cancel_started ? 1u : 0u, cancel_error, after.queued_requests, released,
      canceled);
  SetLastError(ERROR_INVALID_DATA);
  return false;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);

  const options test_options{
      parse_argument(argc, argv, 1, 64, 10000, L"iterations"),
      parse_argument(argc, argv, 2, 4, 64, L"workers")};
  std::printf("KMDF verifier stress: iterations=%u workers=%u\n",
              test_options.iterations, test_options.workers);

  ntl::unique_handle control = open_device();
  ntl::unique_handle operation = open_device();
  if (!control || !operation) {
    std::fwprintf(stderr, L"CreateFileW failed: %lu\n", GetLastError());
    return 1;
  }

  std::atomic<bool> start{false};
  std::atomic<DWORD> first_error{ERROR_SUCCESS};
  std::atomic<unsigned> failed_worker{0};
  std::atomic<unsigned> failed_iteration{0};
  std::atomic<unsigned long long> completed{0};
  std::vector<std::thread> workers;
  workers.reserve(test_options.workers);

  for (unsigned worker = 0; worker != test_options.workers; ++worker) {
    workers.emplace_back([&, worker] {
      while (!start.load(std::memory_order_acquire))
        std::this_thread::yield();

      for (unsigned iteration = 0; iteration != test_options.iterations;
           ++iteration) {
        if (first_error.load(std::memory_order_relaxed) != ERROR_SUCCESS)
          return;

        ntl::unique_handle device = open_device();
        DWORD error = ERROR_SUCCESS;
        if (!device) {
          error = GetLastError();
        } else {
          const auto value = static_cast<std::uint32_t>(
              worker * test_options.iterations + iteration + 1);
          if (!issue_transform(device.get(), value))
            error = GetLastError();
        }

        if (error != ERROR_SUCCESS) {
          DWORD expected = ERROR_SUCCESS;
          if (first_error.compare_exchange_strong(expected, error)) {
            failed_worker.store(worker, std::memory_order_relaxed);
            failed_iteration.store(iteration, std::memory_order_relaxed);
          }
          return;
        }
        completed.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  start.store(true, std::memory_order_release);
  for (unsigned iteration = 0; iteration != test_options.iterations;
       ++iteration) {
    if (first_error.load(std::memory_order_relaxed) != ERROR_SUCCESS)
      break;
    if (!run_release_cancel_race(control.get(), operation.get(),
                                 0x1000u + iteration, iteration)) {
      DWORD expected = ERROR_SUCCESS;
      DWORD error = GetLastError();
      if (error == ERROR_SUCCESS)
        error = ERROR_INVALID_DATA;
      (void)first_error.compare_exchange_strong(expected, error);
      break;
    }
    if ((iteration + 1) % 16 == 0 ||
        iteration + 1 == test_options.iterations) {
      std::printf("KMDF verifier stress races: %u/%u\n", iteration + 1,
                  test_options.iterations);
    }
  }

  for (auto &worker : workers)
    worker.join();

  const auto expected_completed =
      static_cast<unsigned long long>(test_options.iterations) *
      test_options.workers;
  const DWORD error = first_error.load(std::memory_order_relaxed);
  if (error != ERROR_SUCCESS ||
      completed.load(std::memory_order_relaxed) != expected_completed) {
    std::fwprintf(stderr,
                  L"stress failed: error=%lu worker=%u iteration=%u "
                  L"transforms=%llu/%llu\n",
                  error, failed_worker.load(std::memory_order_relaxed),
                  failed_iteration.load(std::memory_order_relaxed),
                  completed.load(std::memory_order_relaxed),
                  expected_completed);
    return 1;
  }

  kmdf_verifier_stress::queue_stats final_stats{};
  if (!query_stats(control.get(), final_stats) ||
      final_stats.queued_requests != 0 ||
      final_stats.released_requests + final_stats.canceled_requests !=
          test_options.iterations ||
      final_stats.transformed_requests != expected_completed ||
      final_stats.open_files != 2 ||
      final_stats.forward_progress_requests == 0) {
    std::fwprintf(stderr,
                  L"final stats mismatch: queued=%u released=%u canceled=%u "
                  L"transformed=%u open=%u forward=%u\n",
                  final_stats.queued_requests, final_stats.released_requests,
                  final_stats.canceled_requests,
                  final_stats.transformed_requests, final_stats.open_files,
                  final_stats.forward_progress_requests);
    return 1;
  }

  std::printf("KMDF verifier stress PASS: transforms=%llu races=%u\n",
              expected_completed, test_options.iterations);
  return 0;
}
