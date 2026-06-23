//
// https://en.cppreference.com/w/cpp/chrono#Example
//
#include <chrono>
#include <iostream>
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
