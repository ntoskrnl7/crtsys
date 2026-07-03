//
// https://en.cppreference.com/w/cpp/chrono#Example
//
#include <cassert>
#include <chrono>
#include <iostream>
#if __has_include(<print>)
#include <print>
#endif
#include <stdexcept>

namespace chrono_test {
long fibonacci(unsigned n) {
  if (n < 2)
    return n;
  return fibonacci(n - 1) + fibonacci(n - 2);
}

void run() {
  auto start = std::chrono::steady_clock::now();
  std::cout << "f(42) = " << fibonacci(42) << '\n';
  auto end = std::chrono::steady_clock::now();
  std::chrono::duration<double> elapsed_seconds = end - start;
  std::cout << "elapsed time: " << elapsed_seconds.count() << "s\n";
}
} // namespace chrono_test

//
// https://en.cppreference.com/w/cpp/chrono/current_zone#Example
//
namespace chrono_current_zone_test {
void run() {
  try {
    const std::chrono::zoned_time cur_time{
        std::chrono::current_zone(), // may throw
        std::chrono::system_clock::now()};
    std::cout << cur_time << '\n';
  } catch (const std::runtime_error &ex) {
    std::cerr << ex.what() << '\n';
    throw;
  }
}
} // namespace chrono_current_zone_test

//
// https://en.cppreference.com/w/cpp/chrono/locate_zone
// https://en.cppreference.com/w/cpp/chrono/time_zone/get_info
//
namespace chrono_time_zone_info_test {
void expect_offset(const char *name, std::chrono::seconds expected) {
  const auto *zone = std::chrono::locate_zone(name);
  const auto info =
      zone->get_info(std::chrono::system_clock::time_point{});

  std::cout << zone->name() << " offset " << info.offset.count()
            << "s, abbrev " << info.abbrev << '\n';
  if (info.offset != expected) {
    throw std::runtime_error("unexpected chrono timezone offset");
  }
}

void run() {
  expect_offset("UTC", std::chrono::seconds{0});
  expect_offset("Asia/Seoul", std::chrono::hours{9});
  expect_offset("America/New_York", -std::chrono::hours{5});
  expect_offset("Europe/Berlin", std::chrono::hours{1});
}
} // namespace chrono_time_zone_info_test

//
// https://en.cppreference.com/w/cpp/chrono/clock_cast
//
namespace chrono_clock_conversion_test {
void run() {
#if defined(_MSC_VER) && _MSC_VER >= 1930
  // cppreference currently marks the clock_cast Example section as
  // incomplete/no-example. Keep this direct coverage aligned with the page's
  // documented conversion role instead of inventing an output-oriented sample.
  const auto sys = std::chrono::sys_days{
                       std::chrono::year{2020} / std::chrono::January / 1} +
                   std::chrono::hours{12} + std::chrono::minutes{34} +
                   std::chrono::seconds{56};

  const auto utc = std::chrono::clock_cast<std::chrono::utc_clock>(sys);
  const auto tai = std::chrono::clock_cast<std::chrono::tai_clock>(utc);
  const auto gps = std::chrono::clock_cast<std::chrono::gps_clock>(utc);
  const auto file = std::chrono::clock_cast<std::chrono::file_clock>(sys);

  assert(std::chrono::clock_cast<std::chrono::system_clock>(utc) == sys);
  assert(std::chrono::clock_cast<std::chrono::utc_clock>(tai) == utc);
  assert(std::chrono::clock_cast<std::chrono::utc_clock>(gps) == utc);
  assert(std::chrono::clock_cast<std::chrono::system_clock>(file) == sys);

  // Release builds compile out assert(), so mark assert-only locals as used.
  (void)sys;
  (void)utc;
  (void)tai;
  (void)gps;
  (void)file;
  std::cout << "chrono clock_cast conversions exercised\n";
#else
  std::cout << "std::chrono::clock_cast is not available in this MSVC STL\n";
#endif
}
} // namespace chrono_clock_conversion_test

//
// https://en.cppreference.com/w/cpp/chrono/year_month_day#Example
//
namespace chrono_year_month_day_test {
void run() {
  const std::chrono::time_point now{std::chrono::system_clock::now()};

  const std::chrono::year_month_day ymd{
      std::chrono::floor<std::chrono::days>(now)};
  std::cout << "Current Year: " << static_cast<int>(ymd.year()) << ", "
            << "Month: " << static_cast<unsigned>(ymd.month()) << ", "
            << "Day: " << static_cast<unsigned>(ymd.day()) << "\n"
            << "ymd: " << ymd << '\n';
}
} // namespace chrono_year_month_day_test

//
// https://en.cppreference.com/w/cpp/chrono/weekday#Example
//
namespace chrono_weekday_test {
void run() {
  std::chrono::weekday x{42 / 13};
  std::cout << x++ << '\n';
  std::cout << x << '\n';
  std::cout << ++x << '\n';
}
} // namespace chrono_weekday_test

//
// https://en.cppreference.com/w/cpp/chrono/hh_mm_ss#Example
//
namespace chrono_hh_mm_ss_test {
void run() {
#if defined(__cpp_lib_print)
  std::println("Default constructor: {}",
               std::chrono::hh_mm_ss<std::chrono::minutes>{});

  std::chrono::time_point now = std::chrono::system_clock::now();
  std::chrono::hh_mm_ss time_of_day{
      now - std::chrono::floor<std::chrono::days>(now)};
  std::println("The time of day is: {}", time_of_day);
#else
  // cppreference uses std::println. Older MSVC STLs in the driver build can
  // expose std::chrono::hh_mm_ss before exposing <print>, so keep the chrono
  // object construction intact and stream the same values.
  std::cout << "Default constructor: "
            << std::chrono::hh_mm_ss<std::chrono::minutes>{} << '\n';

  std::chrono::time_point now = std::chrono::system_clock::now();
  std::chrono::hh_mm_ss time_of_day{
      now - std::chrono::floor<std::chrono::days>(now)};
  std::cout << "The time of day is: " << time_of_day << '\n';
#endif
}
} // namespace chrono_hh_mm_ss_test
