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
}

void verify_tzset_localtime() {
  timezone_guard guard;

  expect(_putenv_s("TZ", "UTC0") == 0, "_putenv_s TZ failed");
  _tzset();

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
}
} // namespace

void run() {
  verify_current_time();
  verify_utc_formatting();
  verify_tzset_localtime();
  std::cout << "CRT time semantic assertions passed\n";
}
} // namespace crt_time_semantic_test
