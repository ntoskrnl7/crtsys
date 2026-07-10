#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <Windows.h>

namespace crt_file_io_semantic_test {
namespace {
constexpr char file_path[] = "crtsys_stdio_lowio.tmp";
constexpr char missing_stdio_path[] = "crtsys_stdio_missing.tmp";
constexpr char missing_lowio_path[] = "crtsys_lowio_missing.tmp";
constexpr char missing_remove_path[] = "crtsys_remove_missing.tmp";
constexpr char freopen_first_path[] = "crtsys_stdio_freopen_first.tmp";
constexpr char freopen_second_path[] = "crtsys_stdio_freopen_second.tmp";
constexpr char position_path[] = "crtsys_stdio_position.tmp";
constexpr char append_update_path[] = "crtsys_stdio_append_update.tmp";
constexpr char eof_error_path[] = "crtsys_stdio_eof_error.tmp";
constexpr wchar_t wide_stdio_path[] = L"crtsys_stdio_wide.tmp";

void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void remove_test_files() {
  std::remove(file_path);
  std::remove(missing_stdio_path);
  _unlink(missing_lowio_path);
  std::remove(missing_remove_path);
  std::remove(freopen_first_path);
  std::remove(freopen_second_path);
  std::remove(position_path);
  std::remove(append_update_path);
  std::remove(eof_error_path);
  _wunlink(wide_stdio_path);
}

void expect_doserrno(unsigned long expected, const char *message) {
  unsigned long captured{};
  expect(_get_doserrno(&captured) == 0, "_get_doserrno failed");
  expect(captured == expected, message);
}

void verify_stdio_position_and_buffering() {
  std::FILE *fp = std::fopen(position_path, "wb+");
  expect(fp != nullptr, "fopen position file failed");

  char buffer[64]{};
  expect(std::setvbuf(fp, buffer, _IOFBF, sizeof(buffer)) == 0,
         "setvbuf failed");
  expect(std::fputs("alpha beta", fp) >= 0, "fputs position payload failed");
  expect(std::fflush(fp) == 0, "fflush position file failed");

  std::fpos_t position{};
  expect(std::fseek(fp, 6, SEEK_SET) == 0, "fseek before fgetpos failed");
  expect(std::fgetpos(fp, &position) == 0, "fgetpos failed");

  char word[8]{};
  expect(std::fread(word, 1, 4, fp) == 4, "fread after fgetpos failed");
  expect(std::memcmp(word, "beta", 4) == 0,
         "fread after fgetpos returned unexpected bytes");

  expect(std::fsetpos(fp, &position) == 0, "fsetpos failed");
  const int first = std::fgetc(fp);
  expect(first == 'b', "fgetc after fsetpos failed");
  expect(std::ungetc(first, fp) == first, "ungetc failed");
  expect(std::fgetc(fp) == 'b', "fgetc after ungetc failed");

  expect(std::fclose(fp) == 0, "fclose position file failed");
}

void verify_stdio_append_update_and_error_state() {
  {
    std::FILE *fp = std::fopen(append_update_path, "wb");
    expect(fp != nullptr, "fopen append seed failed");
    expect(std::fputs("alpha", fp) >= 0, "append seed write failed");
    expect(std::fclose(fp) == 0, "append seed close failed");
  }

  std::FILE *append = std::fopen(append_update_path, "ab+");
  expect(append != nullptr, "fopen append update failed");
  expect(std::fseek(append, 0, SEEK_SET) == 0, "append fseek begin failed");
  expect(std::fputs("!", append) >= 0, "append write failed");
  expect(std::fflush(append) == 0, "append fflush failed");
  expect(std::fseek(append, 0, SEEK_SET) == 0, "append rewind failed");

  char append_read[8]{};
  expect(std::fread(append_read, 1, 6, append) == 6,
         "append readback failed");
  expect(std::memcmp(append_read, "alpha!", 6) == 0,
         "append mode did not write at end");
  expect(std::fclose(append) == 0, "append close failed");

  std::FILE *update = std::fopen(append_update_path, "rb+");
  expect(update != nullptr, "fopen update failed");
  expect(std::fseek(update, 1, SEEK_SET) == 0, "update fseek failed");
  expect(std::fputc('Z', update) == 'Z', "update fputc failed");
  expect(std::fflush(update) == 0, "update fflush failed");
  expect(std::fseek(update, 0, SEEK_SET) == 0, "update rewind failed");

  char update_read[8]{};
  expect(std::fread(update_read, 1, 6, update) == 6,
         "update readback failed");
  expect(std::memcmp(update_read, "aZpha!", 6) == 0,
         "update mode did not overwrite in place");

  expect(std::fgetc(update) == EOF, "EOF read did not return EOF");
  expect(std::feof(update) != 0, "feof was not set after EOF");
  expect(std::ferror(update) == 0, "ferror set during EOF path");
  std::clearerr(update);
  expect(std::feof(update) == 0, "clearerr did not clear EOF");
  expect(std::fclose(update) == 0, "update close failed");

  std::FILE *readonly = std::fopen(eof_error_path, "wb");
  expect(readonly != nullptr, "fopen error seed failed");
  expect(std::fputs("readonly", readonly) >= 0, "error seed write failed");
  expect(std::fclose(readonly) == 0, "error seed close failed");

  readonly = std::fopen(eof_error_path, "rb");
  expect(readonly != nullptr, "fopen readonly failed");
  expect(std::fputc('x', readonly) == EOF,
         "write to readonly stream unexpectedly succeeded");
  expect(std::ferror(readonly) != 0, "ferror was not set after write error");
  std::clearerr(readonly);
  expect(std::ferror(readonly) == 0, "clearerr did not clear error state");
  expect(std::fclose(readonly) == 0, "readonly close failed");
}

void verify_stdio_reopen_and_temporary_files() {
  std::FILE *tmp = std::tmpfile();
  expect(tmp != nullptr, "tmpfile failed");
  expect(std::fputs("temporary", tmp) >= 0, "tmpfile write failed");
  expect(std::fseek(tmp, 0, SEEK_SET) == 0, "tmpfile rewind failed");
  char tmp_read[16]{};
  expect(std::fread(tmp_read, 1, 9, tmp) == 9, "tmpfile read failed");
  expect(std::memcmp(tmp_read, "temporary", 9) == 0,
         "tmpfile read returned unexpected bytes");
  expect(std::fclose(tmp) == 0, "tmpfile close failed");

  char tmp_name[L_tmpnam]{};
  expect(tmpnam_s(tmp_name, sizeof(tmp_name)) == 0, "tmpnam_s failed");
  expect(tmp_name[0] != '\0', "tmpnam_s returned empty name");

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996) // _tempnam is intentionally covered here.
#endif
  char *allocated_temp_name = _tempnam(nullptr, "crtsys");
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  expect(allocated_temp_name != nullptr, "_tempnam failed");
  expect(allocated_temp_name[0] != '\0', "_tempnam returned empty name");
  std::free(allocated_temp_name);

