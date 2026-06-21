//
// https://en.cppreference.com/w/cpp/error/exception_ptr#Example
//
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

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
