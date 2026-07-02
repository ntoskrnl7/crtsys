//
// https://en.cppreference.com/w/cpp/algorithm/sort#Example
//
#if defined(_MSC_VER)
// Release builds define NDEBUG, so assert-only cppreference verification values
// can look unused under /WX. Keep the examples intact and silence that warning
// only for this test translation unit.
#pragma warning(disable : 4189)
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <cassert>
#include <charconv>
#include <cctype>
#include <complex>
#include <cstdint>
#include <cmath>
#include <deque>
#include <forward_list>
#include <functional>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace sort_test {
void run() {
  std::array<int, 10> s{5, 7, 4, 2, 8, 6, 1, 9, 0, 3};

  auto print = [&s](std::string_view const rem) {
    for (auto a : s) {
      std::cout << a << ' ';
    }
    std::cout << ": " << rem << '\n';
  };

  std::sort(s.begin(), s.end());
  print("sorted with the default operator<");

  std::sort(s.begin(), s.end(), std::greater<int>());
  print("sorted with the standard library compare function object");

  struct {
    bool operator()(int a, int b) const { return a < b; }
  } customLess;

  std::sort(s.begin(), s.end(), customLess);
  print("sorted with a custom function object");

  std::sort(s.begin(), s.end(), [](int a, int b) { return a > b; });
  print("sorted with a lambda expression");
}
} // namespace sort_test

//
// https://en.cppreference.com/w/cpp/algorithm/find#Example
//
namespace find_test {
bool is_even(int i) { return i % 2 == 0; }

void example_contains() {
  const auto haystack = {1, 2, 3, 4};
  for (const int needle : {3, 5}) {
    if (std::find(haystack.begin(), haystack.end(), needle) ==
        haystack.end()) {
      std::cout << "haystack does not contain " << needle << '\n';
    } else {
      std::cout << "haystack contains " << needle << '\n';
    }
  }
}

void example_predicate() {
  for (const auto &haystack : {std::array{3, 1, 4}, {1, 3, 5}}) {
    const auto it = std::find_if(haystack.begin(), haystack.end(), is_even);
    if (it != haystack.end()) {
      std::cout << "haystack contains an even number " << *it << '\n';
    } else {
      std::cout << "haystack does not contain even numbers\n";
    }
  }
}

void example_list_init() {
  std::vector<std::complex<double>> haystack{{4.0, 2.0}};
#ifdef __cpp_lib_algorithm_default_value_type
  // T gets deduced making list-initialization possible
  const auto it = std::find(haystack.begin(), haystack.end(), {4.0, 2.0});
#else
  const auto it =
      std::find(haystack.begin(), haystack.end(), std::complex{4.0, 2.0});
#endif
  assert(it == haystack.begin());
}

void run() {
  example_contains();
  example_predicate();
  example_list_init();
}
} // namespace find_test

//
// https://en.cppreference.com/w/cpp/algorithm/transform#Example
//
namespace transform_test {
void print_ordinals(const std::vector<unsigned> &ordinals) {
  std::cout << "ordinals: ";
  for (unsigned ord : ordinals) {
    std::cout << std::setw(3) << ord << ' ';
  }
  std::cout << '\n';
}

char to_uppercase(unsigned char c) { return static_cast<char>(std::toupper(c)); }

void unary_transform_example(std::string &hello, std::string world) {
  // transform string to uppercase in-place
  std::transform(hello.cbegin(), hello.cend(), hello.begin(), to_uppercase);
  std::cout << "hello = " << std::quoted(hello) << '\n';

  // for each char in world, write char in uppercase to std::cout
  std::transform(world.cbegin(), world.cend(),
                 std::ostreambuf_iterator<char>{std::cout}, to_uppercase);
  std::cout << '\n';
}

void binary_transform_example(std::vector<unsigned> ordinals) {
  print_ordinals(ordinals);

  std::transform(ordinals.cbegin(), ordinals.cend(), ordinals.cbegin(),
                 ordinals.begin(), std::plus<>{});

  print_ordinals(ordinals);
}

void run() {
  std::string hello("hello");
  unary_transform_example(hello, "world");

  std::vector<unsigned> ordinals;
  std::copy(hello.cbegin(), hello.cend(), std::back_inserter(ordinals));
  binary_transform_example(std::move(ordinals));
}
} // namespace transform_test

//
// https://en.cppreference.com/w/cpp/algorithm/remove#Example
//
namespace remove_test {
void run() {
  std::string str1{"Quick  Red  Dog"};
  std::cout << "1) " << std::quoted(str1) << '\n';
  const auto noSpaceEnd = std::remove(str1.begin(), str1.end(), ' ');
  std::cout << "2) " << std::quoted(str1) << '\n';

  // The spaces are removed from the string only logically.
  // Note, we use view, the original string is still not shrunk:
  std::cout << "3) " << std::quoted(std::string_view(str1.begin(), noSpaceEnd))
            << ", size: " << str1.size() << '\n';

  str1.erase(noSpaceEnd, str1.end());
  std::cout << "4) " << std::quoted(str1) << ", size: " << str1.size()
            << "\n\n";

  // remove_if with a predicate.
  std::string str2{"Quick  Red  Dog"};
  str2.erase(std::remove_if(str2.begin(), str2.end(),
                            [](unsigned char x) { return std::isspace(x); }),
             str2.end());
  std::cout << "5) " << std::quoted(str2) << '\n';
}
} // namespace remove_test

