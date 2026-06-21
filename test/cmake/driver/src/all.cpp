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
#if defined(CRTSYS_ENABLE_UNSUPPORTED_THREAD_LOCAL_TEST)
namespace storage_duration_example_test {
void run();
}
#endif
namespace static_local_initialization_test {
void run();
}
namespace static_local_regression_test {
void run();
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
namespace call_once_test {
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

namespace atomic_test {
void run();
}
namespace atomic_flag_test {
void run();
}
namespace accumulate_test {
void run();
}
namespace any_test {
void run();
}
namespace array_test {
void run();
}
namespace back_inserter_test {
void run();
}
namespace bind_test {
void run();
}
namespace binary_search_test {
void run();
}
namespace bitset_test {
void run();
}
namespace concepts_test {
void run();
}
namespace complex_test {
void run();
}
namespace deque_test {
void run();
}
namespace exception_ptr_test {
void run();
}
namespace expected_test {
void run();
}
namespace exchange_test {
void run();
}
namespace find_test {
void run();
}
namespace forward_list_insert_after_test {
void run();
}
namespace from_chars_test {
void run();
}
namespace function_test {
void run();
}
namespace gcd_test {
void run();
}
namespace inner_product_test {
void run();
}
namespace invoke_test {
void run();
}
namespace iterator_advance_test {
void run();
}
namespace iterator_distance_test {
void run();
}
namespace iterator_next_test {
void run();
}
namespace iota_test {
void run();
}
namespace is_same_test {
void run();
}
namespace list_test {
void run();
}
namespace lcm_test {
void run();
}
#if defined(CRTSYS_ENABLE_UNSUPPORTED_LERP_TEST)
namespace lerp_test {
void run();
}
#endif
namespace map_test {
void run();
}
namespace merge_test {
void run();
}
namespace midpoint_test {
void run();
}
namespace move_test {
void run();
}
namespace multimap_equal_range_test {
void run();
}
namespace multiset_erase_test {
void run();
}
namespace next_permutation_test {
void run();
}
namespace optional_test {
void run();
}
namespace pair_test {
void run();
}
namespace partial_sum_test {
void run();
}
namespace partition_test {
void run();
}
namespace popcount_test {
void run();
}
namespace priority_queue_test {
void run();
}
#if defined(CRTSYS_ENABLE_UNSUPPORTED_PMR_TEST)
namespace pmr_monotonic_buffer_resource_test {
void run();
}
#endif
namespace queue_test {
void run();
}
namespace ranges_sort_test {
void run();
}
namespace ranges_views_test {
void run();
}
namespace ratio_test {
void run();
}
namespace reference_wrapper_test {
void run();
}
namespace remove_test {
void run();
}
namespace set_test {
void run();
}
namespace shared_ptr_test {
void run();
}
namespace sort_test {
void run();
}
namespace span_test {
void run();
}
namespace stack_emplace_test {
void run();
}
namespace stack_push_test {
void run();
}
namespace source_location_test {
void run();
}
namespace strong_ordering_test {
void run();
}
namespace string_test {
void run();
}
namespace string_view_test {
void run();
}
namespace tuple_test {
void run();
}
namespace to_chars_test {
void run();
}
namespace transform_test {
void run();
}
namespace unique_ptr_test {
void run();
}
namespace unordered_map_test {
void run();
}
namespace unordered_multimap_equal_range_test {
void run();
}
namespace unordered_multiset_count_test {
void run();
}
namespace unordered_set_test {
void run();
}
namespace uniform_int_distribution_test {
void run();
}
namespace valarray_slice_test {
void run();
}
namespace variant_test {
void run();
}
namespace vector_test {
void run();
}
namespace weak_ptr_test {
void run();
}
namespace adjacent_difference_test {
void run();
}
namespace heap_test {
void run();
}
namespace inclusive_scan_test {
void run();
}
namespace numbers_test {
void run();
}

//
// ntl_test.cpp
//
bool ntl_expand_stack_test();

bool ntl_irql_test();

bool ntl_spin_lock_test();

bool ntl_resource_test();

//
// C Standard tests.
//
void c_std_tests() { math_test(); }

#if defined(_AMD64_) && !defined(CRTSYS_USE_NTL_MAIN)
// x64 needs more stack to run throw_test.
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
#if defined(CRTSYS_ENABLE_UNSUPPORTED_THREAD_LOCAL_TEST)
  storage_duration_example_test::run();
#endif
  static_local_initialization_test::run();
  static_local_regression_test::run();
#if defined(_AMD64_) && !defined(CRTSYS_USE_NTL_MAIN)
  // x64 needs more stack to run throw_test.
  ntl::expand_stack(throw_test::run);
#else
#if !(defined(_X86_) && defined(UCXXRT))
  // UCXXRT x86 exception handling hangs while running this test.
  throw_test::run();
#endif
#endif
  try_catch_test::run();
#if !(defined(_X86_) && defined(UCXXRT))
  // UCXXRT x86 exception handling can still exhaust the stack and bugcheck
  // while running this test, even after KeExpandKernelStackAndCallout expands
  // the stack to its maximum size.
  function_try_block_test::run();
#endif
  //
  // C++ STL tests.
  //
  chrono_test::run();
  condition_variable_test::run();
  mutex_test::run();
  shared_mutex_test::run();
  call_once_test::run();
  future_test::run();
  promise_test::run();
  packaged_task_test::run();
  array_test::run();
  vector_test::run();
  deque_test::run();
  list_test::run();
  forward_list_insert_after_test::run();
  span_test::run();
  map_test::run();
  set_test::run();
  multiset_erase_test::run();
  multimap_equal_range_test::run();
  unordered_map_test::run();
  unordered_set_test::run();
  unordered_multiset_count_test::run();
  unordered_multimap_equal_range_test::run();
  queue_test::run();
#if defined(_AMD64_) && !defined(CRTSYS_USE_NTL_MAIN)
  // The cppreference stack::push example embeds a 32 KiB interpreter tape.
  ntl::expand_stack(stack_push_test::run);
#else
  stack_push_test::run();
#endif
  stack_emplace_test::run();
  priority_queue_test::run();
  string_test::run();
  string_view_test::run();
  sort_test::run();
  find_test::run();
  transform_test::run();
  remove_test::run();
  partition_test::run();
  binary_search_test::run();
  ranges_views_test::run();
  ranges_sort_test::run();
  merge_test::run();
  heap_test::run();
  next_permutation_test::run();
  accumulate_test::run();
  iota_test::run();
  partial_sum_test::run();
  gcd_test::run();
  midpoint_test::run();
  lcm_test::run();
  inner_product_test::run();
  adjacent_difference_test::run();
  inclusive_scan_test::run();
#if defined(CRTSYS_ENABLE_UNSUPPORTED_LERP_TEST)
  lerp_test::run();
#endif
  bitset_test::run();
  popcount_test::run();
  to_chars_test::run();
  from_chars_test::run();
  iterator_distance_test::run();
  iterator_advance_test::run();
  iterator_next_test::run();
  back_inserter_test::run();
  atomic_test::run();
  atomic_flag_test::run();
  exception_ptr_test::run();
  optional_test::run();
  expected_test::run();
  tuple_test::run();
  pair_test::run();
  variant_test::run();
  any_test::run();
  source_location_test::run();
  reference_wrapper_test::run();
  invoke_test::run();
  exchange_test::run();
  move_test::run();
  is_same_test::run();
  ratio_test::run();
  concepts_test::run();
  strong_ordering_test::run();
  numbers_test::run();
  complex_test::run();
  valarray_slice_test::run();
  uniform_int_distribution_test::run();
  function_test::run();
  bind_test::run();
  unique_ptr_test::run();
#if defined(CRTSYS_ENABLE_UNSUPPORTED_PMR_TEST)
  pmr_monotonic_buffer_resource_test::run();
#endif
  shared_ptr_test::run();
  weak_ptr_test::run();
}

//
// NTL tests.
//

void ntl_test() {
  if (!ntl_expand_stack_test()) {
    std::cerr << "ntl_expand_stack_test failed\n";
  }
  if (!ntl_irql_test()) {
    std::cerr << "ntl_irql_test failed\n";
  }
  if (!ntl_spin_lock_test()) {
    std::cerr << "ntl_spin_lock_test failed\n";
  }
  if (!ntl_resource_test()) {
    std::cerr << "ntl_resource_test failed\n";
  }
}

void test_all() {
  ntl_test();

  c_std_tests();

  cpp_std_tests();
}
