//
// https://en.cppreference.com/w/cpp/utility/functional/function#Example
//
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <random>

namespace function_test {
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
  // store a free function
  std::function<void(int)> f_display = print_num;
  f_display(-9);

  // store a lambda
  std::function<void()> f_display_42 = []() { print_num(42); };
  f_display_42();

  // store the result of a call to std::bind
  std::function<void()> f_display_31337 = std::bind(print_num, 31337);
  f_display_31337();
  // store a call to a member function
  std::function<void(const Foo &, int)> f_add_display = &Foo::print_add;
  const Foo foo(314159);
  f_add_display(foo, 1);
  f_add_display(314159, 1);

  // store a call to a data member accessor
  std::function<int(Foo const &)> f_num = &Foo::num_;
  std::cout << "num_: " << f_num(foo) << '\n';
  // store a call to a member function and object
  using std::placeholders::_1;
  std::function<void(int)> f_add_display2 =
      std::bind(&Foo::print_add, foo, _1);
  f_add_display2(2);

  // store a call to a member function and object ptr
  std::function<void(int)> f_add_display3 =
      std::bind(&Foo::print_add, &foo, _1);
  f_add_display3(3);
  // store a call to a function object
  std::function<void(int)> f_display_obj = PrintNum();
  f_display_obj(18);
  auto factorial = [](int n) {
    // store a lambda object to emulate "recursive lambda"; aware of extra
    // overhead
    std::function<int(int)> fac = [&](int n) {
      return (n < 2) ? 1 : n * fac(n - 1);
    };
    // note that "auto fac = [&](int n) {...};" does not work in recursive calls
    return fac(n);
  };
  for (int i{5}; i != 8; ++i) {
    std::cout << i << "! = " << factorial(i) << ";  ";
  }
  std::cout << '\n';
}
} // namespace function_test

//
// https://en.cppreference.com/w/cpp/utility/functional/bind#Example
//
namespace bind_test {
void f(int n1, int n2, int n3, const int &n4, int n5) {
  std::cout << n1 << ' ' << n2 << ' ' << n3 << ' ' << n4 << ' ' << n5
            << '\n';
}

int g(int n1) { return n1; }

struct Foo {
  void print_sum(int n1, int n2) { std::cout << n1 + n2 << '\n'; }

  int data = 10;
};

void run() {
  using namespace std::placeholders; // for _1, _2, _3...
  std::cout << "1) argument reordering and pass-by-reference: ";
  int n = 7;
  // (_1 and _2 are from std::placeholders, and represent future
  // arguments that will be passed to f1)
  auto f1 = std::bind(f, _2, 42, _1, std::cref(n), n);
  n = 10;
  f1(1, 2, 1001); // 1 is bound by _1, 2 is bound by _2, 1001 is unused
                  // makes a call to f(2, 42, 1, n, 7)
  std::cout << "2) achieving the same effect using a lambda: ";
  n = 7;
  auto lambda = [&ncref = n, n](auto a, auto b, auto /*unused*/) {
    f(b, 42, a, ncref, n);
  };
  n = 10;
  lambda(1, 2, 1001); // same as a call to f1(1, 2, 1001)

  std::cout << "3) nested bind subexpressions share the placeholders: ";
  auto f2 = std::bind(f, _3, std::bind(g, _3), _3, 4, 5);
  f2(10, 11, 12); // makes a call to f(12, g(12), 12, 4, 5);
  std::cout << "4) bind a RNG with a distribution: ";
  std::default_random_engine e;
  std::uniform_int_distribution<> d(0, 10);
  auto rnd = std::bind(d, e); // a copy of e is stored in rnd
  for (int i = 0; i < 10; ++i) {
    std::cout << rnd() << ' ';
  }
  std::cout << '\n';

  std::cout << "5) bind to a pointer to member function: ";
  Foo foo;
  auto f3 = std::bind(&Foo::print_sum, &foo, 95, _1);
  f3(5);
  std::cout << "6) bind to a mem_fn that is a pointer to member function: ";
  auto ptr_to_print_sum = std::mem_fn(&Foo::print_sum);
  auto f4 = std::bind(ptr_to_print_sum, &foo, 95, _1);
  f4(5);

  std::cout << "7) bind to a pointer to data member: ";
  auto f5 = std::bind(&Foo::data, _1);
  std::cout << f5(foo) << '\n';
  std::cout << "8) bind to a mem_fn that is a pointer to data member: ";
  auto ptr_to_data = std::mem_fn(&Foo::data);
  auto f6 = std::bind(ptr_to_data, _1);
  std::cout << f6(foo) << '\n';

  std::cout << "9) use smart pointers to call members of the referenced "
               "objects: ";
  std::cout << f6(std::make_shared<Foo>(foo)) << ' '
            << f6(std::make_unique<Foo>(foo)) << '\n';
}
} // namespace bind_test