//
// https://en.cppreference.com/w/cpp/algorithm/partition#Example
//
namespace partition_test {
template <class ForwardIt> void quicksort(ForwardIt first, ForwardIt last) {
  if (first == last) {
    return;
  }

  auto pivot = *std::next(first, std::distance(first, last) / 2);
  auto middle1 = std::partition(first, last,
                                [pivot](const auto &em) { return em < pivot; });
  auto middle2 =
      std::partition(middle1, last,
                     [pivot](const auto &em) { return !(pivot < em); });

  quicksort(first, middle1);
  quicksort(middle2, last);
}

void run() {
  std::vector<int> v{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  std::cout << "Original vector: ";
  for (int elem : v) {
    std::cout << elem << ' ';
  }

  auto it = std::partition(v.begin(), v.end(),
                           [](int i) { return i % 2 == 0; });

  std::cout << "\nPartitioned vector: ";
  std::copy(std::begin(v), it, std::ostream_iterator<int>(std::cout, " "));
  std::cout << "* ";
  std::copy(it, std::end(v), std::ostream_iterator<int>(std::cout, " "));

  std::forward_list<int> fl{7, 6, 5, 4, 3, 2, 1, 0};
  std::cout << "\nUnsorted list: ";
  for (int n : fl) {
    std::cout << n << ' ';
  }
  std::cout << '\n';

  quicksort(std::begin(fl), std::end(fl));
  std::cout << "Sorted using quicksort: ";
  for (int fi : fl) {
    std::cout << fi << ' ';
  }
  std::cout << '\n';
}
} // namespace partition_test

//
// https://en.cppreference.com/w/cpp/algorithm/binary_search#Example
//
namespace binary_search_test {
void run() {
  const auto haystack = {1, 3, 4, 5, 9};

  for (const auto needle : {1, 2, 3}) {
    std::cout << "Searching for " << needle << '\n';
    if (std::binary_search(haystack.begin(), haystack.end(), needle)) {
      std::cout << "Found " << needle << '\n';
    } else {
      std::cout << "Not found!\n";
    }
  }

  std::vector<std::complex<double>> haystack_complex{{4, 2}, {1, 3}};
  auto cmpz = [](std::complex<double> x, std::complex<double> y) {
    return std::norm(x) < std::norm(y);
  };
  std::sort(haystack_complex.begin(), haystack_complex.end(), cmpz);
  assert(std::binary_search(haystack_complex.begin(), haystack_complex.end(),
                            std::complex<double>{4, 2}, cmpz));
}
} // namespace binary_search_test

//
// https://en.cppreference.com/w/cpp/algorithm/lower_bound#Example
//
namespace lower_bound_test {
struct PriceInfo {
  double price;
};

void run() {
  const std::vector<int> data{1, 2, 4, 5, 5, 6};

  for (int i = 0; i < 8; ++i) {
    // Search for first element x such that i <= x.
    // The cppreference example prints the Unicode <= glyph; this driver log
    // keeps ASCII output so it remains readable in KD/debugger transports.
    auto lower = std::lower_bound(data.begin(), data.end(), i);
    std::cout << i << " <= ";
    lower != data.end()
        ? std::cout << *lower << " at index "
                    << std::distance(data.begin(), lower)
        : std::cout << "not found";
    std::cout << '\n';
  }

  std::vector<PriceInfo> prices{{100.0}, {101.5}, {102.5}, {102.5}, {107.3}};
  for (const double to_find : {102.5, 110.2}) {
    auto prc_info =
        std::lower_bound(prices.begin(), prices.end(), to_find,
                         [](const PriceInfo &info, double value) {
                           return info.price < value;
                         });
    prc_info != prices.end()
        ? std::cout << prc_info->price << " at index "
                    << prc_info - prices.begin()
        : std::cout << to_find << " not found";
    std::cout << '\n';
  }
  using CD = std::complex<double>;
  std::vector<CD> nums{{1, 0}, {2, 2}, {2, 1}, {3, 0}};
  auto cmpz = [](CD x, CD y) { return x.real() < y.real(); };
  // cppreference uses braced value arguments for the C++26 default value type
  // form. Older MSVC STL versions still need an explicitly typed value.
#ifdef __cpp_lib_algorithm_default_value_type
  auto it = std::lower_bound(nums.cbegin(), nums.cend(), {2, 0}, cmpz);
#else
  auto it = std::lower_bound(nums.cbegin(), nums.cend(), CD{2, 0}, cmpz);
#endif
  assert((*it == CD{2, 2}));
}
} // namespace lower_bound_test

//
// https://en.cppreference.com/w/cpp/algorithm/upper_bound#Example
//
namespace upper_bound_test {
struct PriceInfo {
  double price;
};

void run() {
  const std::vector<int> data{1, 2, 4, 5, 5, 6};

  for (int i = 0; i < 7; ++i) {
    // Search first element that is greater than i
    auto upper = std::upper_bound(data.begin(), data.end(), i);
    std::cout << i << " < ";
    upper != data.end()
        ? std::cout << *upper << " at index "
                    << std::distance(data.begin(), upper)
        : std::cout << "not found";
    std::cout << '\n';
  }

  std::vector<PriceInfo> prices{{100.0}, {101.5}, {102.5}, {102.5}, {107.3}};
  for (double to_find : {102.5, 110.2}) {
    auto prc_info =
        std::upper_bound(prices.begin(), prices.end(), to_find,
                         [](double value, const PriceInfo &info) {
                           return value < info.price;
                         });
    prc_info != prices.end()
        ? std::cout << prc_info->price << " at index "
                    << prc_info - prices.begin()
        : std::cout << to_find << " not found";
    std::cout << '\n';
  }
  using CD = std::complex<double>;
  std::vector<CD> nums{{1, 0}, {2, 2}, {2, 1}, {3, 0}, {3, 1}};
  auto cmpz = [](CD x, CD y) { return x.real() < y.real(); };
  // cppreference uses braced value arguments for the C++26 default value type
  // form. Older MSVC STL versions still need an explicitly typed value.
#ifdef __cpp_lib_algorithm_default_value_type
  auto it = std::upper_bound(nums.cbegin(), nums.cend(), {2, 0}, cmpz);
#else
  auto it = std::upper_bound(nums.cbegin(), nums.cend(), CD{2, 0}, cmpz);
#endif
  assert((*it == CD{3, 0}));
}
} // namespace upper_bound_test

//
// https://en.cppreference.com/w/cpp/algorithm/equal_range#Example
//
namespace equal_range_test {
struct S {
  int number;
  char name;
  // note: name is ignored by this comparison operator
  bool operator<(const S &s) const { return number < s.number; }
};

struct Comp {
  bool operator()(const S &s, int i) const { return s.number < i; }
  bool operator()(int i, const S &s) const { return i < s.number; }
};

void run() {
  // note: not ordered, only partitioned w.r.t. S defined below
  const std::vector<S> vec{{1, 'A'}, {2, 'B'}, {2, 'C'},
                           {2, 'D'}, {4, 'G'}, {3, 'F'}};
  const S value{2, '?'};

  std::cout << "Compare using S::operator<(): ";
  const auto p = std::equal_range(vec.begin(), vec.end(), value);

  for (auto it = p.first; it != p.second; ++it) {
    std::cout << it->name << ' ';
  }
  std::cout << '\n';
  std::cout << "Using heterogeneous comparison: ";
  const auto p2 = std::equal_range(vec.begin(), vec.end(), 2, Comp{});

  for (auto it = p2.first; it != p2.second; ++it) {
    std::cout << it->name << ' ';
  }
  std::cout << '\n';
  using CD = std::complex<double>;
  std::vector<CD> nums{{1, 0}, {2, 2}, {2, 1}, {3, 0}, {3, 1}};
  auto cmpz = [](CD x, CD y) { return x.real() < y.real(); };
  // cppreference uses braced value arguments for the C++26 default value type
  // form. Older MSVC STL versions still need an explicitly typed value.
#ifdef __cpp_lib_algorithm_default_value_type
  auto p3 = std::equal_range(nums.cbegin(), nums.cend(), {2, 0}, cmpz);
#else
  auto p3 = std::equal_range(nums.cbegin(), nums.cend(), CD{2, 0}, cmpz);
#endif
  for (auto it = p3.first; it != p3.second; ++it) {
    std::cout << *it << ' ';
  }
  std::cout << '\n';
}
} // namespace equal_range_test

//
// https://en.cppreference.com/w/cpp/algorithm/stable_sort#Example
//
namespace stable_sort_test {
struct Employee {
  int age;
  std::string name; // Does not participate in comparisons
};

bool operator<(const Employee &lhs, const Employee &rhs) {
  return lhs.age < rhs.age;
}
#if __cpp_lib_constexpr_algorithms >= 202306L
consteval auto get_sorted() {
  auto v = std::array{3, 1, 4, 1, 5, 9};
  std::stable_sort(v.begin(), v.end());
  return v;
}
static_assert(std::ranges::is_sorted(get_sorted()));
#endif

void run() {
  std::vector<Employee> v{{108, "Zaphod"}, {32, "Arthur"}, {108, "Ford"}};

  std::stable_sort(v.begin(), v.end());
  for (const Employee &e : v) {
    std::cout << e.age << ", " << e.name << '\n';
  }
}
} // namespace stable_sort_test

//
// https://en.cppreference.com/w/cpp/algorithm/nth_element#Example
//
namespace nth_element_test {
void printVec(const std::vector<int> &vec) {
  std::cout << "v = {";
  for (char sep[]{0, ' ', 0}; const int i : vec) {
    std::cout << sep << i, sep[0] = ',';
  }
  std::cout << "};\n";
}

void run() {
  std::vector<int> v{5, 10, 6, 4, 3, 2, 6, 7, 9, 3};
  printVec(v);
  auto m = v.begin() + v.size() / 2;
  std::nth_element(v.begin(), m, v.end());
  std::cout << "\n The median is " << v[v.size() / 2] << '\n';
  // The consequence of the inequality of elements before/after the Nth one:
  assert(std::accumulate(v.begin(), m, 0) <
         std::accumulate(m, v.end(), 0));
  printVec(v);
  // Note: comp function changed
  std::nth_element(v.begin(), v.begin() + 1, v.end(), std::greater{});
  std::cout << "\n The second largest element is " << v[1] << '\n';
  std::cout << "The largest element is " << v[0] << '\n';
  printVec(v);
}
} // namespace nth_element_test

//
// https://en.cppreference.com/w/cpp/algorithm/partial_sort#Example
//
namespace partial_sort_test {
void print(const auto &s, int middle) {
  for (int a : s) {
    std::cout << a << ' ';
  }
  std::cout << '\n';
  if (middle > 0) {
    while (middle-- > 0) {
      std::cout << "--";
    }
    std::cout << '^';
  } else if (middle < 0) {
    for (auto i = s.size() + middle; --i; std::cout << "  ") {
    }
    for (std::cout << '^'; middle++ < 0; std::cout << "--") {
    }
  }
  std::cout << '\n';
};

void run() {
  std::array<int, 10> s{5, 7, 4, 2, 8, 6, 1, 9, 0, 3};
  print(s, 0);
  std::partial_sort(s.begin(), s.begin() + 3, s.end());
  print(s, 3);
  std::partial_sort(s.rbegin(), s.rbegin() + 4, s.rend());
  print(s, -4);
  std::partial_sort(s.rbegin(), s.rbegin() + 5, s.rend(), std::greater{});
  print(s, -5);
}
} // namespace partial_sort_test

//
// https://en.cppreference.com/w/cpp/algorithm/all_any_none_of#Example
//
namespace all_any_none_of_test {
void run() {
  // Keep this as assertion coverage instead of verbatim output so the driver
  // log stays small while preserving the cppreference predicate semantics.
  const std::vector<int> v{0, 2, 4, 6, 8};

  auto is_even = [](int i) { return i % 2 == 0; };
  auto is_negative = [](int i) { return i < 0; };

  assert(std::all_of(v.begin(), v.end(), is_even));
  assert(std::any_of(v.begin(), v.end(), [](int i) { return i == 4; }));
  assert(std::none_of(v.begin(), v.end(), is_negative));

  std::cout << "all_of/any_of/none_of assertions passed\n";
}
} // namespace all_any_none_of_test

//
// https://en.cppreference.com/w/cpp/algorithm/count#Example
//
namespace count_test {
void run() {
  const std::array data{1, 2, 4, 5, 5, 5, 7, 9};

  for (const int target : {3, 5}) {
    const auto num_items = std::count(data.begin(), data.end(), target);
    std::cout << "number: " << target << ", count: " << num_items << '\n';
  }

  const auto divisible_by_three =
      std::count_if(data.begin(), data.end(), [](int x) { return x % 3 == 0; });
  std::cout << "numbers divisible by three: " << divisible_by_three << '\n';
}
} // namespace count_test

//
// https://en.cppreference.com/w/cpp/algorithm/mismatch#Example
// https://en.cppreference.com/w/cpp/algorithm/equal#Example
//
namespace mismatch_equal_test {
void run() {
  // The linked pages have independent standalone examples. This harness folds
  // the palindrome/equality behavior into one compact driver test.
  const std::string mirror_ends{"radar"};
  const auto first_mismatch =
      std::mismatch(mirror_ends.begin(), mirror_ends.end(),
                    mirror_ends.rbegin());

  assert(first_mismatch.first == mirror_ends.end());
  assert(std::equal(mirror_ends.begin(), mirror_ends.end(),
                    mirror_ends.rbegin()));

  const std::string text{"radix"};
  const auto [left, right] =
      std::mismatch(text.begin(), text.end(), text.rbegin());

  std::cout << "first mismatch: " << *left << " != " << *right << '\n';
}
} // namespace mismatch_equal_test

//
// https://en.cppreference.com/w/cpp/algorithm/copy#Example
// https://en.cppreference.com/w/cpp/algorithm/copy_n#Example
// https://en.cppreference.com/w/cpp/algorithm/copy_backward#Example
// https://en.cppreference.com/w/cpp/algorithm/move_backward#Example
//
namespace copy_move_test {
void run() {
  // The linked pages are separate copy-family examples. Keep the important
  // source/destination semantics here; in particular copy_backward must copy
  // into a destination that ends at the supplied output iterator.
  const std::vector<int> source{1, 2, 3, 4, 5};
  std::vector<int> destination(source.size());

  std::copy(source.begin(), source.end(), destination.begin());
  assert(destination == source);

  std::fill(destination.begin(), destination.end(), 0);
  std::copy_n(source.begin(), 3, destination.begin());
  assert((destination == std::vector<int>{1, 2, 3, 0, 0}));

  std::vector<int> backward_source(4);
  std::iota(backward_source.begin(), backward_source.end(), 1);
  std::vector<int> backward_destination(6);
  std::copy_backward(backward_source.begin(), backward_source.end(),
                     backward_destination.end());
  assert((backward_destination == std::vector<int>{0, 0, 1, 2, 3, 4}));

  std::vector<std::string> words{"alpha", "beta", "gamma", "", ""};
  std::move_backward(words.begin(), words.begin() + 3, words.end());
  assert(words[2] == "alpha");
  assert(words[3] == "beta");
  assert(words[4] == "gamma");

  std::cout << "copy/move algorithm assertions passed\n";
}
} // namespace copy_move_test

//
// https://en.cppreference.com/w/cpp/algorithm/replace#Example
// https://en.cppreference.com/w/cpp/algorithm/fill#Example
// https://en.cppreference.com/w/cpp/algorithm/generate#Example
// https://en.cppreference.com/w/cpp/algorithm/unique#Example
// https://en.cppreference.com/w/cpp/algorithm/reverse#Example
// https://en.cppreference.com/w/cpp/algorithm/rotate#Example
// https://en.cppreference.com/w/cpp/algorithm/shift#Example
// https://en.cppreference.com/w/cpp/algorithm/sample#Example
//
namespace modifying_sequence_test {
template <class Range> void print_range(std::string_view label, Range &&range) {
  std::cout << label;
  for (const auto &value : range) {
    std::cout << value << ' ';
  }
  std::cout << '\n';
}

void run() {
  // Compact several cppreference modifying-sequence examples into one stable
  // driver test. Random sampling is still bounded and assertion-checked.
  std::vector<int> data(10);
  std::iota(data.begin(), data.end(), 0);

  std::replace(data.begin(), data.end(), 3, 99);
  std::fill(data.begin(), data.begin() + 2, -1);

  int n = 0;
  std::generate(data.begin() + 2, data.begin() + 5, [&n] { return n += 2; });

  data.erase(std::unique(data.begin(), data.end()), data.end());
  std::reverse(data.begin(), data.end());
  std::rotate(data.begin(), data.begin() + data.size() / 2, data.end());

#if defined(__cpp_lib_shift)
  std::shift_left(data.begin(), data.end(), 1);
  std::shift_right(data.begin(), data.end(), 1);
#endif

  std::vector<int> out;
  std::sample(data.begin(), data.end(), std::back_inserter(out),
              std::min<std::size_t>(3, data.size()),
              std::mt19937{std::random_device{}()});

  assert(out.size() <= 3);
  print_range("modifying sequence result: ", data);
  print_range("sample result: ", out);
}
} // namespace modifying_sequence_test

//
// https://en.cppreference.com/w/cpp/algorithm/partition_copy#Example
// https://en.cppreference.com/w/cpp/algorithm/stable_partition#Example
// https://en.cppreference.com/w/cpp/algorithm/is_partitioned#Example
// https://en.cppreference.com/w/cpp/algorithm/partition_point#Example
// https://en.cppreference.com/w/cpp/algorithm/includes#Example
// https://en.cppreference.com/w/cpp/algorithm/set_union#Example
// https://en.cppreference.com/w/cpp/algorithm/set_intersection#Example
// https://en.cppreference.com/w/cpp/algorithm/set_difference#Example
// https://en.cppreference.com/w/cpp/algorithm/set_symmetric_difference#Example
// https://en.cppreference.com/w/cpp/algorithm/inplace_merge#Example
// https://en.cppreference.com/w/cpp/algorithm/is_heap#Example
// https://en.cppreference.com/w/cpp/algorithm/minmax#Example
// https://en.cppreference.com/w/cpp/algorithm/clamp#Example
// https://en.cppreference.com/w/cpp/algorithm/minmax_element#Example
// https://en.cppreference.com/w/cpp/algorithm/prev_permutation#Example
// https://en.cppreference.com/w/cpp/algorithm/is_permutation#Example
//
namespace algorithm_edges_test {
template <class Range> std::vector<int> to_vector(Range &&range) {
  return {range.begin(), range.end()};
}

void run() {
  // These cppreference pages mostly demonstrate related algorithms with small
  // printed examples. Use deterministic inputs and assertions so a kernel
  // driver run verifies the same algorithm contracts without huge KD output.
  const std::array ints{1, 2, 3, 4, 5, 6};
  std::vector<int> odds, evens;
  std::partition_copy(ints.begin(), ints.end(), std::back_inserter(odds),
                      std::back_inserter(evens),
                      [](int i) { return i % 2 != 0; });
  assert((odds == std::vector{1, 3, 5}));
  assert((evens == std::vector{2, 4, 6}));

  std::vector<int> partitioned{5, 3, 1, 2, 4, 6};
  assert(std::is_partitioned(partitioned.begin(), partitioned.end(),
                             [](int i) { return i % 2 != 0; }));
  const auto point = std::partition_point(
      partitioned.begin(), partitioned.end(), [](int i) { return i % 2 != 0; });
  assert(point != partitioned.end() && *point == 2);

  std::vector<int> stable{1, 2, 3, 4, 5, 6};
  std::stable_partition(stable.begin(), stable.end(),
                        [](int i) { return i % 2 != 0; });
  assert((stable == std::vector{1, 3, 5, 2, 4, 6}));

  const std::vector<int> a{1, 2, 3, 4, 5};
  const std::vector<int> b{2, 4};
  assert(std::includes(a.begin(), a.end(), b.begin(), b.end()));

  const std::vector<int> c{1, 2, 3, 4};
  const std::vector<int> d{3, 4, 5, 6};
  std::vector<int> result;
  std::set_union(c.begin(), c.end(), d.begin(), d.end(),
                 std::back_inserter(result));
  assert((result == std::vector{1, 2, 3, 4, 5, 6}));

  result.clear();
  std::set_intersection(c.begin(), c.end(), d.begin(), d.end(),
                        std::back_inserter(result));
  assert((result == std::vector{3, 4}));

  result.clear();
  std::set_difference(c.begin(), c.end(), d.begin(), d.end(),
                      std::back_inserter(result));
  assert((result == std::vector{1, 2}));

  result.clear();
  std::set_symmetric_difference(c.begin(), c.end(), d.begin(), d.end(),
                                std::back_inserter(result));
  assert((result == std::vector{1, 2, 5, 6}));

  std::vector<int> merge_me{1, 3, 5, 2, 4, 6};
  std::inplace_merge(merge_me.begin(), merge_me.begin() + 3, merge_me.end());
  assert((merge_me == std::vector{1, 2, 3, 4, 5, 6}));

  std::vector<int> heap{9, 5, 4, 1, 1, 3};
  assert(std::is_heap(heap.begin(), heap.end()));
  std::push_heap(heap.begin(), heap.end());
  std::sort_heap(heap.begin(), heap.end());
  assert(std::is_sorted(heap.begin(), heap.end()));

  const auto [min_value, max_value] = std::minmax({3, 1, 4, 1, 5});
  assert(min_value == 1);
  assert(max_value == 5);
  assert(std::clamp(42, 0, 10) == 10);

  const auto [min_it, max_it] = std::minmax_element(c.begin(), c.end());
  assert(*min_it == 1);
  assert(*max_it == 4);

  std::string s = "cba";
  assert(std::prev_permutation(s.begin(), s.end()));
  assert(s == "cab");
  assert(std::is_permutation(s.begin(), s.end(), std::string{"abc"}.begin()));

  std::cout << "algorithm edge assertions passed\n";
}
} // namespace algorithm_edges_test

//
// https://en.cppreference.com/w/cpp/ranges#Example
//
namespace ranges_views_test {
void run() {
  auto const ints = {0, 1, 2, 3, 4, 5};
  auto even = [](int i) { return 0 == i % 2; };
  auto square = [](int i) { return i * i; };

  // the "pipe" syntax of composing the views:
  for (int i : ints | std::views::filter(even) |
                   std::views::transform(square)) {
    std::cout << i << ' ';
  }

  std::cout << '\n';
  // a traditional "functional" composing syntax:
  for (int i : std::views::transform(std::views::filter(ints, even), square)) {
    std::cout << i << ' ';
  }
  std::cout << '\n';
}
} // namespace ranges_views_test

//
// https://en.cppreference.com/w/cpp/ranges/take_view#Example
// https://en.cppreference.com/w/cpp/ranges/drop_view#Example
// https://en.cppreference.com/w/cpp/ranges/reverse_view#Example
// https://en.cppreference.com/w/cpp/ranges/join_view#Example
// https://en.cppreference.com/w/cpp/ranges/split_view#Example
// https://en.cppreference.com/w/cpp/ranges/keys_view#Example
// https://en.cppreference.com/w/cpp/ranges/values_view#Example
// https://en.cppreference.com/w/cpp/ranges/elements_view#Example
//
namespace ranges_more_adaptors_test {
void run() {
#if defined(__cpp_lib_ranges)
  // The linked cppreference pages are separate presentation examples. Keep the
  // same adaptor operations in one compact driver test to avoid a large amount
  // of debugger output.
  std::array ints{0, 1, 2, 3, 4, 5};
  const std::array expected_take{0, 1, 2};
  const std::array expected_drop{2, 3, 4, 5};
  const std::array expected_reverse{5, 4, 3, 2, 1, 0};

  assert(std::ranges::equal(ints | std::views::take(3), expected_take));
  assert(std::ranges::equal(ints | std::views::drop(2), expected_drop));
  assert(std::ranges::equal(ints | std::views::reverse, expected_reverse));
  (void)expected_take;
  (void)expected_drop;
  (void)expected_reverse;

  const std::vector<std::vector<int>> nested{{1, 2}, {3}, {4, 5}};
  const std::array expected_join{1, 2, 3, 4, 5};
  assert(std::ranges::equal(nested | std::views::join, expected_join));
  (void)expected_join;

  std::string_view text{"alpha beta gamma"};
  std::size_t words = 0;
  for (auto word : text | std::views::split(' ')) {
    assert(!std::ranges::empty(word));
    ++words;
  }
  assert(words == 3);
  (void)words;

  const std::vector<std::pair<int, std::string>> pairs{
      {1, "one"}, {2, "two"}, {3, "three"}};
  const std::array expected_keys{1, 2, 3};
  assert(std::ranges::equal(pairs | std::views::keys, expected_keys));

  std::size_t value_count = 0;
  std::size_t value_size_sum = 0;
  for (const std::string &value : pairs | std::views::values) {
    assert(!value.empty());
    (void)value;
    value_size_sum += value.size();
    ++value_count;
  }
  assert(value_count == pairs.size());
  assert(value_size_sum == 11);
  (void)value_count;
  (void)value_size_sum;

  assert(std::ranges::equal(pairs | std::views::elements<0>, expected_keys));
  (void)expected_keys;
#else
  std::cout << "C++20 ranges are not available in this MSVC STL\n";
#endif
}
} // namespace ranges_more_adaptors_test

//
// https://en.cppreference.com/w/cpp/algorithm/ranges/sort#Example
//
namespace ranges_sort_test {
void print(auto comment, auto const &seq, char term = ' ') {
  for (std::cout << comment << '\n'; auto const &elem : seq) {
    std::cout << elem << term;
  }
  std::cout << '\n';
}

struct Particle {
  std::string name;
  double mass; // MeV

