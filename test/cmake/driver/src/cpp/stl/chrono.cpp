//
// https://en.cppreference.com/w/cpp/chrono#Example
//
#include <cassert>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
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
// https://en.cppreference.com/w/cpp/chrono/system_clock/now
// https://en.cppreference.com/w/cpp/chrono/system_clock/to_time_t
// https://en.cppreference.com/w/cpp/chrono/system_clock/from_time_t
//
namespace chrono_system_clock_time_t_test {
void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <class Duration> Duration abs_duration(Duration value) {
  return value < Duration::zero() ? -value : value;
}

void run() {
  using namespace std::chrono;

  const sys_seconds fixed = sys_days{1970y / January / 2} + 3h + 4min + 5s;
  const std::time_t fixed_time_t = system_clock::to_time_t(fixed);
  const auto fixed_round_trip =
      floor<seconds>(system_clock::from_time_t(fixed_time_t));
  std::cout << "fixed system_clock time_t: " << fixed_time_t << '\n';
  expect(fixed_round_trip == fixed, "system_clock fixed time_t round-trip failed");

  const auto now = system_clock::now();
  const std::time_t now_time_t = system_clock::to_time_t(now);
  const auto now_round_trip = system_clock::from_time_t(now_time_t);
  const auto diff = abs_duration(now - now_round_trip);
  std::cout << "current system_clock time_t: " << now_time_t << '\n';
  expect(diff < 2s, "system_clock current time_t conversion drifted too far");
}
} // namespace chrono_system_clock_time_t_test

//
// https://en.cppreference.com/w/cpp/chrono/file_clock/now
// https://en.cppreference.com/w/cpp/chrono/clock_cast
//
namespace chrono_file_clock_now_test {
void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <class Duration> Duration abs_duration(Duration value) {
  return value < Duration::zero() ? -value : value;
}

void run() {
  using namespace std::chrono;

  // cppreference's file_clock::now example allocates benchmark vectors. This
  // driver harness keeps the file_clock::now path and compares it with the
  // adjacent system_clock instant through clock_cast.
  const auto sys_before = system_clock::now();
  const auto file_now = file_clock::now();
  const auto sys_after = system_clock::now();
  const auto sys_from_file = clock_cast<system_clock>(file_now);

  std::cout << "file_clock now as system_clock: "
            << floor<milliseconds>(sys_from_file).time_since_epoch().count()
            << "ms\n";

  expect(sys_from_file >= sys_before - 1s,
         "file_clock::now converted before system_clock lower bound");
  expect(sys_from_file <= sys_after + 1s,
         "file_clock::now converted after system_clock upper bound");
  expect(abs_duration(sys_after - sys_before) < 5s,
         "system_clock measurement window is unexpectedly large");
}
} // namespace chrono_file_clock_now_test

//
// https://en.cppreference.com/w/cpp/chrono/time_zone/to_local
// https://en.cppreference.com/w/cpp/chrono/time_zone/to_sys
//
namespace chrono_time_zone_conversion_test {
void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void expect_conversion(const char *name, std::chrono::seconds offset) {
  using namespace std::chrono;

  const auto *zone = locate_zone(name);
  const sys_seconds sys = sys_days{2024y / January / 1} + 12h;
  const auto local = zone->to_local(sys);
  const auto local_seconds = floor<seconds>(local);

  std::cout << zone->name() << " local seconds "
            << local_seconds.time_since_epoch().count() << '\n';

  expect(local_seconds.time_since_epoch() == sys.time_since_epoch() + offset,
         "time_zone::to_local returned unexpected fixed offset");
  expect(zone->to_sys(local_seconds) == sys,
         "time_zone::to_sys did not invert to_local");
}

void run() {
  expect_conversion("UTC", std::chrono::seconds{0});
  expect_conversion("Asia/Seoul", std::chrono::hours{9});
  expect_conversion("America/New_York", -std::chrono::hours{5});
  expect_conversion("Europe/Berlin", std::chrono::hours{1});
}
} // namespace chrono_time_zone_conversion_test

