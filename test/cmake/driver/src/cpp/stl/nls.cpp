#include <Windows.h>

#include <cerrno>
#include <climits>
#include <clocale>
#include <cstdlib>
#include <cuchar>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace nls_conversion_semantic_test {
namespace {
extern "C" unsigned int __cdecl ___lc_codepage_func();

void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void expect_with_value(bool condition, const char *message, unsigned int value) {
  if (!condition) {
    throw std::runtime_error(std::string{message} + ": " +
                             std::to_string(value));
  }
}

void expect_with_value(bool condition, const char *message, int value) {
  if (!condition) {
    throw std::runtime_error(std::string{message} + ": " +
                             std::to_string(value));
  }
}

bool bytes_equal(const char *left, const char *right, std::size_t count) {
  return std::memcmp(left, right, count) == 0;
}

class c_locale_guard {
public:
  c_locale_guard() {
    if (const char *current = std::setlocale(LC_ALL, nullptr)) {
      original_ = current;
    }
  }

  ~c_locale_guard() {
    if (!original_.empty()) {
      (void)std::setlocale(LC_ALL, original_.c_str());
    }
  }

  c_locale_guard(const c_locale_guard &) = delete;
  c_locale_guard &operator=(const c_locale_guard &) = delete;

private:
  std::string original_;
};

void set_utf8_c_locale() {
  // MSVC's C locale conversion functions key mbtowc/wctomb off the CRT
  // codepage field. Prefer the CRT codepage spelling over a named C++ locale
  // spelling such as en_US.UTF-8, which can be accepted without making that
  // field CP_UTF8.
  const char *locale_name = nullptr;
  for (const char *candidate :
       {"en-US.utf8", "en-US.UTF-8", ".UTF8", ".UTF-8", ".65001"}) {
    locale_name = std::setlocale(LC_CTYPE, candidate);
    if (locale_name != nullptr) {
      break;
    }
  }

  expect(locale_name != nullptr, "setlocale UTF-8 failed");
  (void)std::mbtowc(nullptr, nullptr, 0);
  expect_with_value(___lc_codepage_func() == CP_UTF8,
                    "setlocale UTF-8 codepage mismatch",
                    ___lc_codepage_func());
}

std::string utf8_sample() {
  const char bytes[] = {
      'A',
      static_cast<char>(0xe2),
      static_cast<char>(0x82),
      static_cast<char>(0xac),
      static_cast<char>(0xed),
      static_cast<char>(0x95),
      static_cast<char>(0x9c),
      '\0',
  };

  return bytes;
}

std::filesystem::path path_from_utf8(const std::string &value) {
#if defined(__cpp_char8_t)
  std::u8string utf8;
  utf8.reserve(value.size());
  for (const unsigned char ch : value) {
    utf8.push_back(static_cast<char8_t>(ch));
  }
  return std::filesystem::path{utf8};
#else
  return std::filesystem::u8path(value);
#endif
}

std::string path_to_utf8(const std::filesystem::path &path) {
  const auto value = path.u8string();
#if defined(__cpp_char8_t)
  return std::string{reinterpret_cast<const char *>(value.data()),
                     value.size()};
#else
  return value;
#endif
}

void verify_acp_and_buffer_edges() {
  const char acp_text[] = "Driver";
  const int required =
      MultiByteToWideChar(CP_ACP, 0, acp_text, -1, nullptr, 0);
  expect(required == 7, "CP_ACP required length mismatch");

  wchar_t wide[7]{};
  const int converted =
      MultiByteToWideChar(CP_ACP, 0, acp_text, -1, wide,
                          static_cast<int>(std::size(wide)));
  expect(converted == required, "CP_ACP conversion failed");
  expect(std::wcscmp(wide, L"Driver") == 0, "CP_ACP conversion mismatch");

  wchar_t too_small_wide[3]{};
  SetLastError(ERROR_SUCCESS);
  const int small_wide_result =
      MultiByteToWideChar(CP_ACP, 0, acp_text, -1, too_small_wide,
                          static_cast<int>(std::size(too_small_wide)));
  expect(small_wide_result == 0,
         "MultiByteToWideChar unexpectedly accepted a small buffer");
  expect(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
         "MultiByteToWideChar small-buffer error mismatch");

  const char explicit_count_text[] = {'O', 'K', '!'};
  wchar_t explicit_count_wide[3]{};
  const int explicit_count_result =
      MultiByteToWideChar(CP_ACP, 0, explicit_count_text, 3,
                          explicit_count_wide,
                          static_cast<int>(std::size(explicit_count_wide)));
  expect(explicit_count_result == 3,
         "MultiByteToWideChar explicit-count conversion failed");
  expect(explicit_count_wide[0] == L'O' && explicit_count_wide[1] == L'K' &&
             explicit_count_wide[2] == L'!',
         "MultiByteToWideChar explicit-count value mismatch");

  const wchar_t wide_text[] = L"Kernel";
  const int narrow_required =
      WideCharToMultiByte(CP_ACP, 0, wide_text, -1, nullptr, 0, nullptr,
                          nullptr);
  expect(narrow_required == 7, "CP_ACP narrow required length mismatch");

  char narrow[7]{};
  const int narrow_converted =
      WideCharToMultiByte(CP_ACP, 0, wide_text, -1, narrow,
                          static_cast<int>(std::size(narrow)), nullptr,
                          nullptr);
  expect(narrow_converted == narrow_required, "CP_ACP narrow conversion failed");
  expect(std::strcmp(narrow, "Kernel") == 0, "CP_ACP narrow value mismatch");

  char too_small_narrow[3]{};
  SetLastError(ERROR_SUCCESS);
  const int small_narrow_result =
      WideCharToMultiByte(CP_ACP, 0, wide_text, -1, too_small_narrow,
                          static_cast<int>(std::size(too_small_narrow)),
                          nullptr, nullptr);
  expect(small_narrow_result == 0,
         "WideCharToMultiByte unexpectedly accepted a small buffer");
  expect(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
         "WideCharToMultiByte small-buffer error mismatch");
}

void verify_utf8_roundtrip() {
  const std::string utf8 = utf8_sample();

  const int required = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(), -1, nullptr, 0);
  expect(required == 4, "MultiByteToWideChar required length mismatch");