  template <class Os> friend Os &operator<<(Os &os, Particle const &p) {
    return os << std::left << std::setw(8) << p.name << " : " << p.mass
              << ' ';
  }
};

void run() {
  std::array s{5, 7, 4, 2, 8, 6, 1, 9, 0, 3};

  namespace ranges = std::ranges;

  ranges::sort(s);
  print("Sort using the default operator<", s);
  ranges::sort(s, ranges::greater());
  print("Sort using a standard library compare function object", s);

  struct {
    bool operator()(int a, int b) const { return a < b; }
  } customLess;

  ranges::sort(s.begin(), s.end(), customLess);
  print("Sort using a custom function object", s);

  ranges::sort(s, [](int a, int b) { return a > b; });
  print("Sort using a lambda expression", s);

  Particle particles[]{{"Electron", 0.511}, {"Muon", 105.66},
                       {"Tau", 1776.86},   {"Positron", 0.511},
                       {"Proton", 938.27}, {"Neutron", 939.57}};

  ranges::sort(particles, {}, &Particle::name);
  print("\n Sort by name using a projection", particles, '\n');
  ranges::sort(particles, {}, &Particle::mass);
  print("Sort by mass using a projection", particles, '\n');
}
} // namespace ranges_sort_test

namespace ranges_cxx23_adaptors_test {
// The linked C++23 range adaptor pages are mostly presentation examples. The
// driver harness keeps the same adaptor operations but verifies them with
// assertions to avoid large debugger-output blocks and to work across feature
// macros that differ by MSVC STL version.
//
// https://en.cppreference.com/w/cpp/ranges/zip_view#Example
//
#if defined(__cpp_lib_ranges_zip)
void zip_view_example() {
  auto x = std::vector{1, 2, 3, 4};
  auto y = std::list<std::string>{"alpha", "beta", "gamma", "delta",
                                  "epsilon"};
  auto z = std::array{'A', 'B', 'C', 'D', 'E', 'F'};

  size_t rows = 0;
  for (std::tuple<int &, std::string &, char &> elem :
       std::views::zip(x, y, z)) {
    assert(std::get<0>(elem) == x[rows]);
    std::get<2>(elem) =
        static_cast<char>(std::get<2>(elem) + ('a' - 'A'));
    ++rows;
  }

  assert(rows == x.size());
  assert((z == std::array{'a', 'b', 'c', 'd', 'E', 'F'}));
}
#endif

//
// https://en.cppreference.com/w/cpp/ranges/chunk_view#Example
//
#if defined(__cpp_lib_ranges_chunk)
void chunk_view_example() {
  const auto v = {1, 2, 3, 4, 5, 6};
  std::array<size_t, 7> expected_chunk_counts{6, 3, 2, 2, 2, 1, 1};
  size_t width_index = 0;

  for (const unsigned width : std::views::iota(1U, 2U + v.size())) {
    auto const chunks = v | std::views::chunk(width);
    size_t chunk_count = 0;
    for (auto chunk : chunks) {
      assert(!std::ranges::empty(chunk));
      ++chunk_count;
    }
    assert(width_index < expected_chunk_counts.size());
    assert(chunk_count == expected_chunk_counts[width_index++]);
  }
  assert(width_index == expected_chunk_counts.size());
  // Release builds compile out assert(), so mark assert-only locals as used.
  (void)expected_chunk_counts;
  (void)width_index;
}
#endif

//
// https://en.cppreference.com/w/cpp/ranges/slide_view#Example
//
#if defined(__cpp_lib_ranges_slide)
void slide_view_example() {
  const auto v = {1, 2, 3, 4, 5, 6};
  std::array<size_t, 6> expected_window_counts{6, 5, 4, 3, 2, 1};
  size_t width_index = 0;

  for (const unsigned width : std::views::iota(1U, 1U + v.size())) {
    auto const windows = v | std::views::slide(width);
    size_t window_count = 0;
    for (auto window : windows) {
      assert(!std::ranges::empty(window));
      ++window_count;
    }
    assert(width_index < expected_window_counts.size());
    assert(window_count == expected_window_counts[width_index++]);
  }
  assert(width_index == expected_window_counts.size());
  // Release builds compile out assert(), so mark assert-only locals as used.
  (void)expected_window_counts;
  (void)width_index;
}
#endif

//
// https://en.cppreference.com/w/cpp/ranges/stride_view#Example
//
#if defined(__cpp_lib_ranges_stride)
void stride_view_example() {
  std::array expected_forward{1, 4, 7, 10};
  std::array expected_reversed_after_stride{10, 7, 4, 1};
  std::array expected_stride_after_reverse{12, 9, 6, 3};

  assert(std::ranges::equal(std::views::iota(1, 13) | std::views::stride(3),
                            expected_forward));
  assert(std::ranges::equal(std::views::iota(1, 13) | std::views::stride(3) |
                                std::views::reverse,
                            expected_reversed_after_stride));
  assert(std::ranges::equal(std::views::iota(1, 13) | std::views::reverse |
                                std::views::stride(3),
                            expected_stride_after_reverse));

  std::string decoded;
  for (char c :
       std::string_view("0x0!133713337*x//42/A$@") | std::views::stride(0B11) |
           std::views::transform([](char ch) -> char { return 0100 | ch; })) {
    decoded.push_back(c);
  }
  assert(decoded == "password");
}
#endif

//
// https://en.cppreference.com/w/cpp/ranges/repeat_view#Example
//
#if defined(__cpp_lib_ranges_repeat)
void repeat_view_example() {
  using namespace std::literals;

  int bounded_count = 0;
  for (auto s : std::views::repeat("C++"sv, 3)) {
    assert(s == "C++"sv);
    ++bounded_count;
  }
  assert(bounded_count == 3);

  int unbounded_count = 0;
  for (auto s : std::views::repeat("I know that you know that"sv) |
                    std::views::take(3)) {
    assert(s == "I know that you know that"sv);
    ++unbounded_count;
  }
  assert(unbounded_count == 3);
}
#endif

//
// https://en.cppreference.com/w/cpp/ranges/enumerate_view#Example
//
#if defined(__cpp_lib_ranges_enumerate)
void enumerate_view_example() {
  constexpr static auto v = {'A', 'B', 'C', 'D'};

  std::size_t index_sum = 0;
  std::string letters;
  for (auto const [index, letter] : std::views::enumerate(v)) {
    index_sum += static_cast<std::size_t>(index);
    letters.push_back(letter);
  }
  assert(index_sum == 6);
  assert(letters == "ABCD");

#if defined(__cpp_lib_ranges_to_container)
  auto m = v | std::views::enumerate | std::ranges::to<std::map>();
  assert(m.size() == 4);
  assert(m[0] == 'A');
  assert(m[3] == 'D');
#endif

  std::vector<int> numbers{1, 3, 5, 7};
  for (auto const [index, num] : std::views::enumerate(numbers)) {
    ++num; // the type is int&
    assert(numbers[static_cast<std::size_t>(index)] % 2 == 0);
  }
  assert((numbers == std::vector{2, 4, 6, 8}));
}
#endif

//
// https://en.cppreference.com/w/cpp/ranges/cartesian_product_view#Example
//
#if defined(__cpp_lib_ranges_cartesian_product)
void cartesian_product_view_example() {
  const auto x = std::array{'A', 'B'};
  const auto y = std::vector{1, 2, 3};
  const auto z = std::list<std::string>{"alpha", "beta", "gamma", "delta"};

  std::size_t count = 0;
  std::string first;
  std::string last;
  for (auto const &tuple : std::views::cartesian_product(x, y, z)) {
    const auto &[a, b, c] = tuple;
    if (count == 0) {
      first = std::string{a} + std::to_string(b) + c;
    }
    last = std::string{a} + std::to_string(b) + c;
    ++count;
  }

  assert(count == 24);
  assert(first == "A1alpha");
  assert(last == "B3delta");
}
#endif

//
// https://en.cppreference.com/w/cpp/ranges/chunk_by_view#Example
//
#if defined(__cpp_lib_ranges_chunk_by)
void chunk_by_view_example() {
  std::initializer_list v1 = {1, 2, 3, 1, 2, 3, 3, 3, 1, 2, 3};
  auto view1 = v1 | std::views::chunk_by(std::ranges::less{});

  std::array expected_sizes1{3UZ, 3UZ, 1UZ, 1UZ, 3UZ};
  std::size_t index = 0;
  for (auto const subrange : view1) {
    assert(index < expected_sizes1.size());
    assert(static_cast<std::size_t>(std::ranges::distance(subrange)) ==
           expected_sizes1[index++]);
  }
  assert(index == expected_sizes1.size());

  std::initializer_list v2 = {1, 2, 3, 4, 4, 0, 2, 3, 3, 3, 2, 1};
  auto view2 = v2 | std::views::chunk_by(std::ranges::not_equal_to{});
  std::array expected_sizes2{4UZ, 4UZ, 1UZ, 3UZ};
  index = 0;
  for (auto const subrange : view2) {
    assert(index < expected_sizes2.size());
    assert(static_cast<std::size_t>(std::ranges::distance(subrange)) ==
           expected_sizes2[index++]);
  }
  assert(index == expected_sizes2.size());

  std::string_view v3 = "__cpp_lib_ranges_chunk_by";
  auto view3 =
      v3 | std::views::chunk_by([](auto x, auto y) {
        return !(x == '_' || y == '_');
      });
  std::array expected_sizes3{1UZ, 1UZ, 3UZ, 1UZ, 3UZ, 1UZ,
                             6UZ, 1UZ, 5UZ, 1UZ, 2UZ};
  index = 0;
  for (auto const subrange : view3) {
    assert(index < expected_sizes3.size());
    assert(static_cast<std::size_t>(std::ranges::distance(subrange)) ==
           expected_sizes3[index++]);
  }
  assert(index == expected_sizes3.size());
}
#endif

//
// https://en.cppreference.com/w/cpp/ranges/join_with_view#Example
//
#if defined(__cpp_lib_ranges_join_with)
void join_with_view_example() {
  std::list<std::string_view> v{"_", "cpp", "lib", "ranges", "join", "with"};
  std::string joined;
  for (char ch : v | std::views::join_with('_')) {
    joined.push_back(ch);
  }
  assert(joined == "__cpp_lib_ranges_join_with");
}
#endif

//
// https://en.cppreference.com/w/cpp/ranges/as_rvalue_view#Example
//
#if defined(__cpp_lib_ranges_as_rvalue)
void as_rvalue_view_example() {
  std::vector<std::string> words = {"Quick", "red", "fox", "jumped",
                                    "over",  "a",   "pterodactyl"};
  std::vector<std::string> new_words;

  std::ranges::copy(words | std::views::as_rvalue,
                    std::back_inserter(new_words));
  auto quoted =
      std::views::transform([](auto &&s) { return "\"" + s + "\""; });

  std::cout << "Old words: ";
  for (auto &&word : words | std::views::as_rvalue | quoted) {
    std::cout << word << ' ';
  }

  std::cout << "\n New words: ";
  for (auto &&word : new_words | std::views::as_rvalue | quoted) {
    std::cout << word << ' ';
  }
  std::cout << '\n';

  // cppreference prints the moved-from source strings as a possible output.
  // The standard only guarantees a valid but unspecified moved-from state, so
  // the driver assertion checks the destination sequence instead.
  assert((new_words == std::vector<std::string>{"Quick", "red", "fox",
                                                "jumped", "over", "a",
                                                "pterodactyl"}));
}
#endif

//
// https://en.cppreference.com/w/cpp/ranges/as_const_view#Example
//
#if defined(__cpp_lib_ranges_as_const)
void as_const_view_example() {
  int x[]{1, 2, 3, 4, 5};

  auto v1 = x | std::views::drop(2);
  assert(v1.back() == 5);
  v1[0]++; // OK, can modify non-const element

  auto v2 = x | std::views::drop(2) | std::views::as_const;
  assert(v2.back() == 5);
  // v2[0]++; // Compile-time error, cannot modify read-only element
  assert(x[2] == 4);
}
#endif

void run() {
  int exercised = 0;

#if defined(__cpp_lib_ranges_zip)
  zip_view_example();
  ++exercised;
#endif
#if defined(__cpp_lib_ranges_chunk)
  chunk_view_example();
  ++exercised;
#endif
#if defined(__cpp_lib_ranges_slide)
  slide_view_example();
  ++exercised;
#endif
#if defined(__cpp_lib_ranges_stride)
  stride_view_example();
  ++exercised;
#endif
#if defined(__cpp_lib_ranges_repeat)
  repeat_view_example();
  ++exercised;
#endif
#if defined(__cpp_lib_ranges_enumerate)
  enumerate_view_example();
  ++exercised;
#endif
#if defined(__cpp_lib_ranges_cartesian_product)
  cartesian_product_view_example();
  ++exercised;
#endif
#if defined(__cpp_lib_ranges_chunk_by)
  chunk_by_view_example();
  ++exercised;
#endif
#if defined(__cpp_lib_ranges_join_with)
  join_with_view_example();
  ++exercised;
#endif
#if defined(__cpp_lib_ranges_as_rvalue)
  as_rvalue_view_example();
  ++exercised;
#endif
#if defined(__cpp_lib_ranges_as_const)
  as_const_view_example();
  ++exercised;
#endif

  if (exercised == 0) {
    std::cout << "C++23 ranges adaptor feature-test macros are not available\n";
  } else {
    std::cout << "C++23 ranges adaptor cppreference examples exercised "
              << exercised << " feature group(s)\n";
  }
}
} // namespace ranges_cxx23_adaptors_test

namespace ranges_cxx23_algorithms_test {
//
// https://en.cppreference.com/w/cpp/algorithm/ranges/contains#Example
//
#if defined(__cpp_lib_ranges_contains)
void contains_example() {
  namespace ranges = std::ranges;

  constexpr auto haystack = std::array{3, 1, 4, 1, 5};
  constexpr auto needle = std::array{1, 4, 1};
  constexpr auto bodkin = std::array{2, 5, 2};
  static_assert(ranges::contains(haystack, 4) &&
                !ranges::contains(haystack, 6) &&
                ranges::contains_subrange(haystack, needle) &&
                !ranges::contains_subrange(haystack, bodkin));

  constexpr std::array<std::complex<double>, 3> nums{
      {{1, 2}, {3, 4}, {5, 6}}};
#ifdef __cpp_lib_algorithm_default_value_type
  static_assert(ranges::contains(nums, {3, 4}));
#else
  static_assert(ranges::contains(nums, std::complex<double>{3, 4}));
#endif
}
#endif

//
// https://en.cppreference.com/w/cpp/algorithm/ranges/starts_with#Example
//
#if defined(__cpp_lib_ranges_starts_ends_with)
void starts_with_example() {
  using namespace std::literals;

  constexpr auto ascii_upper = [](char8_t c) {
    return u8'a' <= c && c <= u8'z'
               ? static_cast<char8_t>(c + u8'A' - u8'a')
               : c;
  };

  constexpr auto cmp_ignore_case = [=](char8_t x, char8_t y) {
    return ascii_upper(x) == ascii_upper(y);
  };
  static_assert(std::ranges::starts_with("const_cast", "const"sv));
  static_assert(std::ranges::starts_with("constexpr", "const"sv));
  static_assert(!std::ranges::starts_with("volatile", "const"sv));
  std::cout << std::boolalpha
            << std::ranges::starts_with(u8"Constantinopolis", u8"constant"sv,
                                        {}, ascii_upper, ascii_upper)
            << ' '
            << std::ranges::starts_with(u8"Istanbul", u8"constant"sv, {},
                                        ascii_upper, ascii_upper)
            << ' '
            << std::ranges::starts_with(u8"Metropolis", u8"metro"sv,
                                        cmp_ignore_case)
            << ' '
            << std::ranges::starts_with(u8"Acropolis", u8"metro"sv,
                                        cmp_ignore_case)
            << '\n';
  constexpr static auto v = {1, 3, 5, 7, 9};
  constexpr auto odd = [](int x) { return x % 2; };
  static_assert(std::ranges::starts_with(v, std::views::iota(1) |
                                                std::views::filter(odd) |
                                                std::views::take(3)));
}

//
// https://en.cppreference.com/w/cpp/algorithm/ranges/ends_with#Example
//
void ends_with_example() {
  static_assert(!std::ranges::ends_with("for", "cast") &&
                std::ranges::ends_with("dynamic_cast", "cast") &&
                !std::ranges::ends_with("as_const", "cast") &&
                std::ranges::ends_with("bit_cast", "cast") &&
                !std::ranges::ends_with("to_underlying", "cast") &&
                std::ranges::ends_with(std::array{1, 2, 3, 4},
                                       std::array{3, 4}) &&
                !std::ranges::ends_with(std::array{1, 2, 3, 4},
                                        std::array{4, 5}));
}
#endif

//
// https://en.cppreference.com/w/cpp/algorithm/ranges/find_last#Example
//
#if defined(__cpp_lib_ranges_find_last)
void find_last_example() {
  namespace ranges = std::ranges;

  constexpr static auto v = {1, 2, 3, 1, 2, 3, 1, 2};
  {
    constexpr auto i1 = ranges::find_last(v.begin(), v.end(), 3);
    constexpr auto i2 = ranges::find_last(v, 3);
    static_assert(ranges::distance(v.begin(), i1.begin()) == 5);
    static_assert(ranges::distance(v.begin(), i2.begin()) == 5);
  }
  {
    constexpr auto i1 = ranges::find_last(v.begin(), v.end(), -3);
    constexpr auto i2 = ranges::find_last(v, -3);
    static_assert(i1.begin() == v.end());
    static_assert(i2.begin() == v.end());
  }
  auto abs = [](int x) { return x < 0 ? -x : x; };
  {
    auto pred = [](int x) { return x == 3; };
    constexpr auto i1 = ranges::find_last_if(v.begin(), v.end(), pred, abs);
    constexpr auto i2 = ranges::find_last_if(v, pred, abs);
    static_assert(ranges::distance(v.begin(), i1.begin()) == 5);
    static_assert(ranges::distance(v.begin(), i2.begin()) == 5);
  }
  {
    auto pred = [](int x) { return x == -3; };
    constexpr auto i1 = ranges::find_last_if(v.begin(), v.end(), pred, abs);
    constexpr auto i2 = ranges::find_last_if(v, pred, abs);
    static_assert(i1.begin() == v.end());
    static_assert(i2.begin() == v.end());
  }
  {
    auto pred = [](int x) { return x == 1 or x == 2; };
    constexpr auto i1 =
        ranges::find_last_if_not(v.begin(), v.end(), pred, abs);
    constexpr auto i2 = ranges::find_last_if_not(v, pred, abs);
    static_assert(ranges::distance(v.begin(), i1.begin()) == 5);
    static_assert(ranges::distance(v.begin(), i2.begin()) == 5);
  }
  {
    auto pred = [](int x) { return x == 1 or x == 2 or x == 3; };
    constexpr auto i1 =
        ranges::find_last_if_not(v.begin(), v.end(), pred, abs);
    constexpr auto i2 = ranges::find_last_if_not(v, pred, abs);
    static_assert(i1.begin() == v.end());
    static_assert(i2.begin() == v.end());
  }
  using P = std::pair<std::string_view, int>;
  std::forward_list<P> list{
      {"one", 1}, {"two", 2}, {"three", 3},
      {"one", 4}, {"two", 5}, {"three", 6},
  };
  auto cmp_one = [](const std::string_view &s) { return s == "one"; };

  // find latest element that satisfy the comparator, and projecting pair::first
  const auto subrange = ranges::find_last_if(list, cmp_one, &P::first);
  std::cout << "The found element and the tail after it are:\n";
  for (P const &e : subrange) {
    std::cout << '{' << std::quoted(e.first) << ", " << e.second << "} ";
  }
  std::cout << '\n';
#if __cpp_lib_algorithm_default_value_type
  const auto i3 = ranges::find_last(list, {"three", 3}); // (2) C++26
#else
  const auto i3 = ranges::find_last(list, P{"three", 3}); // (2) C++23
#endif
  assert(i3.begin()->first == "three" && i3.begin()->second == 3);
}
#endif

//
// https://en.cppreference.com/w/cpp/algorithm/ranges/fold_left#Example
//
#if defined(__cpp_lib_ranges_fold)
void fold_left_example() {
  namespace ranges = std::ranges;

  std::vector v{1, 2, 3, 4, 5, 6, 7, 8};

  int sum = ranges::fold_left(v.begin(), v.end(), 0, std::plus<int>()); // (1)
  std::cout << "sum: " << sum << '\n';
  int mul = ranges::fold_left(v, 1, std::multiplies<int>()); // (2)
  std::cout << "mul: " << mul << '\n';

  // get the product of the std::pair::second of all pairs in the vector:
  std::vector<std::pair<char, float>> data{
      {'A', 2.f}, {'B', 3.f}, {'C', 3.5f}};
  float sec =
      ranges::fold_left(data | ranges::views::values, 2.0f, std::multiplies<>());
  std::cout << "sec: " << sec << '\n';
  // use a program defined function object (lambda-expression):
  std::string str = ranges::fold_left(
      v, "A", [](std::string s, int x) { return s + ':' + std::to_string(x); });
  std::cout << "str: " << str << '\n';
  using CD = std::complex<double>;
  std::vector<CD> nums{{1, 1}, {2, 0}, {3, 0}};
#ifdef __cpp_lib_algorithm_default_value_type
  auto res = ranges::fold_left(nums, {7, 0}, std::multiplies{}); // (2)
#else
  auto res = ranges::fold_left(nums, CD{7, 0}, std::multiplies{}); // (2)
#endif
  std::cout << "res: " << res << '\n';

  assert(sum == 36);
  assert(mul == 40320);
  assert(sec == 42.0f);
  assert(str == "A:1:2:3:4:5:6:7:8");
  assert(res == CD(42, 42));
}
#endif

void run() {
  int exercised = 0;

#if defined(__cpp_lib_ranges_contains)
  contains_example();
  ++exercised;
#endif
#if defined(__cpp_lib_ranges_starts_ends_with)
  starts_with_example();
  ends_with_example();
  ++exercised;
#endif
#if defined(__cpp_lib_ranges_find_last)
  find_last_example();
  ++exercised;
#endif
#if defined(__cpp_lib_ranges_fold)
  fold_left_example();
  ++exercised;
#endif

  if (exercised == 0) {
    std::cout << "C++23 ranges algorithm feature-test macros are not available\n";
  } else {
    std::cout << "C++23 ranges algorithm cppreference examples exercised "
              << exercised << " feature group(s)\n";
  }
}
} // namespace ranges_cxx23_algorithms_test

//
// https://en.cppreference.com/w/cpp/algorithm/merge#Example
//
namespace merge_test {
auto print = [](const auto rem, const auto &v) {
  std::cout << rem;
  std::copy(v.begin(), v.end(), std::ostream_iterator<int>(std::cout, " "));
  std::cout << '\n';
};

void run() {
  // fill the vectors with random numbers
  std::random_device rd;
  std::mt19937 mt(rd());
  std::uniform_int_distribution<> dis(0, 9);

  std::vector<int> v1(10), v2(10);
  std::generate(v1.begin(), v1.end(), std::bind(dis, std::ref(mt)));
  std::generate(v2.begin(), v2.end(), std::bind(dis, std::ref(mt)));

  print("Originally:\n v1: ", v1);
  print("v2: ", v2);

  std::sort(v1.begin(), v1.end());
  std::sort(v2.begin(), v2.end());

  print("After sorting:\n v1: ", v1);
  print("v2: ", v2);

  // merge
  std::vector<int> dst;
  std::merge(v1.begin(), v1.end(), v2.begin(), v2.end(),
             std::back_inserter(dst));
  print("After merging:\n dst: ", dst);
}
} // namespace merge_test

//
// https://en.cppreference.com/w/cpp/algorithm/make_heap#Example
//
namespace heap_test {
void print(std::string_view text, const std::vector<int> &v = {}) {
  std::cout << text << ": ";
  for (const auto &e : v) {
    std::cout << e << ' ';
  }
  std::cout << '\n';
}

void run() {
  print("Max heap");

  std::vector<int> v{3, 2, 4, 1, 5, 9};
  print("initially, v", v);

  std::make_heap(v.begin(), v.end());
  print("after make_heap, v", v);

  std::pop_heap(v.begin(), v.end());
  print("after pop_heap, v", v);

  auto top = v.back();
  v.pop_back();
  std::cout << "former top element: " << top << '\n';
  print("after removing the former top element, v", v);

  print("Min heap");

  std::vector<int> v1{3, 2, 4, 1, 5, 9};
  print("initially, v1", v1);

  std::make_heap(v1.begin(), v1.end(), std::greater<>{});
  print("after make_heap, v1", v1);

  std::pop_heap(v1.begin(), v1.end(), std::greater<>{});
  print("after pop_heap, v1", v1);

  auto top1 = v1.back();
  v1.pop_back();
  std::cout << "former top element: " << top1 << '\n';
  print("after removing the former top element, v1", v1);
}
} // namespace heap_test

//
// https://en.cppreference.com/w/cpp/algorithm/next_permutation#Example
//
namespace next_permutation_test {
void run() {
  std::string s = "ABBA";
  std::sort(s.begin(), s.end());

  do {
    std::cout << s << '\n';
  } while (std::next_permutation(s.begin(), s.end()));

  assert(std::is_sorted(s.begin(), s.end()));
}
} // namespace next_permutation_test

//
// https://en.cppreference.com/w/cpp/algorithm/accumulate#Example
//
namespace accumulate_test {
void run() {
  std::vector<int> v{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

  int sum = std::accumulate(v.begin(), v.end(), 0);
  int product =
      std::accumulate(v.begin(), v.end(), 1, std::multiplies<int>());

  auto dash_fold = [](std::string a, int b) {
    return std::move(a) + '-' + std::to_string(b);
  };

  std::string s = std::accumulate(std::next(v.begin()), v.end(),
                                  std::to_string(v[0]), // start with first element
                                  dash_fold);

  // Right fold using reverse iterators
  std::string rs =
      std::accumulate(std::next(v.rbegin()), v.rend(),
                      std::to_string(v.back()), // start with last element
                      dash_fold);

  std::cout << "sum: " << sum << '\n'
            << "product: " << product << '\n'
            << "dash-separated string: " << s << '\n'
            << "dash-separated string (right-folded): " << rs << '\n';
}
} // namespace accumulate_test

//
// https://en.cppreference.com/w/cpp/algorithm/iota#Example
//
namespace iota_test {
class BigData { // inefficient to copy
  int data[1024]; /* some raw data */

public:
  explicit BigData(int i = 0) { data[0] = i; /* ... */ }
  operator int() const { return data[0]; }
  BigData &operator=(int i) {
    data[0] = i;
    return *this;
  }
  /* ... */
};

void run() {
  std::list<BigData> l(10);
  std::iota(l.begin(), l.end(), -4);
  std::vector<std::list<BigData>::iterator> v(l.size());
  std::iota(v.begin(), v.end(), l.begin());
  // Vector of iterators (to original data) is used to avoid expensive copying,
  // and because std::shuffle (below) cannot be applied to a std::list directly.

  std::shuffle(v.begin(), v.end(), std::mt19937{std::random_device{}()});
  std::cout << "Original contents of the list l:\t";
  for (const auto &n : l) {
    std::cout << std::setw(2) << n << ' ';
  }
  std::cout << '\n';

  std::cout << "Contents of l, viewed via shuffled v:\t";
  for (const auto i : v) {
    std::cout << std::setw(2) << *i << ' ';
  }
  std::cout << '\n';
}
} // namespace iota_test

//
// https://en.cppreference.com/w/cpp/algorithm/partial_sum#Example
//
namespace partial_sum_test {
void run() {
  std::vector<int> v(10, 2); // v = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2}

  std::cout << "The first " << v.size() << " even numbers are: ";
  // write the result to the cout stream
  std::partial_sum(v.cbegin(), v.cend(),
                   std::ostream_iterator<int>(std::cout, " "));
  std::cout << '\n';

  // write the result back to the vector v
  std::partial_sum(v.cbegin(), v.cend(), v.begin(), std::multiplies<int>());

  std::cout << "The first " << v.size() << " powers of 2 are: ";
  for (int n : v) {
    std::cout << n << ' ';
  }
  std::cout << '\n';
}
} // namespace partial_sum_test

//
// https://en.cppreference.com/w/cpp/numeric/gcd#Example
//
namespace gcd_test {
void run() {
  constexpr int p{2 * 2 * 3};
  constexpr int q{2 * 3 * 3};
  static_assert(2 * 3 == std::gcd(p, q));

  static_assert(std::gcd(6, 10) == 2);
  static_assert(std::gcd(6, -10) == 2);
  static_assert(std::gcd(-6, -10) == 2);

  static_assert(std::gcd(24, 0) == 24);
  static_assert(std::gcd(-24, 0) == 24);

  std::cout << "std::gcd static assertions passed\n";
}
} // namespace gcd_test

//
// https://en.cppreference.com/w/cpp/numeric/midpoint#Example
//
namespace midpoint_test {
void run() {
  std::uint32_t a = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t b = std::numeric_limits<std::uint32_t>::max() - 2;
  std::cout << "a: " << a << '\n'
            << "b: " << b << '\n'
            << "Incorrect (overflow and wrapping): " << (a + b) / 2 << '\n'
            << "Correct: " << std::midpoint(a, b) << "\n\n";
  auto on_pointers = [](int i, int j) {
    char const *text = "0123456789";
    char const *p = text + i;
    char const *q = text + j;
    std::cout << "std::midpoint('" << *p << "', '" << *q << "'): '"
              << *std::midpoint(p, q) << "'\n";
  };

  on_pointers(2, 4);
  on_pointers(2, 5);
  on_pointers(5, 2);
  on_pointers(2, 6);
}
} // namespace midpoint_test

//
// https://en.cppreference.com/w/cpp/numeric/lcm#Example
//
namespace lcm_test {
constexpr auto lcm(auto x, auto... xs) {
  return ((x = std::lcm(x, xs)), ...);
}

void run() {
  constexpr int p{2 * 2 * 3};
  constexpr int q{2 * 3 * 3};
  static_assert(2 * 2 * 3 * 3 == std::lcm(p, q));
  static_assert(225 == std::lcm(45, 75));

  static_assert(std::lcm(6, 10) == 30);
  static_assert(std::lcm(6, -10) == 30);
  static_assert(std::lcm(-6, -10) == 30);

  static_assert(std::lcm(24, 0) == 0);
  static_assert(std::lcm(-24, 0) == 0);

  std::cout << "lcm(2 * 3, 3 * 4, 4 * 5) = "
            << lcm(2 * 3, 3 * 4, 4 * 5) << '\n';
  std::cout << "lcm(2 * 3 * 4, 3 * 4 * 5, 4 * 5 * 6) = "
            << lcm(2 * 3 * 4, 3 * 4 * 5, 4 * 5 * 6) << '\n';
  std::cout << "lcm(2 * 3 * 4, 3 * 4 * 5, 4 * 5 * 6, 5 * 6 * 7) = "
            << lcm(2 * 3 * 4, 3 * 4 * 5, 4 * 5 * 6, 5 * 6 * 7) << '\n';
}
} // namespace lcm_test

//
// https://en.cppreference.com/w/cpp/algorithm/inner_product#Example
//
namespace inner_product_test {
void run() {
  std::vector<int> a{0, 1, 2, 3, 4};
  std::vector<int> b{5, 4, 2, 3, 1};

  int r1 = std::inner_product(a.begin(), a.end(), b.begin(), 0);
  std::cout << "Inner product of a and b: " << r1 << '\n';

  int r2 = std::inner_product(a.begin(), a.end(), b.begin(), 0,
                              std::plus<>(), std::equal_to<>());
  std::cout << "Number of pairwise matches between a and b: " << r2 << '\n';
}
} // namespace inner_product_test

//
// https://en.cppreference.com/w/cpp/algorithm/adjacent_difference#Example
//
namespace adjacent_difference_test {
void println(auto comment, const auto &sequence) {
  std::cout << comment;
  for (const auto &n : sequence) {
    std::cout << n << ' ';
  }
  std::cout << '\n';
}

void run() {
  // Default implementation: the difference between two adjacent items.
  std::vector v{4, 6, 9, 13, 18, 19, 19, 15, 10};
  println("Initially, v = ", v);
  std::adjacent_difference(v.begin(), v.end(), v.begin());
  println("Modified v = ", v);

  // Fibonacci.
  std::array<int, 10> a{1};
  std::adjacent_difference(a.begin(), std::prev(a.end()), std::next(a.begin()),
                           std::plus<>{});
  println("Fibonacci, a = ", a);
}
} // namespace adjacent_difference_test

//
// https://en.cppreference.com/w/cpp/algorithm/inclusive_scan#Example
//
namespace inclusive_scan_test {
void run() {
  std::vector data{3, 1, 4, 1, 5, 9, 2, 6};

  std::cout << "Exclusive sum: ";
  std::exclusive_scan(data.begin(), data.end(),
                      std::ostream_iterator<int>(std::cout, " "), 0);

  std::cout << "\nInclusive sum: ";
  std::inclusive_scan(data.begin(), data.end(),
                      std::ostream_iterator<int>(std::cout, " "));

  std::cout << "\n\nExclusive product: ";
  std::exclusive_scan(data.begin(), data.end(),
                      std::ostream_iterator<int>(std::cout, " "), 1,
                      std::multiplies<>{});

  std::cout << "\nInclusive product: ";
  std::inclusive_scan(data.begin(), data.end(),
                      std::ostream_iterator<int>(std::cout, " "),
                      std::multiplies<>{});
  std::cout << '\n';
}
} // namespace inclusive_scan_test

//
// https://en.cppreference.com/w/cpp/algorithm/reduce#Example
// https://en.cppreference.com/w/cpp/algorithm/transform_reduce#Example
// https://en.cppreference.com/w/cpp/algorithm/exclusive_scan#Example
// https://en.cppreference.com/w/cpp/algorithm/transform_inclusive_scan#Example
// https://en.cppreference.com/w/cpp/algorithm/transform_exclusive_scan#Example
//
namespace numeric_reduce_scan_test {
void run() {
  // The linked numeric pages have separate standalone examples. This compact
  // form keeps the same reduce/scan operations and verifies the exact results.
  const std::vector<int> data{1, 2, 3, 4, 5};

  const int sum = std::reduce(data.begin(), data.end());
  const int product =
      std::reduce(data.begin(), data.end(), 1, std::multiplies<>{});
  const int square_sum =
      std::transform_reduce(data.begin(), data.end(), 0, std::plus<>{},
                            [](int value) { return value * value; });

  assert(sum == 15);
  assert(product == 120);
  assert(square_sum == 55);

  std::vector<int> exclusive(data.size());
  std::exclusive_scan(data.begin(), data.end(), exclusive.begin(), 0);
  assert((exclusive == std::vector{0, 1, 3, 6, 10}));

  std::vector<int> transformed_inclusive(data.size());
  std::transform_inclusive_scan(data.begin(), data.end(),
                                transformed_inclusive.begin(), std::plus<>{},
                                [](int value) { return value * 2; });
  assert((transformed_inclusive == std::vector{2, 6, 12, 20, 30}));

  std::vector<int> transformed_exclusive(data.size());
  std::transform_exclusive_scan(data.begin(), data.end(),
                                transformed_exclusive.begin(), 0,
                                std::plus<>{},
                                [](int value) { return value * 2; });
  assert((transformed_exclusive == std::vector{0, 2, 6, 12, 20}));

  std::cout << "numeric reduce/scan assertions passed\n";
}
} // namespace numeric_reduce_scan_test

//
// https://en.cppreference.com/w/cpp/numeric/lerp#Example
//
namespace lerp_test {
float naive_lerp(float a, float b, float t) { return a + t * (b - a); }

void run() {
  std::cout << std::boolalpha;

  const float a = 1e8f, b = 1.0f;
  const float midpoint = std::lerp(a, b, 0.5f);

  std::cout << "a = " << a << ", " << "b = " << b << '\n'
            << "midpoint = " << midpoint << '\n';
  std::cout << "std::lerp is exact: "
            << (a == std::lerp(a, b, 0.0f)) << ' '
            << (b == std::lerp(a, b, 1.0f)) << '\n';

  std::cout << "naive_lerp is exact: "
            << (a == naive_lerp(a, b, 0.0f)) << ' '
            << (b == naive_lerp(a, b, 1.0f)) << '\n';

  std::cout << "std::lerp(a, b, 1.0f) = " << std::lerp(a, b, 1.0f) << '\n'
            << "naive_lerp(a, b, 1.0f) = " << naive_lerp(a, b, 1.0f)
            << '\n';
  assert(not std::isnan(std::lerp(a, b, INFINITY))); // lerp here can be -inf

  std::cout << "Extrapolation demo, given std::lerp(5, 10, t):\n";
  for (auto t{-2.0}; t <= 2.0; t += 0.5) {
    std::cout << std::lerp(5.0, 10.0, t) << ' ';
  }
  std::cout << '\n';
}
} // namespace lerp_test

//
// https://en.cppreference.com/w/cpp/utility/bitset#Example
//
namespace bitset_test {
void run() {
  typedef std::size_t length_t, position_t; // the hints

  // constructors:
  constexpr std::bitset<4> b1;
  constexpr std::bitset<4> b2{0xA}; // == 0B1010
  std::bitset<4> b3{"0011"};       // can also be constexpr since C++23
  std::bitset<8> b4{"ABBA", length_t(4), /*0:*/ 'A',
                    /*1:*/ 'B'}; // == 0B0000'0110

  // bitsets can be printed out to a stream:
  std::cout << "b1:" << b1 << "; b2:" << b2 << "; b3:" << b3
            << "; b4:" << b4 << '\n';

  // bitset supports bitwise operations:
  b3 |= 0b0100;
  assert(b3 == 0b0111);
  b3 &= 0b0011;
  assert(b3 == 0b0011);
  b3 ^= std::bitset<4>{0b1100};
  assert(b3 == 0b1111);

  // operations on the whole set:
  b3.reset();
  assert(b3 == 0);
  b3.set();
  assert(b3 == 0b1111);
  assert(b3.all() && b3.any() && !b3.none());
  b3.flip();
  assert(b3 == 0);

  // operations on individual bits:
  b3.set(position_t(1), true);
  assert(b3 == 0b0010);
  b3.set(position_t(1), false);
  assert(b3 == 0);
  b3.flip(position_t(2));
  assert(b3 == 0b0100);
  b3.reset(position_t(2));
  assert(b3 == 0);

  // subscript operator[] is supported:
  b3[2] = true;
  assert(true == b3[2]);

  // conversions:
  assert(b3.to_ulong() == 0b0100);
  assert(b3.to_ullong() == 0b0100);
  std::cout << "bitset assertions passed\n";
}
} // namespace bitset_test

//
// https://en.cppreference.com/w/cpp/numeric/popcount#Example
//
namespace popcount_test {
void run() {
  static_assert(std::popcount(0xFULL) == 4);

  // Keep cppreference's braced initializer-list. MSVC reports narrowing
  // conversion warnings for the std::uint8_t range variable under /W4 /WX.
#pragma warning(push)
#pragma warning(disable : 4242 4244 4838)
  for (const std::uint8_t x : {0, 0b00011101, 0b11111111}) {
    std::cout << "popcount( " << std::bitset<8>(x) << " ) = "
              << std::popcount(x) << '\n';
  }
#pragma warning(pop)

  static_assert(std::has_single_bit(0b1000U));
  static_assert(std::bit_width(0b1000U) == 4);
  static_assert(std::bit_floor(0b1010U) == 0b1000U);
  static_assert(std::bit_ceil(0b1001U) == 0b10000U);
}
} // namespace popcount_test

//
// https://en.cppreference.com/w/cpp/numeric/rotl#Example
//
namespace rotl_test {
void run() {
  using bin = std::bitset<8>;
  const std::uint8_t x{0b00011101};
  std::cout << bin(x) << " <- x\n";
  for (const int s : {0, 1, 4, 9, -1}) {
    std::cout << bin(std::rotl(x, s)) << " <- rotl(x, " << s << ")\n";
  }
}
} // namespace rotl_test

//
// https://en.cppreference.com/w/cpp/numeric/rotr#Example
//
namespace rotr_test {
void run() {
  using bin = std::bitset<8>;
  const std::uint8_t x{0b00011101};
  std::cout << bin(x) << " <- x\n";
  for (const int s : {0, 1, 9, -1, 2}) {
    std::cout << bin(std::rotr(x, s)) << " <- rotr(x, " << s << ")\n";
  }
}
} // namespace rotr_test

//
// https://en.cppreference.com/w/cpp/numeric/countl_zero#Example
//
namespace countl_zero_test {
void run() {
  // Keep cppreference's braced initializer-list. MSVC reports narrowing
  // conversion warnings for the std::uint8_t range variable under /W4 /WX.
#pragma warning(push)
#pragma warning(disable : 4242 4244 4838)
  for (const std::uint8_t i : {0, 0b11111111, 0b11110000, 0b00011110}) {
    std::cout << "countl_zero( " << std::bitset<8>(i) << " ) = "
              << std::countl_zero(i) << '\n';
  }
#pragma warning(pop)
}
} // namespace countl_zero_test

//
// https://en.cppreference.com/w/cpp/numeric/countr_zero#Example
//
namespace countr_zero_test {
void run() {
  // Keep cppreference's braced initializer-list. MSVC reports narrowing
  // conversion warnings for the std::uint8_t range variable under /W4 /WX.
#pragma warning(push)
#pragma warning(disable : 4242 4244 4838)
  for (const std::uint8_t i : {0, 0b11111111, 0b00011100, 0b00011101}) {
    std::cout << "countr_zero( " << std::bitset<8>(i) << " ) = "
              << std::countr_zero(i) << '\n';
  }
#pragma warning(pop)
}
} // namespace countr_zero_test

//
// https://en.cppreference.com/w/cpp/numeric/has_single_bit#Example
//
namespace has_single_bit_test {
void run() {
  for (auto u{0u}; u != 0B1010; ++u) {
    std::cout << "u = " << u << " = " << std::bitset<4>(u);
    if (std::has_single_bit(u)) {
      std::cout << " = 2^" << std::log2(u) << " (is power of two)";
    }
    std::cout << '\n';
  }
}
} // namespace has_single_bit_test

//
// https://en.cppreference.com/w/cpp/numeric/bit_ceil#Example
//
namespace bit_ceil_test {
void run() {
  using bin = std::bitset<8>;
  for (auto x{0U}; 0XA != x; ++x) {
    std::cout << "bit_ceil( " << bin(x) << " ) = "
              << bin(std::bit_ceil(x)) << '\n';
  }
}
} // namespace bit_ceil_test

//
// https://en.cppreference.com/w/cpp/numeric/bit_floor#Example
//
namespace bit_floor_test {
void run() {
  using bin = std::bitset<8>;
  for (unsigned x{}; x != 012; ++x) {
    std::cout << "bit_floor( " << bin(x) << " ) = "
              << bin(std::bit_floor(x)) << '\n';
  }
}
} // namespace bit_floor_test

//
// https://en.cppreference.com/w/cpp/numeric/bit_width#Example
//
namespace bit_width_test {
void run() {
  for (unsigned x{}; x != 010; ++x) {
    std::cout << "bit_width( " << std::bitset<4>{x}
              << " ) = " << std::bit_width(x) << '\n';
  }
}
} // namespace bit_width_test

//
// https://en.cppreference.com/w/cpp/numeric/bit_cast#Example
//
namespace bit_cast_test {
void run() {
  constexpr double f64v = 19880124.0;
  constexpr auto u64v = std::bit_cast<std::uint64_t>(f64v);
  static_assert(std::bit_cast<double>(u64v) == f64v); // round-trip

  constexpr std::uint64_t u64v2 = 0x3fe9000000000000ull;
  constexpr auto f64v2 = std::bit_cast<double>(u64v2);
  static_assert(std::bit_cast<std::uint64_t>(f64v2) == u64v2); // round-trip

  std::cout << "std::bit_cast<std::uint64_t>(" << std::fixed << f64v
            << ") == 0x" << std::hex << u64v << '\n'
            << "std::bit_cast<double>(0x" << std::hex << u64v2 << ") == "
            << std::fixed << f64v2 << '\n';
  std::cout << std::dec << std::defaultfloat;
}
} // namespace bit_cast_test

//
// https://en.cppreference.com/w/cpp/types/endian#Example
//
namespace endian_test {
void run() {
  if constexpr (std::endian::native == std::endian::big) {
    std::cout << "big-endian\n";
  } else if constexpr (std::endian::native == std::endian::little) {
    std::cout << "little-endian\n";
  } else {
    std::cout << "mixed-endian\n";
  }
}
} // namespace endian_test

//
// https://en.cppreference.com/w/cpp/numeric/byteswap#Example
//
namespace byteswap_test {
template <std::integral T> void dump(T v, char term = '\n') {
  std::cout << std::hex << std::uppercase << std::setfill('0')
            << std::setw(sizeof(T) * 2) << v << " : ";
  for (std::size_t i{}; i != sizeof(T); ++i, v >>= 8) {
    std::cout << std::setw(2) << static_cast<unsigned>(T(0xFF) & v) << ' ';
  }
  std::cout << std::dec << term;
}

void run() {
#if defined(__cpp_lib_byteswap) && __cpp_lib_byteswap >= 202110L
  static_assert(std::byteswap('a') == 'a');
  std::cout << "byteswap for U16:\n";
  constexpr auto x = std::uint16_t(0xCAFE);
  dump(x);
  dump(std::byteswap(x));
  static_assert(std::byteswap(x) == std::uint16_t(0xFECA));

  std::cout << "\n byteswap for U32:\n";
  constexpr auto y = std::uint32_t(0xDEADBEEFu);
  dump(y);
  dump(std::byteswap(y));
  static_assert(std::byteswap(y) == std::uint32_t(0xEFBEADDEu));

  std::cout << "\n byteswap for U64:\n";
  constexpr auto z = std::uint64_t{0x0123456789ABCDEFull};
  dump(z);
  dump(std::byteswap(z));
  static_assert(std::byteswap(z) == std::uint64_t{0xEFCDAB8967452301ull});
#else
  std::cout << "std::byteswap is not available in this STL\n";
#endif
  std::cout << std::nouppercase << std::setfill(' ');
}
} // namespace byteswap_test

//
// https://en.cppreference.com/w/cpp/utility/to_chars#Example
//
namespace to_chars_test {
void show_to_chars(auto... format_args) {
  const size_t buf_size = 10;
  char buf[buf_size]{};
  std::to_chars_result result =
      std::to_chars(buf, buf + buf_size, format_args...);

  if (result.ec != std::errc()) {
    std::cout << std::make_error_code(result.ec).message() << '\n';
  } else {
    std::string_view str(buf, result.ptr - buf);
    std::cout << std::quoted(str) << '\n';
  }
}

void run() {
  show_to_chars(42);
  show_to_chars(+3.14159F);
  show_to_chars(-3.14159, std::chars_format::fixed);
  show_to_chars(-3.14159, std::chars_format::scientific, 3);
  show_to_chars(3.1415926535, std::chars_format::fixed, 10);
}
} // namespace to_chars_test

//
// https://en.cppreference.com/w/cpp/utility/from_chars#Example
//
namespace from_chars_test {
void run_floating_point_overloads() {
  struct test_case {
    std::string_view text;
    std::chars_format format;
    double lower;
    double upper;
    std::string_view rest;
  };

  //
  // The cppreference example above is integer-focused; this block checks the
  // floating-point overloads described on the same page.
  //
  for (const auto &test : {test_case{"3.14159", std::chars_format::general,
                                     3.14158, 3.14160, ""},
                           test_case{"-3.142e+00",
                                     std::chars_format::scientific, -3.143,
                                     -3.141, ""},
                           test_case{"1.25foo", std::chars_format::general,
                                     1.24, 1.26, "foo"},
                           test_case{"1.25e2", std::chars_format::fixed,
                                     1.24, 1.26, "e2"},
                           test_case{"0x123", std::chars_format::hex, -0.1,
                                     0.1, "x123"}}) {
    double result{};
    const auto [ptr, ec] = std::from_chars(
        test.text.data(), test.text.data() + test.text.size(), result,
        test.format);
    // Release builds compile out assert(), so mark assert-only locals as used.
    (void)ptr;
    (void)ec;

    assert(ec == std::errc());
    assert(result > test.lower);
    assert(result < test.upper);
    assert(std::string_view(ptr, test.text.data() + test.text.size() - ptr) ==
           test.rest);
  }

  double unchanged = 7.0;
  const std::string_view invalid = "foo";
  const auto [ptr, ec] =
      std::from_chars(invalid.data(), invalid.data() + invalid.size(),
                      unchanged, std::chars_format::general);
  // Release builds compile out assert(), so mark assert-only locals as used.
  (void)ptr;
  (void)ec;
  assert(ec == std::errc::invalid_argument);
  assert(ptr == invalid.data());
  assert(unchanged == 7.0);

  std::cout << "floating-point from_chars assertions passed\n";
}

void run() {
  for (std::string_view const str :
       {"1234", "15 foo", "bar", " 42", "5000000000"}) {
    std::cout << "String: " << std::quoted(str) << ". ";
    int result{};
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(),
                                     result);

    if (ec == std::errc()) {
      std::cout << "Result: " << result << ", ptr -> " << std::quoted(ptr)
                << '\n';
    } else if (ec == std::errc::invalid_argument) {
      std::cout << "This is not a number.\n";
    } else if (ec == std::errc::result_out_of_range) {
      std::cout << "This number is larger than an int.\n";
    }
  }

  auto to_int = [](std::string_view s) -> std::optional<int> {
    int value{};
#if __cpp_lib_to_chars >= 202306L
    if (std::from_chars(s.data(), s.data() + s.size(), value))
#else
    if (std::from_chars(s.data(), s.data() + s.size(), value).ec ==
        std::errc{})
#endif
      return value;
    else
      return std::nullopt;
  };
  assert(to_int("42") == 42);
  assert(to_int("foo") == std::nullopt);
#if __cpp_lib_constexpr_charconv and __cpp_lib_optional >= 202106
  static_assert(to_int("42") == 42);
  static_assert(to_int("foo") == std::nullopt);
#endif

  run_floating_point_overloads();
}
} // namespace from_chars_test
