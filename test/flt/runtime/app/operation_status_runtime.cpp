#include "operation_status_runtime.hpp"

#include "../shared/runtime_test.hpp"

#include <Windows.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <thread>

namespace crtsys_flt_runtime_test {

bool run_operation_status_runtime_tests(ntl::flt::communication_client &client,
                                        const std::filesystem::path &root,
                                        std::string &failure) {
  namespace fs = std::filesystem;
  const fs::path directory = root / L"crtsys_flt_operation_status.tmp";
  std::error_code error;
  fs::remove_all(directory, error);
  error.clear();
  if (!fs::create_directory(directory, error) || error) {
    failure =
        "failed to create the operation-status directory: " + error.message();
    return false;
  }

  HANDLE handle =
      CreateFileW(directory.c_str(), FILE_LIST_DIRECTORY,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING,
                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    failure = "failed to open the operation-status directory: " +
              std::to_string(GetLastError());
    fs::remove(directory, error);
    return false;
  }

  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!event) {
    failure = "failed to create the operation-status event: " +
              std::to_string(GetLastError());
    CloseHandle(handle);
    fs::remove(directory, error);
    return false;
  }

  const auto before = client.invoke(operation_status_observations_method);
  if (client.invoke(arm_operation_status_method) != 1) {
    failure = "the minifilter refused to arm operation-status observation";
    CloseHandle(event);
    CloseHandle(handle);
    fs::remove(directory, error);
    return false;
  }

  alignas(void *) std::array<std::byte, 1024> buffer{};
  OVERLAPPED overlapped{};
  overlapped.hEvent = event;
  const BOOL started = ReadDirectoryChangesW(
      handle, buffer.data(), static_cast<DWORD>(buffer.size()), FALSE,
      FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
          FILE_NOTIFY_CHANGE_LAST_WRITE,
      nullptr, &overlapped, nullptr);
  const DWORD start_error = started ? ERROR_SUCCESS : GetLastError();
  if (!started && start_error != ERROR_IO_PENDING) {
    failure =
        "ReadDirectoryChangesW could not start an overlapped request; result " +
        std::to_string(start_error);
    (void)CancelIoEx(handle, &overlapped);
    CloseHandle(event);
    CloseHandle(handle);
    fs::remove(directory, error);
    return false;
  }

  DWORD immediate_bytes = 0;
  const BOOL immediately_completed =
      GetOverlappedResult(handle, &overlapped, &immediate_bytes, FALSE);
  const DWORD immediate_error =
      immediately_completed ? ERROR_SUCCESS : GetLastError();
  if (immediately_completed || immediate_error != ERROR_IO_INCOMPLETE) {
    failure =
        "directory notification was not pending after its overlapped start; "
        "result " +
        std::to_string(immediate_error);
    (void)CancelIoEx(handle, &overlapped);
    CloseHandle(event);
    CloseHandle(handle);
    fs::remove(directory, error);
    return false;
  }

  operation_status_observations after{};
  for (int attempt = 0; attempt != 200; ++attempt) {
    after = client.invoke(operation_status_observations_method);
    if (after.callbacks > before.callbacks &&
        after.states_destroyed > before.states_destroyed) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  (void)CancelIoEx(handle, &overlapped);
  DWORD transferred = 0;
  const BOOL completed =
      GetOverlappedResult(handle, &overlapped, &transferred, TRUE);
  const DWORD completion_error = completed ? ERROR_SUCCESS : GetLastError();
  CloseHandle(event);
  CloseHandle(handle);
  fs::remove(directory, error);

  if (completed || completion_error != ERROR_OPERATION_ABORTED) {
    failure = "pending directory notification did not cancel cleanly: " +
              std::to_string(completion_error);
    return false;
  }
  if (after.matched_operations <= before.matched_operations ||
      after.requests_accepted <= before.requests_accepted ||
      after.callbacks <= before.callbacks ||
      after.pending_statuses <= before.pending_statuses ||
      after.snapshots_valid <= before.snapshots_valid ||
      after.states_created <= before.states_created ||
      after.states_destroyed <= before.states_destroyed) {
    failure =
        "typed operation-status callback did not observe a valid pending "
        "directory notification; request status " +
        std::to_string(static_cast<unsigned long>(after.last_request_status)) +
        ", operation status " +
        std::to_string(static_cast<unsigned long>(after.last_operation_status));
    return false;
  }
  if (after.states_created - before.states_created !=
      after.states_destroyed - before.states_destroyed) {
    failure = "operation-status request state was not destroyed exactly once";
    return false;
  }
  return true;
}

} // namespace crtsys_flt_runtime_test