//
// https://en.cppreference.com/w/cpp/utility/functional/not_fn#Example
//
namespace not_fn_test {
bool is_same(int a, int b) noexcept { return a == b; }

struct S {
  int val;
  bool is_same(int arg) const noexcept { return val == arg; }
};

void run() {
  auto is_differ = std::not_fn(is_same);
  assert(is_differ(8, 8) == false);
  assert(is_differ(6, 9) == true);

  auto member_differ = std::not_fn(&S::is_same);
  assert(member_differ(S{3}, 3) == false);
  static_assert(noexcept(is_differ) == noexcept(is_same));
  static_assert(noexcept(member_differ) == noexcept(&S::is_same));

  auto same = [](int a, int b) { return a == b; };
  auto differ = std::not_fn(same);
  assert(differ(1, 2) == true);
  assert(differ(2, 2) == false);
#if defined(__cpp_lib_not_fn) && __cpp_lib_not_fn >= 202306L
  auto is_differ_cpp26 = std::not_fn<is_same>();
  assert(is_differ_cpp26(8, 8) == false);
  assert(is_differ_cpp26(6, 9) == true);

  auto member_differ_cpp26 = std::not_fn<&S::is_same>();
  assert(member_differ_cpp26(S{3}, 3) == false);

  auto differ_cpp26 = std::not_fn<same>();
  static_assert(differ_cpp26(1, 2) == true);
  static_assert(differ_cpp26(2, 2) == false);
#endif
}
} // namespace not_fn_test

//
// https://en.cppreference.com/w/cpp/utility/functional/bind_front#Example
//
namespace bind_front_test {
int minus(int a, int b) { return a - b; }

struct S {
  int val;
  int minus(int arg) const noexcept { return val - arg; }
};

void run() {
#if defined(__cpp_lib_bind_front)
  auto fifty_minus = std::bind_front(minus, 50);
  assert(fifty_minus(3) == 47);
  auto member_minus = std::bind_front(&S::minus, S{50});
  assert(member_minus(3) == 47);

  static_assert(!noexcept(fifty_minus(3)));
  static_assert(noexcept(member_minus(3)));

  auto plus = [](int a, int b) { return a + b; };
  auto forty_plus = std::bind_front(plus, 40);
  assert(forty_plus(7) == 47);
#if defined(__cpp_lib_bind_front) && __cpp_lib_bind_front >= 202306L
  auto fifty_minus_cpp26 = std::bind_front<minus>(50);
  assert(fifty_minus_cpp26(3) == 47);

  auto member_minus_cpp26 = std::bind_front<&S::minus>(S{50});
  assert(member_minus_cpp26(3) == 47);

  auto forty_plus_cpp26 = std::bind_front<plus>(40);
  assert(forty_plus(7) == 47);
#endif
#if defined(__cpp_lib_bind_back) && __cpp_lib_bind_back >= 202202L
  auto madd = [](int a, int b, int c) { return a * b + c; };
  auto mul_plus_seven = std::bind_back(madd, 7);
  assert(mul_plus_seven(4, 10) == 47);
#endif
#if defined(__cpp_lib_bind_back) && __cpp_lib_bind_back >= 202306L
  auto mul_plus_seven_cpp26 = std::bind_back<madd>(7);
  assert(mul_plus_seven_cpp26(4, 10) == 47);
#endif
#else
  std::cout << "std::bind_front is not available in this MSVC STL\n";
#endif
}
} // namespace bind_front_test

//
// https://en.cppreference.com/w/cpp/utility/functional/mem_fn#Example
//
namespace mem_fn_test {
struct Foo {
  void display_greeting() { std::cout << "Hello, world.\n"; }

  void display_number(int i) { std::cout << "number: " << i << '\n'; }

  int add_xy(int x, int y) { return data + x + y; }
  template <typename... Args> int add_many(Args... args) {
    return data + (args + ...);
  }

  auto add_them(auto... args) { return data + (args + ...); }

  int data = 7;
};

void run() {
  auto f = Foo{};

  auto greet = std::mem_fn(&Foo::display_greeting);
  greet(f);

  auto print_num = std::mem_fn(&Foo::display_number);
  print_num(f, 42);
  auto access_data = std::mem_fn(&Foo::data);
  std::cout << "data: " << access_data(f) << '\n';

  auto add_xy = std::mem_fn(&Foo::add_xy);
  std::cout << "add_xy: " << add_xy(f, 1, 2) << '\n';

  auto u = std::make_unique<Foo>();
  std::cout << "access_data(u): " << access_data(u) << '\n';
  std::cout << "add_xy(u, 1, 2): " << add_xy(u, 1, 2) << '\n';

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4242 4244)
#endif
  // Keep cppreference's plain integer arguments. MSVC emits narrowing
  // diagnostics for the explicit `short` member template parameter under /WX.
  auto add_many = std::mem_fn(&Foo::add_many<short, int, long>);
  std::cout << "add_many(u, ...): " << add_many(u, 1, 2, 3) << '\n';

  auto add_them = std::mem_fn(&Foo::add_them<short, int, float, double>);
  std::cout << "add_them(u, ...): " << add_them(u, 5, 7, 10.0f, 13.0)
            << '\n';
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}
} // namespace mem_fn_test
