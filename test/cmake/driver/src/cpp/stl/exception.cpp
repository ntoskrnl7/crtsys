//
// https://en.cppreference.com/w/cpp/error/exception_ptr#Example
//
#include <cassert>
#include <cerrno>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace exception_ptr_test {
void handle_eptr(std::exception_ptr eptr) // passing by value is OK
{
  try {
    if (eptr) {
      std::rethrow_exception(eptr);
    }
  } catch (const std::exception &e) {
    std::cout << "Caught exception: '" << e.what() << "'\n";
  }
}

void run() {
  std::exception_ptr eptr;

  try {
    [[maybe_unused]] char ch = std::string().at(1); // this generates a std::out_of_range
  } catch (...) {
    eptr = std::current_exception(); // capture
  }

  handle_eptr(eptr);

} // destructor for std::out_of_range called here, when the eptr is destructed
} // namespace exception_ptr_test

//
// https://en.cppreference.com/w/cpp/error/uncaught_exception#Example
//
namespace uncaught_exceptions_test {
struct Foo {
  char id{'?'};
  int count = std::uncaught_exceptions();

  ~Foo() {
    count == std::uncaught_exceptions()
        ? std::cout << id << ".~Foo() called normally\n"
        : std::cout << id << ".~Foo() called during stack unwinding\n";
  }
};

void run() {
  Foo f{'f'};
  try {
    Foo g{'g'};
    std::cout << "Exception thrown\n";
    throw std::runtime_error("test exception");
  } catch (const std::exception &e) {
    std::cout << "Exception caught: " << e.what() << '\n';
  }
}
} // namespace uncaught_exceptions_test

//
// https://en.cppreference.com/w/cpp/error/nested_exception#Example
// https://en.cppreference.com/w/cpp/error/throw_with_nested#Example
// https://en.cppreference.com/w/cpp/error/rethrow_if_nested#Example
//
namespace nested_exception_test {
void print_exception(const std::exception &e, int level = 0) {
  std::cout << std::string(level, ' ') << "exception: " << e.what() << '\n';
  try {
    std::rethrow_if_nested(e);
  } catch (const std::exception &nested) {
    print_exception(nested, level + 1);
  } catch (...) {
  }
}

void open_file() { throw std::system_error(ENOENT, std::generic_category()); }

void run() {
  try {
    try {
      open_file();
    } catch (...) {
      std::throw_with_nested(std::runtime_error("could not open file"));
    }
  } catch (const std::exception &e) {
    print_exception(e);
  }
}
} // namespace nested_exception_test

//
// https://en.cppreference.com/w/cpp/error/error_code#Example
// https://en.cppreference.com/w/cpp/error/error_condition
//
namespace error_code_test {
void run() {
  // Keep this as assertion coverage for the cppreference error_code /
  // error_condition relationship instead of relying on localized message text.
  std::error_code ec = std::make_error_code(std::errc::permission_denied);
  std::error_condition condition =
      std::make_error_condition(std::errc::permission_denied);

  assert(ec == condition);
  assert(ec == std::errc::permission_denied);
  assert(ec.category() == std::generic_category());

  std::cout << "error_code: " << ec.value() << ", " << ec.message() << '\n';
  std::cout << "error_condition: " << condition.value() << ", "
            << condition.message() << '\n';
}
} // namespace error_code_test

//
// https://en.cppreference.com/w/cpp/error/system_error#Example
//
namespace system_error_test {
void run() {
  try {
    // cppreference demonstrates attaching a human-readable operation string.
    // Use a kernel-driver-flavored string; the tested system_error code path is
    // still the same.
    throw std::system_error(std::make_error_code(std::errc::permission_denied),
                            "kernel driver operation");
  } catch (const std::system_error &e) {
    std::cout << "system_error code: " << e.code() << '\n';
    std::cout << "system_error what: " << e.what() << '\n';
    assert(e.code() == std::errc::permission_denied);
  }
}
} // namespace system_error_test
