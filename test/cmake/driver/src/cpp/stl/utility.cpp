//
// https://en.cppreference.com/w/cpp/utility/optional#Example
//
#include <algorithm>
#include <any>
#include <cassert>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#if __has_include(<expected>)
#include <expected>
#endif
#if __has_include(<format>)
#include <format>
#endif
#if __has_include(<print>)
#include <print>
#endif
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <list>
#include <numeric>
#include <numbers>
#include <optional>
#include <random>
#include <ranges>
#include <ratio>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace optional_test {
// optional can be used as the return type of a factory that may fail
std::optional<std::string> create(bool b) {
  if (b) {
    return "Godzilla";
  }
  return {};
}

// std::nullopt can be used to create any (empty) std::optional
auto create2(bool b) {
  return b ? std::optional<std::string>{"Godzilla"} : std::nullopt;
}

void run() {
  std::cout << "create(false) returned " << create(false).value_or("empty")
            << '\n';

  // optional-returning factory functions are usable as conditions of while and
  // if
  if (auto str = create2(true)) {
    std::cout << "create2(true) returned " << *str << '\n';
  }
}
} // namespace optional_test

//
// https://en.cppreference.com/w/cpp/utility/expected#Example
//
namespace expected_test {
#if defined(__cpp_lib_expected)
enum class parse_error { invalid_input, overflow };

auto parse_number(std::string_view &str) -> std::expected<double, parse_error> {
  const char *begin = str.data();
  char *end;
  double retval = std::strtod(begin, &end);
  if (begin == end) {
    return std::unexpected(parse_error::invalid_input);
  } else if (std::isinf(retval)) {
    return std::unexpected(parse_error::overflow);
  }

  str.remove_prefix(static_cast<std::size_t>(end - begin));
  return retval;
}

void run() {
  auto process = [](std::string_view str) {
    std::cout << "str: " << std::quoted(str) << ", ";
    if (const auto num = parse_number(str); num.has_value()) {
      std::cout << "value: " << *num << '\n';
      // If num did not have a value, dereferencing num would cause an
      // undefined behavior, and num.value() would throw
      // std::bad_expected_access. num.value_or(123) uses specified default
      // value 123.
    } else if (num.error() == parse_error::invalid_input) {
      std::cout << "error: invalid input\n";
    } else if (num.error() == parse_error::overflow) {
      std::cout << "error: overflow\n";
    } else {
      std::cout << "unexpected!\n";
    }
  };

  for (auto src : {"42", "42abc", "meow", "inf"}) {
    process(src);
  }
}
#else
void run() { std::cout << "std::expected is not available in this toolset\n"; }
#endif
} // namespace expected_test

//
// https://en.cppreference.com/w/cpp/utility/format/format#Example
//
namespace format_test {
template <typename... Args>
std::string dyna_print(std::string_view rt_fmt_str, Args &&...args) {
#if defined(__cpp_lib_format)
  return std::vformat(rt_fmt_str, std::make_format_args(args...));
#else
  rt_fmt_str;
  (args, ...);
  return {};
#endif
}

void run() {
#if defined(__cpp_lib_format)
  std::cout << std::format("Hello {}!\n", "world");
  std::string fmt;
  for (int i{}; i != 3; ++i) {
    fmt += "{} "; // constructs the formatting string
    std::cout << fmt << " : ";
    std::cout << dyna_print(fmt, "alpha", 'Z', 3.14, "unused");
    std::cout << '\n';
  }
#else
  std::cout << "std::format is not available in this MSVC STL\n";
#endif
}
} // namespace format_test

//
// https://en.cppreference.com/w/cpp/io/print#Example
//
namespace print_test {
void run() {
#if defined(__cpp_lib_print)
  std::print("{2} {1}{0}!\n", 23, "C++", "Hello");
#else
  std::cout << "std::print is not available in this MSVC STL\n";
#endif
}
} // namespace print_test

