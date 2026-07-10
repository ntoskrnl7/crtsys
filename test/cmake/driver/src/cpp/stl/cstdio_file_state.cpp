#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <direct.h>
#include <filesystem>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/locking.h>
#include <sys/stat.h>
#include <Windows.h>

namespace crt_file_state_semantic_test {
namespace {
constexpr char sandbox[] = "crtsys_crt_file_state";
constexpr wchar_t wide_sandbox[] = L"crtsys_crt_file_state";
constexpr char nested[] = "crtsys_crt_file_state\\nested";
constexpr wchar_t wide_nested[] = L"crtsys_crt_file_state\\nested";
constexpr char data_path[] = "crtsys_crt_file_state\\data.txt";
constexpr wchar_t wide_data_path[] = L"crtsys_crt_file_state\\data.txt";
constexpr char dup_path[] = "crtsys_crt_file_state\\dup.txt";
constexpr char dup_target_path[] = "crtsys_crt_file_state\\dup_target.txt";
constexpr char find_a_path[] = "crtsys_crt_file_state\\find_a.txt";
constexpr char find_b_path[] = "crtsys_crt_file_state\\find_b.log";
constexpr char find_legacy_path[] = "crtsys_crt_file_state\\find_legacy.txt";
constexpr char find_pattern[] = "crtsys_crt_file_state\\find_*.txt";

void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void expect_errno(int expected, const char *message) {
  if (errno != expected) {
    throw std::runtime_error(std::string{message} + ": " +
                             std::to_string(errno));
  }
}

int invalid_parameter_hits{};

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

bool contains(const char *text, const char *needle) {
  return std::strstr(text, needle) != nullptr;
}

bool contains(const wchar_t *text, const wchar_t *needle) {
  return std::wcsstr(text, needle) != nullptr;
}

class current_directory_guard {
public:
  current_directory_guard() {
    expect(_getcwd(original_, sizeof(original_)) != nullptr,
           "_getcwd guard failed");
  }

  ~current_directory_guard() { (void)_chdir(original_); }

