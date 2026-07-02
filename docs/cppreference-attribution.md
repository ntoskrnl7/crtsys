# cppreference Example Attribution

[Back to feature coverage](./feature-coverage.md)

Some driver tests are ports of examples from
[cppreference](https://en.cppreference.com). The source pages are linked from
the corresponding test files.

cppreference content is licensed under the Creative Commons Attribution-
ShareAlike 3.0 Unported License and the GNU Free Documentation License. See:

- <https://cppreference.com/Cppreference:Copyrights>
- <https://www.cppreference.com/Cppreference:Copyright/CC-BY-SA>
- <https://en.cppreference.com/Cppreference:Copyright/GDFL>

The examples are adapted only as needed to run inside the crtsys kernel-driver
test harness, typically by moving the sample `main()` body into a namespaced
`run()` function and by avoiding unsupported execution contexts.

## Ported Examples

- `std::exception_ptr`: <https://en.cppreference.com/w/cpp/error/exception_ptr>
- `std::uncaught_exceptions`: <https://en.cppreference.com/w/cpp/error/uncaught_exception>
- `std::nested_exception`: <https://en.cppreference.com/w/cpp/error/nested_exception>
- `std::throw_with_nested`: <https://en.cppreference.com/w/cpp/error/throw_with_nested>
- `std::rethrow_if_nested`: <https://en.cppreference.com/w/cpp/error/rethrow_if_nested>
- `std::error_code`: <https://en.cppreference.com/w/cpp/error/error_code>
- `std::error_condition`: <https://en.cppreference.com/w/cpp/error/error_condition>
- `std::system_error`: <https://en.cppreference.com/w/cpp/error/system_error>
- `std::optional`: <https://en.cppreference.com/w/cpp/utility/optional>
- `std::optional::and_then`: <https://en.cppreference.com/w/cpp/utility/optional/and_then>
- `std::optional::transform`: <https://en.cppreference.com/w/cpp/utility/optional/transform>
- `std::tuple`: <https://en.cppreference.com/w/cpp/utility/tuple>
- `std::pair` / `std::make_pair`: <https://en.cppreference.com/w/cpp/utility/pair/make_pair>
- `std::shared_ptr`: <https://en.cppreference.com/w/cpp/memory/shared_ptr>
- `std::atomic`: <https://en.cppreference.com/w/cpp/atomic/atomic>
- `std::atomic_ref`: <https://en.cppreference.com/w/cpp/atomic/atomic_ref/atomic_ref>
- `std::atomic_flag`: <https://en.cppreference.com/w/cpp/atomic/atomic_flag>
- `std::atomic_thread_fence`: <https://en.cppreference.com/w/cpp/atomic/atomic_thread_fence>
- `std::atomic_signal_fence`: <https://en.cppreference.com/w/cpp/atomic/atomic_signal_fence>
- `std::atomic_fetch_add`: <https://en.cppreference.com/w/cpp/atomic/atomic_fetch_add>
- `std::atomic_compare_exchange`: <https://en.cppreference.com/w/cpp/atomic/atomic_compare_exchange>
- `std::latch`: <https://en.cppreference.com/w/cpp/thread/latch>
- `std::barrier`: <https://en.cppreference.com/w/cpp/thread/barrier>
- `std::counting_semaphore` / `std::binary_semaphore`: <https://en.cppreference.com/w/cpp/thread/counting_semaphore>
- `std::condition_variable`: <https://en.cppreference.com/w/cpp/thread/condition_variable>
- `std::condition_variable_any::wait`: <https://en.cppreference.com/w/cpp/thread/condition_variable_any/wait>
- `std::mutex`: <https://en.cppreference.com/w/cpp/thread/mutex>
- `std::lock_guard`: <https://en.cppreference.com/w/cpp/thread/lock_guard>
- `std::shared_mutex`: <https://en.cppreference.com/w/cpp/thread/shared_mutex>
- `std::shared_lock`: <https://en.cppreference.com/w/cpp/thread/shared_lock>
- `std::shared_timed_mutex`: <https://en.cppreference.com/w/cpp/thread/shared_timed_mutex>
- `std::timed_mutex`: <https://en.cppreference.com/w/cpp/thread/timed_mutex>
- `std::recursive_mutex`: <https://en.cppreference.com/w/cpp/thread/recursive_mutex>
- `std::recursive_timed_mutex`: <https://en.cppreference.com/w/cpp/thread/recursive_timed_mutex>
- `std::scoped_lock`: <https://en.cppreference.com/w/cpp/thread/scoped_lock>
- `std::lock`: <https://en.cppreference.com/w/cpp/thread/lock>
- `std::unique_lock`: <https://en.cppreference.com/w/cpp/thread/unique_lock>
- `std::try_lock`: <https://en.cppreference.com/w/cpp/thread/try_lock>
- `std::call_once`: <https://en.cppreference.com/w/cpp/thread/call_once>
- `std::future`: <https://en.cppreference.com/w/cpp/thread/future>
- `std::future::valid`: <https://en.cppreference.com/w/cpp/thread/future/valid>
- `std::async`: <https://en.cppreference.com/w/cpp/thread/async>
- `std::future::wait_for`: <https://en.cppreference.com/w/cpp/thread/future/wait_for>
- `std::future::wait_until`: <https://en.cppreference.com/w/cpp/thread/future/wait_until>
- `std::future_error`: <https://en.cppreference.com/w/cpp/thread/future_error>
- `std::shared_future`: <https://en.cppreference.com/w/cpp/thread/shared_future>
- `std::shared_future::wait_for`: <https://en.cppreference.com/w/cpp/thread/shared_future/wait_for>
- `std::shared_timed_mutex::try_lock_for`: <https://en.cppreference.com/w/cpp/thread/shared_timed_mutex/try_lock_for>
- `std::shared_timed_mutex::try_lock_shared_for`: <https://en.cppreference.com/w/cpp/thread/shared_timed_mutex/try_lock_shared_for>
- `std::shared_timed_mutex::try_lock_shared_until`: <https://en.cppreference.com/w/cpp/thread/shared_timed_mutex/try_lock_shared_until>
- `std::promise`: <https://en.cppreference.com/w/cpp/thread/promise>
- `std::promise::set_exception`: <https://en.cppreference.com/w/cpp/thread/promise/set_exception>
- `std::packaged_task`: <https://en.cppreference.com/w/cpp/thread/packaged_task>
- `std::jthread`: <https://en.cppreference.com/w/cpp/thread/jthread/jthread>
- `std::stop_source` / `std::stop_token`: <https://en.cppreference.com/w/cpp/thread/stop_source>
- `std::stop_callback`: <https://en.cppreference.com/w/cpp/thread/stop_callback>
- `std::weak_ptr`: <https://en.cppreference.com/w/cpp/memory/weak_ptr>
- `std::array`: <https://en.cppreference.com/w/cpp/container/array>
- `std::vector`: <https://en.cppreference.com/w/cpp/container/vector>
- `std::deque`: <https://en.cppreference.com/w/cpp/container/deque>
- `std::list`: <https://en.cppreference.com/w/cpp/container/list>
- `std::forward_list::insert_after`: <https://en.cppreference.com/w/cpp/container/forward_list/insert_after>
- `std::locale`: <https://en.cppreference.com/w/cpp/locale/locale>
- `std::locale` constructors: <https://en.cppreference.com/w/cpp/locale/locale/locale>
- `std::has_facet`: <https://en.cppreference.com/w/cpp/locale/has_facet>
- `std::use_facet`: <https://en.cppreference.com/w/cpp/locale/use_facet>
- `std::numpunct`: <https://en.cppreference.com/w/cpp/locale/numpunct>
- `std::ctype<char>`: <https://en.cppreference.com/w/cpp/locale/ctype_char>
- `std::messages::open`: <https://en.cppreference.com/w/cpp/locale/messages/open>
- `std::get_money`: <https://en.cppreference.com/w/cpp/io/manip/get_money>
- `std::put_money`: <https://en.cppreference.com/w/cpp/io/manip/put_money>
- `std::get_time`: <https://en.cppreference.com/w/cpp/io/manip/get_time>
- `std::put_time`: <https://en.cppreference.com/w/cpp/io/manip/put_time>
- `std::map`: <https://en.cppreference.com/w/cpp/container/map>
- `std::set`: <https://en.cppreference.com/w/cpp/container/set>
- `std::multiset::erase`: <https://en.cppreference.com/w/cpp/container/multiset/erase>
- `std::multimap::equal_range`: <https://en.cppreference.com/w/cpp/container/multimap/equal_range>
- `std::unordered_map`: <https://en.cppreference.com/w/cpp/container/unordered_map>
- `std::unordered_set`: <https://en.cppreference.com/w/cpp/container/unordered_set>
- `std::unordered_multiset::count`: <https://en.cppreference.com/w/cpp/container/unordered_multiset/count>
- `std::unordered_multimap::equal_range`: <https://en.cppreference.com/w/cpp/container/unordered_multimap/equal_range>
- `std::queue`: <https://en.cppreference.com/w/cpp/container/queue>
- `std::stack::push`: <https://en.cppreference.com/w/cpp/container/stack/push>
- `std::stack::emplace`: <https://en.cppreference.com/w/cpp/container/stack/emplace>
- `std::priority_queue`: <https://en.cppreference.com/w/cpp/container/priority_queue>
- `std::span`: <https://en.cppreference.com/w/cpp/container/span>
- `std::string`: <https://en.cppreference.com/w/cpp/string/basic_string>
- `std::string_view`: <https://en.cppreference.com/w/cpp/string/basic_string_view>
- `std::sort`: <https://en.cppreference.com/w/cpp/algorithm/sort>
- `std::find`: <https://en.cppreference.com/w/cpp/algorithm/find>
- `std::transform`: <https://en.cppreference.com/w/cpp/algorithm/transform>
- `std::remove`: <https://en.cppreference.com/w/cpp/algorithm/remove>
- `std::partition`: <https://en.cppreference.com/w/cpp/algorithm/partition>
- `std::binary_search`: <https://en.cppreference.com/w/cpp/algorithm/binary_search>
- `std::lower_bound`: <https://en.cppreference.com/w/cpp/algorithm/lower_bound>
- `std::upper_bound`: <https://en.cppreference.com/w/cpp/algorithm/upper_bound>
- `std::equal_range`: <https://en.cppreference.com/w/cpp/algorithm/equal_range>
- `std::stable_sort`: <https://en.cppreference.com/w/cpp/algorithm/stable_sort>
- `std::nth_element`: <https://en.cppreference.com/w/cpp/algorithm/nth_element>
- `std::partial_sort`: <https://en.cppreference.com/w/cpp/algorithm/partial_sort>
- `std::all_of` / `std::any_of` / `std::none_of`: <https://en.cppreference.com/w/cpp/algorithm/all_any_none_of>
- `std::count`: <https://en.cppreference.com/w/cpp/algorithm/count>
- `std::mismatch`: <https://en.cppreference.com/w/cpp/algorithm/mismatch>
- `std::equal`: <https://en.cppreference.com/w/cpp/algorithm/equal>
- `std::copy`: <https://en.cppreference.com/w/cpp/algorithm/copy>
- `std::copy_n`: <https://en.cppreference.com/w/cpp/algorithm/copy_n>
- `std::copy_backward`: <https://en.cppreference.com/w/cpp/algorithm/copy_backward>
- `std::move_backward`: <https://en.cppreference.com/w/cpp/algorithm/move_backward>
- `std::replace`: <https://en.cppreference.com/w/cpp/algorithm/replace>
- `std::fill`: <https://en.cppreference.com/w/cpp/algorithm/fill>
- `std::generate`: <https://en.cppreference.com/w/cpp/algorithm/generate>
- `std::unique`: <https://en.cppreference.com/w/cpp/algorithm/unique>
- `std::reverse`: <https://en.cppreference.com/w/cpp/algorithm/reverse>
- `std::rotate`: <https://en.cppreference.com/w/cpp/algorithm/rotate>
- `std::shift_left` / `std::shift_right`: <https://en.cppreference.com/w/cpp/algorithm/shift>
- `std::sample`: <https://en.cppreference.com/w/cpp/algorithm/sample>
- `std::partition_copy`: <https://en.cppreference.com/w/cpp/algorithm/partition_copy>
- `std::stable_partition`: <https://en.cppreference.com/w/cpp/algorithm/stable_partition>
- `std::is_partitioned`: <https://en.cppreference.com/w/cpp/algorithm/is_partitioned>
- `std::partition_point`: <https://en.cppreference.com/w/cpp/algorithm/partition_point>
- `std::includes`: <https://en.cppreference.com/w/cpp/algorithm/includes>
- `std::set_union`: <https://en.cppreference.com/w/cpp/algorithm/set_union>
- `std::set_intersection`: <https://en.cppreference.com/w/cpp/algorithm/set_intersection>
- `std::set_difference`: <https://en.cppreference.com/w/cpp/algorithm/set_difference>
- `std::set_symmetric_difference`: <https://en.cppreference.com/w/cpp/algorithm/set_symmetric_difference>
- `std::inplace_merge`: <https://en.cppreference.com/w/cpp/algorithm/inplace_merge>
- `std::is_heap`: <https://en.cppreference.com/w/cpp/algorithm/is_heap>
- `std::minmax`: <https://en.cppreference.com/w/cpp/algorithm/minmax>
- `std::clamp`: <https://en.cppreference.com/w/cpp/algorithm/clamp>
- `std::minmax_element`: <https://en.cppreference.com/w/cpp/algorithm/minmax_element>
- `std::prev_permutation`: <https://en.cppreference.com/w/cpp/algorithm/prev_permutation>
- `std::is_permutation`: <https://en.cppreference.com/w/cpp/algorithm/is_permutation>
- `std::ranges` filter/transform views: <https://en.cppreference.com/w/cpp/ranges>
- `std::ranges::sort`: <https://en.cppreference.com/w/cpp/algorithm/ranges/sort>
- `std::ranges::take_view`: <https://en.cppreference.com/w/cpp/ranges/take_view>
- `std::ranges::drop_view`: <https://en.cppreference.com/w/cpp/ranges/drop_view>
- `std::ranges::reverse_view`: <https://en.cppreference.com/w/cpp/ranges/reverse_view>
- `std::ranges::join_view`: <https://en.cppreference.com/w/cpp/ranges/join_view>
- `std::ranges::split_view`: <https://en.cppreference.com/w/cpp/ranges/split_view>
- `std::ranges::keys_view`: <https://en.cppreference.com/w/cpp/ranges/keys_view>
- `std::ranges::values_view`: <https://en.cppreference.com/w/cpp/ranges/values_view>
- `std::ranges::elements_view`: <https://en.cppreference.com/w/cpp/ranges/elements_view>
- `std::ranges::zip_view`: <https://en.cppreference.com/w/cpp/ranges/zip_view>
- `std::ranges::chunk_view`: <https://en.cppreference.com/w/cpp/ranges/chunk_view>
- `std::ranges::slide_view`: <https://en.cppreference.com/w/cpp/ranges/slide_view>
- `std::ranges::stride_view`: <https://en.cppreference.com/w/cpp/ranges/stride_view>
- `std::ranges::repeat_view`: <https://en.cppreference.com/w/cpp/ranges/repeat_view>
- `std::ranges::enumerate_view`: <https://en.cppreference.com/w/cpp/ranges/enumerate_view>
- `std::ranges::cartesian_product_view`: <https://en.cppreference.com/w/cpp/ranges/cartesian_product_view>
- `std::ranges::chunk_by_view`: <https://en.cppreference.com/w/cpp/ranges/chunk_by_view>
- `std::ranges::join_with_view`: <https://en.cppreference.com/w/cpp/ranges/join_with_view>
- `std::ranges::as_rvalue_view`: <https://en.cppreference.com/w/cpp/ranges/as_rvalue_view>
- `std::ranges::as_const_view`: <https://en.cppreference.com/w/cpp/ranges/as_const_view>
- `std::ranges::contains` / `std::ranges::contains_subrange`: <https://en.cppreference.com/w/cpp/algorithm/ranges/contains>
- `std::ranges::starts_with`: <https://en.cppreference.com/w/cpp/algorithm/ranges/starts_with>
- `std::ranges::ends_with`: <https://en.cppreference.com/w/cpp/algorithm/ranges/ends_with>
- `std::ranges::find_last`: <https://en.cppreference.com/w/cpp/algorithm/ranges/find_last>
- `std::ranges::fold_left`: <https://en.cppreference.com/w/cpp/algorithm/ranges/fold_left>
- `std::merge`: <https://en.cppreference.com/w/cpp/algorithm/merge>
- `std::make_heap`: <https://en.cppreference.com/w/cpp/algorithm/make_heap>
- `std::next_permutation`: <https://en.cppreference.com/w/cpp/algorithm/next_permutation>
- `std::accumulate`: <https://en.cppreference.com/w/cpp/algorithm/accumulate>
- `std::iota`: <https://en.cppreference.com/w/cpp/algorithm/iota>
- `std::partial_sum`: <https://en.cppreference.com/w/cpp/algorithm/partial_sum>
- `std::gcd`: <https://en.cppreference.com/w/cpp/numeric/gcd>
- `std::midpoint`: <https://en.cppreference.com/w/cpp/numeric/midpoint>
- `std::lcm`: <https://en.cppreference.com/w/cpp/numeric/lcm>
- `std::inner_product`: <https://en.cppreference.com/w/cpp/algorithm/inner_product>
- `std::adjacent_difference`: <https://en.cppreference.com/w/cpp/algorithm/adjacent_difference>
- `std::inclusive_scan`: <https://en.cppreference.com/w/cpp/algorithm/inclusive_scan>
- `std::reduce`: <https://en.cppreference.com/w/cpp/algorithm/reduce>
- `std::transform_reduce`: <https://en.cppreference.com/w/cpp/algorithm/transform_reduce>
- `std::exclusive_scan`: <https://en.cppreference.com/w/cpp/algorithm/exclusive_scan>
- `std::transform_inclusive_scan`: <https://en.cppreference.com/w/cpp/algorithm/transform_inclusive_scan>
- `std::transform_exclusive_scan`: <https://en.cppreference.com/w/cpp/algorithm/transform_exclusive_scan>
- `std::lerp`: <https://en.cppreference.com/w/cpp/numeric/lerp>
- `std::bitset`: <https://en.cppreference.com/w/cpp/utility/bitset>
- `std::popcount`: <https://en.cppreference.com/w/cpp/numeric/popcount>
- `std::rotl`: <https://en.cppreference.com/w/cpp/numeric/rotl>
- `std::rotr`: <https://en.cppreference.com/w/cpp/numeric/rotr>
- `std::countl_zero`: <https://en.cppreference.com/w/cpp/numeric/countl_zero>
- `std::countr_zero`: <https://en.cppreference.com/w/cpp/numeric/countr_zero>
- `std::has_single_bit`: <https://en.cppreference.com/w/cpp/numeric/has_single_bit>
- `std::bit_ceil`: <https://en.cppreference.com/w/cpp/numeric/bit_ceil>
- `std::bit_floor`: <https://en.cppreference.com/w/cpp/numeric/bit_floor>
- `std::bit_width`: <https://en.cppreference.com/w/cpp/numeric/bit_width>
- `std::bit_cast`: <https://en.cppreference.com/w/cpp/numeric/bit_cast>
- `std::endian`: <https://en.cppreference.com/w/cpp/types/endian>
- `std::byteswap`: <https://en.cppreference.com/w/cpp/numeric/byteswap>
- `std::to_chars`: <https://en.cppreference.com/w/cpp/utility/to_chars>
- `std::from_chars`: <https://en.cppreference.com/cpp/utility/from_chars>
- `std::variant`: <https://en.cppreference.com/w/cpp/utility/variant>
- `std::visit`: <https://en.cppreference.com/w/cpp/utility/variant/visit>
- `std::any`: <https://en.cppreference.com/w/cpp/utility/any>
- `std::expected`: <https://en.cppreference.com/w/cpp/utility/expected>
- `std::source_location`: <https://en.cppreference.com/w/cpp/utility/source_location>
- `std::reference_wrapper`: <https://en.cppreference.com/w/cpp/utility/functional/reference_wrapper>
- `std::invoke`: <https://en.cppreference.com/w/cpp/utility/functional/invoke>
- `std::exchange`: <https://en.cppreference.com/w/cpp/utility/exchange>
- `std::move`: <https://en.cppreference.com/w/cpp/utility/move>
- `std::move_only_function`: <https://en.cppreference.com/w/cpp/utility/functional/move_only_function>
- `std::as_const`: <https://en.cppreference.com/w/cpp/utility/as_const>
- `std::apply`: <https://en.cppreference.com/w/cpp/utility/apply>
- `std::make_from_tuple`: <https://en.cppreference.com/w/cpp/utility/make_from_tuple>
- `std::forward_like`: <https://en.cppreference.com/w/cpp/utility/forward_like>
- `std::cmp_*` / `std::in_range`: <https://en.cppreference.com/w/cpp/utility/intcmp>
- `std::is_same`: <https://en.cppreference.com/w/cpp/types/is_same>
- `std::type_identity`: <https://en.cppreference.com/w/cpp/types/type_identity>
- `std::type_index`: <https://en.cppreference.com/w/cpp/types/type_index>
- `std::integer_sequence`: <https://en.cppreference.com/w/cpp/utility/integer_sequence>
- `std::to_underlying`: <https://en.cppreference.com/w/cpp/utility/to_underlying>
- `std::ratio`: <https://en.cppreference.com/w/cpp/numeric/ratio>
- `std::derived_from` / `std::same_as`: <https://en.cppreference.com/w/cpp/concepts>
- `std::strong_ordering`: <https://en.cppreference.com/w/cpp/utility/compare/strong_ordering>
- `std::weak_ordering`: <https://en.cppreference.com/w/cpp/utility/compare/weak_ordering>
- `std::partial_ordering`: <https://en.cppreference.com/w/cpp/utility/compare/partial_ordering>
- `std::numbers`: <https://en.cppreference.com/w/cpp/numeric/constants>
- `std::format`: <https://en.cppreference.com/w/cpp/utility/format/format>
- `std::formatter`: <https://en.cppreference.com/w/cpp/utility/format/formatter>
- `std::formatter<range>`: <https://en.cppreference.com/w/cpp/utility/format/ranges_formatter>
- `std::range_formatter`: <https://en.cppreference.com/w/cpp/utility/format/range_formatter>
- `std::print`: <https://en.cppreference.com/w/cpp/io/print>
- `std::quoted`: <https://en.cppreference.com/w/cpp/io/manip/quoted>
- `std::stringstream`: <https://en.cppreference.com/w/cpp/io/basic_stringstream>
- `std::basic_ispanstream`: <https://en.cppreference.com/w/cpp/io/basic_ispanstream>
- `std::basic_ospanstream`: <https://en.cppreference.com/w/cpp/io/basic_ospanstream>
- `std::basic_spanstream`: <https://en.cppreference.com/w/cpp/io/basic_spanstream>
- `std::regex`: <https://en.cppreference.com/w/cpp/regex>
- `std::regex_match`: <https://en.cppreference.com/w/cpp/regex/regex_match>
- `std::regex_iterator`: <https://en.cppreference.com/w/cpp/regex/regex_iterator>
- `std::regex_token_iterator`: <https://en.cppreference.com/w/cpp/regex/regex_token_iterator>
- `std::distance`: <https://en.cppreference.com/w/cpp/iterator/distance>
- `std::advance`: <https://en.cppreference.com/w/cpp/iterator/advance>
- `std::next`: <https://en.cppreference.com/w/cpp/iterator/next>
- `std::back_inserter`: <https://en.cppreference.com/w/cpp/iterator/back_inserter>
- `std::complex`: <https://en.cppreference.com/w/cpp/numeric/complex>
- `std::complex std::exp`: <https://en.cppreference.com/w/cpp/numeric/complex/exp>
- `std::complex std::pow`: <https://en.cppreference.com/w/cpp/numeric/complex/pow>
- `std::valarray::slice`: <https://en.cppreference.com/w/cpp/numeric/valarray/slice>
- `std::random_device`: <https://en.cppreference.com/w/cpp/numeric/random/random_device>
- `std::uniform_int_distribution`: <https://en.cppreference.com/w/cpp/numeric/random/uniform_int_distribution>
- `std::chrono::current_zone`: <https://en.cppreference.com/w/cpp/chrono/current_zone>
- `std::chrono::locate_zone`: <https://en.cppreference.com/w/cpp/chrono/locate_zone>
- `std::chrono::zoned_time`: <https://en.cppreference.com/w/cpp/chrono/zoned_time>
- `std::chrono::time_zone::get_info`: <https://en.cppreference.com/w/cpp/chrono/time_zone/get_info>
- `std::chrono::year_month_weekday` accessors: <https://en.cppreference.com/w/cpp/chrono/year_month_weekday/accessors>
- `std::chrono::year_month_day`: <https://en.cppreference.com/w/cpp/chrono/year_month_day>
- `std::chrono::weekday`: <https://en.cppreference.com/w/cpp/chrono/weekday>
- `std::chrono::hh_mm_ss`: <https://en.cppreference.com/w/cpp/chrono/hh_mm_ss>
- `std::chrono::file_clock`: <https://en.cppreference.com/w/cpp/chrono/file_clock>
- `std::chrono::utc_clock`: <https://en.cppreference.com/w/cpp/chrono/utc_clock>
- `std::chrono::tai_clock`: <https://en.cppreference.com/w/cpp/chrono/tai_clock>
- `std::chrono::gps_clock`: <https://en.cppreference.com/w/cpp/chrono/gps_clock>
- `std::chrono::clock_cast`: <https://en.cppreference.com/w/cpp/chrono/clock_cast>
- `std::chrono::parse`: <https://en.cppreference.com/w/cpp/chrono/parse>
- `std::chrono::floor`: <https://en.cppreference.com/w/cpp/chrono/duration/floor>
- `std::chrono::round`: <https://en.cppreference.com/w/cpp/chrono/duration/round>
- `std::chrono::ceil`: <https://en.cppreference.com/w/cpp/chrono/duration/ceil>
- `std::unique_ptr`: <https://en.cppreference.com/w/cpp/memory/unique_ptr>
- `std::pmr::monotonic_buffer_resource`: <https://en.cppreference.com/w/cpp/memory/monotonic_buffer_resource>
- `std::allocator`: <https://en.cppreference.com/w/cpp/memory/allocator>
- `std::allocator_traits::construct`: <https://en.cppreference.com/w/cpp/memory/allocator_traits/construct>
- `std::uses_allocator`: <https://en.cppreference.com/w/cpp/memory/uses_allocator>
- `std::uses_allocator_construction_args`: <https://en.cppreference.com/w/cpp/memory/uses_allocator_construction_args>
- `std::make_obj_using_allocator`: <https://en.cppreference.com/w/cpp/memory/make_obj_using_allocator>
- `std::uninitialized_construct_using_allocator`: <https://en.cppreference.com/w/cpp/memory/uninitialized_construct_using_allocator>
- `std::scoped_allocator_adaptor`: <https://en.cppreference.com/w/cpp/memory/scoped_allocator_adaptor>
- `std::enable_shared_from_this`: <https://en.cppreference.com/w/cpp/memory/enable_shared_from_this>
- `std::owner_less`: <https://en.cppreference.com/w/cpp/memory/owner_less>
- `std::to_address`: <https://en.cppreference.com/w/cpp/memory/to_address>
- `std::addressof`: <https://en.cppreference.com/w/cpp/memory/addressof>
- `std::align`: <https://en.cppreference.com/w/cpp/memory/align>
- `std::assume_aligned`: <https://en.cppreference.com/w/cpp/memory/assume_aligned>
- `std::pmr::null_memory_resource`: <https://en.cppreference.com/w/cpp/memory/null_memory_resource>
- `std::pmr::polymorphic_allocator`: <https://en.cppreference.com/w/cpp/memory/polymorphic_allocator>
- `std::pmr::unsynchronized_pool_resource`: <https://en.cppreference.com/w/cpp/memory/unsynchronized_pool_resource>
- `std::pmr::synchronized_pool_resource`: <https://en.cppreference.com/w/cpp/memory/synchronized_pool_resource>
- `std::pmr::new_delete_resource`: <https://en.cppreference.com/w/cpp/memory/new_delete_resource>
- `std::filesystem::path` lexical operations: <https://en.cppreference.com/w/cpp/filesystem/path/lexically_normal>
- `std::filesystem::absolute`: <https://en.cppreference.com/w/cpp/filesystem/absolute>
- `std::filesystem::relative` / `std::filesystem::proximate`: <https://en.cppreference.com/w/cpp/filesystem/relative>
- `std::filesystem::current_path`: <https://en.cppreference.com/w/cpp/filesystem/current_path>
- `std::filesystem::directory_iterator`: <https://en.cppreference.com/w/cpp/filesystem/directory_iterator>
- `std::filesystem::recursive_directory_iterator`: <https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator>
- `std::filesystem::recursive_directory_iterator::disable_recursion_pending`: <https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator/disable_recursion_pending>
- `std::filesystem::recursive_directory_iterator::increment`: <https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator/increment>
- `std::filesystem::copy`: <https://en.cppreference.com/w/cpp/filesystem/copy>
- `std::filesystem::copy_file`: <https://en.cppreference.com/w/cpp/filesystem/copy_file>
- `std::filesystem::create_directory`: <https://en.cppreference.com/w/cpp/filesystem/create_directory>
- `std::filesystem::create_hard_link`: <https://en.cppreference.com/w/cpp/filesystem/create_hard_link>
- `std::filesystem::create_symlink` / `std::filesystem::create_directory_symlink`: <https://en.cppreference.com/w/cpp/filesystem/create_symlink>
- `std::filesystem::read_symlink`: <https://en.cppreference.com/w/cpp/filesystem/read_symlink>
- `std::filesystem::copy_symlink`: <https://en.cppreference.com/w/cpp/filesystem/copy_symlink>
- `std::filesystem::directory_entry`: <https://en.cppreference.com/w/cpp/filesystem/directory_entry>
- `std::filesystem::directory_entry::refresh`: <https://en.cppreference.com/w/cpp/filesystem/directory_entry/refresh>
- `std::filesystem::equivalent`: <https://en.cppreference.com/w/cpp/filesystem/equivalent>
- `std::filesystem::file_size`: <https://en.cppreference.com/w/cpp/filesystem/file_size>
- `std::filesystem::hard_link_count`: <https://en.cppreference.com/w/cpp/filesystem/hard_link_count>
- `std::filesystem::status` / `std::filesystem::symlink_status`: <https://en.cppreference.com/w/cpp/filesystem/status>
- `std::filesystem::exists`: <https://en.cppreference.com/w/cpp/filesystem/exists>
- `std::filesystem::is_directory`: <https://en.cppreference.com/w/cpp/filesystem/is_directory>
- `std::filesystem::is_regular_file`: <https://en.cppreference.com/w/cpp/filesystem/is_regular_file>
- `std::filesystem::is_empty`: <https://en.cppreference.com/w/cpp/filesystem/is_empty>
- `std::filesystem::permissions`: <https://en.cppreference.com/w/cpp/filesystem/permissions>
- `std::filesystem::resize_file`: <https://en.cppreference.com/w/cpp/filesystem/resize_file>
- `std::filesystem::remove`: <https://en.cppreference.com/w/cpp/filesystem/remove>
- `std::filesystem::remove_all`: <https://en.cppreference.com/w/cpp/filesystem/remove>
- `std::filesystem::space`: <https://en.cppreference.com/w/cpp/filesystem/space>
- `std::filesystem::rename`: <https://en.cppreference.com/w/cpp/filesystem/rename>
- `std::filesystem::temp_directory_path`: <https://en.cppreference.com/w/cpp/filesystem/temp_directory_path>
- `std::filesystem::last_write_time`: <https://en.cppreference.com/w/cpp/filesystem/last_write_time>
- `std::filesystem::canonical` / `std::filesystem::weakly_canonical`: <https://en.cppreference.com/w/cpp/filesystem/canonical>
- `std::function`: <https://en.cppreference.com/w/cpp/utility/functional/function>
- `std::bind`: <https://en.cppreference.com/w/cpp/utility/functional/bind>
- `std::not_fn`: <https://en.cppreference.com/w/cpp/utility/functional/not_fn>
- `std::mem_fn`: <https://en.cppreference.com/w/cpp/utility/functional/mem_fn>
- `std::bind_front`: <https://en.cppreference.com/w/cpp/utility/functional/bind_front>

The `std::atomic` example keeps the cppreference worker logic and uses the
exact `std::jthread` form when C++20 atomic wait/notify support is available.

The `std::atomic_flag` example keeps the cppreference spinlock logic and uses
the optional C++20 wait/notify path when the feature-test macro is available.

The `std::atomic_fetch_add` and `std::atomic_compare_exchange` examples keep
the cppreference algorithms. The compare-exchange stack adds driver-harness
cleanup so pushed nodes are freed before unload. The `atomic_thread_fence`
page presents the mailbox pattern as a sketch, so the harness wraps the same
fence operation in a deterministic mailbox check. The `atomic_signal_fence`
page has no standalone example, so the harness uses a small direct API check.

The `std::latch`, `std::barrier`, and semaphore examples keep the cppreference
synchronization flow. The barrier example uses `std::osyncstream`, and the
semaphore example keeps the original worker delay.

The `std::stop_source` and `std::stop_callback` examples keep the
cppreference cooperative-cancellation flow.

The `std::condition_variable_any`, `std::lock_guard`, `std::scoped_lock`,
`std::lock`, `std::unique_lock`, `std::try_lock`, `std::recursive_mutex`,
`std::async`, `std::future`, `std::future::valid`,
`std::future::wait_for`,
`std::future::wait_until`, `std::shared_future`, and
`std::shared_future::wait_for` examples keep the cppreference synchronization
flow. The driver harness shortens long illustrative sleep durations or loop
counts where the original example uses second-scale delays or very large
iteration counts. The `std::shared_timed_mutex` page marks its example
incomplete and only constructs the resource class, so the harness performs one
assignment to exercise the demonstrated lock path. The `std::shared_lock`,
`std::timed_mutex`, and `std::recursive_timed_mutex` pages have no standalone
examples, so the harness uses small direct checks for those types. The
`std::future_error` page demonstrates an undefined empty-future `get()` path;
the harness uses a defined `future_already_retrieved` error path instead.
The `std::promise::set_exception` example keeps the cppreference exception
handoff flow.

The `std::unique_ptr` example keeps the ownership, polymorphism, custom deleter,
array, and linked-list portions, including the hosted file I/O custom deleter,
the locale-imbued output, and the cppreference linked-list stress size.

The locale harness ports the linked cppreference locale, facet, money, and time
examples. The messages example does not claim hosted message-catalog support.

The `std::any` example keeps the `any_cast`, bad-cast, reset, pointer-access,
and `any::type().name()` behavior.

The `std::iota` example keeps the cppreference list/iterator/shuffle structure.

The `std::reference_wrapper` example keeps the cppreference shuffle structure.

The chrono timezone tests keep the cppreference `current_zone` / `zoned_time`
flow and add explicit `locate_zone` / `time_zone::get_info` offset checks for
selected zones.

The chrono calendar/time-of-day tests use the linked cppreference calendar
accessor and `hh_mm_ss` APIs.

The chrono clock conversion pages document public conversion APIs but do not
provide standalone examples. The driver harness exercises those documented
conversion paths directly. For `file_clock`, MSVC STL exposes the documented
UTC conversion pair.
The `std::chrono::parse` page documents the parser family rather than one fixed
standalone example, so the harness exercises the documented stream manipulator
on a timezone-free `year_month_day`.

The `std::stack::push` example keeps the cppreference BrainHack interpreter
shape. The x64 driver-test harness invokes it through `ntl::expand_stack`
because the example embeds a 32 KiB tape buffer in the interpreter object.

The listed C++23 `std::views` examples are compiled into the driver test when
the matching feature-test macro is available. The `enumerate`,
`cartesian_product`, `chunk_by`, and `join_with` examples keep the linked page
operations and replace large presentation output with assertions suitable for
the driver harness.

The additional C++20 range adaptor harness groups the linked `take`, `drop`,
`reverse`, `join`, `split`, `keys`, `values`, and `elements` examples into one
compact assertion-based driver test to avoid large debugger-output blocks.

The `std::to_chars` test follows the cppreference example, including the
floating-point overload calls. The `std::from_chars` test follows the
cppreference integer example and adds floating-point overload checks for the
same page.

The `std::expected` example is compiled into the driver test when the
`__cpp_lib_expected` feature-test macro is available.

The `std::format` and `std::print` examples run in the default driver build.
The `std::formatter` customization example keeps the cppreference formatter
specialization and output calls when `std::format` is available. The
`std::formatter<range>` / `std::range_formatter` coverage keeps the
cppreference range-format assertions when the feature-test macro is available.
The `std::quoted` example keeps the cppreference default/custom delimiter flow.
The `basic_stringstream` and spanstream pages are API references without
standalone examples, so the harness uses small direct string-backed and
span-backed stream checks.

The `std::regex` example keeps the cppreference `regex_search`, iterator, and
`regex_replace` flow.

The filesystem examples listed above are ported into the driver harness.
The `copy_symlink` page currently has no cppreference example, so the harness
uses a small direct check for that function.
The path-relation test restores `current_path` after running because the
driver harness executes many filesystem examples in one process.
The `error_code&` overload, `directory_entry::refresh`, and recursive iterator
modifier checks use scratch directories to exercise the documented public
APIs without depending on a particular host filesystem layout.

The `std::complex` test keeps the arithmetic portion of the cppreference
example and also includes the `std::exp` and `std::pow` examples.

The `std::lerp` example is enabled in the default kernel-driver build.

The `std::pmr::monotonic_buffer_resource` example is compiled into the default
driver run with the cppreference iteration and node counts.
The `polymorphic_allocator` and pool-resource pages are API reference pages
without standalone examples; the harness uses direct pmr container/resource
checks for those public types.
