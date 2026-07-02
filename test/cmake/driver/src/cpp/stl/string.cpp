//
// https://en.cppreference.com/w/cpp/string/basic_string#Example
//
#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace string_test_detail {
void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
} // namespace string_test_detail

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
// https://en.cppreference.com/w/cpp/string/basic_string/find#Example
// https://en.cppreference.com/w/cpp/string/basic_string/substr#Example
// https://en.cppreference.com/w/cpp/string/basic_string/starts_with#Example
// https://en.cppreference.com/w/cpp/string/basic_string/ends_with#Example
// https://en.cppreference.com/w/cpp/string/basic_string/contains#Example
// https://en.cppreference.com/w/cpp/string/basic_string/erase#Example
//
namespace string_member_operations_test {
void run() {
  using namespace std::literals;

  // The linked cppreference pages are output/assert examples. This driver
  // harness checks the same observable values with runtime checks so Release
  // builds, where assert can be compiled out, still verify the semantics.
  std::string::size_type n;
  const std::string s = "This is a string";

  n = s.find("is");
  string_test_detail::expect(n == 2, "string find from beginning mismatch");

  n = s.find("is", 5);
  string_test_detail::expect(n == 5, "string find from position mismatch");

  n = s.find('a');
  string_test_detail::expect(n == 8, "string find character mismatch");
  string_test_detail::expect(s.find("missing") == std::string::npos,
                             "string find npos mismatch");

  std::string a = "0123456789abcdefghij";
  string_test_detail::expect(a.substr(10) == "abcdefghij",
                             "string substr suffix mismatch");
  string_test_detail::expect(a.substr(5, 3) == "567",
                             "string substr bounded mismatch");
  string_test_detail::expect(a.substr(a.size() - 3, 50) == "hij",
                             "string substr clamped mismatch");

#if defined(__cpp_lib_starts_ends_with) && __cpp_lib_starts_ends_with >= 201711L
  const auto str = "Hello, C++20!"s;
  string_test_detail::expect(str.starts_with("He"sv),
                             "string starts_with string_view mismatch");
  string_test_detail::expect(!str.starts_with("he"sv),
                             "string starts_with negative string_view mismatch");
  string_test_detail::expect(str.starts_with("He"s),
                             "string starts_with string mismatch");
  string_test_detail::expect(!str.starts_with("he"s),
                             "string starts_with negative string mismatch");
  string_test_detail::expect(str.starts_with('H'),
                             "string starts_with char mismatch");
  string_test_detail::expect(!str.starts_with('h'),
                             "string starts_with negative char mismatch");
  string_test_detail::expect(str.starts_with("He"),
                             "string starts_with c-string mismatch");
  string_test_detail::expect(!str.starts_with("he"),
                             "string starts_with negative c-string mismatch");

  string_test_detail::expect(str.ends_with("C++20!"sv),
                             "string ends_with string_view mismatch");
  string_test_detail::expect(!str.ends_with("c++20!"sv),
                             "string ends_with negative string_view mismatch");
  string_test_detail::expect(str.ends_with("C++20!"s),
                             "string ends_with string mismatch");
  string_test_detail::expect(!str.ends_with("c++20!"s),
                             "string ends_with negative string mismatch");
  string_test_detail::expect(str.ends_with('!'), "string ends_with char mismatch");
  string_test_detail::expect(!str.ends_with('?'),
                             "string ends_with negative char mismatch");
  string_test_detail::expect(str.ends_with("C++20!"),
                             "string ends_with c-string mismatch");
  string_test_detail::expect(!str.ends_with("c++20!"),
                             "string ends_with negative c-string mismatch");
#endif

#if defined(__cpp_lib_string_contains) && __cpp_lib_string_contains >= 202011L
  auto helloWorld = "hello world"s;
  string_test_detail::expect(helloWorld.contains("hello"sv),
                             "string contains string_view mismatch");
  string_test_detail::expect(!helloWorld.contains("goodbye"sv),
                             "string contains negative string_view mismatch");
  string_test_detail::expect(helloWorld.contains('w'),
                             "string contains char mismatch");
  string_test_detail::expect(!helloWorld.contains('x'),
                             "string contains negative char mismatch");
#endif

  std::string erased = "This Is An Example";
  erased.erase(7, 3);
  string_test_detail::expect(erased == "This Is Example",
                             "string erase substring mismatch");

  erased.erase(std::find(erased.begin(), erased.end(), ' '));
  string_test_detail::expect(erased == "ThisIs Example",
                             "string erase iterator mismatch");

  erased.erase(erased.find(' '));
  string_test_detail::expect(erased == "ThisIs",
                             "string erase tail mismatch");

  auto it = std::next(erased.begin(), erased.find('s'));
  erased.erase(it, std::next(it, 2));
  string_test_detail::expect(erased == "This",
                             "string erase iterator range mismatch");
}
} // namespace string_member_operations_test

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