  current_directory_guard(const current_directory_guard &) = delete;
  current_directory_guard &operator=(const current_directory_guard &) = delete;

private:
  char original_[512]{};
};

void remove_tree() {
  (void)_unlink(data_path);
  (void)_unlink(dup_path);
  (void)_unlink(dup_target_path);
  (void)_unlink(find_a_path);
  (void)_unlink(find_b_path);
  (void)_unlink(find_legacy_path);
  (void)_rmdir(nested);
  (void)_rmdir(sandbox);
}

void create_file_with_payload(const char *path, const char *payload) {
  const int handle =
      _open(path, _O_CREAT | _O_TRUNC | _O_RDWR | _O_BINARY,
            _S_IREAD | _S_IWRITE);
  expect(handle != -1, "_open create failed");
  const auto length = static_cast<unsigned>(std::strlen(payload));
  expect(_write(handle, payload, length) == static_cast<int>(length),
         "_write payload failed");
  expect(_close(handle) == 0, "_close created file failed");
}

void verify_stat_access_and_fullpath() {
  create_file_with_payload(data_path, "abcdef");

  struct _stat64 stat_result {};
  expect(_stat64(data_path, &stat_result) == 0, "_stat64 failed");
  expect((stat_result.st_mode & _S_IFREG) != 0, "_stat64 did not report file");
  expect(stat_result.st_size == 6, "_stat64 returned unexpected size");

  struct _stat legacy_stat_result {};
  expect(_stat(data_path, &legacy_stat_result) == 0, "_stat failed");
  expect((legacy_stat_result.st_mode & _S_IFREG) != 0,
         "_stat did not report file");
  expect(legacy_stat_result.st_size == stat_result.st_size,
         "_stat returned unexpected size");

  struct _stat64 wide_stat_result {};
  expect(_wstat64(wide_data_path, &wide_stat_result) == 0, "_wstat64 failed");
  expect(wide_stat_result.st_size == stat_result.st_size,
         "_wstat64 returned unexpected size");

  errno = 0;
  expect(_access(data_path, 0) == 0, "_access existence failed");
  expect(_access(data_path, 4) == 0, "_access read failed");
  expect(_waccess(wide_data_path, 0) == 0, "_waccess existence failed");

  char full_path[512]{};
  expect(_fullpath(full_path, data_path, sizeof(full_path)) != nullptr,
         "_fullpath failed");
  expect(contains(full_path, sandbox), "_fullpath missed sandbox component");

  wchar_t wide_full_path[512]{};
  expect(_wfullpath(wide_full_path, wide_data_path,
                    sizeof(wide_full_path) / sizeof(wide_full_path[0])) !=
             nullptr,
         "_wfullpath failed");
  expect(contains(wide_full_path, wide_sandbox),
         "_wfullpath missed sandbox component");

  errno = 0;
  expect(_access("crtsys_crt_file_state\\missing.txt", 0) == -1,
         "_access unexpectedly found missing file");
  expect(errno != 0, "_access missing file did not set errno");
}

void verify_current_directory_state() {
  char cwd_before[512]{};
  expect(_getcwd(cwd_before, sizeof(cwd_before)) != nullptr,
         "_getcwd before chdir failed");
  const auto filesystem_cwd_before = std::filesystem::current_path();
  expect(std::filesystem::equivalent(filesystem_cwd_before, cwd_before),
         "std::filesystem::current_path and _getcwd disagree before chdir");

  expect(_chdir(sandbox) == 0, "_chdir sandbox failed");

  char cwd_inside[512]{};
  expect(_getcwd(cwd_inside, sizeof(cwd_inside)) != nullptr,
         "_getcwd inside sandbox failed");
  expect(contains(cwd_inside, sandbox), "_getcwd missed sandbox component");
  expect(std::filesystem::equivalent(std::filesystem::current_path(),
                                     cwd_inside),
         "std::filesystem::current_path did not observe _chdir");

  expect(_wchdir(L"nested") == 0, "_wchdir nested failed");

  wchar_t wide_cwd_inside[512]{};
  expect(_wgetcwd(wide_cwd_inside,
                  sizeof(wide_cwd_inside) / sizeof(wide_cwd_inside[0])) !=
             nullptr,
         "_wgetcwd inside nested failed");
  expect(contains(wide_cwd_inside, L"nested"),
         "_wgetcwd missed nested component");
  expect(std::filesystem::equivalent(std::filesystem::current_path(),
                                     wide_cwd_inside),
         "std::filesystem::current_path did not observe _wchdir");

  expect(_wchdir(L"..") == 0, "_wchdir parent failed");
  expect(_chdir(cwd_before) == 0, "_chdir restore failed");

  std::filesystem::current_path(filesystem_cwd_before / sandbox);
  char cwd_after_filesystem_set[512]{};
  expect(_getcwd(cwd_after_filesystem_set, sizeof(cwd_after_filesystem_set)) !=
             nullptr,
         "_getcwd after std::filesystem::current_path failed");
  expect(contains(cwd_after_filesystem_set, sandbox),
         "_getcwd did not observe std::filesystem::current_path");

  char full_path_from_filesystem_cwd[512]{};
  expect(_fullpath(full_path_from_filesystem_cwd, "data.txt",
                   sizeof(full_path_from_filesystem_cwd)) != nullptr,
         "_fullpath after std::filesystem::current_path failed");
  expect(contains(full_path_from_filesystem_cwd, sandbox),
         "_fullpath did not use std::filesystem current path");

  std::filesystem::current_path(filesystem_cwd_before);
}

void verify_lowio_handle_state() {
  create_file_with_payload(dup_path, "abcdef");

  const int source = _open(dup_path, _O_RDWR | _O_BINARY);
  expect(source != -1, "_open dup source failed");
  expect(_commit(source) == 0, "_commit failed");
  expect(_chsize_s(source, 10) == 0, "_chsize_s grow failed");

  struct _stat64 source_stat {};
  expect(_fstat64(source, &source_stat) == 0, "_fstat64 grow failed");
  expect(source_stat.st_size == 10, "_fstat64 grow returned unexpected size");

  struct _stat legacy_source_stat {};
  expect(_fstat(source, &legacy_source_stat) == 0, "_fstat grow failed");
  expect(legacy_source_stat.st_size == 10,
         "_fstat grow returned unexpected size");

  expect(_chsize_s(source, 2) == 0, "_chsize_s shrink failed");
  expect(_fstat64(source, &source_stat) == 0, "_fstat64 shrink failed");
  expect(source_stat.st_size == 2, "_fstat64 shrink returned unexpected size");

  // Exercise the legacy lowio entry as well as _chsize_s. The secure variant is
  // preferred for new code, but hosted CRT callers may still use _chsize.
  expect(_chsize(source, 4) == 0, "_chsize grow failed");
  expect(_fstat(source, &legacy_source_stat) == 0, "_fstat after _chsize failed");
  expect(legacy_source_stat.st_size == 4,
         "_fstat after _chsize returned unexpected size");
  expect(_chsize(source, 2) == 0, "_chsize shrink failed");
  expect(_fstat(source, &legacy_source_stat) == 0,
         "_fstat after _chsize shrink failed");
  expect(legacy_source_stat.st_size == 2,
         "_fstat after _chsize shrink returned unexpected size");
  expect(_filelength(source) == 2, "_filelength after shrink failed");
  expect(_filelengthi64(source) == 2, "_filelengthi64 after shrink failed");

  expect(_lseeki64(source, 0, SEEK_SET) == 0, "_lseeki64 set failed");
  expect(_telli64(source) == 0, "_telli64 after seek set failed");
  expect(_eof(source) == 0, "_eof before end failed");
  expect(_lseeki64(source, 0, SEEK_END) == 2, "_lseeki64 end failed");
  expect(_telli64(source) == 2, "_telli64 at end failed");
  expect(_eof(source) != 0, "_eof at end failed");
  expect(_lseeki64(source, 0, SEEK_SET) == 0, "_lseeki64 reset failed");

  expect(_locking(source, _LK_NBLCK, 1) == 0, "_locking lock failed");
  expect(_locking(source, _LK_UNLCK, 1) == 0, "_locking unlock failed");

  const int duplicate = _dup(source);
  expect(duplicate != -1, "_dup failed");
  expect(_lseek(duplicate, 0, SEEK_SET) == 0, "_lseek duplicate failed");
  expect(_tell(duplicate) == 0, "_tell duplicate failed");

  char read_back[3]{};
  expect(_read(duplicate, read_back, 2) == 2, "_read duplicate failed");
  expect(std::memcmp(read_back, "ab", 2) == 0,
         "_read duplicate returned unexpected bytes");
  expect(_tell(duplicate) == 2, "_tell after duplicate read failed");

  const int target =
      _open(dup_target_path, _O_CREAT | _O_TRUNC | _O_RDWR | _O_BINARY,
            _S_IREAD | _S_IWRITE);
  expect(target != -1, "_open dup target failed");
  expect(_dup2(source, target) == 0, "_dup2 failed");
  expect(_lseek(target, 0, SEEK_SET) == 0, "_lseek dup2 target failed");

  char dup2_read_back[3]{};
  expect(_read(target, dup2_read_back, 2) == 2, "_read dup2 target failed");
  expect(std::memcmp(dup2_read_back, "ab", 2) == 0,
         "_read dup2 target returned unexpected bytes");

  expect(_close(target) == 0, "_close dup2 target failed");
  expect(_close(duplicate) == 0, "_close duplicate failed");
  expect(_close(source) == 0, "_close source failed");

  errno = 0;
  const int exclusive =
      _open(dup_path, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
            _S_IREAD | _S_IWRITE);
  expect(exclusive == -1, "_open exclusive unexpectedly replaced file");
  expect_errno(EEXIST, "_open exclusive errno mismatch");

  const int append = _open(dup_path, _O_WRONLY | _O_APPEND | _O_BINARY);
  expect(append != -1, "_open append failed");
  expect(_lseek(append, 0, SEEK_SET) == 0, "_lseek append failed");
  expect(_write(append, "XY", 2) == 2, "_write append failed");
  expect(_close(append) == 0, "_close append failed");

  const int appended_read = _open(dup_path, _O_RDONLY | _O_BINARY);
  expect(appended_read != -1, "_open appended readback failed");
  char appended[5]{};
  expect(_read(appended_read, appended, 4) == 4,
         "_read appended readback failed");
  expect(std::memcmp(appended, "abXY", 4) == 0,
         "_O_APPEND did not append at end of file");
  expect(_close(appended_read) == 0, "_close appended readback failed");

  const int old_umask = _umask(0);
  expect(old_umask != -1, "_umask read failed");
  expect(_umask(old_umask) != -1, "_umask restore failed");

  // UCRT lowio treats an invalid file descriptor as an invalid-parameter
  // contract violation before returning -1/EBADF. The default handler calls
  // Watson/fast-fail, which would bugcheck a kernel test driver, so this block
  // installs a temporary handler and verifies both parts of the UCRT contract.
  invalid_parameter_guard invalid_guard;
  char invalid_buffer[1]{};
  int previous_hits = invalid_guard.hits();
  errno = 0;
  expect(_read(-1, invalid_buffer, sizeof(invalid_buffer)) == -1,
         "_read invalid descriptor unexpectedly succeeded");
  expect_invalid_parameter_hit(invalid_guard, previous_hits,
                               "_read invalid descriptor missed handler");
  expect_errno(EBADF, "_read invalid descriptor errno mismatch");

  previous_hits = invalid_guard.hits();
  errno = 0;
  expect(_write(-1, invalid_buffer, sizeof(invalid_buffer)) == -1,
         "_write invalid descriptor unexpectedly succeeded");
  expect_invalid_parameter_hit(invalid_guard, previous_hits,
                               "_write invalid descriptor missed handler");
  expect_errno(EBADF, "_write invalid descriptor errno mismatch");

  previous_hits = invalid_guard.hits();
  errno = 0;
  expect(_lseek(-1, 0, SEEK_SET) == -1,
         "_lseek invalid descriptor unexpectedly succeeded");
  expect_invalid_parameter_hit(invalid_guard, previous_hits,
                               "_lseek invalid descriptor missed handler");
  expect_errno(EBADF, "_lseek invalid descriptor errno mismatch");

  previous_hits = invalid_guard.hits();
  errno = 0;
  expect(_commit(-1) == -1, "_commit invalid descriptor unexpectedly succeeded");
  expect_invalid_parameter_hit(invalid_guard, previous_hits,
                               "_commit invalid descriptor missed handler");
  expect_errno(EBADF, "_commit invalid descriptor errno mismatch");

  struct _stat64 invalid_stat {};
  previous_hits = invalid_guard.hits();
  errno = 0;
  expect(_fstat64(-1, &invalid_stat) == -1,
         "_fstat64 invalid descriptor unexpectedly succeeded");
  expect_invalid_parameter_hit(invalid_guard, previous_hits,
                               "_fstat64 invalid descriptor missed handler");
  expect_errno(EBADF, "_fstat64 invalid descriptor errno mismatch");
}

void verify_findfirst_findnext() {
  create_file_with_payload(find_a_path, "a");
  create_file_with_payload(find_b_path, "b");
  create_file_with_payload(find_legacy_path, "legacy");

  struct __finddata64_t data {};
  const intptr_t handle = _findfirst64(find_pattern, &data);
  expect(handle != -1, "_findfirst64 failed");

  bool saw_a = std::strcmp(data.name, "find_a.txt") == 0;
  int count = 1;
  while (_findnext64(handle, &data) == 0) {
    saw_a = saw_a || std::strcmp(data.name, "find_a.txt") == 0;
    ++count;
  }

  expect(_findclose(handle) == 0, "_findclose failed");
  expect(saw_a, "_findfirst64/_findnext64 missed find_a.txt");
  expect(count == 2, "_findfirst64/_findnext64 matched unexpected entries");

  struct _finddata_t legacy_data {};
  const intptr_t legacy_handle = _findfirst(find_pattern, &legacy_data);
  expect(legacy_handle != -1, "_findfirst failed");

  bool legacy_saw_a = std::strcmp(legacy_data.name, "find_a.txt") == 0;
  bool legacy_saw_legacy =
      std::strcmp(legacy_data.name, "find_legacy.txt") == 0;
  int legacy_count = 1;
  while (_findnext(legacy_handle, &legacy_data) == 0) {
    legacy_saw_a =
        legacy_saw_a || std::strcmp(legacy_data.name, "find_a.txt") == 0;
    legacy_saw_legacy =
        legacy_saw_legacy ||
        std::strcmp(legacy_data.name, "find_legacy.txt") == 0;
    ++legacy_count;
  }

  expect(_findclose(legacy_handle) == 0, "_findclose legacy failed");
  expect(legacy_saw_a, "_findfirst/_findnext missed find_a.txt");
  expect(legacy_saw_legacy, "_findfirst/_findnext missed find_legacy.txt");
  expect(legacy_count == 2, "_findfirst/_findnext matched unexpected entries");

  struct __finddata64_t missing_data {};
  errno = 0;
  const intptr_t missing_handle =
      _findfirst64("crtsys_crt_file_state\\missing_*.txt", &missing_data);
  expect(missing_handle == -1, "_findfirst64 unexpectedly found missing files");
  expect_errno(ENOENT, "_findfirst64 missing-pattern errno mismatch");
}

void verify_module_filename_state() {
  char module_path[MAX_PATH]{};
  const DWORD module_length =
      GetModuleFileNameA(nullptr, module_path,
                         static_cast<DWORD>(sizeof(module_path)));
  expect(module_length != 0, "GetModuleFileNameA failed");
  expect(module_length < sizeof(module_path),
         "GetModuleFileNameA unexpectedly truncated");
  expect(contains(module_path, "crtsys_test"),
         "GetModuleFileNameA did not report the test driver image");
  expect(contains(module_path, ".sys"),
         "GetModuleFileNameA did not report a driver image path");

  wchar_t wide_module_path[MAX_PATH]{};
  const DWORD wide_module_length = GetModuleFileNameW(
      nullptr, wide_module_path,
      static_cast<DWORD>(sizeof(wide_module_path) / sizeof(wide_module_path[0])));
  expect(wide_module_length != 0, "GetModuleFileNameW failed");
  expect(wide_module_length <
             sizeof(wide_module_path) / sizeof(wide_module_path[0]),
         "GetModuleFileNameW unexpectedly truncated");
  expect(contains(wide_module_path, L"crtsys_test"),
         "GetModuleFileNameW did not report the test driver image");
  expect(contains(wide_module_path, L".sys"),
         "GetModuleFileNameW did not report a driver image path");

  char *pgmptr{};
  expect(_get_pgmptr(&pgmptr) == 0, "_get_pgmptr failed");
  expect(pgmptr != nullptr, "_get_pgmptr returned null");
  expect(std::strcmp(pgmptr, module_path) == 0,
         "_get_pgmptr does not match GetModuleFileNameA");

  wchar_t *wide_pgmptr{};
  expect(_get_wpgmptr(&wide_pgmptr) == 0, "_get_wpgmptr failed");
  expect(wide_pgmptr != nullptr, "_get_wpgmptr returned null");
  expect(std::wcscmp(wide_pgmptr, wide_module_path) == 0,
         "_get_wpgmptr does not match GetModuleFileNameW");
}
} // namespace

void run() {
  current_directory_guard cwd_guard;
  remove_tree();

  expect(_mkdir(sandbox) == 0, "_mkdir sandbox failed");
  expect(_wmkdir(wide_nested) == 0, "_wmkdir nested failed");

  verify_stat_access_and_fullpath();
  verify_current_directory_state();
  verify_lowio_handle_state();
  verify_findfirst_findnext();
  verify_module_filename_state();

  remove_tree();
  std::cout << "CRT file/process-state semantic assertions passed\n";
}
} // namespace crt_file_state_semantic_test
