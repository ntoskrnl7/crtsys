#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <Windows.h>

namespace error_diagnostics_semantic_test {
namespace {
constexpr char missing_path[] = "crtsys_error_diagnostics_missing.tmp";

void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string format_system_message_a(DWORD error) {
  char buffer[128]{};
  const DWORD written =
      FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
                         FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, error, 0, buffer,
                     static_cast<DWORD>(sizeof(buffer)), nullptr);
  expect(written != 0, "FormatMessageA failed");
  return buffer;
}

std::wstring format_system_message_w(DWORD error) {
  wchar_t buffer[128]{};
  const DWORD written =
      FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                         FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, error, 0, buffer,
                     static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])),
                     nullptr);
  expect(written != 0, "FormatMessageW failed");
  return buffer;
}

void verify_win32_last_error_and_messages() {
  SetLastError(ERROR_ACCESS_DENIED);
  expect(GetLastError() == ERROR_ACCESS_DENIED, "GetLastError mismatch");

  const std::string access_denied = format_system_message_a(ERROR_ACCESS_DENIED);
  expect(access_denied.find("Access is denied") != std::string::npos,
         "FormatMessageA access-denied message mismatch");

  const std::wstring wide_access_denied =
      format_system_message_w(ERROR_ACCESS_DENIED);
  expect(wide_access_denied.find(L"Access is denied") != std::wstring::npos,
         "FormatMessageW access-denied message mismatch");

  const std::error_code system_error_code{static_cast<int>(ERROR_ACCESS_DENIED),
                                          std::system_category()};
  expect(system_error_code.message().find("Access is denied") !=
             std::string::npos,
         "system_category access-denied message mismatch");

  const std::system_error ex{system_error_code, "crtsys"};
  expect(ex.code() == system_error_code, "system_error code mismatch");
  expect(std::string{ex.what()}.find("Access is denied") != std::string::npos,
         "system_error what() message mismatch");
}

void verify_lowio_errno_and_doserrno() {
  errno = 0;
  _doserrno = 0;

  const int fd = _open(missing_path, _O_RDONLY | _O_BINARY);
  expect(fd == -1, "_open unexpectedly opened missing file");
  expect(errno == ENOENT, "_open missing file errno mismatch");

  int captured_errno{};
  expect(_get_errno(&captured_errno) == 0, "_get_errno failed");
  expect(captured_errno == ENOENT, "_get_errno value mismatch");

  unsigned long captured_doserrno{};
  expect(_get_doserrno(&captured_doserrno) == 0, "_get_doserrno failed");
  expect(captured_doserrno == ERROR_FILE_NOT_FOUND,
         "_get_doserrno value mismatch");

  const std::error_code file_not_found{
      static_cast<int>(captured_doserrno), std::system_category()};
  expect(file_not_found.message().find("cannot find the file") !=
             std::string::npos,
         "system_category file-not-found message mismatch");
}
} // namespace

void run() {
  verify_win32_last_error_and_messages();
  verify_lowio_errno_and_doserrno();
  std::cout << "error diagnostics semantic assertions passed\n";
}
} // namespace error_diagnostics_semantic_test
