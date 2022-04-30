#if !DBG
#pragma warning(disable : 4702) // line 25
#endif

//
// https://en.cppreference.com/w/cpp/language/throw#Example
//
#include <iostream>
#include <stdexcept>
namespace throw_test {
struct A {
  int n;

  A(int n = 0) : n(n) {
    std::cout << "A(" << n << ") constructed successfully\n";
  }
  ~A() { std::cout << "A(" << n << ") destroyed\n"; }
};

int foo() { throw std::runtime_error("error"); }

struct B {
  A a1, a2, a3;

  B() try : a1(1), a2(foo()), a3(3) {
    std::cout << "B constructed successfully\n";
  } catch (...) {
    std::cout << "B::B() exiting with exception\n";
  }

  ~B() { std::cout << "B destroyed\n"; }
};

struct C : A, B {
  C() try { std::cout << "C::C() completed successfully\n"; } catch (...) {
    std::cout << "C::C() exiting with exception\n";
  }

  ~C() { std::cout << "C destroyed\n"; }
};

void run() try {
  // creates the A base subobject
  // creates the a1 member of B
  // fails to create the a2 member of B
  // unwinding destroys the a1 member of B
  // unwinding destroys the A base subobject
  C c;
} catch (const std::exception &e) {
  std::cout << "main() failed to create C with: " << e.what();
}
} // namespace throw_test

//
// https://en.cppreference.com/w/cpp/language/try_catch#Example
//
#include <iostream>
#include <vector>
namespace try_catch_test {
void run() {
  try {
    std::cout << "Throwing an integer exception...\n";
    throw 42;
  } catch (int i) {
    std::cout << " the integer exception was caught, with value: " << i << '\n';
  }

  try {
    std::cout << "Creating a vector of size 5... \n";
    std::vector<int> v(5);
    std::cout << "Accessing the 11th element of the vector...\n";
    std::cout << v.at(10);          // vector::at() throws std::out_of_range
  } catch (const std::exception &e) // caught by reference to base
  {
    std::cout << " a standard exception was caught, with message '" << e.what()
              << "'\n";
  }
}
} // namespace try_catch_test

//
// https://en.cppreference.com/w/cpp/language/function-try-block#Explanation
//
#include <iostream>
#include <string>
namespace function_try_block_test {
#include <iostream>
#include <string>
struct S {
  std::string m;
  S(const std::string &str, int idx) try : m(str, idx) {
    std::cout << "S(" << str << ", " << idx << ") constructed, m = " << m
              << '\n';
  } catch (const std::exception &e) {
    std::cout << "S(" << str << ", " << idx << ") failed: " << e.what() << '\n';
  } // implicit "throw;" here
};
void run() {
  S s1{"ABC", 1}; // does not throw (index is in bounds)
  try {
    S s2{"ABC", 4}; // throws (out of bounds)
  } catch (std::exception &e) {
    std::cout << "S s2... raised an exception: " << e.what() << '\n';
  }
}
} // namespace function_try_block_test