#include <iostream>

//
// c/math.c
//
extern "C" void math_test();

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
#if !(defined(_X86_) && defined(UCXXRT_X86_NO_THROW_TEST))
  // UCXXRT의 x86 예외처리 기능은 아래 테스트 수행 시, hang이 발생합니다. :-(
  throw_test::run();
#endif
#endif
  try_catch_test::run();
#if !(defined(_X86_) && defined(UCXXRT_X86_NO_THROW_TEST))
  // UCXXRT의 x86 예외처리 기능은 아래 테스트 수행 시,
  // KeExpandKernelStackAndCallout 루틴으로 스택 크기를 최대로 늘려 놓은
  // 상태임에도 불구하고 스택 부족으로 인한 BSOD가 발생합니다.  :-(
  function_try_block_test::run();
#endif
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

//
// NTL tests.
//

void ntl_test() {
  if (!ntl_expand_stack_test()) {
    std::cerr << "ntl_expand_stack_test failed\n";
  }
}

void test_all() {

  ntl_test();

  c_std_tests();

  cpp_std_tests();
}