//
// https://en.cppreference.com/w/cpp/utility/tuple#Example
//
namespace tuple_test {
std::tuple<double, char, std::string> get_student(int id) {
  switch (id) {
  case 0:
    return {3.8, 'A', "Lisa Simpson"};
  case 1:
    return {2.9, 'C', "Milhouse Van Houten"};
  case 2:
    return {1.7, 'D', "Ralph Wiggum"};
  case 3:
    return {0.6, 'F', "Bart Simpson"};
  }

  throw std::invalid_argument("id");
}

void run() {
  const auto student0 = get_student(0);
  std::cout << "ID: 0, "
            << "GPA: " << std::get<0>(student0) << ", "
            << "grade: " << std::get<1>(student0) << ", "
            << "name: " << std::get<2>(student0) << '\n';

  const auto student1 = get_student(1);
  std::cout << "ID: 1, "
            << "GPA: " << std::get<double>(student1) << ", "
            << "grade: " << std::get<char>(student1) << ", "
            << "name: " << std::get<std::string>(student1) << '\n';

  double gpa2;
  char grade2;
  std::string name2;
  std::tie(gpa2, grade2, name2) = get_student(2);
  std::cout << "ID: 2, "
            << "GPA: " << gpa2 << ", "
            << "grade: " << grade2 << ", "
            << "name: " << name2 << '\n';

  // C++17 structured binding:
  const auto [gpa3, grade3, name3] = get_student(3);
  std::cout << "ID: 3, "
            << "GPA: " << gpa3 << ", "
            << "grade: " << grade3 << ", "
            << "name: " << name3 << '\n';
}
} // namespace tuple_test

//
// https://en.cppreference.com/w/cpp/utility/pair/make_pair#Example
//
namespace pair_test {
void run() {
  int n = 1;
  int a[5] = {1, 2, 3, 4, 5};

  // build a pair from two ints
  auto p1 = std::make_pair(n, a[1]);
  std::cout << "The value of p1 is " << '(' << p1.first << ", " << p1.second
            << ")\n";

  // build a pair from a reference to int and an array (decayed to pointer)
  auto p2 = std::make_pair(std::ref(n), a);
  n = 7;
  std::cout << "The value of p2 is " << '(' << p2.first << ", "
            << *(p2.second + 2) << ")\n";
}
} // namespace pair_test

//
// https://en.cppreference.com/w/cpp/utility/variant#Example
//
namespace variant_test {
void run() {
  std::variant<int, float> v, w;
  v = 42; // v contains int
  [[maybe_unused]] int i = std::get<int>(v);
  assert(42 == i); // succeeds
  w = std::get<int>(v);
  w = std::get<0>(v); // same effect as the previous line
  w = v;              // same effect as the previous line
  // std::get<double>(v); // error: no double in [int, float]
  // std::get<3>(v);      // error: valid index values are 0 and 1

  try {
    [[maybe_unused]] float f =
        std::get<float>(w); // w contains int, not float: will throw
  } catch (const std::bad_variant_access &ex) {
    std::cout << ex.what() << '\n';
  }

  using namespace std::literals;
  std::variant<std::string> x("abc");
  // converting constructors work when unambiguous
  x = "def"; // converting assignment also works when unambiguous

  std::variant<std::string, void const *> y("abc");
  // casts to void const* when passed a char const*
  assert(std::holds_alternative<void const *>(y)); // succeeds
  y = "xyz"s;
  assert(std::holds_alternative<std::string>(y)); // succeeds
}
} // namespace variant_test

//
// https://en.cppreference.com/w/cpp/utility/any#Example
//
namespace any_test {
void run() {
  std::cout << std::boolalpha;

  // any type
  std::any a = 1;
  std::cout << a.type().name() << ": " << std::any_cast<int>(a) << '\n';
  a = 3.14;
  std::cout << a.type().name() << ": " << std::any_cast<double>(a) << '\n';
  a = true;
  std::cout << a.type().name() << ": " << std::any_cast<bool>(a) << '\n';
  // bad cast
  try {
    a = 1;
    std::cout << std::any_cast<float>(a) << '\n';
  } catch (const std::bad_any_cast &e) {
    std::cout << e.what() << '\n';
  }

  // has value
  a = 2;
  if (a.has_value()) {
    std::cout << a.type().name() << ": " << std::any_cast<int>(a) << '\n';
  }

  // reset
  a.reset();
  if (!a.has_value()) {
    std::cout << "no value\n";
  }
  // pointer to contained data
  a = 3;
  int *i = std::any_cast<int>(&a);
  std::cout << *i << '\n';
}
} // namespace any_test

//
// https://en.cppreference.com/w/cpp/utility/source_location#Example
//
namespace source_location_test {
void log(const std::string_view message,
         const std::source_location location =
             std::source_location::current()) {
  std::clog << "file: " << location.file_name() << '(' << location.line() << ':'
            << location.column() << ") `" << location.function_name()
            << "`: " << message << '\n';
}

template <typename T> void fun(T x) { log(x); }

void run() {
  log("Hello world!");
  fun("Hello C++20!");
}
} // namespace source_location_test

