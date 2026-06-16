#include <gtest/gtest.h>

//
// https://en.cppreference.com/w/cpp/chrono#Example
//
#include <chrono>
#include <iostream>
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
// Google Test.
//
TEST(cxx_stl_test, chrono_test) {
  auto start = std::chrono::steady_clock::now();
  auto actual = chrono_test::fibonacci(42);
  EXPECT_EQ(actual, 267914296);
  std::cout << "f(42) = " << actual << '\n';
  auto end = std::chrono::steady_clock::now();
  std::chrono::duration<double> elapsed_seconds = end - start;
  std::cout << "elapsed time: " << elapsed_seconds.count() << "s\n";
}