#include <Windows.h>

#include <cstring>
#include <cwchar>
#include <iostream>
#include <stdexcept>

namespace nls_conversion_semantic_test {
namespace {
void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void verify_utf8_roundtrip() {
  const char utf8[] = {
      'A', static_cast<char>(0xe2), static_cast<char>(0x82),
      static_cast<char>(0xac), static_cast<char>(0xed),
      static_cast<char>(0x95), static_cast<char>(0x9c), '\0'};

  const int required = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, nullptr, 0);
  expect(required == 4, "MultiByteToWideChar required length mismatch");

  wchar_t wide[4]{};
  const int converted = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide,
      static_cast<int>(sizeof(wide) / sizeof(wide[0])));
  expect(converted == required, "MultiByteToWideChar conversion failed");
  expect(wide[0] == L'A', "UTF-8 roundtrip ASCII mismatch");
  expect(wide[1] == static_cast<wchar_t>(0x20ac),
         "UTF-8 roundtrip Euro sign mismatch");
  expect(wide[2] == static_cast<wchar_t>(0xd55c),
         "UTF-8 roundtrip Hangul mismatch");
  expect(wide[3] == L'\0', "UTF-8 roundtrip terminator mismatch");

  char roundtrip[16]{};
  const int bytes = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, roundtrip,
      static_cast<int>(sizeof(roundtrip)), nullptr, nullptr);
  expect(bytes == static_cast<int>(sizeof(utf8)),
         "WideCharToMultiByte byte length mismatch");
  expect(std::memcmp(roundtrip, utf8, sizeof(utf8)) == 0,
         "WideCharToMultiByte bytes mismatch");
}

void verify_invalid_utf8_errors() {
  const char invalid_utf8[] = {static_cast<char>(0xc3), '(', '\0'};

  SetLastError(ERROR_SUCCESS);
  const int result = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         invalid_utf8, -1, nullptr, 0);
  expect(result == 0, "invalid UTF-8 unexpectedly converted");
  expect(GetLastError() == ERROR_NO_UNICODE_TRANSLATION,
         "invalid UTF-8 last-error mismatch");
}

void verify_invalid_utf16_errors() {
  const wchar_t invalid_utf16[] = {static_cast<wchar_t>(0xd800), L'\0'};

  SetLastError(ERROR_SUCCESS);
  char buffer[16]{};
  const int result = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, invalid_utf16, -1, buffer,
      static_cast<int>(sizeof(buffer)), nullptr, nullptr);
  expect(result == 0, "invalid UTF-16 unexpectedly converted");
  expect(GetLastError() == ERROR_NO_UNICODE_TRANSLATION,
         "invalid UTF-16 last-error mismatch");
}

void verify_character_type_and_mapping() {
  const wchar_t chars[] = {L'A', L'1', L' ', L'\0'};
  WORD types[3]{};
  expect(GetStringTypeW(CT_CTYPE1, chars, 3, types) != FALSE,
         "GetStringTypeW failed");
  expect((types[0] & C1_UPPER) != 0, "GetStringTypeW uppercase mismatch");
  expect((types[1] & C1_DIGIT) != 0, "GetStringTypeW digit mismatch");
  expect((types[2] & C1_SPACE) != 0, "GetStringTypeW space mismatch");

  const wchar_t lower[] = L"crtsys";
  wchar_t upper[8]{};
  const int mapped = LCMapStringEx(
      LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, lower, -1, upper,
      static_cast<int>(sizeof(upper) / sizeof(upper[0])), nullptr, nullptr, 0);
  expect(mapped == 7, "LCMapStringEx uppercase length mismatch");
  expect(std::wcscmp(upper, L"CRTSYS") == 0,
         "LCMapStringEx uppercase value mismatch");
}
} // namespace

void run() {
  verify_utf8_roundtrip();
  verify_invalid_utf8_errors();
  verify_invalid_utf16_errors();
  verify_character_type_and_mapping();
  std::cout << "NLS conversion semantic assertions passed\n";
}
} // namespace nls_conversion_semantic_test
