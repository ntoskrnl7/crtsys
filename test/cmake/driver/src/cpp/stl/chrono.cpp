//
// https://en.cppreference.com/w/cpp/chrono#Example
//
#include <cassert>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
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
// https://en.cppreference.com/w/cpp/chrono/duration/floor#Example
//
namespace chrono_duration_rounding_test {
void run() {
  using namespace std::chrono_literals;

  std::cout << "Duration\t Floor\t Round\t Ceil\n";
  // cppreference declares this alias in the range-for init-statement. Some
  // MSVC toolsets used by the driver CI do not accept alias-declarations there,
  // so keep the same example body with the alias immediately before the loop.
  using Sec = std::chrono::seconds;
  for (auto const d : {+4999ms, +5000ms, +5001ms, +5499ms, +5500ms, +5999ms,
                       -4999ms, -5000ms, -5001ms, -5499ms, -5500ms, -5999ms}) {
    std::cout << std::showpos << d << "\t\t" << std::chrono::floor<Sec>(d)
              << '\t' << std::chrono::round<Sec>(d) << '\t'
              << std::chrono::ceil<Sec>(d) << '\n';
  }

  assert(std::chrono::floor<std::chrono::seconds>(+4999ms) == 4s);
  assert(std::chrono::round<std::chrono::seconds>(+5500ms) == 6s);
  assert(std::chrono::ceil<std::chrono::seconds>(-4999ms) == -4s);
  std::cout << std::noshowpos;
}
} // namespace chrono_duration_rounding_test

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
// The linked cppreference pages expose timezone lookup/query APIs rather than
// one standalone combined example, so this harness keeps the public API calls
// and verifies deterministic fixed-offset zones provided by the driver test.
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
// https://en.cppreference.com/w/cpp/chrono/file_clock
// https://en.cppreference.com/w/cpp/chrono/utc_clock
// https://en.cppreference.com/w/cpp/chrono/tai_clock
// https://en.cppreference.com/w/cpp/chrono/gps_clock
// https://en.cppreference.com/w/cpp/chrono/clock_cast
//
namespace chrono_clock_conversion_test {
// The linked cppreference clock pages document conversion APIs but do not have
// standalone Example sections. Exercise the documented conversion paths
// directly without formatting the time_points; file_time formatting is a
// separate chrono I/O path with tzdb/ICU dependencies.
void run() {
  using namespace std::chrono;

  const sys_seconds sys{sys_days{year{2024} / July / 2} + 12h + 34min + 56s};

  const auto utc = utc_clock::from_sys(sys);
  const auto sys_from_utc = utc_clock::to_sys(utc);
  assert(sys_from_utc == sys);

  const auto tai = tai_clock::from_utc(utc);
  const auto utc_from_tai = tai_clock::to_utc(tai);
  assert(utc_from_tai == utc);

  const auto gps = gps_clock::from_utc(utc);
  const auto utc_from_gps = gps_clock::to_utc(gps);
  assert(utc_from_gps == utc);

  // cppreference documents that file_clock provides either a UTC conversion
  // pair or a system_clock conversion pair. MSVC STL exposes from_utc/to_utc.
  const auto file = file_clock::from_utc(utc);
  const auto utc_from_file = file_clock::to_utc(file);
  assert(utc_from_file == utc);
  std::cout << "file_clock utc conversion ticks "
            << file.time_since_epoch().count() << '\n';

  const auto sys_via_tai = clock_cast<system_clock>(tai);
  assert(sys_via_tai == sys);
  const auto gps_via_clock_cast = clock_cast<gps_clock>(sys);
  assert(clock_cast<system_clock>(gps_via_clock_cast) == sys);

  std::cout << "utc ticks " << utc.time_since_epoch().count() << ", tai ticks "
            << tai.time_since_epoch().count() << ", gps ticks "
            << gps.time_since_epoch().count() << '\n';
}
} // namespace chrono_clock_conversion_test

//
// https://en.cppreference.com/w/cpp/chrono/year_month_weekday/accessors#Example
//
namespace chrono_calendar_test {
void run() {
  constexpr auto ym{std::chrono::year(2021) / std::chrono::January};
  constexpr auto wdi{std::chrono::Wednesday[1]};
  auto ymwdi{ym / wdi};
  const auto index{ymwdi.index() + 1};
  auto weekday{ymwdi.weekday() + std::chrono::days(1)};
  ymwdi = {ymwdi.year() / ymwdi.month() / weekday[index]};
  // Second Thursday in January, 2021
  assert(std::chrono::year_month_day{ymwdi} ==
         std::chrono::January / 14 / 2021);

  const auto ymd = std::chrono::year{2024} / std::chrono::July / 2;
  assert(ymd.ok());
  std::cout << ymd << '\n';
}
} // namespace chrono_calendar_test

//
// https://en.cppreference.com/w/cpp/chrono/hh_mm_ss
//
namespace chrono_hh_mm_ss_test {
void run() {
  using namespace std::chrono_literals;

  std::chrono::hh_mm_ss time_of_day{14h + 12min + 34s + 56ms};
  assert(time_of_day.hours() == 14h);
  assert(time_of_day.minutes() == 12min);
  assert(time_of_day.seconds() == 34s);
  assert(time_of_day.subseconds() == 56ms);
  std::cout << time_of_day << '\n';
}
} // namespace chrono_hh_mm_ss_test

//
// https://en.cppreference.com/w/cpp/chrono/parse
//
namespace chrono_parse_test {
void run() {
  using namespace std::chrono;

  // cppreference's chrono parse page documents the stream manipulator family
  // rather than one fixed standalone example. Keep the documented parse()
  // path on a timezone-free calendar type so this test does not depend on
  // hosted timezone data.
  std::istringstream input{"2024-07-02"};
  year_month_day ymd;
  input >> parse("%F", ymd);

  assert(!input.fail());
  assert(ymd == year{2024} / July / 2);
  std::cout << ymd << '\n';
}
} // namespace chrono_parse_test