//
// https://en.cppreference.com/w/cpp/utility/functional/reference_wrapper#Example
//
namespace reference_wrapper_test {
void println(auto const rem, std::ranges::range auto const &v) {
  for (std::cout << rem; auto const &e : v) {
    std::cout << e << ' ';
  }
  std::cout << '\n';
}

void run() {
  std::list<int> l(10);
  std::iota(l.begin(), l.end(), -4);

  // Cannot use shuffle on a list (requires random access), but can use it on a
  // vector.
  std::vector<std::reference_wrapper<int>> v(l.begin(), l.end());
  std::ranges::shuffle(v, std::mt19937{std::random_device{}()});

  // This file also includes <print> for the std::print example. Qualify the
  // cppreference helper so MSVC doesn't consider std::println during ADL.
  reference_wrapper_test::println("Contents of the list: ", l);
  reference_wrapper_test::println(
      "Contents of the list, as seen through a shuffled vector: ", v);

  std::cout << "Doubling the values in the initial list...\n";
  std::ranges::for_each(l, [](int &i) { i *= 2; });

  reference_wrapper_test::println(
      "Contents of the list, as seen through a shuffled vector: ", v);
}
} // namespace reference_wrapper_test

//
// https://en.cppreference.com/w/cpp/utility/functional/invoke#Example
//
namespace invoke_test {
struct Foo {
  Foo(int num) : num_(num) {}
  void print_add(int i) const { std::cout << num_ + i << '\n'; }
  int num_;
};

void print_num(int i) { std::cout << i << '\n'; }

struct PrintNum {
  void operator()(int i) const { std::cout << i << '\n'; }
};

void run() {
  // invoke a free function
  std::cout << "invoke a free function: ";
  std::invoke(print_num, -9);

  // invoke a lambda
  std::cout << "invoke a lambda: ";
  std::invoke([] { print_num(42); });

  // invoke a member function
  const Foo foo(314159);
  std::cout << "invoke a member function: ";
  std::invoke(&Foo::print_add, foo, 1);

  // invoke a data member
  std::cout << "invoke a data member: " << std::invoke(&Foo::num_, foo) << '\n';

  // invoke a function object
  std::cout << "invoke a function object: ";
  std::invoke(PrintNum(), 18);
}
} // namespace invoke_test

//
// https://en.cppreference.com/w/cpp/utility/exchange#Example
//
namespace exchange_test {
class stream {
public:
  using flags_type = int;

public:
  flags_type flags() const { return flags_; }

  // Replaces flags_ by newf, and returns the old value.
  flags_type flags(flags_type newf) { return std::exchange(flags_, newf); }

private:
  flags_type flags_ = 0;
};

void run() {
  stream s;

  std::cout << s.flags() << '\n';
  std::cout << s.flags(12) << '\n';
  std::cout << s.flags() << "\n\n";

  std::vector<int> v;

  // Since the second template parameter has a default value, it is possible to
  // use a braced-init-list as the second argument. The expression below is
  // equivalent to std::exchange(v, std::vector<int>{1, 2, 3, 4});
  std::exchange(v, {1, 2, 3, 4});

  std::copy(v.begin(), v.end(), std::ostream_iterator<int>(std::cout, ", "));
  std::cout << '\n';
}
} // namespace exchange_test

//
// https://en.cppreference.com/w/cpp/utility/move#Example
//
namespace move_test {
void run() {
  std::string str = "Salut";
  std::vector<std::string> v;

  // uses the push_back(const T&) overload, which means we incur the cost of
  // copying str
  v.push_back(str);
  std::cout << "After copy, str is " << std::quoted(str) << '\n';

  // uses the rvalue reference push_back(T&&) overload, which means no strings
  // will be copied; instead, the contents of str will be moved into the vector.
  // This is less expensive, but also means str might now be empty.
  v.push_back(std::move(str));
  std::cout << "After move, str is " << std::quoted(str) << '\n';

  std::cout << "The contents of the vector are { " << std::quoted(v[0]) << ", "
            << std::quoted(v[1]) << " }\n";
}
} // namespace move_test

