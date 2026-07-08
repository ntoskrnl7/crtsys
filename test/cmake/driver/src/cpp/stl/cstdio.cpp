#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <Windows.h>

namespace crt_file_io_semantic_test {
namespace {
constexpr char file_path[] = "crtsys_stdio_lowio.tmp";
constexpr char missing_stdio_path[] = "crtsys_stdio_missing.tmp";
constexpr char missing_lowio_path[] = "crtsys_lowio_missing.tmp";
constexpr char missing_remove_path[] = "crtsys_remove_missing.tmp";

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
}

void expect_doserrno(unsigned long expected, const char *message) {
  unsigned long captured{};
  expect(_get_doserrno(&captured) == 0, "_get_doserrno failed");
  expect(captured == expected, message);
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

  expect(std::remove(file_path) == 0, "remove failed");
  std::cout << "CRT stdio/lowio file I/O semantic assertions passed\n";
}
} // namespace crt_file_io_semantic_test
