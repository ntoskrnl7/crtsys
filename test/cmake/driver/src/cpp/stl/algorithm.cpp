//
// https://en.cppreference.com/w/cpp/algorithm/sort#Example
//
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
#include <numeric>
#include <random>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
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
  // Fill the vectors with deterministic pseudo-random numbers. The
  // cppreference example uses std::random_device to seed the engine.
  std::mt19937 mt{};
  std::uniform_int_distribution<> dis(0, 9);

  std::vector<int> v1(10), v2(10);
  std::generate(v1.begin(), v1.end(), std::bind(dis, std::ref(mt)));
  std::generate(v2.begin(), v2.end(), std::bind(dis, std::ref(mt)));

  print("Originally:\nv1: ", v1);
  print("v2: ", v2);

  std::sort(v1.begin(), v1.end());
  std::sort(v2.begin(), v2.end());

  print("After sorting:\nv1: ", v1);
  print("v2: ", v2);

  // Merge.
  std::vector<int> dst;
  std::merge(v1.begin(), v1.end(), v2.begin(), v2.end(),
             std::back_inserter(dst));
  print("After merging:\ndst: ", dst);
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

#if defined(CRTSYS_ENABLE_EXACT_CPPREFERENCE_RANDOM_DEVICE_EXAMPLE)
  std::shuffle(v.begin(), v.end(), std::mt19937{std::random_device{}()});
#else
  // The cppreference example seeds from std::random_device. Kernel tests use a
  // fixed seed to avoid depending on hosted entropy APIs.
  std::shuffle(v.begin(), v.end(), std::mt19937{});
#endif
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
// https://en.cppreference.com/w/cpp/numeric/lerp#Example
//
#if defined(CRTSYS_ENABLE_UNSUPPORTED_LERP_TEST)
namespace lerp_test {
float naive_lerp(float a, float b, float t) { return a + t * (b - a); }

void run() {
  std::cout << std::boolalpha;

  const float a = 1e8f, b = 1.0f;
  const float midpoint = std::lerp(a, b, 0.5f);

  std::cout << "a = " << a << ", " << "b = " << b << '\n'
            << "midpoint = " << midpoint << '\n';

  assert(std::lerp(a, b, 0.0f) == a);
  assert(std::lerp(a, b, 1.0f) == b);
  std::cout << "std::lerp exact endpoint checks passed\n";

  const float naive_midpoint = naive_lerp(a, b, 0.5f);
  std::cout << "std::lerp midpoint differs from naive midpoint: "
            << (midpoint != naive_midpoint) << '\n';
}
} // namespace lerp_test
#endif

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

  for (const std::uint8_t x :
       std::array<std::uint8_t, 3>{0, 0b00011101, 0b11111111}) {
    std::cout << "popcount( " << std::bitset<8>(x) << " ) = "
              << std::popcount(x) << '\n';
  }

  static_assert(std::has_single_bit(0b1000U));
  static_assert(std::bit_width(0b1000U) == 4);
  static_assert(std::bit_floor(0b1010U) == 0b1000U);
  static_assert(std::bit_ceil(0b1001U) == 0b10000U);
}
} // namespace popcount_test

//
// https://en.cppreference.com/w/cpp/utility/to_chars#Example
//
namespace to_chars_test {
void show_to_chars(auto... format_args) {
  const size_t buf_size = 32;
  char buf[buf_size]{};
  std::to_chars_result result =
      std::to_chars(buf, buf + buf_size, format_args...);

  if (result.ec == std::errc()) {
    std::cout << std::string_view(buf, result.ptr) << '\n';
  } else {
    std::cout << std::make_error_code(result.ec).message() << '\n';
  }
}

void run() {
  show_to_chars(42);
  show_to_chars(+1234567890);
  show_to_chars(-1234567890);
}
} // namespace to_chars_test

//
// https://en.cppreference.com/w/cpp/utility/from_chars#Example
//
namespace from_chars_test {
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
}
} // namespace from_chars_test
