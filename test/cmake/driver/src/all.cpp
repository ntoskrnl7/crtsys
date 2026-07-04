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
#if CRTSYS_ENABLE_UNSUPPORTED_THREAD_LOCAL_TEST
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
namespace typeid_test {
void run();
}
namespace dynamic_cast_test {
void run();
}

//
// cpp/stl/chrono.cpp
//
namespace chrono_test {
void run();
}
namespace chrono_current_zone_test {
void run();
}
namespace chrono_time_zone_info_test {
void run();
}
namespace chrono_year_month_day_test {
void run();
}
namespace chrono_weekday_test {
void run();
}
namespace chrono_hh_mm_ss_test {
void run();
}
namespace chrono_clock_conversion_test {
void run();
}

//
// cpp/stl/thread.cpp
//
namespace condition_variable_test {
void run();
}
namespace condition_variable_any_test {
void run();
}
namespace mutex_test {
void run();
}
namespace lock_guard_test {
void run();
}
namespace shared_mutex_test {
void run();
}
namespace shared_timed_mutex_test {
void run();
}
namespace shared_lock_test {
void run();
}
namespace scoped_lock_test {
void run();
}
namespace lock_test {
void run();
}
namespace unique_lock_test {
void run();
}
namespace recursive_mutex_test {
void run();
}
namespace timed_mutex_test {
void run();
}
namespace recursive_timed_mutex_test {
void run();
}
namespace try_lock_test {
void run();
}
namespace call_once_test {
void run();
}
namespace future_test {
void run();
}
namespace async_test {
void run();
}
namespace future_status_test {
void run();
}
namespace future_error_test {
void run();
}
namespace shared_future_test {
void run();
}
namespace promise_test {
void run();
}
namespace packaged_task_test {
void run();
}
namespace latch_test {
void run();
}
namespace barrier_test {
void run();
}
namespace counting_semaphore_test {
void run();
}
namespace jthread_constructor_test {
void run();
}
namespace stop_source_test {
void run();
}
namespace stop_callback_test {
void run();
}
namespace threading_semantic_edge_test {
void run();
}

