#include <wdm.h>

#include <stdio.h>
#define printf(...)                                                            \
  (DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, __VA_ARGS__))

//
// initialization test
//
struct test_object {
  int n;
  test_object(int n = 0) : n(n) {
    printf("test_object(%d) constructed successfully\n", n);
  }
  ~test_object() { printf("test_object(%d) destroyed\n", n); }
};

test_object object_(1);
static test_object static_object_(2);

//
// math_test.cpp
//
bool float_to_integer();
bool integer_to_float();
bool double_to_integer();
bool integer_to_double();

//
// std_test.cpp
//
void throw_test();
void chrono_test();
void condition_variable_test();
void mutex_test();
void shared_mutex_test();
void future_test();
void promise_test();
void packaged_task();
void try_catch_test();

//
// ntl_test.cpp
//
bool ntl_expand_stack_test();

#if defined(_AMD64_) && !defined(CRTSYS_USE_NTL_MAIN)
#include <ntl/expand_stack>
#endif

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

  chrono_test();
  condition_variable_test();
  mutex_test();
  shared_mutex_test();
  future_test();
  promise_test();
  packaged_task();
  try_catch_test();

#if defined(_AMD64_) && !defined(CRTSYS_USE_NTL_MAIN)
  ntl::expand_stack(throw_test);
#else
  throw_test();
#endif
}