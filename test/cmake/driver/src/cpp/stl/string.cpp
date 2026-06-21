//
// https://en.cppreference.com/w/cpp/string/basic_string#Example
//
#include <iostream>
#include <string>
#include <string_view>

namespace string_test {
void run() {
  using namespace std::literals;

  // Creating a string from const char*
  std::string str1 = "hello";

  // Creating a string using string literal
  auto str2 = "world"s;

  // Concatenating strings
  std::string str3 = str1 + " " + str2;

  // Print out the result
  std::cout << str3 << '\n';
}
} // namespace string_test

//
// https://en.cppreference.com/w/cpp/string/basic_string_view#Example
//
namespace string_view_test {
void run() {
#define A "\xE2\x96\x80"
#define B "\xE2\x96\x84"
#define C "\xE2\x94\x80"

  constexpr std::string_view blocks[]{A B C, B A C, A C B, B C A};

  for (int y{}, p{}; y != 8; ++y, p = ((p + 1) % 4)) {
    for (char x{}; x != 29; ++x) {
      std::cout << blocks[p];
    }
    std::cout << '\n';
  }

#undef A
#undef B
#undef C
}
} // namespace string_view_test
