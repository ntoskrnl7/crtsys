//
// https://en.cppreference.com/w/cpp/container/array#Example
//
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <deque>
#include <forward_list>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <span>
#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace container_test_detail {
void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <class Container, class Expected>
void expect_equal(const Container &actual, const Expected &expected,
                  const char *message) {
  if (!(actual == expected)) {
    throw std::runtime_error(message);
  }
}
} // namespace container_test_detail

namespace array_test {
void run() {
  // Construction uses aggregate initialization
  std::array<int, 3> a1{{1, 2, 3}}; // Double-braces required in C++11 prior to
                                    // the CWG 1270 revision (not needed in
                                    // C++11 after the revision and in C++14
                                    // and beyond)
  std::array<int, 3> a2 = {1, 2, 3}; // Double braces never required after =

  // Container operations are supported
  std::sort(a1.begin(), a1.end());
  std::ranges::reverse_copy(a2, std::ostream_iterator<int>(std::cout, " "));
  std::cout << '\n';

  // Ranged for loop is supported
  std::array<std::string, 2> a3{"E", "\xC6\x8E"};
  for (const auto &s : a3) {
    std::cout << s << ' ';
  }
  std::cout << '\n';

  // Deduction guide for array creation (since C++17)
  [[maybe_unused]] std::array a4{3.0, 1.0, 4.0}; // std::array<double, 3>

  // Behavior of unspecified elements is the same as with built-in arrays
  [[maybe_unused]] std::array<int, 2> a5; // No list init, a5[0] and a5[1]
                                          // are default initialized
  [[maybe_unused]] std::array<int, 2> a6{}; // List init, both elements are
                                            // value initialized,
                                            // a6[0] = a6[1] = 0
  [[maybe_unused]] std::array<int, 2> a7{1}; // List init, unspecified element
                                             // is value initialized,
                                             // a7[0] = 1, a7[1] = 0
}
} // namespace array_test

//
// https://en.cppreference.com/w/cpp/container/vector#Example
//
namespace vector_test {
void run() {
  // Create a vector containing integers
  std::vector<int> v = {8, 4, 5, 9};

  // Add two more integers to vector
  v.push_back(6);
  v.push_back(9);

  // Overwrite element at position 2
  v[2] = -1;

  // Print out the vector
  for (int n : v) {
    std::cout << n << ' ';
  }
  std::cout << '\n';
}
} // namespace vector_test

//
// https://en.cppreference.com/w/cpp/container/vector/emplace#Example
// https://en.cppreference.com/w/cpp/container/vector/erase#Example
//
namespace vector_member_operations_test {
struct A {
  std::string s;

  explicit A(std::string str) : s(std::move(str)) {}
  A(const A &) = default;
  A(A &&) noexcept = default;
  A &operator=(const A &) = default;
  A &operator=(A &&) noexcept = default;
};

void run() {
  // These two cppreference member-function examples are folded into one
  // assert-oriented driver test to keep KD output small; the emplace copy/move
  // and erase single/range semantics are kept.
  std::vector<A> container;
  container.reserve(10);

  A two{"two"};
  A three{"three"};

  container.emplace(container.end(), "one");
  container.emplace(container.end(), two);
  container.emplace(container.end(), std::move(three));

  assert(container.size() == 3);
  assert(container[0].s == "one");
  assert(container[1].s == "two");
  assert(container[2].s == "three");

  std::vector<int> values{0, 1, 2, 3, 4, 5, 6};
  auto it = values.erase(values.begin() + 1);
  assert(it != values.end());
  assert(*it == 2);

  it = values.erase(values.begin() + 2, values.begin() + 4);
  assert(it != values.end());
  assert(*it == 5);
  assert((values == std::vector<int>{0, 2, 5, 6}));
}
} // namespace vector_member_operations_test

//
// https://en.cppreference.com/w/cpp/container/deque#Example
//
namespace deque_test {
void run() {
  // Create a deque containing integers
  std::deque<int> d = {7, 5, 16, 8};

  // Add an integer to the beginning and end of the deque
  d.push_front(13);
  d.push_back(25);

  // Iterate and print values of deque
  for (int n : d) {
    std::cout << n << ' ';
  }
  std::cout << '\n';
}
} // namespace deque_test

//
// https://en.cppreference.com/w/cpp/container/deque/emplace#Example
// https://en.cppreference.com/w/cpp/container/deque/erase#Example
//
namespace deque_member_operations_test {
struct A {
  std::string s;

