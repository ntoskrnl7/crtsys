#include <Windows.h>
#include <winioctl.h>

#include <iostream>
#include <string>
#include <string_view>

namespace {

struct unique_handle {
  HANDLE value = INVALID_HANDLE_VALUE;
  ~unique_handle() {
    if (value != INVALID_HANDLE_VALUE)
      CloseHandle(value);
  }
};

bool volume_control(HANDLE volume, DWORD code) {
  DWORD returned = 0;
  return DeviceIoControl(volume, code, nullptr, 0, nullptr, 0,
                         &returned, nullptr) != FALSE;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  if (argc != 3 || std::wstring_view(argv[1]) != L"--lock" ||
      std::wstring_view(argv[2]).size() != 2 || argv[2][1] != L':') {
    std::wcerr
        << L"usage: volume-metadata-sample-app --lock X:\n"
        << L"Use only a disposable NTFS/ReFS data volume.\n";
    return 2;
  }

  const std::wstring path = L"\\\\.\\" + std::wstring(argv[2]);
  unique_handle volume{CreateFileW(
      path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
  if (volume.value == INVALID_HANDLE_VALUE) {
    std::cerr << "opening the volume failed: " << GetLastError() << '\n';
    return 1;
  }

  if (!volume_control(volume.value, FSCTL_LOCK_VOLUME)) {
    std::cerr << "FSCTL_LOCK_VOLUME failed: " << GetLastError() << '\n';
    return 1;
  }

  if (!volume_control(volume.value, FSCTL_UNLOCK_VOLUME)) {
    std::cerr << "FSCTL_UNLOCK_VOLUME failed: " << GetLastError() << '\n';
    return 1;
  }

  std::wcout << L"volume-metadata PASS: " << argv[2]
             << L" locked and unlocked\n";
  return 0;
}