  std::FILE *fp = std::fopen(freopen_first_path, "wb+");
  expect(fp != nullptr, "fopen freopen first failed");
  expect(std::fputs("first", fp) >= 0, "freopen first write failed");
  fp = std::freopen(freopen_second_path, "wb+", fp);
  expect(fp != nullptr, "freopen second failed");
  expect(std::fputs("second", fp) >= 0, "freopen second write failed");
  expect(std::fflush(fp) == 0, "freopen second fflush failed");
  expect(std::fseek(fp, 0, SEEK_SET) == 0, "freopen second rewind failed");

  char read_back[8]{};
  expect(std::fread(read_back, 1, 6, fp) == 6, "freopen second read failed");
  expect(std::memcmp(read_back, "second", 6) == 0,
         "freopen second read returned unexpected bytes");
  expect(std::fclose(fp) == 0, "freopen second close failed");
}

void verify_wide_stdio() {
  std::FILE *fp = _wfopen(wide_stdio_path, L"w+");
  expect(fp != nullptr, "_wfopen wide stdio file failed");

  expect(std::fputwc(L'A', fp) == L'A', "fputwc failed");
  expect(std::fputws(L"BC\n", fp) >= 0, "fputws failed");
  expect(std::fflush(fp) == 0, "fflush wide stdio failed");
  expect(std::fseek(fp, 0, SEEK_SET) == 0, "rewind wide stdio failed");

  expect(std::fgetwc(fp) == L'A', "fgetwc returned unexpected character");
  wchar_t line[8]{};
  expect(std::fgetws(line, static_cast<int>(sizeof(line) / sizeof(line[0])),
                     fp) != nullptr,
         "fgetws failed");
  expect(std::wcscmp(line, L"BC\n") == 0,
         "fgetws returned unexpected string");

  expect(std::fclose(fp) == 0, "fclose wide stdio failed");
}
} // namespace

