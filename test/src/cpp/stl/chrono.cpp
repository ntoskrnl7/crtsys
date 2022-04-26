#include "../../test.h"

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
  // std::cout << "f(42) = " << fibonacci(42) << '\n';
  printf("f(40) = %d\n", fibonacci(40));
  auto end = std::chrono::steady_clock::now();
  // std::chrono::duration<double> elapsed_seconds = end - start;
  // std::cout << "elapsed time: " << elapsed_seconds.count() << "s\n";
  printf("elapsed time: %dms\n",
         std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
             .count());
}
} // namespace chrono_test