//
// https://en.cppreference.com/w/cpp/utility/functional/function#Example
//
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
