#include "../metadata_shared/metadata_runtime.hpp"

#include <Windows.h>
#include <winioctl.h>

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {
using namespace crtsys_flt_metadata_runtime;
inline constexpr DWORD snapshot_flush_and_hold_ioctl =
    CTL_CODE(0x00000053, 0, METHOD_BUFFERED,
             FILE_READ_ACCESS | FILE_WRITE_ACCESS);

struct unique_handle {
  HANDLE value = INVALID_HANDLE_VALUE;

  unique_handle() = default;
  explicit unique_handle(HANDLE handle) noexcept : value(handle) {}
  unique_handle(const unique_handle &) = delete;
  unique_handle &operator=(const unique_handle &) = delete;
  unique_handle(unique_handle &&other) noexcept
      : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
  unique_handle &operator=(unique_handle &&other) noexcept {
    if (this != &other) {
      reset();
      value = std::exchange(other.value, INVALID_HANDLE_VALUE);
    }
    return *this;
  }
  ~unique_handle() { reset(); }

  explicit operator bool() const noexcept {
    return value != INVALID_HANDLE_VALUE;
  }

  void reset() noexcept {
    if (value != INVALID_HANDLE_VALUE) {
      CloseHandle(value);
      value = INVALID_HANDLE_VALUE;
    }
  }
};

unique_handle open_volume(std::wstring_view drive, DWORD share) {
  std::wstring path = LR"(\\.\)";
  path.append(drive);
  return unique_handle{CreateFileW(
      path.c_str(), GENERIC_READ | GENERIC_WRITE, share, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
}

bool control(HANDLE volume, DWORD code, DWORD &error) {
  DWORD returned = 0;
  if (DeviceIoControl(volume, code, nullptr, 0, nullptr, 0, &returned,
                      nullptr)) {
    error = ERROR_SUCCESS;
    return true;
  }
  error = GetLastError();
  return false;
}

bool require(bool condition, const char *message) {
  if (condition)
    return true;
  std::cerr << message << '\n';
  return false;
}

void print_observations(const char *label, const observations &value) {
  std::cerr << label << ": open_status=0x" << std::hex << std::uppercase
            << static_cast<std::uint32_t>(value.last_open_status)
            << " transition_status=0x"
            << static_cast<std::uint32_t>(value.last_transition_status)
            << std::dec << std::nouppercase
            << " instances_opened=" << value.instances_opened
            << " instance_teardowns=" << value.instance_teardowns
            << " instances_closed=" << value.instances_closed
            << " releases=" << value.releases
            << " reopen_attempts=" << value.reopen_attempts
            << " reopens=" << value.reopens
            << " reopen_noops=" << value.reopen_noops
            << " implicit=" << value.implicit_lock_succeeded
            << " explicit_lock=" << value.explicit_lock_succeeded
            << " unlock=" << value.unlock_succeeded
            << " dismount_succeeded=" << value.dismount_succeeded
            << " dismount_failed=" << value.dismount_failed << '\n';
}

bool metadata_returned(const observations &before,
                       const observations &after) {
  return after.reopens > before.reopens ||
         after.instances_opened > before.instances_opened;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  const std::wstring drive = argc >= 2 ? argv[1] : L"E:";
  if (drive.size() != 2 || drive[1] != L':') {
    std::cerr << "usage: metadata_app [drive:]\n";
    return 2;
  }

  std::wstring root = drive + L"\\";
  wchar_t filesystem[MAX_PATH]{};
  if (!GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr,
                             nullptr, filesystem, MAX_PATH)) {
    std::cerr << "GetVolumeInformationW failed: " << GetLastError() << '\n';
    return 1;
  }
  if (_wcsicmp(filesystem, L"NTFS") != 0 &&
      _wcsicmp(filesystem, L"ReFS") != 0) {
    std::wcerr << L"unsupported test filesystem: " << filesystem << L'\n';
    return 1;
  }

  try {
    auto client = connect();
    const observations baseline =
        client.invoke(get_observations_method);
    if (!require(baseline.instances_opened != 0,
                 "metadata file was not opened on any attached volume"))
      return 1;

    {
      unique_handle implicit = open_volume(drive, FILE_SHARE_READ);
      if (!require(static_cast<bool>(implicit),
                   "implicit volume-lock open failed"))
        return 1;
    }
    const observations after_implicit =
        client.invoke(get_observations_method);
    if (!require(after_implicit.implicit_lock_pre >
                     baseline.implicit_lock_pre &&
                 after_implicit.implicit_lock_succeeded >
                     baseline.implicit_lock_succeeded &&
                 after_implicit.releases > baseline.releases &&
                 after_implicit.reopens > baseline.reopens,
                 "implicit volume lock did not close and reopen metadata"))
      return 1;

    unique_handle volume = open_volume(
        drive, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
    if (!require(static_cast<bool>(volume), "shared volume open failed"))
      return 1;

    DWORD snapshot_error = ERROR_SUCCESS;
    (void)control(volume.value, snapshot_flush_and_hold_ioctl,
                  snapshot_error);
    const observations after_snapshot =
        client.invoke(get_observations_method);
    if (!require(after_snapshot.snapshot_pre > baseline.snapshot_pre &&
                     after_snapshot.snapshot_post > baseline.snapshot_post &&
                     after_snapshot.snapshot_update_rejected >
                         baseline.snapshot_update_rejected,
                 "snapshot pre/post update gate was not exercised"))
      return 1;

    DWORD lock_error = ERROR_SUCCESS;
    if (!require(control(volume.value, FSCTL_LOCK_VOLUME, lock_error),
                 "FSCTL_LOCK_VOLUME failed on disposable volume")) {
      std::cerr << "lock error=" << lock_error << '\n';
      return 1;
    }
    const observations while_locked =
        client.invoke(get_observations_method);
    if (!require(while_locked.explicit_lock_succeeded >
                     after_snapshot.explicit_lock_succeeded &&
                 while_locked.releases > after_snapshot.releases,
                 "explicit lock did not release metadata first"))
      return 1;

    DWORD unlock_error = ERROR_SUCCESS;
    if (!require(control(volume.value, FSCTL_UNLOCK_VOLUME, unlock_error),
                 "FSCTL_UNLOCK_VOLUME failed")) {
      std::cerr << "unlock error=" << unlock_error << '\n';
      return 1;
    }
    // A successful explicit unlock can invalidate the old volume instance.
    // Close its stale handle and touch the drive so the file system mounts a
    // replacement instance on which instance-setup reopens the metadata.
    volume.reset();
    observations after_unlock{};
    for (unsigned attempt = 0; attempt != 50; ++attempt) {
      wchar_t remounted_filesystem[MAX_PATH]{};
      (void)GetVolumeInformationW(
          root.c_str(), nullptr, 0, nullptr, nullptr, nullptr,
          remounted_filesystem, MAX_PATH);
      after_unlock = client.invoke(get_observations_method);
      if (metadata_returned(after_snapshot, after_unlock))
        break;
      Sleep(100);
    }
    if (!require(after_unlock.unlock_succeeded >
                     while_locked.unlock_succeeded &&
                     after_unlock.reopen_attempts >
                         while_locked.reopen_attempts &&
                     metadata_returned(after_snapshot, after_unlock),
                 "explicit unlock neither reopened metadata nor mounted a "
                 "replacement instance")) {
      print_observations("before explicit lock", after_snapshot);
      print_observations("while locked", while_locked);
      print_observations("after unlock", after_unlock);
      return 1;
    }

    volume = open_volume(
        drive, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
    if (!require(static_cast<bool>(volume),
                 "post-remount shared volume open failed"))
      return 1;

    DWORD dismount_error = ERROR_SUCCESS;
    const bool dismount_completed =
        control(volume.value, FSCTL_DISMOUNT_VOLUME, dismount_error);
    observations after_dismount =
        client.invoke(get_observations_method);

    const char *dismount_path = nullptr;
    if (dismount_completed) {
      if (!require(after_dismount.dismount_succeeded >
                       after_unlock.dismount_succeeded,
                   "successful dismount was not observed by the filter")) {
        print_observations("before dismount", after_unlock);
        print_observations("after dismount", after_dismount);
        return 1;
      }

      volume.reset();
      for (unsigned attempt = 0; attempt != 50; ++attempt) {
        wchar_t remounted_filesystem[MAX_PATH]{};
        (void)GetVolumeInformationW(
            root.c_str(), nullptr, 0, nullptr, nullptr, nullptr,
            remounted_filesystem, MAX_PATH);
        after_dismount = client.invoke(get_observations_method);
        if (after_dismount.instance_teardowns >
                after_unlock.instance_teardowns &&
            after_dismount.instances_opened >
                after_unlock.instances_opened)
          break;
        Sleep(100);
      }
      if (!require(after_dismount.instance_teardowns >
                       after_unlock.instance_teardowns &&
                       after_dismount.instances_opened >
                           after_unlock.instances_opened,
                   "successful dismount did not detach and remount the "
                   "metadata instance")) {
        print_observations("before successful dismount", after_unlock);
        print_observations("after successful dismount", after_dismount);
        return 1;
      }
      dismount_path = "success_detach_remount";
    } else {
      if (!require(after_dismount.dismount_failed >
                       after_unlock.dismount_failed &&
                       after_dismount.reopen_attempts >
                           after_unlock.reopen_attempts &&
                       metadata_returned(after_unlock, after_dismount),
                   "failed dismount did not restore metadata")) {
        print_observations("before failed dismount", after_unlock);
        print_observations("after failed dismount", after_dismount);
        return 1;
      }
      dismount_path = "failure_reopen";
    }

    std::cout << "metadata_manager=PASS filesystem=";
    std::wcout << filesystem;
    std::cout << " implicit=1 explicit_lock=1 snapshot=1 "
              << "dismount_path=" << dismount_path
              << " snapshot_ioctl_error=" << snapshot_error
              << " dismount_error=" << dismount_error
              << " reopen_attempts="
              << after_dismount.reopen_attempts
              << " reopens=" << after_dismount.reopens
              << " remount_opens="
              << (after_dismount.instances_opened -
                  baseline.instances_opened)
              << '\n';
    return 0;
  } catch (const ntl::flt::communication_error &error) {
    std::cerr << "metadata communication failed: 0x" << std::hex
              << std::uppercase
              << static_cast<unsigned long>(error.result()) << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "metadata runtime failed: " << error.what() << '\n';
    return 1;
  }
}
