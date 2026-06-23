//
// https://en.cppreference.com/w/cpp/language/constant_initialization#Example
//
#include <array>
#include <iostream>

namespace constant_initialization_test {
struct S {
  static const int c;
};
const int d = 10 * S::c; // not a constant expression: S::c has no preceding
                         // initializer, this initialization happens after const
const int S::c = 5;      // constant initialization, guaranteed to happen first
void run() {
  std::cout << "d = " << d << '\n';
  std::array<int, S::c> a1; // OK: S::c is a constant expression
  //  std::array<int, d> a2;    // error: d is not a constant expression
  a1;
}
} // namespace constant_initialization_test

//
// https://en.cppreference.com/w/cpp/language/zero_initialization#Example
//
#include <iostream>
#include <string>

namespace zero_initialization_test {
struct A {
  int a, b, c;
};

double f[3]; // zero-initialized to three 0.0's

int *p; // zero-initialized to null pointer value
        // (even if the value is not integral 0)

std::string
    s; // zero-initialized to indeterminate value, then
       // default-initialized to "" by the std::string default constructor

void run(int argc, char *[]) {
  delete p; // safe to delete a null pointer

  static int n = argc; // zero-initialized to 0 then copy-initialized to argc
  std::cout << "n = " << n << '\n';

  A a = A(); // the effect is same as: A a{}; or A a = {};
  std::cout << "a = {" << a.a << ' ' << a.b << ' ' << a.c << "}\n";
}
} // namespace zero_initialization_test

//
// https://en.cppreference.com/w/cpp/language/initialization#Dynamic_initialization
//
// (no cppreference example.)
namespace dynamic_initialization_test {
struct test_object {
  int n;
  test_object(int n = 0) : n(n) {
    std::cout << "test_object(" << n << ") constructed successfully\n";
  }
  ~test_object() { std::cout << "test_object(" << n << ") destroyed\n"; }
};

test_object global_object_(1);
static test_object static_object_(2);
} // namespace dynamic_initialization_test

//
// https://en.cppreference.com/w/cpp/language/storage_duration#Example
//
#include <mutex>
#include <thread>

namespace storage_duration_example_test {
// General C++ thread_local is intentionally not covered by the default driver
// run. The kernel GS/FS base does not provide user-mode TEB TLS semantics here,
// so this cppreference example is only useful as an unsupported diagnostic.
thread_local unsigned int rage = 1;
std::mutex cout_mutex;

void increase_rage(const std::string &thread_name) {
  ++rage; // modifying outside a lock is okay; this is a thread-local variable
  std::lock_guard<std::mutex> lock(cout_mutex);
  std::cout << "Rage counter for " << thread_name << ": " << rage << '\n';
}

void run() {
  std::thread a(increase_rage, "a"), b(increase_rage, "b");

  {
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << "Rage counter for main: " << rage << '\n';
  }

  a.join();
  b.join();
}
} // namespace storage_duration_example_test

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>
namespace static_local_initialization_test {
std::atomic<int> constructed_count;
std::atomic<int> concurrent_constructed_count;
std::atomic<int> retry_constructed_count;

struct test_object {
  test_object() : value(42) { ++constructed_count; }

  int value;
};

test_object &instance() {
  static test_object object;
  return object;
}

void run() {
  test_object &first = instance();
  test_object &second = instance();

  if (&first != &second || first.value != 42 || constructed_count.load() != 1) {
    std::cerr << "function-local static initialization failed\n";
  } else {
    std::cout << "function-local static initialized successfully\n";
  }
}
} // namespace static_local_initialization_test

//
// Static block variable behavior regression checks.
//
// cppreference documents the guarantees:
// - initialization is retried if it exits by throwing an exception,
// - concurrent initialization of the same local static happens exactly once.
//
// These tests are crtsys-specific checks for MSVC compiler-generated
// _Init_thread_* paths, not copied cppreference examples.
//
namespace static_local_regression_test {
std::atomic<int> concurrent_constructed_count;
std::atomic<int> retry_constructed_count;

struct concurrent_test_object {
  concurrent_test_object() : value(84) {
    ++concurrent_constructed_count;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  int value;
};

concurrent_test_object &concurrent_instance() {
  static concurrent_test_object object;
  return object;
}

struct retry_test_object {
  retry_test_object() : value(126) {
    if (++retry_constructed_count == 1) {
      throw std::runtime_error("retry function-local static initialization");
    }
  }

  int value;
};

retry_test_object &retry_instance() {
  static retry_test_object object;
  return object;
}

void run_concurrent() {
  std::atomic<int> failed_count{0};
  std::vector<std::thread> threads;
  threads.reserve(8);

  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([&failed_count] {
      concurrent_test_object &object = concurrent_instance();
      if (object.value != 84) {
        ++failed_count;
      }
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  if (failed_count.load() != 0 || concurrent_constructed_count.load() != 1) {
    std::cerr << "function-local static concurrent initialization failed\n";
  } else {
    std::cout
        << "function-local static concurrent initialization succeeded\n";
  }
}

void run_exception_retry() {
  bool first_call_threw = false;
  bool second_call_succeeded = false;

  try {
    retry_instance();
  } catch (const std::runtime_error &) {
    first_call_threw = true;
  }

  try {
    retry_test_object &object = retry_instance();
    second_call_succeeded = object.value == 126;
  } catch (...) {
  }

  if (!first_call_threw || !second_call_succeeded ||
      retry_constructed_count.load() != 2) {
    std::cerr << "function-local static exception retry failed\n";
  } else {
    std::cout << "function-local static exception retry succeeded\n";
  }
}

void run() {
  run_concurrent();
  run_exception_retry();
}
} // namespace static_local_regression_test
