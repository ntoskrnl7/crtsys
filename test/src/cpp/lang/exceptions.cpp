#include "../../test.h"

//
// https://en.cppreference.com/w/cpp/language/throw#Example
//
#include <iostream>
#include <stdexcept>
namespace throw_test {
struct A {
  int n;

  A(int n = 0) : n(n) {
    // std::cout << "A(" << n << ") constructed successfully\n";
    printf("A(%d) constructed successfully\n", n);
  }
  ~A() {
    // std::cout << "A(" << n << ") destroyed\n";
    printf("A(%d) destroyed\n", n);
  }
};

// foo 함수에서 예외로 인해서 중단되기 때문에
// 경고 4702(unreachable code)가 발생합니다.
#if !DBG
#pragma warning(disable : 4702)
#endif
int foo() { throw std::runtime_error("error"); }

struct B {
  A a1, a2, a3;

  B() try : a1(1), a2(foo()), a3(3) {
    // std::cout << "B constructed successfully\n";
    printf("B constructed successfully\n");
  } catch (...) {
    // std::cout << "B::B() exiting with exception\n";
    printf("B::B() exiting with exception\n");
  }

  ~B() {
    // std::cout << "C destroyed\n"
    printf("B destroyed\n");
  }
};

struct C : A, B {
  C() try { printf("C::C() completed successfully\n"); } catch (...) {
    // std::cout << "C::C() exiting with exception\n";
    printf("C::C() exiting with exception\n");
  }

  ~C() {
    // std::cout << "C destroyed\n";
    printf("C destroyed\n");
  }
};

void run() try {
  // creates the A base subobject
  // creates the a1 member of B
  // fails to create the a2 member of B
  // unwinding destroys the a1 member of B
  // unwinding destroys the A base subobject
  C c;
} catch (const std::exception &e) {
  // std::cout << "throw_test::run() failed to create C with: " << e.what();
  printf("run() failed to create C with: %s\n", e.what());
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
    printf("Throwing an integer exception...\n");
    // std::cout << "Throwing an integer exception...\n";
    throw 42;
  } catch (int i) {
    printf(" the integer exception was caught, with value: %d\n", i);
    // std::cout << " the integer exception was caught, with value: " << i
    // << '\n';
  }

  try {
    printf("Creating a vector of size 5... \n");
    // std::cout << "Creating a vector of size 5... \n";
    std::vector<int> v(5);
    printf("Accessing the 11th element of the vector...\n");
    // std::cout << "Accessing the 11th element of the vector...\n";
    printf("%d", v.at(10));
    // std::cout << v.at(10);          // vector::at() throws std::out_of_range
  } catch (const std::exception &e) // caught by reference to base
  {
    printf("a standard exception was caught, with message '%s'\n", e.what());
    // std::cout << " a standard exception was caught, with message '" <<
    // e.what()
    //           << "'\n";
  }
}
} // namespace try_catch_test

//
// https://en.cppreference.com/w/cpp/language/function-try-block#Explanation
//
#include <iostream>
#include <string>
namespace function_try_block_test {
struct S {
  std::string m;
  S(const std::string &str, int idx) try : m(str, idx) {
    // std::cout << "S(" << str << ", " << idx << ") constructed, m = " << m <<
    // '\n';
    printf("S(%s, %d) constructed, m = %s\n", str.c_str(), idx, m.c_str());
  } catch (const std::exception &e) {
    printf("S(%s, %d) failed: %s\n", str.c_str(), idx, e.what());
    // std::cout << "S(" << str << ", " << idx << ") failed: " << e.what() <<
    // '\n';

  } // implicit "throw;" here
};
void run() {
  S s1{"ABC", 1}; // does not throw (index is in bounds)
  try {
    S s2{"ABC", 4}; // throws (out of bounds)
  } catch (std::exception &e) {
    printf("S s2... raised an exception: %s\n", e.what());
    // std::cout << "S s2... raised an exception: " << e.what() << '\n';
  }
}
} // namespace function_try_block_test