//
// https://en.cppreference.com/w/cpp/types/is_same#Example
//
namespace is_same_test {
void run() {
  std::cout << std::boolalpha;

  // some implementation-defined facts
  std::cout << "std::is_same<int, std::int32_t>::value : "
            << std::is_same<int, std::int32_t>::value << '\n';
  std::cout << "std::is_same<int, std::int64_t>::value : "
            << std::is_same<int, std::int64_t>::value << '\n';

  auto num1 = 1;
  auto num2 = 1u;
  static_assert(std::is_same_v<decltype(num1), decltype(num2)> == false);

  static_assert(std::is_same<float, std::int32_t>::value == false);
  static_assert(std::is_same_v<int, int> == true);
  static_assert(std::is_same_v<int, unsigned int> == false);
  static_assert(std::is_same_v<int, signed int> == true);
  static_assert(std::is_same_v<char, char> == true);
  static_assert(std::is_same_v<char, unsigned char> == false);
  static_assert(std::is_same_v<char, signed char> == false);
  static_assert(!std::is_same<const int, int>());
}
} // namespace is_same_test

//
// https://en.cppreference.com/w/cpp/numeric/ratio/ratio_add#Example
//
namespace ratio_test {
void run() {
  using two_third = std::ratio<2, 3>;
  using one_sixth = std::ratio<1, 6>;
  using sum = std::ratio_add<two_third, one_sixth>;

  std::cout << "2/3 + 1/6 = " << sum::num << '/' << sum::den << '\n';

  static_assert(std::ratio_equal_v<std::ratio_multiply<std::femto, std::exa>,
                                   std::kilo>);
}
} // namespace ratio_test

//
// https://en.cppreference.com/w/cpp/concepts/derived_from#Example
//
namespace concepts_test {
class A {};
class B : public A {};
class C : private A {};

// std::derived_from == true only for public inheritance or exact same class.
static_assert(std::derived_from<B, B> == true);
static_assert(std::derived_from<int, int> == false);
static_assert(std::derived_from<B, A> == true);
static_assert(std::derived_from<C, A> == false);

// std::is_base_of == true also for private inheritance.
static_assert(std::is_base_of_v<B, B> == true);
static_assert(std::is_base_of_v<int, int> == false);
static_assert(std::is_base_of_v<A, B> == true);
static_assert(std::is_base_of_v<A, C> == true);

template <typename T, typename... U>
concept either = (std::same_as<T, U> || ...);

static_assert(either<int, double, int>);
static_assert(!either<int, double, char>);

void run() { std::cout << "concept static assertions passed\n"; }
} // namespace concepts_test

//
// https://en.cppreference.com/w/cpp/utility/compare/strong_ordering#Example
//
namespace strong_ordering_test {
struct Point {
  int x{}, y{};

  friend constexpr bool operator==(Point, Point) = default;

  friend constexpr std::strong_ordering operator<=>(Point lhs, Point rhs) {
    if (auto cmp = lhs.x <=> rhs.x; cmp != 0) {
      return cmp;
    }
    return lhs.y <=> rhs.y;
  }
};

void print_three_way_comparison(Point lhs, Point rhs) {
  const auto cmp = lhs <=> rhs;
  if (cmp < 0) {
    std::cout << "less\n";
  } else if (cmp > 0) {
    std::cout << "greater\n";
  } else {
    std::cout << "equal\n";
  }
}

void print_two_way_comparison(Point lhs, Point rhs) {
  std::cout << std::boolalpha << (lhs == rhs) << ' ' << (lhs != rhs) << ' '
            << (lhs < rhs) << ' ' << (lhs <= rhs) << ' ' << (lhs > rhs) << ' '
            << (lhs >= rhs) << '\n';
}

void run() {
  const Point p1{0, 1}, p2{0, 1}, p3{0, 2};

  print_three_way_comparison(p1, p2);
  print_two_way_comparison(p1, p2);

  print_three_way_comparison(p1, p3);
  print_two_way_comparison(p1, p3);
}
} // namespace strong_ordering_test

//
// https://en.cppreference.com/w/cpp/numeric/constants#Example
//
namespace numbers_test {
void run() {
  using namespace std::numbers;

  static_assert(pi > 3.141 && pi < 3.142);
  static_assert(inv_pi > 0.318 && inv_pi < 0.319);
  static_assert(sqrt2 > 1.414 && sqrt2 < 1.415);

  std::cout << "pi = " << pi << '\n'
            << "e = " << e << '\n'
            << "sqrt2 = " << sqrt2 << '\n';
}
} // namespace numbers_test
