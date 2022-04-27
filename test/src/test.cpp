#include "test.h"

//
// c/math.c
//
EXTERN_C void math_test();

//
// cpp/lang/initialization.cpp
//
namespace constant_initialization_test {
void run();
}
namespace zero_initialization_test {
void run(int argc = 5, char *[] = nullptr);
}
//
// cpp/lang/exceptions.cpp
//
namespace throw_test {
void run();
}
namespace try_catch_test {
void run();
}
namespace function_try_block_test {
void run();
}

//
// cpp/stl/chrono.cpp
//
namespace chrono_test {
void run();
}

//
// cpp/stl/thread.cpp
//
namespace condition_variable_test {
void run();
}
namespace mutex_test {
void run();
}
namespace shared_mutex_test {
void run();
}
namespace future_test {
void run();
}
namespace promise_test {
void run();
}
namespace packaged_task_test {
void run();
}

//
// ntl_test.cpp
//
bool ntl_expand_stack_test();

//
// C Standard tests.
//
void c_std_tests() { math_test(); }

#if defined(_AMD64_) && !defined(CRTSYS_USE_NTL_MAIN)
// x64에서는 throw_test 테스트를 진행하기엔 스택이 부족합니다 :-(
#include <ntl/expand_stack>
#endif

//
// C++ Standard tests.
//
void cpp_std_tests() {
  //
  // C++ Language tests.
  //
  constant_initialization_test::run();
  zero_initialization_test::run();
#if defined(_AMD64_) && !defined(CRTSYS_USE_NTL_MAIN)
  // x64에서는 throw_test 테스트를 진행하기엔 스택이 부족합니다 :-(
  ntl::expand_stack(throw_test::run);
#else
  throw_test::run();
#endif
  try_catch_test::run();
  function_try_block_test::run();

  //
  // C++ STL tests.
  //
  chrono_test::run();
  condition_variable_test::run();
  mutex_test::run();
  shared_mutex_test::run();
  future_test::run();
  promise_test::run();
  packaged_task_test::run();
}

void test_all() {
  if (!ntl_expand_stack_test()) {
    DbgPrint("ntl_expand_stack_test failed");
  }

  c_std_tests();

  cpp_std_tests();
}