  wchar_t wide[4]{};
  const int converted = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(), -1, wide,
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
  expect(bytes == static_cast<int>(utf8.size() + 1),
         "WideCharToMultiByte byte length mismatch");
  expect(std::memcmp(roundtrip, utf8.c_str(), utf8.size() + 1) == 0,
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

void verify_ucrt_c_locale_multibyte() {
  c_locale_guard locale_guard;
  expect(std::setlocale(LC_ALL, "C") != nullptr, "setlocale C failed");

  wchar_t wide{};
  expect(std::mbtowc(&wide, "A", 1) == 1, "C locale mbtowc failed");
  expect(wide == L'A', "C locale mbtowc value mismatch");

  char narrow[MB_LEN_MAX]{};
  expect(std::wctomb(narrow, L'Z') == 1, "C locale wctomb failed");
  expect(narrow[0] == 'Z', "C locale wctomb value mismatch");

  wchar_t wide_text[4]{};
  expect(std::mbstowcs(wide_text, "abc", std::size(wide_text)) == 3,
         "C locale mbstowcs failed");
  expect(std::wcscmp(wide_text, L"abc") == 0,
         "C locale mbstowcs value mismatch");

  size_t converted{};
  wchar_t wide_text_s[4]{};
  expect(mbstowcs_s(&converted, wide_text_s, std::size(wide_text_s), "abc",
                    _TRUNCATE) == 0,
         "C locale mbstowcs_s failed");
  expect(converted == 4 && std::wcscmp(wide_text_s, L"abc") == 0,
         "C locale mbstowcs_s value mismatch");

  char narrow_text[4]{};
  expect(std::wcstombs(narrow_text, L"xyz", std::size(narrow_text)) == 3,
         "C locale wcstombs failed");
  expect(std::strcmp(narrow_text, "xyz") == 0,
         "C locale wcstombs value mismatch");

  char narrow_text_s[4]{};
  expect(wcstombs_s(&converted, narrow_text_s, std::size(narrow_text_s), L"xyz",
                    _TRUNCATE) == 0,
         "C locale wcstombs_s failed");
  expect(converted == 4 && std::strcmp(narrow_text_s, "xyz") == 0,
         "C locale wcstombs_s value mismatch");
}

void verify_ucrt_utf8_multibyte() {
  c_locale_guard locale_guard;
  set_utf8_c_locale();

  const std::string utf8 = utf8_sample();
  const char *euro = utf8.c_str() + 1;
  const char *hangul = utf8.c_str() + 4;

  wchar_t wide{};
  errno = 0;
  const int mbtowc_result = std::mbtowc(&wide, euro, 3);
  expect_with_value(mbtowc_result == 3, "UTF-8 mbtowc failed",
                    mbtowc_result);
  expect_with_value(errno == 0, "UTF-8 mbtowc errno mismatch", errno);
  expect(wide == static_cast<wchar_t>(0x20ac),
         "UTF-8 mbtowc Euro value mismatch");

  char narrow[MB_LEN_MAX]{};
  const int euro_bytes = std::wctomb(narrow, static_cast<wchar_t>(0x20ac));
  expect(euro_bytes == 3, "UTF-8 wctomb Euro length mismatch");
  expect(bytes_equal(narrow, euro, 3), "UTF-8 wctomb Euro value mismatch");

  wchar_t wide_text[4]{};
  expect(std::mbstowcs(wide_text, utf8.c_str(), std::size(wide_text)) == 3,
         "UTF-8 mbstowcs failed");
  expect(wide_text[0] == L'A' &&
              wide_text[1] == static_cast<wchar_t>(0x20ac) &&
              wide_text[2] == static_cast<wchar_t>(0xd55c) &&
              wide_text[3] == L'\0',
         "UTF-8 mbstowcs value mismatch");

  size_t converted{};
  wchar_t wide_text_s[4]{};
  expect(mbstowcs_s(&converted, wide_text_s, std::size(wide_text_s),
                    utf8.c_str(), _TRUNCATE) == 0,
         "UTF-8 mbstowcs_s failed");
  expect(converted == 4 && wide_text_s[0] == L'A' &&
             wide_text_s[1] == static_cast<wchar_t>(0x20ac) &&
             wide_text_s[2] == static_cast<wchar_t>(0xd55c) &&
             wide_text_s[3] == L'\0',
         "UTF-8 mbstowcs_s value mismatch");

  char narrow_text[16]{};
  expect(std::wcstombs(narrow_text, wide_text, std::size(narrow_text)) ==
             utf8.size(),
         "UTF-8 wcstombs failed");
  expect(bytes_equal(narrow_text, utf8.c_str(), utf8.size() + 1),
         "UTF-8 wcstombs value mismatch");

  char narrow_text_s[16]{};
  expect(wcstombs_s(&converted, narrow_text_s, std::size(narrow_text_s),
                    wide_text, _TRUNCATE) == 0,
         "UTF-8 wcstombs_s failed");
  expect(converted == utf8.size() + 1 &&
             bytes_equal(narrow_text_s, utf8.c_str(), utf8.size() + 1),
         "UTF-8 wcstombs_s value mismatch");

  std::mbstate_t state{};
  wchar_t restartable_wide{};
  expect(std::mbrtowc(&restartable_wide, hangul, 3, &state) == 3,
         "UTF-8 mbrtowc failed");
  expect(restartable_wide == static_cast<wchar_t>(0xd55c),
         "UTF-8 mbrtowc Hangul value mismatch");

  state = {};
  char restartable_narrow[MB_LEN_MAX]{};
  expect(std::wcrtomb(restartable_narrow, static_cast<wchar_t>(0xd55c),
                      &state) == 3,
         "UTF-8 wcrtomb failed");
  expect(bytes_equal(restartable_narrow, hangul, 3),
         "UTF-8 wcrtomb Hangul value mismatch");

  state = {};
  errno = 0;
  const char invalid_utf8[] = {static_cast<char>(0xc3), '(', '\0'};
  expect(std::mbrtowc(&restartable_wide, invalid_utf8, 2, &state) ==
             static_cast<std::size_t>(-1),
         "UTF-8 mbrtowc unexpectedly accepted invalid sequence");
  expect(errno == EILSEQ, "UTF-8 mbrtowc invalid errno mismatch");
}

void verify_cuchar_utf8_conversions() {
  // MSVC UCRT's <uchar.h> conversion entry points are implemented as UTF-8
  // conversions independently of the process C locale.
  const char emoji[] = {static_cast<char>(0xf0), static_cast<char>(0x9f),
                        static_cast<char>(0x98), static_cast<char>(0x80),
                        '\0'};

  std::mbstate_t state{};
  char32_t decoded32{};
  expect(std::mbrtoc32(&decoded32, emoji, 4, &state) == 4,
         "mbrtoc32 emoji conversion failed");
  expect(decoded32 == static_cast<char32_t>(0x1f600),
         "mbrtoc32 emoji value mismatch");

  state = {};
  char encoded32[MB_LEN_MAX]{};
  expect(std::c32rtomb(encoded32, static_cast<char32_t>(0x1f600), &state) == 4,
         "c32rtomb emoji conversion failed");
  expect(bytes_equal(encoded32, emoji, 4), "c32rtomb emoji bytes mismatch");

  state = {};
  char16_t decoded16{};
  expect(std::mbrtoc16(&decoded16, emoji, 4, &state) == 4,
         "mbrtoc16 emoji high surrogate failed");
  expect(decoded16 == static_cast<char16_t>(0xd83d),
         "mbrtoc16 emoji high surrogate mismatch");
  expect(std::mbrtoc16(&decoded16, "", 0, &state) ==
             static_cast<std::size_t>(-3),
         "mbrtoc16 emoji low surrogate state failed");
  expect(decoded16 == static_cast<char16_t>(0xde00),
         "mbrtoc16 emoji low surrogate mismatch");

  state = {};
  char encoded16[MB_LEN_MAX]{};
  expect(std::c16rtomb(encoded16, static_cast<char16_t>(0xd83d), &state) == 0,
         "c16rtomb high surrogate state failed");
  expect(std::c16rtomb(encoded16, static_cast<char16_t>(0xde00), &state) == 4,
         "c16rtomb low surrogate conversion failed");
  expect(bytes_equal(encoded16, emoji, 4), "c16rtomb emoji bytes mismatch");
}

void verify_character_type_and_mapping() {
  const wchar_t chars[] = {L'A', L'1', L' ', L'\0'};
  WORD types[3]{};
  expect(GetStringTypeW(CT_CTYPE1, chars, 3, types) != FALSE,
         "GetStringTypeW failed");
  expect((types[0] & C1_UPPER) != 0, "GetStringTypeW uppercase mismatch");
  expect((types[1] & C1_DIGIT) != 0, "GetStringTypeW digit mismatch");
  expect((types[2] & C1_SPACE) != 0, "GetStringTypeW space mismatch");

  WORD invariant_types[3]{};
  expect(GetStringTypeExW(LOCALE_INVARIANT, CT_CTYPE1, chars, 3,
                          invariant_types) != FALSE,
         "GetStringTypeExW invariant locale failed");
  expect((invariant_types[0] & C1_UPPER) != 0,
         "GetStringTypeExW uppercase mismatch");
  expect((invariant_types[1] & C1_DIGIT) != 0,
         "GetStringTypeExW digit mismatch");
  expect((invariant_types[2] & C1_SPACE) != 0,
         "GetStringTypeExW space mismatch");

  const wchar_t lower[] = L"crtsys";
  wchar_t upper[8]{};
  const int required = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE,
                                     lower, -1, nullptr, 0, nullptr, nullptr,
                                     0);
  expect(required == 7, "LCMapStringEx uppercase query length mismatch");

  const int mapped = LCMapStringEx(
      LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, lower, -1, upper,
      static_cast<int>(sizeof(upper) / sizeof(upper[0])), nullptr, nullptr, 0);
  expect(mapped == 7, "LCMapStringEx uppercase length mismatch");
  expect(std::wcscmp(upper, L"CRTSYS") == 0,
         "LCMapStringEx uppercase value mismatch");

  wchar_t too_small_upper[3]{};
  SetLastError(ERROR_SUCCESS);
  const int small_map = LCMapStringEx(
      LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, lower, -1, too_small_upper,
      static_cast<int>(std::size(too_small_upper)), nullptr, nullptr, 0);
  expect(small_map == 0, "LCMapStringEx unexpectedly accepted small buffer");
  expect(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
         "LCMapStringEx small-buffer error mismatch");

  const int case_insensitive_compare =
      CompareStringEx(LOCALE_NAME_INVARIANT, NORM_IGNORECASE, L"kernel", -1,
                      L"KERNEL", -1, nullptr, nullptr, 0);
  expect(case_insensitive_compare == CSTR_EQUAL,
         "CompareStringEx case-insensitive equality mismatch");

  const int ordinal_ignore_case =
      CompareStringOrdinal(L"kernel", -1, L"KERNEL", -1, TRUE);
  expect(ordinal_ignore_case == CSTR_EQUAL,
         "CompareStringOrdinal ignore-case equality mismatch");

  const int ordinal_case_sensitive =
      CompareStringOrdinal(L"kernel", -1, L"KERNEL", -1, FALSE);
  expect(ordinal_case_sensitive != CSTR_EQUAL,
         "CompareStringOrdinal case-sensitive comparison mismatch");
}

void verify_filesystem_utf8_path_roundtrip() {
  namespace fs = std::filesystem;

  const fs::path sandbox = "crtsys-nls-path-sandbox";
  std::error_code ec;
  fs::remove_all(sandbox, ec);
  expect(fs::create_directory(sandbox), "create UTF-8 path sandbox failed");

  const std::string utf8_filename = std::string{"hangul-"} +
                                    static_cast<char>(0xed) +
                                    static_cast<char>(0x95) +
                                    static_cast<char>(0x9c) + ".txt";
  const fs::path file = sandbox / path_from_utf8(utf8_filename);
  const fs::path copied = sandbox / path_from_utf8(std::string{"copy-"} +
                                                   utf8_filename);
  const fs::path renamed = sandbox / path_from_utf8(std::string{"renamed-"} +
                                                    utf8_filename);

  std::ofstream(file).put('x');
  expect(fs::exists(file), "UTF-8 filesystem path create failed");
  expect(path_to_utf8(file.filename()) == utf8_filename,
         "filesystem path UTF-8 filename roundtrip mismatch");
  expect(file.filename().wstring().find(static_cast<wchar_t>(0xd55c)) !=
             std::wstring::npos,
         "filesystem path wide filename missed Hangul");

  std::ifstream input(file);
  expect(input.get() == 'x', "UTF-8 filesystem path readback failed");

  ec.clear();
  expect(fs::copy_file(file, copied, ec), "UTF-8 filesystem copy_file failed");
  expect(!ec && fs::exists(copied), "UTF-8 filesystem copy_file state failed");

  ec.clear();
  fs::rename(copied, renamed, ec);
  expect(!ec && !fs::exists(copied) && fs::exists(renamed),
         "UTF-8 filesystem rename state failed");

  bool saw_original = false;
  bool saw_renamed = false;
  for (const auto &entry : fs::directory_iterator(sandbox)) {
    const fs::path filename = entry.path().filename();
    saw_original = saw_original || filename == file.filename();
    saw_renamed = saw_renamed || filename == renamed.filename();
  }
  expect(saw_original && saw_renamed,
         "UTF-8 filesystem directory iteration missed entry");

  fs::remove(file, ec);
  fs::remove(renamed, ec);
  fs::remove(sandbox, ec);
}
} // namespace

void run() {
  verify_acp_and_buffer_edges();
  verify_utf8_roundtrip();
  verify_invalid_utf8_errors();
  verify_invalid_utf16_errors();
  verify_ucrt_c_locale_multibyte();
  verify_ucrt_utf8_multibyte();
  verify_cuchar_utf8_conversions();
  verify_character_type_and_mapping();
  verify_filesystem_utf8_path_roundtrip();
  std::cout << "NLS conversion semantic assertions passed\n";
}
} // namespace nls_conversion_semantic_test
