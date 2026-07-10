#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <iostream>
#include <stdexcept>
#include <sys/timeb.h>

namespace crt_time_semantic_test {
namespace {
void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class timezone_guard {
public:
  timezone_guard() {
    if (_dupenv_s(&saved_, &saved_length_, "TZ") != 0) {
      saved_ = nullptr;
      saved_length_ = 0;
    }
  }

  ~timezone_guard() {
    if (saved_ != nullptr) {
      (void)_putenv_s("TZ", saved_);
      std::free(saved_);
    } else {
      (void)_putenv_s("TZ", "");
    }
    _tzset();
  }

  timezone_guard(const timezone_guard &) = delete;
  timezone_guard &operator=(const timezone_guard &) = delete;

private:
  char *saved_{};
  size_t saved_length_{};
};

void verify_current_time() {
  __time64_t stored{};
  const __time64_t now = _time64(&stored);
  expect(now != -1, "_time64 failed");
  expect(stored == now, "_time64 did not store returned value");
  expect(now > 0, "_time64 returned non-positive time");

  __timeb64 timeb{};
  expect(_ftime64_s(&timeb) == 0, "_ftime64_s failed");
  expect(timeb.time >= now - 1 && timeb.time <= now + 1,
         "_ftime64_s time differs too much from _time64");
  expect(timeb.millitm < 1000, "_ftime64_s returned invalid milliseconds");
}

void verify_utc_formatting() {
  const __time64_t epoch{};
  tm utc{};
  expect(gmtime_s(&utc, &epoch) == 0, "gmtime_s epoch failed");
  expect(utc.tm_year == 70, "gmtime_s epoch year mismatch");
  expect(utc.tm_mon == 0, "gmtime_s epoch month mismatch");
  expect(utc.tm_mday == 1, "gmtime_s epoch day mismatch");
  expect(utc.tm_hour == 0, "gmtime_s epoch hour mismatch");

  char formatted[64]{};
  expect(std::strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S",
                       &utc) != 0,
         "strftime UTC epoch failed");
  expect(std::strcmp(formatted, "1970-01-01 00:00:00") == 0,
         "strftime UTC epoch value mismatch");

  wchar_t wide_formatted[64]{};
  expect(std::wcsftime(wide_formatted,
                       sizeof(wide_formatted) / sizeof(wide_formatted[0]),
                       L"%Y-%m-%d %H:%M:%S", &utc) != 0,
         "wcsftime UTC epoch failed");
  expect(std::wcscmp(wide_formatted, L"1970-01-01 00:00:00") == 0,
         "wcsftime UTC epoch value mismatch");

  char too_small[4]{};
  expect(std::strftime(too_small, sizeof(too_small), "%Y-%m-%d", &utc) == 0,
         "strftime unexpectedly accepted a small buffer");

  wchar_t wide_too_small[4]{};
  expect(std::wcsftime(wide_too_small,
                       sizeof(wide_too_small) / sizeof(wide_too_small[0]),
                       L"%Y-%m-%d", &utc) == 0,
         "wcsftime unexpectedly accepted a small buffer");

  char asctime_buffer[26]{};
  expect(asctime_s(asctime_buffer, sizeof(asctime_buffer), &utc) == 0,
         "asctime_s UTC epoch failed");
  expect(std::strcmp(asctime_buffer, "Thu Jan  1 00:00:00 1970\n") == 0,
         "asctime_s UTC epoch value mismatch");

  char ctime_buffer[26]{};
  expect(_ctime64_s(ctime_buffer, sizeof(ctime_buffer), &epoch) == 0,
         "_ctime64_s UTC epoch failed");
  expect(std::strstr(ctime_buffer, "1970") != nullptr,
         "_ctime64_s UTC epoch did not include year");