  explicit A(std::string str) : s(std::move(str)) {}
  A(const A &) = default;
  A(A &&) noexcept = default;
  A &operator=(const A &) = default;
  A &operator=(A &&) noexcept = default;
};

void run() {
  // Fold the cppreference output examples into assertive driver checks. The
  // covered operations and final observable container states are unchanged.
  std::deque<A> container;

  A two{"two"};
  A three{"three"};
  container.emplace(container.end(), "one");
  container.emplace(container.end(), two);
  container.emplace(container.end(), std::move(three));

  container_test_detail::expect(container.size() == 3,
                                "deque emplace size mismatch");
  container_test_detail::expect(container[0].s == "one",
                                "deque emplace first value mismatch");
  container_test_detail::expect(container[1].s == "two",
                                "deque emplace second value mismatch");
  container_test_detail::expect(container[2].s == "three",
                                "deque emplace third value mismatch");

  std::deque<int> c{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  c.erase(c.begin());
  container_test_detail::expect_equal(
      c, std::deque<int>{1, 2, 3, 4, 5, 6, 7, 8, 9},
      "deque erase first element mismatch");

  c.erase(c.begin() + 2, c.begin() + 5);
  container_test_detail::expect_equal(c, std::deque<int>{1, 2, 6, 7, 8, 9},
                                      "deque erase range mismatch");

  for (std::deque<int>::iterator it = c.begin(); it != c.end();) {
    if (*it % 2 == 0) {
      it = c.erase(it);
    } else {
      ++it;
    }
  }
  container_test_detail::expect_equal(c, std::deque<int>{1, 7, 9},
                                      "deque erase loop mismatch");
}
} // namespace deque_member_operations_test

//
// https://en.cppreference.com/w/cpp/container/list#Example
//
namespace list_test {
void run() {
  // Create a list containing integers
  std::list<int> l = {7, 5, 16, 8};

  // Add an integer to the front of the list
  l.push_front(25);
  // Add an integer to the back of the list
  l.push_back(13);
  // Insert an integer before 16 by searching
  auto it = std::find(l.begin(), l.end(), 16);
  if (it != l.end()) {
    l.insert(it, 42);
  }

  // Print out the list
  std::cout << "l = { ";
  for (int n : l) {
    std::cout << n << ", ";
  }
  std::cout << "};\n";
}
} // namespace list_test

//
// https://en.cppreference.com/w/cpp/container/list/emplace#Example
// https://en.cppreference.com/w/cpp/container/list/erase#Example
// https://en.cppreference.com/w/cpp/container/list/splice#Example
// https://en.cppreference.com/w/cpp/container/list/merge#Example
// https://en.cppreference.com/w/cpp/container/list/remove#Example
// https://en.cppreference.com/w/cpp/container/list/sort#Example
// https://en.cppreference.com/w/cpp/container/list/unique#Example
//
namespace list_member_operations_test {
struct A {
  std::string s;