namespace atomic_test {
void run();
}
namespace atomic_ref_test {
void run();
}
namespace atomic_flag_test {
void run();
}
namespace atomic_thread_fence_test {
void run();
}
namespace atomic_signal_fence_test {
void run();
}
namespace atomic_fetch_add_test {
void run();
}
namespace atomic_compare_exchange_test {
void run();
}
namespace accumulate_test {
void run();
}
namespace allocator_traits_test {
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
namespace bind_front_test {
void run();
}
namespace binary_search_test {
void run();
}
namespace lower_bound_test {
void run();
}
namespace upper_bound_test {
void run();
}
namespace equal_range_test {
void run();
}
namespace stable_sort_test {
void run();
}
namespace nth_element_test {
void run();
}
namespace partial_sort_test {
void run();
}
namespace bitset_test {
void run();
}
namespace rotl_test {
void run();
}
namespace rotr_test {
void run();
}
namespace countl_zero_test {
void run();
}
namespace countr_zero_test {
void run();
}
namespace has_single_bit_test {
void run();
}
namespace bit_ceil_test {
void run();
}
namespace bit_floor_test {
void run();
}
namespace bit_width_test {
void run();
}
namespace bit_cast_test {
void run();
}
namespace endian_test {
void run();
}
namespace byteswap_test {
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
namespace deque_member_operations_test {
void run();
}
namespace exception_ptr_test {
void run();
}
namespace expected_test {
void run();
}
namespace format_test {
void run();
}
namespace format_ranges_test {
void run();
}
namespace formatter_customization_test {
void run();
}
namespace print_test {
void run();
}
namespace exchange_test {
void run();
}
namespace find_test {
void run();
}
namespace filesystem_path_test {
void run();
}
namespace filesystem_directory_iterator_test {
void run();
}
namespace filesystem_copy_file_test {
void run();
}
namespace filesystem_copy_test {
void run();
}
namespace filesystem_status_test {
void run();
}
namespace filesystem_file_size_test {
void run();
}
namespace filesystem_is_empty_test {
void run();
}
namespace filesystem_resize_file_test {
void run();
}
namespace filesystem_permissions_test {
void run();
}
namespace filesystem_hard_link_test {
void run();
}
namespace filesystem_create_symlink_test {
void run();
}
namespace filesystem_read_symlink_test {
void run();
}
namespace filesystem_copy_symlink_test {
void run();
}
namespace filesystem_directory_entry_test {
void run();
}
namespace filesystem_space_test {
void run();
}
namespace filesystem_rename_test {
void run();
}
namespace filesystem_temp_directory_path_test {
void run();
}
namespace filesystem_absolute_test {
void run();
}
namespace filesystem_current_path_test {
void run();
}
namespace filesystem_relative_test {
void run();
}
namespace filesystem_last_write_time_test {
void run();
}
namespace filesystem_canonical_test {
void run();
}
namespace filesystem_semantic_edge_test {
void run();
}
namespace crt_file_io_semantic_test {
void run();
}
namespace crt_file_state_semantic_test {
void run();
}
namespace crt_environment_semantic_test {
void run();
}
namespace forward_list_insert_after_test {
void run();
}
namespace forward_list_member_operations_test {
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
namespace integer_sequence_test {
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
namespace list_member_operations_test {
void run();
}
namespace lcm_test {
void run();
}
namespace lerp_test {
void run();
}
namespace map_test {
void run();
}
namespace map_member_operations_test {
void run();
}
namespace mem_fn_test {
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
namespace not_fn_test {
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
namespace quoted_test {
void run();
}
namespace spanstream_test {
void run();
}
namespace pmr_monotonic_buffer_resource_test {
void run();
}
namespace pmr_pool_resource_test {
void run();
}
namespace queue_test {
void run();
}
namespace ranges_sort_test {
void run();
}
namespace ranges_cxx23_adaptors_test {
void run();
}
namespace ranges_additional_cxx23_adaptors_test {
void run();
}
namespace ranges_take_view_test {
void run();
}
namespace ranges_drop_view_test {
void run();
}
namespace ranges_reverse_view_test {
void run();
}
namespace ranges_join_view_test {
void run();
}
namespace ranges_split_view_test {
void run();
}
namespace ranges_values_view_test {
void run();
}
namespace ranges_keys_view_test {
void run();
}
namespace ranges_elements_view_test {
void run();
}
namespace ranges_views_test {
void run();
}
namespace regex_test {
void run();
}
namespace regex_match_test {
void run();
}
namespace regex_iterator_test {
void run();
}
namespace regex_token_iterator_test {
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
namespace set_member_operations_test {
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
namespace string_member_operations_test {
void run();
}
namespace string_view_test {
void run();
}
namespace string_view_member_operations_test {
void run();
}
namespace locale_test {
void run();
}
namespace locale_constructor_test {
void run();
}
namespace has_facet_test {
void run();
}
namespace use_facet_test {
void run();
}
namespace numpunct_test {
void run();
}
namespace ctype_char_test {
void run();
}
namespace messages_test {
void run();
}
namespace money_get_test {
void run();
}
namespace money_put_test {
void run();
}
namespace time_get_test {
void run();
}
namespace time_put_test {
void run();
}
namespace tuple_test {
void run();
}
namespace type_index_test {
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
namespace unordered_map_member_operations_test {
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
namespace unordered_set_member_operations_test {
void run();
}
namespace visit_test {
void run();
}
namespace random_device_test {
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
namespace vector_member_operations_test {
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

bool ntl_seh_try_except_test();

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

namespace {
template <typename Fn> void run_cppreference_test(const char *name, Fn fn) {
  std::cout << "\n[crtsys] RUN " << name << '\n';
  fn();
  std::cout << "[crtsys] PASS " << name << '\n';
}

template <typename Fn> void run_driver_semantic_test(const char *name, Fn fn) {
  std::cout << "\n[crtsys] RUN " << name << " (driver semantic)\n";
  fn();
  std::cout << "[crtsys] PASS " << name << '\n';
}
} // namespace

#define CRTSYS_RUN_CPPREFERENCE_TEST(test_namespace)                           \
  run_cppreference_test(#test_namespace, [] { test_namespace::run(); })

#define CRTSYS_RUN_CPPREFERENCE_TEST_EXPANDED(test_namespace)                  \
  run_cppreference_test(#test_namespace,                                      \
                        [] { ntl::expand_stack(test_namespace::run); })

#define CRTSYS_RUN_DRIVER_SEMANTIC_TEST(test_namespace)                        \
  run_driver_semantic_test(#test_namespace, [] { test_namespace::run(); })

//
// C++ Standard tests.
//
void cpp_std_tests() {
  //
  // C++ Language tests.
  //
  CRTSYS_RUN_CPPREFERENCE_TEST(constant_initialization_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(zero_initialization_test);
#if CRTSYS_ENABLE_UNSUPPORTED_THREAD_LOCAL_TEST
  CRTSYS_RUN_CPPREFERENCE_TEST(storage_duration_example_test);
#endif
  CRTSYS_RUN_CPPREFERENCE_TEST(static_local_initialization_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(static_local_regression_test);
#if defined(_AMD64_) && !defined(CRTSYS_USE_NTL_MAIN)
  // x64 needs more stack to run throw_test.
  CRTSYS_RUN_CPPREFERENCE_TEST_EXPANDED(throw_test);
#else
#if !(defined(_X86_) && defined(UCXXRT))
  // UCXXRT x86 exception handling hangs while running this test.
  CRTSYS_RUN_CPPREFERENCE_TEST(throw_test);
#endif
#endif
  CRTSYS_RUN_CPPREFERENCE_TEST(try_catch_test);
#if !(defined(_X86_) && defined(UCXXRT))
  // UCXXRT x86 exception handling can still exhaust the stack and bugcheck
  // while running this test, even after KeExpandKernelStackAndCallout expands
  // the stack to its maximum size.
  CRTSYS_RUN_CPPREFERENCE_TEST(function_try_block_test);
#endif
  CRTSYS_RUN_CPPREFERENCE_TEST(typeid_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(dynamic_cast_test);
  //
  // C++ STL tests.
  //
  CRTSYS_RUN_CPPREFERENCE_TEST(chrono_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(chrono_current_zone_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(chrono_time_zone_info_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(chrono_year_month_day_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(chrono_weekday_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(chrono_hh_mm_ss_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(chrono_clock_conversion_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(condition_variable_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(condition_variable_any_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(mutex_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(lock_guard_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(shared_mutex_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(shared_timed_mutex_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(shared_lock_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(scoped_lock_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(lock_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(unique_lock_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(recursive_mutex_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(timed_mutex_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(recursive_timed_mutex_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(try_lock_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(call_once_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(future_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(async_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(future_status_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(future_error_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(shared_future_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(promise_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(packaged_task_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(latch_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(barrier_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(counting_semaphore_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(jthread_constructor_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(stop_source_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(stop_callback_test);
  CRTSYS_RUN_DRIVER_SEMANTIC_TEST(threading_semantic_edge_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(array_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(vector_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(vector_member_operations_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(deque_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(deque_member_operations_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(list_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(list_member_operations_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(forward_list_insert_after_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(forward_list_member_operations_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(span_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(map_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(map_member_operations_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(set_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(set_member_operations_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(multiset_erase_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(multimap_equal_range_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(unordered_map_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(unordered_map_member_operations_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(unordered_set_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(unordered_set_member_operations_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(unordered_multiset_count_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(unordered_multimap_equal_range_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(queue_test);
#if defined(_AMD64_) && !defined(CRTSYS_USE_NTL_MAIN)
  // The cppreference stack::push example embeds a 32 KiB interpreter tape.
  CRTSYS_RUN_CPPREFERENCE_TEST_EXPANDED(stack_push_test);
#else
  CRTSYS_RUN_CPPREFERENCE_TEST(stack_push_test);
#endif
  CRTSYS_RUN_CPPREFERENCE_TEST(stack_emplace_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(priority_queue_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(string_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(string_member_operations_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(string_view_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(string_view_member_operations_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(locale_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(locale_constructor_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(has_facet_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(use_facet_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(numpunct_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(ctype_char_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(messages_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(money_get_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(money_put_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(time_get_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(time_put_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_path_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_directory_iterator_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_copy_file_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_copy_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_status_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_file_size_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_is_empty_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_resize_file_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_permissions_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_hard_link_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_create_symlink_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_read_symlink_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_copy_symlink_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_directory_entry_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_space_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_rename_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_temp_directory_path_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_absolute_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_current_path_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_relative_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_last_write_time_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(filesystem_canonical_test);
  CRTSYS_RUN_DRIVER_SEMANTIC_TEST(filesystem_semantic_edge_test);
  CRTSYS_RUN_DRIVER_SEMANTIC_TEST(crt_file_io_semantic_test);
  CRTSYS_RUN_DRIVER_SEMANTIC_TEST(crt_file_state_semantic_test);
  CRTSYS_RUN_DRIVER_SEMANTIC_TEST(crt_environment_semantic_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(sort_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(find_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(transform_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(remove_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(partition_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(binary_search_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(lower_bound_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(upper_bound_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(equal_range_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(stable_sort_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(nth_element_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(partial_sort_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(ranges_views_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(ranges_sort_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(ranges_cxx23_adaptors_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(ranges_additional_cxx23_adaptors_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(ranges_take_view_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(ranges_drop_view_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(ranges_reverse_view_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(ranges_join_view_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(ranges_split_view_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(ranges_values_view_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(ranges_keys_view_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(ranges_elements_view_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(regex_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(regex_match_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(regex_iterator_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(regex_token_iterator_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(quoted_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(spanstream_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(merge_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(heap_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(next_permutation_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(accumulate_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(iota_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(partial_sum_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(gcd_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(midpoint_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(lcm_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(inner_product_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(adjacent_difference_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(inclusive_scan_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(lerp_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(bitset_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(popcount_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(rotl_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(rotr_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(countl_zero_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(countr_zero_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(has_single_bit_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(bit_ceil_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(bit_floor_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(bit_width_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(bit_cast_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(endian_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(byteswap_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(to_chars_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(from_chars_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(iterator_distance_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(iterator_advance_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(iterator_next_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(back_inserter_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(atomic_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(atomic_ref_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(atomic_flag_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(atomic_thread_fence_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(atomic_signal_fence_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(atomic_fetch_add_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(atomic_compare_exchange_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(exception_ptr_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(optional_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(expected_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(format_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(format_ranges_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(formatter_customization_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(print_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(tuple_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(pair_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(variant_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(visit_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(any_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(type_index_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(source_location_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(reference_wrapper_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(invoke_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(exchange_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(move_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(is_same_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(integer_sequence_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(ratio_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(concepts_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(strong_ordering_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(numbers_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(complex_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(valarray_slice_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(random_device_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(uniform_int_distribution_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(function_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(bind_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(not_fn_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(bind_front_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(mem_fn_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(unique_ptr_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(pmr_monotonic_buffer_resource_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(pmr_pool_resource_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(allocator_traits_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(shared_ptr_test);
  CRTSYS_RUN_CPPREFERENCE_TEST(weak_ptr_test);
}

//
// NTL tests.
//

void ntl_test() {
  if (!ntl_expand_stack_test()) {
    std::cerr << "ntl_expand_stack_test failed\n";
  }
  if (!ntl_seh_try_except_test()) {
    std::cerr << "ntl_seh_try_except_test failed\n";
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
