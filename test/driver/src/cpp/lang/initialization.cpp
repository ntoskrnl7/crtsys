//
// https://en.cppreference.com/w/cpp/language/constant_initialization#Example
//
#include <array>
#include <iostream>
namespace constant_initialization_test {
struct S {
  static const int c;
};
const int d = 10 * S::c; // not a constant expression: S::c has no preceding
                         // initializer, this initialization happens after const
const int S::c = 5;      // constant initialization, guaranteed to happen first
void run() {
  std::cout << "d = " << d << '\n';
  std::array<int, S::c> a1; // OK: S::c is a constant expression
  //  std::array<int, d> a2;    // error: d is not a constant expression
  a1;
}
} // namespace constant_initialization_test

//
// https://en.cppreference.com/w/cpp/language/zero_initialization#Example
//

#include <iostream>
#include <string>
namespace zero_initialization_test {
struct A {
  int a, b, c;
};

double f[3]; // zero-initialized to three 0.0's

int *p; // zero-initialized to null pointer value
        // (even if the value is not integral 0)

std::string
    s; // zero-initialized to indeterminate value, then
       // default-initialized to "" by the std::string default constructor

void run(int argc, char *[]) {
  delete p; // safe to delete a null pointer

  static int n = argc; // zero-initialized to 0 then copy-initialized to argc
  std::cout << "n = " << n << '\n';

  A a = A(); // the effect is same as: A a{}; or A a = {};
  std::cout << "a = {" << a.a << ' ' << a.b << ' ' << a.c << "}\n";
}
} // namespace zero_initialization_test

//
// https://en.cppreference.com/w/cpp/language/initialization#Dynamic_initialization
// (no cppreference example.)
//
namespace dynamic_initialization_test {
struct test_object {
  int n;
  test_object(int n = 0) : n(n) {
    std::cout << "test_object(" << n << ") constructed successfully\n";
  }
  ~test_object() { std::cout << "test_object(" << n << ") destroyed\n"; }
};

test_object global_object_(1);
static test_object static_object_(2);
} // namespace dynamic_initialization_test