//
// https://en.cppreference.com/w/cpp/chrono/get_tzdb
// https://en.cppreference.com/w/cpp/chrono/get_tzdb_list
//
namespace chrono_tzdb_list_test {
void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void run() {
  const auto &tzdb = std::chrono::get_tzdb();
  const auto &tzdb_list = std::chrono::get_tzdb_list();
  const auto &front = tzdb_list.front();

  std::cout << "tzdb version: " << tzdb.version << '\n';

  expect(&front == &tzdb, "get_tzdb_list front does not match get_tzdb");
  expect(tzdb.locate_zone("UTC") != nullptr, "tzdb UTC lookup failed");
  expect(tzdb.locate_zone("Asia/Seoul") != nullptr,
         "tzdb Asia/Seoul lookup failed");
  expect(tzdb.locate_zone("America/New_York") != nullptr,
         "tzdb America/New_York lookup failed");
  expect(tzdb.locate_zone("Europe/Berlin") != nullptr,
         "tzdb Europe/Berlin lookup failed");
}
} // namespace chrono_tzdb_list_test

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

namespace chrono_file_timestamp_semantic_test {
void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <class Duration> Duration abs_duration(Duration value) {
  return value < Duration::zero() ? -value : value;
}

void run() {
  namespace fs = std::filesystem;
  using namespace std::chrono;

  const fs::path path =
      fs::temp_directory_path() / "crtsys_chrono_timestamp_semantic.bin";
  std::error_code ec;
  fs::remove(path, ec);

  std::ofstream(path) << "timestamp";
  expect(fs::exists(path), "timestamp semantic file was not created");

  const sys_seconds first_sys = sys_days{2024y / February / 3} + 4h + 5min + 6s;
  const auto first_file = clock_cast<file_clock>(first_sys);
  fs::last_write_time(path, first_file, ec);
  expect(!ec, "last_write_time set first timestamp failed");

  const auto read_first = fs::last_write_time(path, ec);
  expect(!ec, "last_write_time read first timestamp failed");
  const auto read_first_sys = floor<seconds>(clock_cast<system_clock>(read_first));
  std::cout << "first file timestamp sys seconds: "
            << read_first_sys.time_since_epoch().count() << '\n';
  expect(abs_duration(read_first_sys - first_sys) <= 1s,
         "first file timestamp round-trip drifted too far");

  const sys_seconds second_sys = first_sys + 2h + 30min;
  const auto second_file = clock_cast<file_clock>(second_sys);
  fs::last_write_time(path, second_file, ec);
  expect(!ec, "last_write_time set second timestamp failed");

  const auto read_second = fs::last_write_time(path, ec);
  expect(!ec, "last_write_time read second timestamp failed");
  const auto read_second_sys =
      floor<seconds>(clock_cast<system_clock>(read_second));
  std::cout << "second file timestamp sys seconds: "
            << read_second_sys.time_since_epoch().count() << '\n';
  expect(abs_duration(read_second_sys - second_sys) <= 1s,
         "second file timestamp round-trip drifted too far");
  expect(read_second > read_first, "updated file timestamp did not increase");

  const fs::path missing = path;
  fs::remove(path, ec);
  expect(!ec, "timestamp semantic file cleanup failed");

  ec.clear();
  const auto missing_time = fs::last_write_time(missing, ec);
  (void)missing_time;
  expect(static_cast<bool>(ec), "missing timestamp did not report error_code");

  bool threw = false;
  try {
    (void)fs::last_write_time(missing);
  } catch (const fs::filesystem_error &ex) {
    threw = true;
    expect(ex.code() == ec, "missing timestamp exception code mismatch");
    expect(ex.path1() == missing, "missing timestamp exception path mismatch");
    std::cout << "missing file timestamp rejected: " << ex.what() << '\n';
  }
  expect(threw, "missing timestamp throwing overload did not throw");

  std::cout << "chrono file timestamp semantic assertions passed\n";
}
} // namespace chrono_file_timestamp_semantic_test