  explicit A(std::string str) : s(std::move(str)) {}
  A(const A &) = default;
  A(A &&) noexcept = default;
  A &operator=(const A &) = default;
  A &operator=(A &&) noexcept = default;
};

void run() {
  // These member-function pages are separate cppreference programs. The driver
  // harness checks their observable final states directly instead of replaying
  // every cout line, so Release builds verify the same semantics too.
  std::list<A> container;
  A two{"two"};
  A three{"three"};
  container.emplace(container.end(), "one");
  container.emplace(container.end(), two);
  container.emplace(container.end(), std::move(three));

  auto emplaced = container.begin();
  container_test_detail::expect(emplaced->s == "one",
                                "list emplace first value mismatch");
  ++emplaced;
  container_test_detail::expect(emplaced->s == "two",
                                "list emplace second value mismatch");
  ++emplaced;
  container_test_detail::expect(emplaced->s == "three",
                                "list emplace third value mismatch");

  std::list<int> c{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  c.erase(c.begin());
  container_test_detail::expect_equal(
      c, std::list<int>{1, 2, 3, 4, 5, 6, 7, 8, 9},
      "list erase first element mismatch");

  std::list<int>::iterator range_begin = c.begin();
  std::list<int>::iterator range_end = c.begin();
  std::advance(range_begin, 2);
  std::advance(range_end, 5);
  c.erase(range_begin, range_end);
  container_test_detail::expect_equal(c, std::list<int>{1, 2, 6, 7, 8, 9},
                                      "list erase range mismatch");

  for (std::list<int>::iterator it = c.begin(); it != c.end();) {
    if (*it % 2 == 0) {
      it = c.erase(it);
    } else {
      ++it;
    }
  }
  container_test_detail::expect_equal(c, std::list<int>{1, 7, 9},
                                      "list erase loop mismatch");

  std::list<int> list1{1, 2, 3, 4, 5};
  std::list<int> list2{10, 20, 30, 40, 50};
  auto it = list1.begin();
  std::advance(it, 2);
  list1.splice(it, list2);
  container_test_detail::expect_equal(
      list1, std::list<int>{1, 2, 10, 20, 30, 40, 50, 3, 4, 5},
      "list splice whole-list mismatch");
  container_test_detail::expect(list2.empty(),
                                "list splice source should be empty");

  list2.splice(list2.begin(), list1, it, list1.end());
  container_test_detail::expect_equal(
      list1, std::list<int>{1, 2, 10, 20, 30, 40, 50},
      "list splice range destination mismatch");
  container_test_detail::expect_equal(list2, std::list<int>{3, 4, 5},
                                      "list splice range source mismatch");

  list1 = {5, 9, 1, 3, 3};
  list2 = {8, 7, 2, 3, 4, 4};
  list1.sort();
  list2.sort();
  list1.merge(list2);
  container_test_detail::expect_equal(
      list1, std::list<int>{1, 2, 3, 3, 3, 4, 4, 5, 7, 8, 9},
      "list merge mismatch");
  container_test_detail::expect(list2.empty(),
                                "list merge source should be empty");

  std::list<int> removable = {1, 100, 2, 3, 10, 1, 11, -1, 12};
  auto count1 = removable.remove(1);
  auto count2 = removable.remove_if([](int n) { return n > 10; });
  container_test_detail::expect(count1 == 2,
                                "list remove count mismatch");
  container_test_detail::expect(count2 == 3,
                                "list remove_if count mismatch");
  container_test_detail::expect_equal(removable, std::list<int>{2, 3, 10, -1},
                                      "list remove final state mismatch");

  std::list<int> sortable{8, 7, 5, 9, 0, 1, 3, 2, 6, 4};
  sortable.sort();
  container_test_detail::expect_equal(
      sortable, std::list<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
      "list sort ascending mismatch");
  sortable.sort(std::greater<int>());
  container_test_detail::expect_equal(
      sortable, std::list<int>{9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
      "list sort descending mismatch");

  std::list<int> unique_values{1, 2, 2, 3, 3, 2, 1, 1, 2};
  const auto unique_count1 = unique_values.unique();
  container_test_detail::expect(unique_count1 == 3,
                                "list unique count mismatch");
  container_test_detail::expect_equal(
      unique_values, std::list<int>{1, 2, 3, 2, 1, 2},
      "list unique final state mismatch");

  unique_values = {1, 2, 12, 23, 3, 2, 51, 1, 2, 2};
  const auto unique_count2 = unique_values.unique(
      [mod = 10](int x, int y) { return (x % mod) == (y % mod); });
  container_test_detail::expect(unique_count2 == 4,
                                "list unique predicate count mismatch");
  container_test_detail::expect_equal(
      unique_values, std::list<int>{1, 2, 23, 2, 51, 2},
      "list unique predicate final state mismatch");
}
} // namespace list_member_operations_test

//
// https://en.cppreference.com/w/cpp/container/forward_list/insert_after#Example
//
namespace forward_list_insert_after_test {
void print(const std::forward_list<int> &list) {
  std::cout << "list: {";
  for (char comma[3] = {'\0', ' ', '\0'}; int i : list) {
    std::cout << comma << i;
    comma[0] = ',';
  }
  std::cout << "}\n";
}

void run() {
  std::forward_list<int> ints{1, 2, 3, 4, 5};
  print(ints);

  // insert_after (2)
  auto beginIt = ints.begin();
  ints.insert_after(beginIt, -6);
  print(ints);

  // insert_after (3)
  auto anotherIt = beginIt;
  ++anotherIt;
  anotherIt = ints.insert_after(anotherIt, 2, -7);
  print(ints);

  // insert_after (4)
  const std::vector<int> v = {-8, -9, -10};
  anotherIt = ints.insert_after(anotherIt, v.cbegin(), v.cend());
  print(ints);

  // insert_after (5)
  ints.insert_after(anotherIt, {-11, -12, -13, -14});
  print(ints);
}
} // namespace forward_list_insert_after_test

//
// https://en.cppreference.com/w/cpp/container/forward_list/erase_after#Example
// https://en.cppreference.com/w/cpp/container/forward_list/splice_after#Example
// https://en.cppreference.com/w/cpp/container/forward_list/merge#Example
// https://en.cppreference.com/w/cpp/container/forward_list/remove#Example
// https://en.cppreference.com/w/cpp/container/forward_list/sort#Example
// https://en.cppreference.com/w/cpp/container/forward_list/unique#Example
//
namespace forward_list_member_operations_test {
void run() {
  // The linked cppreference examples already use compact observable sequences.
  // This driver version verifies the same resulting lists with explicit
  // checks, avoiding dependence on KD log formatting.
  using F = std::forward_list<int>;

  F l = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  l.erase_after(l.before_begin());
  container_test_detail::expect_equal(l, F{2, 3, 4, 5, 6, 7, 8, 9},
                                      "forward_list erase_after first mismatch");

  auto fi = std::next(l.begin());
  auto la = std::next(fi, 3);
  l.erase_after(fi, la);
  container_test_detail::expect_equal(l, F{2, 3, 6, 7, 8, 9},
                                      "forward_list erase_after range mismatch");

  F l1 = {1, 2, 3, 4, 5};
  F l2 = {10, 11, 12};
  l2.splice_after(l2.cbegin(), l1, l1.cbegin(), l1.cend());
  container_test_detail::expect_equal(l1, F{1},
                                      "forward_list splice_after open source");
  container_test_detail::expect_equal(
      l2, F{10, 2, 3, 4, 5, 11, 12},
      "forward_list splice_after open destination");

  F x = {1, 2, 3, 4, 5};
  F y = {10, 11, 12};
  x.splice_after(x.cbegin(), y);
  container_test_detail::expect_equal(
      x, F{1, 10, 11, 12, 2, 3, 4, 5},
      "forward_list splice_after whole-list destination");
  container_test_detail::expect_equal(y, F{},
                                      "forward_list splice_after whole source");

  x = {1, 2, 3, 4, 5};
  y = {10, 11, 12};
  x.splice_after(x.cbegin(), y, y.cbegin());
  container_test_detail::expect_equal(
      x, F{1, 11, 2, 3, 4, 5},
      "forward_list splice_after single destination");
  container_test_detail::expect_equal(y, F{10, 12},
                                      "forward_list splice_after single source");

  F merge1 = {5, 9, 1, 3, 3};
  F merge2 = {8, 7, 2, 3, 4, 4};
  merge1.sort();
  merge2.sort();
  merge1.merge(merge2);
  container_test_detail::expect_equal(
      merge1, F{1, 2, 3, 3, 3, 4, 4, 5, 7, 8, 9},
      "forward_list merge mismatch");
  container_test_detail::expect(merge2.empty(),
                                "forward_list merge source should be empty");

  F removable = {1, 100, 2, 3, 10, 1, 11, -1, 12};
  auto count1 = removable.remove(1);
  auto count2 = removable.remove_if([](int n) { return n > 10; });
  container_test_detail::expect(count1 == 2,
                                "forward_list remove count mismatch");
  container_test_detail::expect(count2 == 3,
                                "forward_list remove_if count mismatch");
  container_test_detail::expect_equal(removable, F{2, 3, 10, -1},
                                      "forward_list remove final state");

  F sortable{8, 7, 5, 9, 0, 1, 3, 2, 6, 4};
  sortable.sort();
  container_test_detail::expect_equal(sortable, F{0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
                                      "forward_list sort ascending mismatch");
  sortable.sort(std::greater<int>());
  container_test_detail::expect_equal(sortable, F{9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
                                      "forward_list sort descending mismatch");

  F unique_values{1, 2, 2, 3, 3, 2, 1, 1, 2};
  const auto unique_count1 = unique_values.unique();
  container_test_detail::expect(unique_count1 == 3,
                                "forward_list unique count mismatch");
  container_test_detail::expect_equal(
      unique_values, F{1, 2, 3, 2, 1, 2},
      "forward_list unique final state mismatch");

  unique_values = {1, 2, 12, 23, 3, 2, 51, 1, 2, 2};
  const auto unique_count2 = unique_values.unique(
      [mod = 10](int x, int y) { return (x % mod) == (y % mod); });
  container_test_detail::expect(unique_count2 == 4,
                                "forward_list unique predicate count mismatch");
  container_test_detail::expect_equal(
      unique_values, F{1, 2, 23, 2, 51, 2},
      "forward_list unique predicate final state mismatch");
}
} // namespace forward_list_member_operations_test

//
// https://en.cppreference.com/w/cpp/container/span#Example
//
namespace span_test {
template <class T, std::size_t N>
[[nodiscard]] constexpr auto slide(std::span<T, N> s, std::size_t offset,
                                   std::size_t width) {
  return s.subspan(offset, offset + width <= s.size() ? width : 0U);
}

template <class T, std::size_t N, std::size_t M>
constexpr bool starts_with(std::span<T, N> data, std::span<T, M> prefix) {
  return data.size() >= prefix.size() &&
         std::equal(prefix.begin(), prefix.end(), data.begin());
}

template <class T, std::size_t N, std::size_t M>
constexpr bool ends_with(std::span<T, N> data, std::span<T, M> suffix) {
  return data.size() >= suffix.size() &&
         std::equal(data.end() - suffix.size(), data.end(),
                    suffix.end() - suffix.size());
}

template <class T, std::size_t N, std::size_t M>
constexpr bool contains(std::span<T, N> span, std::span<T, M> sub) {
  return std::ranges::search(span, sub).begin() != span.end();
}

void println(const auto &seq) {
  for (const auto &elem : seq) {
    std::cout << elem << ' ';
  }
  std::cout << '\n';
}

void run() {
  constexpr int a[]{0, 1, 2, 3, 4, 5, 6, 7, 8};
  constexpr int b[]{8, 7, 6};
  constexpr static std::size_t width{6};

  for (std::size_t offset{};; ++offset) {
    if (auto s = slide(std::span{a}, offset, width); !s.empty()) {
      println(s);
    } else {
      break;
    }
  }

  static_assert("" && starts_with(std::span{a}, std::span{a, 4}) &&
                starts_with(std::span{a + 1, 4}, std::span{a + 1, 3}) &&
                !starts_with(std::span{a}, std::span{b}) &&
                !starts_with(std::span{a, 8}, std::span{a + 1, 3}) &&
                ends_with(std::span{a}, std::span{a + 6, 3}) &&
                !ends_with(std::span{a}, std::span{a + 6, 2}) &&
                contains(std::span{a}, std::span{a + 1, 4}) &&
                !contains(std::span{a, 8}, std::span{a, 9}));
}
} // namespace span_test

//
// https://en.cppreference.com/w/cpp/container/map#Example
//
namespace map_test {
void print_map(std::string_view comment, const std::map<std::string, int> &m) {
  std::cout << comment;
  // Iterate using C++17 facilities
  for (const auto &[key, value] : m) {
    std::cout << '[' << key << "] = " << value << "; ";
  }
  // C++11 alternative:
  //  for (const auto& n : m)
  //      std::cout << n.first << " = " << n.second << "; ";
  //
  // C++98 alternative:
  //  for (std::map<std::string, int>::const_iterator it = m.begin();
  //       it != m.end(); ++it)
  //      std::cout << it->first << " = " << it->second << "; ";

  std::cout << '\n';
}

void run() {
  // Create a map of three (string, int) pairs
  std::map<std::string, int> m{{"CPU", 10}, {"GPU", 15}, {"RAM", 20}};
  print_map("1) Initial map: ", m);

  m["CPU"] = 25; // update an existing value
  m["SSD"] = 30; // insert a new value
  print_map("2) Updated map: ", m);

  // Using operator[] with non-existent key always performs an insert
  std::cout << "3) m[UPS] = " << m["UPS"] << '\n';
  print_map("4) Updated map: ", m);

  m.erase("GPU");
  print_map("5) After erase: ", m);
  std::erase_if(m, [](const auto &pair) { return pair.second > 25; });
  print_map("6) After erase: ", m);
  std::cout << "7) m.size() = " << m.size() << '\n';

  m.clear();
  std::cout << std::boolalpha << "8) Map is empty: " << m.empty() << '\n';
}
} // namespace map_test

//
// https://en.cppreference.com/w/cpp/container/map/insert_or_assign#Example
// https://en.cppreference.com/w/cpp/container/map/try_emplace#Example
// https://en.cppreference.com/w/cpp/container/map/contains#Example
// https://en.cppreference.com/w/cpp/container/map/extract#Example
// https://en.cppreference.com/w/cpp/container/map/merge#Example
//
namespace map_member_operations_test {
void run() {
  // cppreference has these as separate member-function examples. The driver
  // harness keeps them together and assert-only so map overwrite, no-overwrite,
  // lookup, node-handle, and merge behavior are checked without noisy output.
  std::map<std::string, int> inventory;

  const auto [cpu_it, cpu_inserted] = inventory.insert_or_assign("CPU", 10);
  assert(cpu_inserted);
  assert(cpu_it->second == 10);

  const auto [cpu_again, cpu_inserted_again] =
      inventory.insert_or_assign("CPU", 25);
  assert(!cpu_inserted_again);
  assert(cpu_again->second == 25);

  const auto [gpu_it, gpu_inserted] = inventory.try_emplace("GPU", 15);
  assert(gpu_inserted);
  assert(gpu_it->second == 15);

  const auto [gpu_again, gpu_inserted_again] =
      inventory.try_emplace("GPU", 99);
  assert(!gpu_inserted_again);
  assert(gpu_again->second == 15);

  assert(inventory.contains("CPU"));
  assert(!inventory.contains("RAM"));

  std::map<int, char> cont{{1, 'a'}, {2, 'b'}, {3, 'c'}};
  auto node = cont.extract(1);
  assert(!node.empty());
  assert(node.key() == 1);
  assert(node.mapped() == 'a');
  node.key() = 4;
  cont.insert(std::move(node));
  assert(!cont.contains(1));
  assert(cont.at(4) == 'a');

  std::map<int, char> dst{{1, 'a'}, {2, 'b'}};
  std::map<int, char> src{{2, 'B'}, {3, 'c'}};
  dst.merge(src);
  assert(dst.contains(3));
  assert(src.contains(2));
  assert(src.size() == 1);
}
} // namespace map_member_operations_test

//
// https://en.cppreference.com/w/cpp/container/set#Example
//
namespace set_test {
template <typename T>
std::ostream &operator<<(std::ostream &out, const std::set<T> &set) {
  if (set.empty()) {
    return out << "{}";
  }
  out << "{ " << *set.begin();
  std::for_each(std::next(set.begin()), set.end(),
                [&out](const T &element) { out << ", " << element; });
  return out << " }";
}

void run() {
  std::set<int> set{1, 5, 3};
  std::cout << set << '\n';
  set.insert(2);
  std::cout << set << '\n';

  set.erase(1);
  std::cout << set << "\n\n";

  std::set<int> keys{3, 4};
  for (int key : keys) {
    if (set.contains(key)) {
      std::cout << set << " does contain " << key << '\n';
    } else {
      std::cout << set << " doesn't contain " << key << '\n';
    }
  }
  std::cout << '\n';

  std::string_view word = "element";
  std::set<char> characters(word.begin(), word.end());
  std::cout << "There are " << characters.size() << " unique characters in "
            << std::quoted(word) << ":\n"
            << characters << '\n';
}
} // namespace set_test

//
// https://en.cppreference.com/w/cpp/container/set/contains#Example
// https://en.cppreference.com/w/cpp/container/set/extract#Example
// https://en.cppreference.com/w/cpp/container/set/merge#Example
//
namespace set_member_operations_test {
void run() {
  // The linked member-function pages are compacted into one driver check;
  // membership, node-handle key mutation, and duplicate-preserving merge
  // behavior are the cppreference-observable parts we need here.
  std::set<int> values{1, 2, 3};
  assert(values.contains(2));
  assert(!values.contains(4));

  auto node = values.extract(1);
  assert(!node.empty());
  assert(node.value() == 1);
  node.value() = 4;
  values.insert(std::move(node));
  assert(!values.contains(1));
  assert(values.contains(4));

  std::set<int> dst{1, 2};
  std::set<int> src{2, 3, 4};
  dst.merge(src);
  assert(dst.contains(3));
  assert(dst.contains(4));
  assert(src.contains(2));
  assert(src.size() == 1);
}
} // namespace set_member_operations_test

//
// https://en.cppreference.com/w/cpp/container/multiset/erase#Example
//
namespace multiset_erase_test {
void run() {
  std::multiset<int> c = {1, 2, 3, 4, 1, 2, 3, 4};

  auto print = [&c] {
    std::cout << "c = { ";
    for (int n : c) {
      std::cout << n << ' ';
    }
    std::cout << "}\n";
  };

  print();
  std::cout << "Erase all odd numbers:\n";
  for (auto it = c.begin(); it != c.end();) {
    if (*it % 2 != 0) {
      it = c.erase(it);
    } else {
      ++it;
    }
  }
  print();

  std::cout << "Erase 1, erased count: " << c.erase(1) << '\n';
  std::cout << "Erase 2, erased count: " << c.erase(2) << '\n';
  std::cout << "Erase 2, erased count: " << c.erase(2) << '\n';
  print();
}
} // namespace multiset_erase_test

//
// https://en.cppreference.com/w/cpp/container/multimap/equal_range#Example
//
namespace multimap_equal_range_test {
void run() {
  std::multimap<int, char> dict{
      {1, 'A'}, {2, 'B'}, {2, 'C'}, {2, 'D'}, {4, 'E'}, {3, 'F'}};

  auto range = dict.equal_range(2);

  for (auto i = range.first; i != range.second; ++i) {
    std::cout << i->first << ": " << i->second << '\n';
  }
}
} // namespace multimap_equal_range_test

//
// https://en.cppreference.com/w/cpp/container/unordered_map#Example
//
namespace unordered_map_test {
void run() {
  // Create an unordered_map of three strings (that map to strings)
  std::unordered_map<std::string, std::string> u = {{"RED", "#FF0000"},
                                                   {"GREEN", "#00FF00"},
                                                   {"BLUE", "#0000FF"}};
  // Helper lambda function to print key-value pairs
  auto print_key_value = [](const auto &key, const auto &value) {
    std::cout << "Key:[" << key << "] Value:[" << value << "]\n";
  };

  std::cout << "Iterate and print key-value pairs of unordered_map, being\n"
               "explicit with their types:\n";
  for (const std::pair<const std::string, std::string> &n : u) {
    print_key_value(n.first, n.second);
  }
  std::cout
      << "\n Iterate and print key-value pairs using C++17 structured binding:\n";
  for (const auto &[key, value] : u) {
    print_key_value(key, value);
  }

  // Add two new entries to the unordered_map
  u["BLACK"] = "#000000";
  u["WHITE"] = "#FFFFFF";

  std::cout << "\n Output values by key:\n"
               "The HEX of color RED is:["
            << u["RED"] << "]\n"
            << "The HEX of color BLACK is:[" << u["BLACK"] << "]\n\n";
  std::cout << "Use operator[] with non-existent key to insert a new key-value "
               "pair:\n";
  print_key_value("new_key", u["new_key"]);

  std::cout << "\n Iterate and print key-value pairs, using `auto`;\n"
               "new_key is now one of the keys in the map:\n";
  for (const auto &n : u) {
    print_key_value(n.first, n.second);
  }
}
} // namespace unordered_map_test

//
// https://en.cppreference.com/w/cpp/container/unordered_map/contains#Example
// https://en.cppreference.com/w/cpp/container/unordered_map/extract#Example
// https://en.cppreference.com/w/cpp/container/unordered_map/merge#Example
//
namespace unordered_map_member_operations_test {
void run() {
  // The unordered member-function examples are assertion-based here because
  // iteration order is intentionally unspecified.
  std::unordered_map<int, std::string> colors{{1, "red"}, {2, "green"}};
  assert(colors.contains(1));
  assert(!colors.contains(3));

  auto node = colors.extract(2);
  assert(!node.empty());
  assert(node.key() == 2);
  assert(node.mapped() == "green");
  node.key() = 3;
  colors.insert(std::move(node));
  assert(!colors.contains(2));
  assert(colors.at(3) == "green");

  std::unordered_map<int, std::string> dst{{1, "one"}, {2, "two"}};
  std::unordered_map<int, std::string> src{{2, "TWO"}, {4, "four"}};
  dst.merge(src);
  assert(dst.contains(4));
  assert(src.contains(2));
  assert(src.size() == 1);
}
} // namespace unordered_map_member_operations_test

//
// https://en.cppreference.com/w/cpp/container/unordered_set#Example
//
namespace unordered_set_test {
void print(const auto &set) {
  for (const auto &elem : set) {
    std::cout << elem << ' ';
  }
  std::cout << '\n';
}

void run() {
  std::unordered_set<int> mySet{2, 7, 1, 8, 2, 8}; // creates a set of ints
  print(mySet);

  mySet.insert(5); // puts an element 5 in the set
  print(mySet);
  if (auto iter = mySet.find(5); iter != mySet.end()) {
    mySet.erase(iter); // removes an element pointed to by iter
  }
  print(mySet);

  mySet.erase(7); // removes an element 7
  print(mySet);
}
} // namespace unordered_set_test

//
// https://en.cppreference.com/w/cpp/container/unordered_set/contains#Example
// https://en.cppreference.com/w/cpp/container/unordered_set/extract#Example
// https://en.cppreference.com/w/cpp/container/unordered_set/merge#Example
//
namespace unordered_set_member_operations_test {
void run() {
  // Kept assert-only for the same reason as unordered_map_member_operations:
  // cppreference-visible behavior is membership and node/merge semantics, not
  // bucket-dependent iteration order.
  std::unordered_set<int> values{1, 2, 3};
  assert(values.contains(2));
  assert(!values.contains(5));

  auto node = values.extract(2);
  assert(!node.empty());
  assert(node.value() == 2);
  node.value() = 5;
  values.insert(std::move(node));
  assert(!values.contains(2));
  assert(values.contains(5));

  std::unordered_set<int> dst{1, 2};
  std::unordered_set<int> src{2, 3};
  dst.merge(src);
  assert(dst.contains(3));
  assert(src.contains(2));
  assert(src.size() == 1);
}
} // namespace unordered_set_member_operations_test

//
// https://en.cppreference.com/w/cpp/container/unordered_multiset/count#Example
//
namespace unordered_multiset_count_test {
void run() {
  std::unordered_multiset set{2, 7, 1, 8, 2, 8, 1, 8, 2, 8};

  std::cout << "The set is:\n";
  for (int e : set) {
    std::cout << e << ' ';
  }

  const auto [min, max] = std::ranges::minmax(set);

  std::cout << "\n Numbers [" << min << ".." << max << "] frequency:\n";
  for (int i{min}; i <= max; ++i) {
    std::cout << i << ':' << set.count(i) << "; ";
  }
  std::cout << '\n';
}
} // namespace unordered_multiset_count_test

//
// https://en.cppreference.com/w/cpp/container/unordered_multimap/equal_range#Example
//
namespace unordered_multimap_equal_range_test {
void run() {
  std::unordered_multimap<int, char> map = {
      {1, 'a'}, {1, 'b'}, {1, 'd'}, {2, 'b'}};
  auto range = map.equal_range(1);
  for (auto it = range.first; it != range.second; ++it) {
    std::cout << it->first << ' ' << it->second << '\n';
  }
}
} // namespace unordered_multimap_equal_range_test

//
// https://en.cppreference.com/w/cpp/container/queue#Example
//
namespace queue_test {
void run() {
  std::queue<int> q;

  q.push(0); // back pushes 0
  q.push(1); // q = 0 1
  q.push(2); // q = 0 1 2
  q.push(3); // q = 0 1 2 3

  assert(q.front() == 0);
  assert(q.back() == 3);
  assert(q.size() == 4);

  q.pop(); // removes the front element, 0
  assert(q.size() == 3);
  // Print and remove all elements. Note that std::queue does not
  // support begin()/end(), so a range-for-loop cannot be used.
  std::cout << "q: ";
  for (; !q.empty(); q.pop()) {
    std::cout << q.front() << ' ';
  }
  std::cout << '\n';
  assert(q.size() == 0);
}
} // namespace queue_test

//
// https://en.cppreference.com/w/cpp/container/stack/push#Example
//
namespace stack_push_test {
class BrainHackInterpreter {
  std::map<unsigned, unsigned> open_brackets, close_brackets;
  std::array<std::uint8_t, 32768> data_{0};
  unsigned program_{0};
  int pos_{0};

  void collect_brackets(const std::string_view program) {
    std::stack<unsigned> brackets_stack;
    for (auto pos{0U}; pos != program.length(); ++pos) {
      if (const char c{program[pos]}; '[' == c) {
        brackets_stack.push(pos);
      } else if (']' == c) {
        if (brackets_stack.empty()) {
          throw std::runtime_error("Brackets [] do not match!");
        } else {
          open_brackets[brackets_stack.top()] = pos;
          close_brackets[pos] = brackets_stack.top();
          brackets_stack.pop();
        }
      }
    }

    if (!brackets_stack.empty()) {
      throw std::runtime_error("Brackets [] do not match!");
    }
  }

  void check_data_pos(int pos) {
    if (pos < 0 or static_cast<int>(data_.size()) <= pos) {
      throw std::out_of_range{"Data pointer out of bound!"};
    }
  }

public:
  BrainHackInterpreter(const std::string_view program) {
    for (collect_brackets(program); program_ < program.length(); ++program_) {
      switch (program[program_]) {
      case '<':
        check_data_pos(--pos_);
        break;
      case '>':
        check_data_pos(++pos_);
        break;
      case '-':
        --data_[pos_];
        break;
      case '+':
        ++data_[pos_];
        break;
      case '.':
        std::cout << data_[pos_];
        break;
      case ',':
        std::cin >> data_[pos_];
        break;
      case '[':
        if (data_[pos_] == 0) {
          program_ = open_brackets[program_];
        }
        break;
      case ']':
        if (data_[pos_] != 0) {
          program_ = close_brackets[program_];
        }
        break;
      }
    }
  }
};

void run() {
  BrainHackInterpreter{
      "++++++++[>++>>++>++++>++++<<<<<-]>[<+++>>+++<-]>[<+"
      "+>>>+<<-]<[>+>+<<-]>>>--------.<<+++++++++.<<----.>"
      ">>>>.<<<------.>..++.<++.+.-.>.<.>----.<--.++.>>>+."};
  std::cout << '\n';
}
} // namespace stack_push_test

//
// https://en.cppreference.com/w/cpp/container/stack/emplace#Example
//
namespace stack_emplace_test {
struct S {
  int id;

  S(int i, double d, std::string s) : id{i} {
    std::cout << "S::S(" << i << ", " << d << ", \"" << s << "\");\n";
  }
};

void run() {
  std::stack<S> stack;
  const S &s = stack.emplace(42, 3.14, "C++");
  std::cout << "id = " << s.id << '\n';
}
} // namespace stack_emplace_test

//
// https://en.cppreference.com/w/cpp/container/priority_queue#Example
//
namespace priority_queue_test {
template <typename T> void pop_println(std::string_view rem, T &pq) {
  std::cout << rem << ": ";
  for (; !pq.empty(); pq.pop()) {
    std::cout << pq.top() << ' ';
  }
  std::cout << '\n';
}

template <typename T> void println(std::string_view rem, const T &v) {
  std::cout << rem << ": ";
  for (const auto &e : v) {
    std::cout << e << ' ';
  }
  std::cout << '\n';
}

void run() {
  const auto data = {1, 8, 5, 6, 3, 4, 0, 9, 7, 2};
  println("data", data);

  std::priority_queue<int> max_priority_queue;

  // Fill the priority queue.
  for (int n : data) {
    max_priority_queue.push(n);
  }
  pop_println("max_priority_queue", max_priority_queue);

  // std::greater<int> makes the max priority queue act as a min priority queue.
  std::priority_queue<int, std::vector<int>, std::greater<int>>
      min_priority_queue1(data.begin(), data.end());

  pop_println("min_priority_queue1", min_priority_queue1);

  // Second way to define a min priority queue.
  std::priority_queue min_priority_queue2(data.begin(), data.end(),
                                          std::greater<int>());
  pop_println("min_priority_queue2", min_priority_queue2);

  // Using a custom function object to compare elements.
  struct {
    bool operator()(const int l, const int r) const { return l > r; }
  } customLess;

  std::priority_queue custom_priority_queue(data.begin(), data.end(),
                                            customLess);

  pop_println("custom_priority_queue", custom_priority_queue);

  // Using lambda to compare elements.
  auto cmp = [](int left, int right) { return (left ^ 1) < (right ^ 1); };
  std::priority_queue<int, std::vector<int>, decltype(cmp)> lambda_priority_queue(
      cmp);

  for (int n : data) {
    lambda_priority_queue.push(n);
  }

  pop_println("lambda_priority_queue", lambda_priority_queue);
}
} // namespace priority_queue_test
