#include <wdm.h>

#include <stdio.h>
#define printf(...)                                                            \
  (DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, __VA_ARGS__))

#include <ntl/expand_stack>

//
// math.c
//

bool float_to_integer();
bool integer_to_float();
bool double_to_integer();
bool integer_to_double();

//
// std_test.cpp
//

void condition_variable_test();
void mutex_test();
void shared_mutex_test();
void future_test();
void throw_test();

//
// ntl_test.cpp
//

bool ntl_expand_stack_test();

void test_all() {
  if (!ntl_expand_stack_test()) {
    printf("float_to_integer failed");
  }

  if (!float_to_integer()) {
    printf("float_to_integer failed");
  }
  if (!integer_to_float()) {
    printf("integer_to_float failed");
  }
  if (!double_to_integer()) {
    printf("double_to_integer failed");
  }
  if (!integer_to_double()) {
    printf("integer_to_double failed");
  }

  condition_variable_test();
  mutex_test();
  shared_mutex_test();
  future_test();

#if defined(_AMD64_) && !defined(CRTSYS_USE_NTL_MAIN)
  ntl::expand_stack(throw_test);
#else
  throw_test();
#endif
}