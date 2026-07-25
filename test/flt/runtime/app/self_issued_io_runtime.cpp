#include "self_issued_io_runtime.hpp"

#include "../shared/runtime_test.hpp"

#include <Windows.h>

#include <ntl/flt/communication_client>

#include <chrono>
#include <thread>

namespace crtsys_flt_runtime_test {

bool run_self_issued_io_runtime_tests(ntl::flt::communication_client &client,
                                      const std::filesystem::path &root,
                                      std::string &failure) {
  namespace fs = std::filesystem;
  const fs::path directory = root / L"crtsys_flt_self_io.tmp";
  std::error_code error;
  fs::remove(directory, error);
  error.clear();
  if (!fs::create_directory(directory, error) || error) {
    failure = "failed to create the self-I/O directory: " + error.message();
    return false;
  }

  const auto before = client.invoke(self_issued_io_observations_method);
  if (client.invoke(arm_self_issued_io_method) != 1) {
    failure = "the minifilter refused to arm self-issued I/O";
    fs::remove(directory, error);
    return false;
  }

  HANDLE handle =
      CreateFileW(directory.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    failure = "failed to open the self-I/O directory: " +
              std::to_string(GetLastError());
    fs::remove(directory, error);
    return false;
  }

  self_issued_io_observations issued{};
  for (int attempt = 0; attempt != 100; ++attempt) {
    issued = client.invoke(self_issued_io_observations_method);
    if (issued.sync_completed > before.sync_completed &&
        issued.async_completed > before.async_completed &&
        issued.cancellable_issued > before.cancellable_issued) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (issued.sync_completed <= before.sync_completed ||
      issued.async_completed <= before.async_completed ||
      issued.cancellable_issued <= before.cancellable_issued) {
    failure =
        "generated sync/async I/O was not established; sync status " +
        std::to_string(static_cast<unsigned long>(issued.last_sync_status)) +
        ", async status " +
        std::to_string(static_cast<unsigned long>(issued.last_async_status)) +
        ", cancel status " +
        std::to_string(static_cast<unsigned long>(issued.last_cancel_status));
    (void)client.invoke(cancel_self_issued_io_method);
    (void)client.invoke(release_self_issued_io_method);
    CloseHandle(handle);
    fs::remove(directory, error);
    return false;
  }

  if (client.invoke(cancel_self_issued_io_method) != 1) {
    failure = "FltCancelIo did not accept the generated directory notify";
    (void)client.invoke(release_self_issued_io_method);
    CloseHandle(handle);
    fs::remove(directory, error);
    return false;
  }

  self_issued_io_observations cancelled{};
  for (int attempt = 0; attempt != 200; ++attempt) {
    cancelled = client.invoke(self_issued_io_observations_method);
    if (cancelled.cancellation_completed > before.cancellation_completed) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (cancelled.cancellation_completed <= before.cancellation_completed) {
    failure = "the generated directory notify did not complete cancellation; "
              "status " +
              std::to_string(
                  static_cast<unsigned long>(cancelled.last_cancel_status));
    (void)client.invoke(release_self_issued_io_method);
    CloseHandle(handle);
    fs::remove(directory, error);
    return false;
  }

  const auto released =
      static_cast<NTSTATUS>(client.invoke(release_self_issued_io_method));
  CloseHandle(handle);
  fs::remove(directory, error);
  if (released < 0) {
    failure = "releasing the generated-I/O handle failed with " +
              std::to_string(static_cast<unsigned long>(released));
    return false;
  }

  const auto after = client.invoke(self_issued_io_observations_method);
  if (after.handles_released <= before.handles_released) {
    failure = "the generated-I/O cancellation handle was not released";
    return false;
  }
  return true;
}

} // namespace crtsys_flt_runtime_test
