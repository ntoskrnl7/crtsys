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
// https://en.cppreference.com/w/cpp/chrono/get_tzdb
// https://en.cppreference.com/w/cpp/chrono/locate_zone
//
namespace chrono_time_zone_error_test {
void run() {
  const auto &tzdb = std::chrono::get_tzdb();
  const auto *utc = tzdb.locate_zone("UTC");
  if (utc == nullptr) {
    throw std::runtime_error("std::chrono::get_tzdb UTC lookup failed");
  }
  const auto utc_info =
      utc->get_info(std::chrono::system_clock::time_point{});
  if (utc_info.offset != std::chrono::seconds{0}) {
    throw std::runtime_error("std::chrono::get_tzdb UTC offset failed");
  }

  bool invalid_zone_threw = false;
  try {
    (void)std::chrono::locate_zone("Etc/Definitely_Not_A_Time_Zone");
  } catch (const std::runtime_error &ex) {
    invalid_zone_threw = true;
    std::cout << "invalid timezone rejected: " << ex.what() << '\n';
  }

  if (!invalid_zone_threw) {
    throw std::runtime_error("std::chrono::locate_zone accepted invalid zone");
  }
}
} // namespace chrono_time_zone_error_test

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

//
// https://en.cppreference.com/w/cpp/chrono/clock_cast
// https://en.cppreference.com/w/cpp/chrono/utc_clock/from_sys
// https://en.cppreference.com/w/cpp/chrono/utc_clock/to_sys
// https://en.cppreference.com/w/cpp/chrono/tai_clock/from_utc
// https://en.cppreference.com/w/cpp/chrono/tai_clock/to_utc
// https://en.cppreference.com/w/cpp/chrono/gps_clock/from_utc
// https://en.cppreference.com/w/cpp/chrono/gps_clock/to_utc
// https://en.cppreference.com/w/cpp/chrono/file_clock
//
namespace chrono_clock_conversion_test {
void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void run() {
  using namespace std::chrono;

  // cppreference has no clock_cast example. The clock now() examples allocate
  // large benchmark vectors, so this driver test uses the conversion equations
  // from the linked cppreference pages with one fixed instant.
  const sys_seconds sys = sys_days{2018y / January / 1};
  const utc_seconds utc = utc_clock::from_sys(sys);
  const tai_seconds tai = tai_clock::from_utc(utc);
  const gps_seconds gps = gps_clock::from_utc(utc);
  const file_time<seconds> file = clock_cast<file_clock>(sys);

  std::cout << "sys seconds: " << sys.time_since_epoch().count() << '\n'
            << "utc seconds: " << utc.time_since_epoch().count() << '\n'
            << "tai seconds: " << tai.time_since_epoch().count() << '\n'
            << "gps seconds: " << gps.time_since_epoch().count() << '\n'
            << "file seconds: " << file.time_since_epoch().count() << '\n';

  expect(utc_clock::to_sys(utc) == sys, "utc_clock round-trip failed");
  expect(tai_clock::to_utc(tai) == utc, "tai_clock round-trip failed");
  expect(gps_clock::to_utc(gps) == utc, "gps_clock round-trip failed");

  expect(clock_cast<utc_clock>(sys) == utc, "system_clock to utc_clock failed");
  expect(clock_cast<system_clock>(utc) == sys,
         "utc_clock to system_clock failed");
  expect(clock_cast<tai_clock>(utc) == tai, "utc_clock to tai_clock failed");
  expect(clock_cast<utc_clock>(tai) == utc, "tai_clock to utc_clock failed");
  expect(clock_cast<gps_clock>(utc) == gps, "utc_clock to gps_clock failed");
  expect(clock_cast<utc_clock>(gps) == utc, "gps_clock to utc_clock failed");
  expect(clock_cast<system_clock>(file) == sys,
         "file_clock to system_clock failed");
}
} // namespace chrono_clock_conversion_test