void run() {
  remove_test_files();

  errno = 0;
  _doserrno = 0;
  std::FILE *missing_stdio = std::fopen(missing_stdio_path, "rb");
  expect(missing_stdio == nullptr, "fopen unexpectedly opened missing file");
  expect(errno == ENOENT, "fopen missing file errno mismatch");
  expect_doserrno(ERROR_FILE_NOT_FOUND,
                  "fopen missing file _doserrno mismatch");

  std::FILE *fp = std::fopen(file_path, "wb+");
  expect(fp != nullptr, "fopen wb+ failed");

  constexpr char first_payload[] = "abc";
  expect(std::fwrite(first_payload, 1, 3, fp) == 3, "fwrite failed");
  expect(std::fflush(fp) == 0, "fflush failed");
  expect(std::fseek(fp, 0, SEEK_END) == 0, "fseek end failed");
  expect(std::ftell(fp) == 3, "ftell returned unexpected file length");
  expect(std::fseek(fp, 0, SEEK_SET) == 0, "fseek set failed");

  char stdio_read[4]{};
  expect(std::fread(stdio_read, 1, 3, fp) == 3, "fread failed");
  expect(std::memcmp(stdio_read, first_payload, 3) == 0,
         "fread returned unexpected bytes");
  expect(std::fclose(fp) == 0, "fclose failed");

  const int fh = _open(file_path, _O_RDWR | _O_BINARY);
  expect(fh != -1, "_open existing file failed");
  expect(_lseek(fh, 0, SEEK_END) == 3, "_lseek end failed");

  constexpr char second_payload[] = "XYZ";
  expect(_write(fh, second_payload, 3) == 3, "_write failed");
  expect(_lseek(fh, 0, SEEK_SET) == 0, "_lseek set failed");

  char lowio_read[7]{};
  expect(_read(fh, lowio_read, 6) == 6, "_read failed");
  expect(std::memcmp(lowio_read, "abcXYZ", 6) == 0,
         "_read returned unexpected bytes");
  expect(_close(fh) == 0, "_close failed");

  errno = 0;
  _doserrno = 0;
  const int missing_fh = _open(missing_lowio_path, _O_RDONLY | _O_BINARY);
  expect(missing_fh == -1, "_open unexpectedly opened missing file");
  expect(errno == ENOENT, "_open missing file errno mismatch");
  expect_doserrno(ERROR_FILE_NOT_FOUND,
                  "_open missing file _doserrno mismatch");

  errno = 0;
  _doserrno = 0;
  expect(std::remove(missing_remove_path) != 0,
         "remove unexpectedly removed missing file");
  expect(errno == ENOENT, "remove missing file errno mismatch");
  expect_doserrno(ERROR_FILE_NOT_FOUND,
                  "remove missing file _doserrno mismatch");

  verify_stdio_position_and_buffering();
  verify_stdio_append_update_and_error_state();
  verify_stdio_reopen_and_temporary_files();
  verify_wide_stdio();

  expect(std::remove(file_path) == 0, "remove failed");
  std::cout << "CRT stdio/lowio file I/O semantic assertions passed\n";
}
} // namespace crt_file_io_semantic_test