//
// https://en.cppreference.com/w/cpp/string/basic_string_view/find#Example
// https://en.cppreference.com/w/cpp/string/basic_string_view/substr#Example
// https://en.cppreference.com/w/cpp/string/basic_string_view/starts_with#Example
// https://en.cppreference.com/w/cpp/string/basic_string_view/ends_with#Example
// https://en.cppreference.com/w/cpp/string/basic_string_view/contains#Example
//
namespace string_view_member_operations_test {
void run() {
  using namespace std::literals;

  constexpr auto str{" long long int;"sv};
  static_assert(1 == str.find("long"sv));
  static_assert(6 == str.find("long"sv, 2));
  static_assert(0 == str.find(' '));
  static_assert(2 == str.find('o', 1));
  static_assert(2 == str.find("on"));
  static_assert(6 == str.find("long double", 5, 4));
  static_assert(str.npos == str.find("float"));

  typedef std::size_t count_t, pos_t;
  constexpr std::string_view data{"ABCDEF"};
  string_test_detail::expect(data.substr() == "ABCDEF"sv,
                             "string_view substr full mismatch");
  string_test_detail::expect(data.substr(pos_t(1)) == "BCDEF"sv,
                             "string_view substr suffix mismatch");
  string_test_detail::expect(data.substr(pos_t(2), count_t(3)) == "CDE"sv,
                             "string_view substr bounded mismatch");
  string_test_detail::expect(data.substr(pos_t(4), count_t(42)) == "EF"sv,
                             "string_view substr clamped mismatch");

  try {
    [[maybe_unused]] auto sub = data.substr(pos_t(666), count_t(1));
    string_test_detail::expect(false,
                               "string_view substr out_of_range not thrown");
  } catch (std::out_of_range const &) {
  }

#if defined(__cpp_lib_starts_ends_with) && __cpp_lib_starts_ends_with >= 201711L
  string_test_detail::expect("https://cppreference.com"sv.starts_with("http"sv),
                             "string_view starts_with string_view mismatch");
  string_test_detail::expect(!"https://cppreference.com"sv.starts_with("ftp"sv),
                             "string_view starts_with negative string_view mismatch");
  string_test_detail::expect("C++20"sv.starts_with('C'),
                             "string_view starts_with char mismatch");
  string_test_detail::expect(!"C++20"sv.starts_with('J'),
                             "string_view starts_with negative char mismatch");
  string_test_detail::expect(
      std::string_view("string_view").starts_with("string"),
      "string_view starts_with c-string mismatch");
  string_test_detail::expect(
      !std::string_view("string_view").starts_with("String"),
      "string_view starts_with negative c-string mismatch");

  string_test_detail::expect(
      std::string_view("https://cppreference.com").ends_with(".com"sv),
      "string_view ends_with string_view mismatch");
  string_test_detail::expect(
      !std::string_view("https://cppreference.com").ends_with(".org"sv),
      "string_view ends_with negative string_view mismatch");
  string_test_detail::expect(std::string_view("C++20").ends_with('0'),
                             "string_view ends_with char mismatch");
  string_test_detail::expect(!std::string_view("C++20").ends_with('3'),
                             "string_view ends_with negative char mismatch");
  string_test_detail::expect(std::string_view("string_view").ends_with("view"),
                             "string_view ends_with c-string mismatch");
  string_test_detail::expect(!std::string_view("string_view").ends_with("View"),
                             "string_view ends_with negative c-string mismatch");
#endif

#if defined(__cpp_lib_string_contains) && __cpp_lib_string_contains >= 202011L
  static_assert("https://cppreference.com"sv.contains("cpp"sv));
  static_assert(!"https://cppreference.com"sv.contains("php"sv));
  static_assert("C++23"sv.contains('+'));
  static_assert(!"C++23"sv.contains('-'));
  static_assert(std::string_view("basic_string_view").contains("string"));
  static_assert(!std::string_view("basic_string_view").contains("String"));
#endif
}
} // namespace string_view_member_operations_test