  tm roundtrip = utc;
  expect(_mkgmtime64(&roundtrip) == 0, "_mkgmtime64 UTC epoch failed");
}

void verify_tzset_localtime() {
  timezone_guard guard;

  expect(_putenv_s("TZ", "UTC0") == 0, "_putenv_s TZ failed");
  _tzset();

  long timezone_seconds{};
  expect(_get_timezone(&timezone_seconds) == 0, "_get_timezone failed");
  expect(timezone_seconds == 0, "_get_timezone UTC0 value mismatch");

  int daylight{};
  expect(_get_daylight(&daylight) == 0, "_get_daylight failed");
  expect(daylight == 0, "_get_daylight UTC0 value mismatch");

  char timezone_name[32]{};
  size_t timezone_name_length{};
  expect(_get_tzname(&timezone_name_length, timezone_name,
                     sizeof(timezone_name), 0) == 0,
         "_get_tzname standard name failed");
  expect(timezone_name_length > 1, "_get_tzname returned empty standard name");
  expect(std::strstr(timezone_name, "UTC") != nullptr,
         "_get_tzname standard name mismatch");

  const __time64_t epoch{};
  tm local{};
  expect(localtime_s(&local, &epoch) == 0, "localtime_s UTC epoch failed");
  expect(local.tm_year == 70, "localtime_s UTC epoch year mismatch");
  expect(local.tm_mon == 0, "localtime_s UTC epoch month mismatch");
  expect(local.tm_mday == 1, "localtime_s UTC epoch day mismatch");
  expect(local.tm_hour == 0, "localtime_s UTC epoch hour mismatch");

  char formatted[64]{};
  expect(std::strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S",
                       &local) != 0,
         "strftime local UTC epoch failed");
  expect(std::strcmp(formatted, "1970-01-01 00:00:00") == 0,
         "strftime local UTC epoch value mismatch");

  tm roundtrip = local;
  expect(std::mktime(&roundtrip) == 0, "mktime UTC epoch failed");
  expect(roundtrip.tm_wday == local.tm_wday,
         "mktime normalized weekday mismatch");
  expect(std::difftime(60, 10) == 50.0, "difftime result mismatch");
}

void verify_positive_timezone_offset() {
  timezone_guard guard;

  expect(_putenv_s("TZ", "KST-9") == 0, "_putenv_s KST TZ failed");
  _tzset();

  long timezone_seconds{};
  expect(_get_timezone(&timezone_seconds) == 0,
         "_get_timezone KST failed");
  expect(timezone_seconds == -9 * 60 * 60,
         "_get_timezone KST value mismatch");

  int daylight{};
  expect(_get_daylight(&daylight) == 0, "_get_daylight KST failed");
  expect(daylight == 0, "_get_daylight KST value mismatch");

  char timezone_name[32]{};
  size_t timezone_name_length{};
  expect(_get_tzname(&timezone_name_length, timezone_name,
                     sizeof(timezone_name), 0) == 0,
         "_get_tzname KST standard name failed");
  expect(std::strcmp(timezone_name, "KST") == 0,
         "_get_tzname KST standard name mismatch");

  const __time64_t epoch{};
  tm local{};
  expect(localtime_s(&local, &epoch) == 0, "localtime_s KST epoch failed");
  expect(local.tm_year == 70, "localtime_s KST epoch year mismatch");
  expect(local.tm_mon == 0, "localtime_s KST epoch month mismatch");
  expect(local.tm_mday == 1, "localtime_s KST epoch day mismatch");
  expect(local.tm_hour == 9, "localtime_s KST epoch hour mismatch");

  char formatted[64]{};
  expect(std::strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S %Z",
                       &local) != 0,
         "strftime KST epoch failed");
  expect(std::strcmp(formatted, "1970-01-01 09:00:00 KST") == 0,
         "strftime KST epoch value mismatch");

  tm roundtrip = local;
  expect(std::mktime(&roundtrip) == 0, "mktime KST epoch failed");
  expect(roundtrip.tm_wday == local.tm_wday,
         "mktime KST normalized weekday mismatch");
}
} // namespace

void run() {
  verify_current_time();
  verify_utc_formatting();
  verify_tzset_localtime();
  verify_positive_timezone_offset();
  std::cout << "CRT time semantic assertions passed\n";
}
} // namespace crt_time_semantic_test
