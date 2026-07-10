#include <cerrno>
#include <cstdint>
#include <cstdlib>
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
int invalid_parameter_hits{};

void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void __cdecl capture_invalid_parameter(const wchar_t *, const wchar_t *,
                                       const wchar_t *, unsigned int,
                                       uintptr_t) {
  ++invalid_parameter_hits;
}

class invalid_parameter_guard {
public:
  invalid_parameter_guard()
      : previous_(_set_invalid_parameter_handler(capture_invalid_parameter)) {
    invalid_parameter_hits = 0;
  }

  ~invalid_parameter_guard() { (void)_set_invalid_parameter_handler(previous_); }

  invalid_parameter_guard(const invalid_parameter_guard &) = delete;
  invalid_parameter_guard &operator=(const invalid_parameter_guard &) = delete;

  int hits() const { return invalid_parameter_hits; }

private:
  _invalid_parameter_handler previous_;
};

void expect_invalid_parameter_hit(const invalid_parameter_guard &guard,
                                  int previous_hits, const char *message) {
  expect(guard.hits() == previous_hits + 1, message);
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

void verify_format_message_failure_edges() {
  char small_buffer[4]{};
  SetLastError(ERROR_SUCCESS);
  const DWORD small_written =
      FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
                         FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, ERROR_ACCESS_DENIED, 0, small_buffer,
                     static_cast<DWORD>(sizeof(small_buffer)), nullptr);
  expect(small_written == 0, "FormatMessageA small buffer unexpectedly worked");
  expect(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
         "FormatMessageA small buffer last-error mismatch");

  wchar_t wide_small_buffer[4]{};
  SetLastError(ERROR_SUCCESS);
  const DWORD wide_small_written = FormatMessageW(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
      ERROR_ACCESS_DENIED, 0, wide_small_buffer,
      static_cast<DWORD>(sizeof(wide_small_buffer) / sizeof(wide_small_buffer[0])),
      nullptr);
  expect(wide_small_written == 0,
         "FormatMessageW small buffer unexpectedly worked");
  expect(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
         "FormatMessageW small buffer last-error mismatch");

  char unknown_buffer[128]{};
  SetLastError(ERROR_SUCCESS);
  const DWORD unknown_written =
      FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
                         FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, 0xe000ffffu, 0, unknown_buffer,
                     static_cast<DWORD>(sizeof(unknown_buffer)), nullptr);
  expect(unknown_written == 0, "FormatMessageA unknown message unexpectedly worked");
  expect(GetLastError() == ERROR_MR_MID_NOT_FOUND,
         "FormatMessageA unknown message last-error mismatch");
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

void verify_invalid_parameter_diagnostics() {
  invalid_parameter_guard guard;

  int previous_hits = guard.hits();
  errno = 0;
  expect(_get_errno(nullptr) == EINVAL,
         "_get_errno(nullptr) did not return EINVAL");
  expect_invalid_parameter_hit(guard, previous_hits,
                               "_get_errno(nullptr) missed handler");

  previous_hits = guard.hits();
  errno = 0;
  expect(_get_doserrno(nullptr) == EINVAL,
         "_get_doserrno(nullptr) did not return EINVAL");
  expect_invalid_parameter_hit(guard, previous_hits,
                               "_get_doserrno(nullptr) missed handler");

  previous_hits = guard.hits();
  errno = 0;
  expect(strerror_s(nullptr, 0, EINVAL) == EINVAL,
         "strerror_s(nullptr) did not return EINVAL");
  expect_invalid_parameter_hit(guard, previous_hits,
                               "strerror_s(nullptr) missed handler");
  expect(errno == EINVAL, "strerror_s(nullptr) errno mismatch");
}

void verify_error_category_mapping() {
  const std::error_code generic_not_found{
      static_cast<int>(std::errc::no_such_file_or_directory),
      std::generic_category()};
  expect(generic_not_found.category() == std::generic_category(),
         "generic no-such-file category mismatch");
  expect(generic_not_found.default_error_condition() ==
             std::make_error_condition(std::errc::no_such_file_or_directory),
         "generic no-such-file condition mismatch");
  expect(!generic_not_found.message().empty(),
         "generic no-such-file message was empty");

  const std::error_code system_not_found{ERROR_FILE_NOT_FOUND,
                                         std::system_category()};
  expect(system_not_found.category() == std::system_category(),
         "system file-not-found category mismatch");
  expect(system_not_found.default_error_condition() ==
             std::make_error_condition(std::errc::no_such_file_or_directory),
         "system file-not-found condition mismatch");
  expect(system_not_found != generic_not_found,
         "system and generic file-not-found error_codes unexpectedly matched");

  const std::error_code system_access_denied{ERROR_ACCESS_DENIED,
                                             std::system_category()};
  expect(system_access_denied.default_error_condition() ==
             std::make_error_condition(std::errc::permission_denied),
         "system access-denied condition mismatch");

  const std::error_code system_already_exists{ERROR_ALREADY_EXISTS,
                                              std::system_category()};
  expect(system_already_exists.default_error_condition() ==
             std::make_error_condition(std::errc::file_exists),
         "system already-exists condition mismatch");
}
} // namespace

void run() {
  verify_win32_last_error_and_messages();
  verify_format_message_failure_edges();
  verify_lowio_errno_and_doserrno();
  verify_invalid_parameter_diagnostics();
  verify_error_category_mapping();
  std::cout << "error diagnostics semantic assertions passed\n";
}
} // namespace error_diagnostics_semantic_test
