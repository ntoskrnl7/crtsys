# cppreference 예제 귀속

[기능 적용 범위로 돌아가기](./feature-coverage.ko-KR.md)

일부 드라이버 테스트는 [cppreference](https://en.cppreference.com)의 예제를
이식한 것입니다. 원본 페이지는 해당 테스트 파일에서 링크합니다.

cppreference 콘텐츠는 Creative Commons Attribution-ShareAlike 3.0 Unported License와
GNU Free Documentation License에 따라 라이선스가 부여됩니다. 다음을 참조하세요.

- <https://cppreference.com/Cppreference:Copyrights>
- <https://www.cppreference.com/Cppreference:Copyright/CC-BY-SA>
- <https://en.cppreference.com/Cppreference:Copyright/GDFL>

예제는 crtsys 커널 드라이버 테스트 하네스 안에서 실행하기 위해 필요한 범위에서만
변경합니다. 보통 샘플 `main()` 본문을 네임스페이스의 `run()` 함수로 옮기고, 지원하지
않는 실행 컨텍스트를 피합니다.

## 이식된 예

- `std::exception_ptr`: <https://en.cppreference.com/w/cpp/error/Exception_ptr>
- `std::optional`: <https://en.cppreference.com/w/cpp/utility/optional>
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
- `std::noop_coroutine`: <https://en.cppreference.com/w/cpp/coroutine/noop_coroutine>
- `std::generator`: <https://en.cppreference.com/w/cpp/coroutine/generator>
- `std::mutex`: <https://en.cppreference.com/w/cpp/thread/mutex>
- `std::lock_guard`: <https://en.cppreference.com/w/cpp/thread/lock_guard>
- `std::shared_mutex`: <https://en.cppreference.com/w/cpp/thread/shared_mutex>
- `std::shared_timed_mutex`: <https://en.cppreference.com/w/cpp/thread/shared_timed_mutex>
- `std::shared_lock`: <https://en.cppreference.com/w/cpp/thread/shared_lock>
- `std::timed_mutex`: <https://en.cppreference.com/w/cpp/thread/timed_mutex>
- `std::recursive_mutex`: <https://en.cppreference.com/w/cpp/thread/recursive_mutex>
- `std::recursive_timed_mutex`: <https://en.cppreference.com/w/cpp/thread/recursive_timed_mutex>
- `std::scoped_lock`: <https://en.cppreference.com/w/cpp/thread/scoped_lock>
- `std::lock`: <https://en.cppreference.com/w/cpp/thread/lock>
- `std::unique_lock`: <https://en.cppreference.com/w/cpp/thread/unique_lock>
- `std::try_lock`: <https://en.cppreference.com/w/cpp/thread/try_lock>
- `std::call_once`: <https://en.cppreference.com/w/cpp/thread/call_once>
- `std::future`: <https://en.cppreference.com/w/cpp/thread/future>
- `std::async`: <https://en.cppreference.com/w/cpp/thread/async>
- `std::future::wait_until`: <https://en.cppreference.com/w/cpp/thread/future/wait_until>
- `std::future_error`: <https://en.cppreference.com/w/cpp/thread/future_error>
- `std::shared_future`: <https://en.cppreference.com/w/cpp/thread/shared_future>
- `std::promise`: <https://en.cppreference.com/w/cpp/thread/promise>
- `std::packaged_task`: <https://en.cppreference.com/w/cpp/thread/packaged_task>
- `std::jthread`: <https://en.cppreference.com/w/cpp/thread/jthread/jthread>
- `std::stop_source` / `std::stop_token`: <https://en.cppreference.com/w/cpp/thread/stop_source>
- `std::stop_callback`: <https://en.cppreference.com/w/cpp/thread/stop_callback>
- `std::weak_ptr`: <https://en.cppreference.com/w/cpp/memory/weak_ptr>
- `std::array`: <https://en.cppreference.com/w/cpp/container/array>
- `std::vector`: <https://en.cppreference.com/w/cpp/container/Vector>
- `std::vector::emplace`: <https://en.cppreference.com/w/cpp/container/Vector/emplace>
- `std::vector::erase`: <https://en.cppreference.com/w/cpp/container/Vector/erase>
- `std::deque`: <https://en.cppreference.com/w/cpp/container/deque>
- `std::deque::emplace`: <https://en.cppreference.com/w/cpp/container/deque/emplace>
- `std::deque::erase`: <https://en.cppreference.com/w/cpp/container/deque/erase>
- `std::list`: <https://en.cppreference.com/w/cpp/container/list>
- `std::list::emplace`: <https://en.cppreference.com/w/cpp/container/list/emplace>
- `std::list::erase`: <https://en.cppreference.com/w/cpp/container/list/erase>
- `std::list::splice`: <https://en.cppreference.com/w/cpp/container/list/splice>
- `std::list::merge`: <https://en.cppreference.com/w/cpp/container/list/merge>
- `std::list::remove`: <https://en.cppreference.com/w/cpp/container/list/remove>
- `std::list::sort`: <https://en.cppreference.com/w/cpp/container/list/sort>
- `std::list::unique`: <https://en.cppreference.com/w/cpp/container/list/unique>
- `std::forward_list::insert_after`: <https://en.cppreference.com/w/cpp/container/forward_list/insert_after>
- `std::forward_list::erase_after`: <https://en.cppreference.com/w/cpp/container/forward_list/erase_after>
- `std::forward_list::splice_after`: <https://en.cppreference.com/w/cpp/container/forward_list/splice_after>
- `std::forward_list::merge`: <https://en.cppreference.com/w/cpp/container/forward_list/merge>
- `std::forward_list::remove`: <https://en.cppreference.com/w/cpp/container/forward_list/remove>
- `std::forward_list::sort`: <https://en.cppreference.com/w/cpp/container/forward_list/sort>
- `std::forward_list::unique`: <https://en.cppreference.com/w/cpp/container/forward_list/unique>
- `std::locale`: <https://en.cppreference.com/w/cpp/locale/locale>
- `std::locale` 생성자: <https://en.cppreference.com/w/cpp/locale/locale/locale>
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
- `std::flat_map`: <https://en.cppreference.com/w/cpp/container/flat_map>
- `std::map::insert_or_assign`: <https://en.cppreference.com/w/cpp/container/map/insert_or_assign>
- `std::map::try_emplace`: <https://en.cppreference.com/w/cpp/container/map/try_emplace>
- `std::map::contains`: <https://en.cppreference.com/w/cpp/container/map/contains>
- `std::map::extract`: <https://en.cppreference.com/w/cpp/container/map/extract>
- `std::map::merge`: <https://en.cppreference.com/w/cpp/container/map/merge>
- `std::set`: <https://en.cppreference.com/w/cpp/container/set>
- `std::flat_set`: <https://en.cppreference.com/w/cpp/container/flat_set>
- `std::set::contains`: <https://en.cppreference.com/w/cpp/container/set/contains>
- `std::set::extract`: <https://en.cppreference.com/w/cpp/container/set/extract>
- `std::set::merge`: <https://en.cppreference.com/w/cpp/container/set/merge>
- `std::multiset::erase`: <https://en.cppreference.com/w/cpp/container/multiset/erase>
- `std::multimap::equal_range`: <https://en.cppreference.com/w/cpp/container/multimap/equal_range>
- `std::unordered_map`: <https://en.cppreference.com/w/cpp/container/unordered_map>
- `std::unordered_map::contains`: <https://en.cppreference.com/w/cpp/container/unordered_map/contains>
- `std::unordered_map::extract`: <https://en.cppreference.com/w/cpp/container/unordered_map/extract>
- `std::unordered_map::merge`: <https://en.cppreference.com/w/cpp/container/unordered_map/merge>
- `std::unordered_set`: <https://en.cppreference.com/w/cpp/container/unordered_set>
- `std::unordered_set::contains`: <https://en.cppreference.com/w/cpp/container/unordered_set/contains>
- `std::unordered_set::extract`: <https://en.cppreference.com/w/cpp/container/unordered_set/extract>
- `std::unordered_set::merge`: <https://en.cppreference.com/w/cpp/container/unordered_set/merge>
- `std::unordered_multiset::count`: <https://en.cppreference.com/w/cpp/container/unordered_multiset/count>
- `std::unordered_multimap::equal_range`: <https://en.cppreference.com/w/cpp/container/unordered_multimap/equal_range>
- `std::queue`: <https://en.cppreference.com/w/cpp/container/queue>
- `std::stack::push`: <https://en.cppreference.com/w/cpp/container/stack/push>
- `std::stack::emplace`: <https://en.cppreference.com/w/cpp/container/stack/emplace>
- `std::priority_queue`: <https://en.cppreference.com/w/cpp/container/priority_queue>
- `std::span`: <https://en.cppreference.com/w/cpp/container/span>
- `std::mdspan`: <https://en.cppreference.com/w/cpp/container/mdspan>
- `std::string`: <https://en.cppreference.com/w/cpp/string/basic_string>
- `std::string::find`: <https://en.cppreference.com/w/cpp/string/basic_string/find>
- `std::string::substr`: <https://en.cppreference.com/w/cpp/string/basic_string/substr>
- `std::string::starts_with`: <https://en.cppreference.com/w/cpp/string/basic_string/starts_with>
- `std::string::ends_with`: <https://en.cppreference.com/w/cpp/string/basic_string/ends_with>
- `std::string::contains`: <https://en.cppreference.com/w/cpp/string/basic_string/contains>
- `std::string::erase`: <https://en.cppreference.com/w/cpp/string/basic_string/erase>
- `std::string_view`: <https://en.cppreference.com/w/cpp/string/basic_string_view>
- `std::string_view::find`: <https://en.cppreference.com/w/cpp/string/basic_string_view/find>
- `std::string_view::substr`: <https://en.cppreference.com/w/cpp/string/basic_string_view/substr>
- `std::string_view::starts_with`: <https://en.cppreference.com/w/cpp/string/basic_string_view/starts_with>
- `std::string_view::ends_with`: <https://en.cppreference.com/w/cpp/string/basic_string_view/ends_with>
- `std::string_view::contains`: <https://en.cppreference.com/w/cpp/string/basic_string_view/contains>
- `std::sort`: <https://en.cppreference.com/w/cpp/algorithm/sort>
- 실행 정책 : <https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag>
- `std::find`: <https://en.cppreference.com/w/cpp/algorithm/find>
- `std::transform`: <https://en.cppreference.com/w/cpp/algorithm/transform>
- `std::remove`: <https://en.cppreference.com/w/cpp/algorithm/remove>
- `std::partition`: <https://en.cppreference.com/w/cpp/algorithm/partition>
- `std::binary_search`: <https://en.cppreference.com/w/cpp/algorithm/binary_search>
- `std::lower_bound`: <https://en.cppreference.com/w/cpp/algorithm/lower_bound>
- `std::upper_bound`: <https://en.cppreference.com/w/cpp/algorithm/upper_bound>
- `std::equal_range`: <https://en.cppreference.com/w/cpp/algorithm/equal_range>
- `std::ranges` 필터/변환 보기: <https://en.cppreference.com/w/cpp/ranges>
- `std::ranges::sort`: <https://en.cppreference.com/w/cpp/algorithm/ranges/sort>
- `std::ranges::zip_view`: <https://en.cppreference.com/w/cpp/ranges/zip_view>
- `std::ranges::zip_transform_view`: <https://en.cppreference.com/w/cpp/ranges/zip_transform_view>
- `std::ranges::adjacent_view`: <https://en.cppreference.com/w/cpp/ranges/adjacent_view>
- `std::ranges::adjacent_transform_view`: <https://en.cppreference.com/w/cpp/ranges/adjacent_transform_view>
- `std::ranges::chunk_view`: <https://en.cppreference.com/w/cpp/ranges/chunk_view>
- `std::ranges::chunk_by_view`: <https://en.cppreference.com/w/cpp/ranges/chunk_by_view>
- `std::ranges::slide_view`: <https://en.cppreference.com/w/cpp/ranges/slide_view>
- `std::ranges::stride_view`: <https://en.cppreference.com/w/cpp/ranges/stride_view>
- `std::ranges::repeat_view`: <https://en.cppreference.com/w/cpp/ranges/repeat_view>
- `std::ranges::cartesian_product_view`: <https://en.cppreference.com/w/cpp/ranges/cartesian_product_view>
- `std::ranges::join_with_view`: <https://en.cppreference.com/w/cpp/ranges/join_with_view>
- `std::ranges::enumerate_view`: <https://en.cppreference.com/w/cpp/ranges/enumerate_view>
- `std::ranges::take_view`: <https://en.cppreference.com/w/cpp/ranges/take_view>
- `std::ranges::drop_view`: <https://en.cppreference.com/w/cpp/ranges/drop_view>
- `std::ranges::reverse_view`: <https://en.cppreference.com/w/cpp/ranges/reverse_view>
- `std::ranges::join_view`: <https://en.cppreference.com/w/cpp/ranges/join_view>
- `std::ranges::split_view`: <https://en.cppreference.com/w/cpp/ranges/split_view>
- `std::ranges::values_view`: <https://en.cppreference.com/w/cpp/ranges/values_view>
- `std::ranges::keys_view`: <https://en.cppreference.com/w/cpp/ranges/keys_view>
- `std::ranges::elements_view`: <https://en.cppreference.com/w/cpp/ranges/elements_view>
- `std::merge`: <https://en.cppreference.com/w/cpp/algorithm/merge>
- `std::make_heap`: <https://en.cppreference.com/w/cpp/algorithm/make_heap>
- `std::next_permutation`: <https://en.cppreference.com/w/cpp/algorithm/next_permutation>
- `std::stable_sort`: <https://en.cppreference.com/w/cpp/algorithm/stable_sort>
- `std::nth_element`: <https://en.cppreference.com/w/cpp/algorithm/nth_element>
- `std::partial_sort`: <https://en.cppreference.com/w/cpp/algorithm/partial_sort>
- `std::accumulate`: <https://en.cppreference.com/w/cpp/algorithm/accumulate>
- `std::iota`: <https://en.cppreference.com/w/cpp/algorithm/iota>
- `std::partial_sum`: <https://en.cppreference.com/w/cpp/algorithm/partial_sum>
- `std::gcd`: <https://en.cppreference.com/w/cpp/numeric/gcd>
- `std::midpoint`: <https://en.cppreference.com/w/cpp/numeric/midpoint>
- `std::lcm`: <https://en.cppreference.com/w/cpp/numeric/lcm>
- `std::inner_product`: <https://en.cppreference.com/w/cpp/algorithm/inner_product>
- `std::adjacent_difference`: <https://en.cppreference.com/w/cpp/algorithm/adjacent_difference>
- `std::inclusive_scan`: <https://en.cppreference.com/w/cpp/algorithm/inclusive_scan>
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
- `std::visit`: <https://en.cppreference.com/w/cpp/utility/variant/visit2>
- `std::any`: <https://en.cppreference.com/w/cpp/utility/any>
- `std::expected`: <https://en.cppreference.com/w/cpp/utility/expected>
- `std::type_index`: <https://en.cppreference.com/w/cpp/types/type_index>
- `std::source_location`: <https://en.cppreference.com/w/cpp/utility/source_location>
- `std::stacktrace`: <https://en.cppreference.com/w/cpp/utility/basic_stacktrace>
- `std::reference_wrapper`: <https://en.cppreference.com/w/cpp/utility/functional/reference_wrapper>
- `std::invoke`: <https://en.cppreference.com/w/cpp/utility/functional/invoke>
- `std::exchange`: <https://en.cppreference.com/w/cpp/utility/exchange>
- `std::move`: <https://en.cppreference.com/w/cpp/utility/move>
- `std::is_same`: <https://en.cppreference.com/w/cpp/types/is_same>
- `std::integer_sequence`: <https://en.cppreference.com/w/cpp/utility/integer_sequence>
- `std::ratio`: <https://en.cppreference.com/w/cpp/numeric/ratio>
- `std::derived_from` / `std::same_as`: <https://en.cppreference.com/w/cpp/concepts>
- `std::strong_ordering`: <https://en.cppreference.com/w/cpp/utility/compare/strong_ordering>
- `std::numbers`: <https://en.cppreference.com/w/cpp/numeric/constants>
- `std::chrono::system_clock::now`: <https://en.cppreference.com/w/cpp/chrono/system_clock/now>
- `std::chrono::system_clock::to_time_t`: <https://en.cppreference.com/w/cpp/chrono/system_clock/to_time_t>
- `std::chrono::system_clock::from_time_t`: <https://en.cppreference.com/w/cpp/chrono/system_clock/from_time_t>
- `std::chrono::year_month_day`: <https://en.cppreference.com/w/cpp/chrono/year_month_day>
- `std::chrono::weekday`: <https://en.cppreference.com/w/cpp/chrono/weekday>
- `std::chrono::hh_mm_ss`: <https://en.cppreference.com/w/cpp/chrono/hh_mm_ss>
- `std::chrono::clock_cast`: <https://en.cppreference.com/w/cpp/chrono/clock_cast>
- `std::chrono::utc_clock::from_sys`: <https://en.cppreference.com/w/cpp/chrono/utc_clock/from_sys>
- `std::chrono::utc_clock::to_sys`: <https://en.cppreference.com/w/cpp/chrono/utc_clock/to_sys>
- `std::chrono::tai_clock::from_utc`: <https://en.cppreference.com/w/cpp/chrono/tai_clock/from_utc>
- `std::chrono::tai_clock::to_utc`: <https://en.cppreference.com/w/cpp/chrono/tai_clock/to_utc>
- `std::chrono::gps_clock::from_utc`: <https://en.cppreference.com/w/cpp/chrono/gps_clock/from_utc>
- `std::chrono::gps_clock::to_utc`: <https://en.cppreference.com/w/cpp/chrono/gps_clock/to_utc>
- `std::chrono::file_clock`: <https://en.cppreference.com/w/cpp/chrono/file_clock>
- `std::chrono::file_clock::now`: <https://en.cppreference.com/w/cpp/chrono/file_clock/now>
- `std::chrono::time_zone::to_local`: <https://en.cppreference.com/w/cpp/chrono/time_zone/to_local>
- `std::chrono::time_zone::to_sys`: <https://en.cppreference.com/w/cpp/chrono/time_zone/to_sys>
- `std::chrono::get_tzdb_list`: <https://en.cppreference.com/w/cpp/chrono/get_tzdb_list>
- `std::format`: <https://en.cppreference.com/w/cpp/utility/format/format>
- `std::range_formatter`: <https://en.cppreference.com/w/cpp/utility/format/range_formatter>
- `std::formatter`: <https://en.cppreference.com/w/cpp/utility/format/formatter>
- `std::range_formatter`: <https://en.cppreference.com/w/cpp/utility/format/range_formatter>
- `std::print`: <https://en.cppreference.com/w/cpp/io/print>
- `std::regex`: <https://en.cppreference.com/w/cpp/regex>
- `std::regex_match`: <https://en.cppreference.com/w/cpp/regex/regex_match>
- `std::regex_iterator`: <https://en.cppreference.com/w/cpp/regex/regex_iterator>
- `std::regex_token_iterator`: <https://en.cppreference.com/w/cpp/regex/regex_token_iterator>
- `std::quoted`: <https://en.cppreference.com/w/cpp/io/manip/quoted>
- `std::basic_filebuf`: <https://en.cppreference.com/w/cpp/io/basic_filebuf>
- `std::basic_filebuf::open`: <https://en.cppreference.com/w/cpp/io/basic_filebuf/open>
- `std::basic_filebuf::is_open`: <https://en.cppreference.com/w/cpp/io/basic_filebuf/is_open>
- `std::basic_filebuf::seekoff`: <https://en.cppreference.com/w/cpp/io/basic_filebuf/seekoff>
- `std::basic_filebuf::seekpos`: <https://en.cppreference.com/w/cpp/io/basic_filebuf/seekpos>
- `std::basic_filebuf::underflow`: <https://en.cppreference.com/w/cpp/io/basic_filebuf/underflow>
- `std::basic_ifstream`: <https://en.cppreference.com/w/cpp/io/basic_ifstream>
- `std::basic_ifstream::is_open`: <https://en.cppreference.com/w/cpp/io/basic_ifstream/is_open>
- `std::basic_ofstream`: <https://en.cppreference.com/w/cpp/io/basic_ofstream>
- `std::basic_ofstream` 생성자: <https://en.cppreference.com/w/cpp/io/basic_ofstream/basic_ofstream>
- `std::basic_fstream`: <https://en.cppreference.com/w/cpp/io/basic_fstream>
- `std::basic_fstream::open`: <https://en.cppreference.com/w/cpp/io/basic_fstream/open>
- `std::basic_fstream::is_open`: <https://en.cppreference.com/w/cpp/io/basic_fstream/is_open>
- `std::basic_spanstream`: <https://en.cppreference.com/w/cpp/io/basic_spanstream>
- `std::basic_spanstream::span`: <https://en.cppreference.com/w/cpp/io/basic_spanstream/span>
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
- `std::unique_ptr`: <https://en.cppreference.com/w/cpp/memory/unique_ptr>
- `std::pmr::monotonic_buffer_resource`: <https://en.cppreference.com/w/cpp/memory/monotonic_buffer_resource>
- `std::allocator_traits`: <https://en.cppreference.com/w/cpp/memory/allocator_traits>
- `std::pmr::unsynchronized_pool_resource`: <https://en.cppreference.com/w/cpp/memory/unsynchronized_pool_resource>
- `std::pmr::synchronized_pool_resource`: <https://en.cppreference.com/w/cpp/memory/synchronized_pool_resource>
- `std::pmr::polymorphic_allocator`: <https://en.cppreference.com/w/cpp/memory/polymorphic_allocator>
- `std::pmr::null_memory_resource`: <https://en.cppreference.com/w/cpp/memory/null_memory_resource>
- `std::pmr::new_delete_resource`: <https://en.cppreference.com/w/cpp/memory/new_delete_resource>
- `std::not_fn`: <https://en.cppreference.com/w/cpp/utility/functional/not_fn>
- `std::bind_front`: <https://en.cppreference.com/w/cpp/utility/functional/bind_front>
- `std::mem_fn`: <https://en.cppreference.com/w/cpp/utility/functional/mem_fn>
- `std::filesystem::path` 어휘 연산: <https://en.cppreference.com/w/cpp/filesystem/path/lexically_normal>
- `std::filesystem::path::filename`: <https://en.cppreference.com/w/cpp/filesystem/path/filename>
- `std::filesystem::path::stem`: <https://en.cppreference.com/w/cpp/filesystem/path/stem>
- `std::filesystem::path::extension`: <https://en.cppreference.com/w/cpp/filesystem/path/extension>
- `std::filesystem::path::parent_path`: <https://en.cppreference.com/w/cpp/filesystem/path/parent_path>
- `std::filesystem::path::root_path`: <https://en.cppreference.com/w/cpp/filesystem/path/root_path>
- `std::filesystem::path::relative_path`: <https://en.cppreference.com/w/cpp/filesystem/path/relative_path>
- `std::filesystem::path::append`: <https://en.cppreference.com/w/cpp/filesystem/path/append>
- `std::filesystem::path::concat`: <https://en.cppreference.com/w/cpp/filesystem/path/concat>
- `std::filesystem::path::remove_filename`: <https://en.cppreference.com/w/cpp/filesystem/path/remove_filename>
- `std::filesystem::path::replace_filename`: <https://en.cppreference.com/w/cpp/filesystem/path/replace_filename>
- `std::filesystem::path::replace_extension`: <https://en.cppreference.com/w/cpp/filesystem/path/replace_extension>
- `std::filesystem::path::make_preferred`: <https://en.cppreference.com/w/cpp/filesystem/path/make_preferred>
- `std::filesystem::path::iterator`: <https://en.cppreference.com/w/cpp/filesystem/path/iterator>
- `std::filesystem::path::string`: <https://en.cppreference.com/w/cpp/filesystem/path/string>
- `std::filesystem::path::native`: <https://en.cppreference.com/w/cpp/filesystem/path/native>
- `std::filesystem::path::compare`: <https://en.cppreference.com/w/cpp/filesystem/path/compare>
- `std::filesystem::path` 해싱: <https://en.cppreference.com/w/cpp/filesystem/path/hash>
- `std::filesystem::directory_iterator`: <https://en.cppreference.com/w/cpp/filesystem/directory_iterator>
- `std::filesystem::recursive_directory_iterator`: <https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator>
- `std::filesystem::copy`: <https://en.cppreference.com/w/cpp/filesystem/copy>
- `std::filesystem::copy_file`: <https://en.cppreference.com/w/cpp/filesystem/copy_file>
- `std::filesystem::copy_options`: <https://en.cppreference.com/w/cpp/filesystem/copy_options>
- `std::filesystem::create_directory`: <https://en.cppreference.com/w/cpp/filesystem/create_directory>
- `std::filesystem::create_hard_link`: <https://en.cppreference.com/w/cpp/filesystem/create_hard_link>
- `std::filesystem::create_symlink` / `std::filesystem::create_directory_symlink`: <https://en.cppreference.com/w/cpp/filesystem/create_symlink>
- `std::filesystem::read_symlink`: <https://en.cppreference.com/w/cpp/filesystem/read_symlink>
- `std::filesystem::copy_symlink`: <https://en.cppreference.com/w/cpp/filesystem/copy_symlink>
- `std::filesystem::directory_entry`: <https://en.cppreference.com/w/cpp/filesystem/directory_entry>
- `std::filesystem::directory_entry::assign`: <https://en.cppreference.com/w/cpp/filesystem/directory_entry/assign>
- `std::filesystem::directory_entry::replace_filename`: <https://en.cppreference.com/w/cpp/filesystem/directory_entry/replace_filename>
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
- `std::filesystem::remove_all`: <https://en.cppreference.com/w/cpp/filesystem/remove>
- `std::filesystem::space`: <https://en.cppreference.com/w/cpp/filesystem/space>
- `std::filesystem::rename`: <https://en.cppreference.com/w/cpp/filesystem/rename>
- `std::filesystem::temp_directory_path`: <https://en.cppreference.com/w/cpp/filesystem/temp_directory_path>
- `std::filesystem::absolute`: <https://en.cppreference.com/w/cpp/filesystem/absolute>
- `std::filesystem::current_path`: <https://en.cppreference.com/w/cpp/filesystem/current_path>
- `std::filesystem::relative` / `std::filesystem::proximate`: <https://en.cppreference.com/w/cpp/filesystem/relative>
- `std::filesystem::last_write_time`: <https://en.cppreference.com/w/cpp/filesystem/last_write_time>
- `std::filesystem::canonical` / `std::filesystem::weakly_canonical`: <https://en.cppreference.com/w/cpp/filesystem/canonical>
- `std::function`: <https://en.cppreference.com/w/cpp/utility/functional/function>
- `std::bind`: <https://en.cppreference.com/w/cpp/utility/functional/bind>

`std::atomic` 예제는 cppreference 작업자 논리를 유지하고
C++20 원자 대기/알림 지원이 사용 가능한 경우 정확한 `std::jthread` 형식입니다.

`std::atomic_flag` 예제는 cppreference 스핀록 논리를 유지하고 다음을 사용합니다.
기능 테스트 매크로를 사용할 수 있는 경우 선택적 C++20 대기/알림 경로입니다.

`std::latch`, `std::barrier` 및 세마포어 예제는 cppreference를 유지합니다.
동기화 흐름. 장벽 예제에서는 `std::osyncstream`를 사용하며
세마포어 예제는 원래 작업자 지연을 유지합니다.

`std::stop_source` 및 `std::stop_callback` 예제는
cppreference 협력 취소 흐름.

`std::condition_variable_any`, `std::lock_guard`, `std::scoped_lock`,
`std::lock`, `std::unique_lock`, `std::try_lock`, `std::recursive_mutex`,
`std::async`, `std::future::wait_until` 및 `std::shared_future` 예제는 유지됩니다.
cppreference 동기화 흐름. 드라이버 하네스가 길게 단축됩니다
원래 예제에서 사용한 예시적인 수면 시간 또는 루프 횟수
두 번째 규모의 지연 또는 매우 큰 반복 횟수. `std::shared_lock`,
`std::shared_lock`, `std::timed_mutex`, `std::recursive_timed_mutex` 페이지에는 독립
실행 예제가 없으므로 테스트 하네스가 해당 형식을 직접 검사하는 작은 테스트를
사용합니다. `std::future_error` 페이지는 동작이 정의되지 않은 빈 future의
`get()` 경로를 보여주지만, 하네스에서는 대신 동작이 정의된
`future_already_retrieved` 오류 경로를 사용합니다.

`std::unique_ptr` 예제는 소유권, 다형성, 사용자 정의 deleter, 배열, 연결 리스트
부분을 유지합니다. 여기에는 호스트 파일 I/O용 사용자 정의 deleter, locale을
적용한 출력, cppreference 연결 리스트 스트레스 크기도 포함됩니다.

locale 하네스는 연결된 cppreference의 locale, facet, money, time 예제를
포팅합니다. messages 예제는 저장소의 메모리 내 카탈로그 계약을 다루며, 호스트
메시지 카탈로그는 별도 통합이 필요합니다.

`std::any` 예제는 `any_cast`, 잘못된 cast, reset, 포인터 접근,
`any::type().name()` 동작을 유지합니다.

`std::iota` 예제는 cppreference 목록/반복자/셔플 구조를 유지합니다.

`std::reference_wrapper` 예제는 cppreference 셔플 구조를 유지합니다.

chrono 시간대 테스트는 cppreference의 `current_zone`/`zoned_time` 흐름을 유지하고,
선택한 시간대에 대해 `locate_zone`/`time_zone::get_info` 오프셋 검사를 추가합니다.
chrono 시계 변환 테스트는 연결된 cppreference 변환 규칙을 따릅니다.
`clock_cast` 페이지에는 현재 실행 가능한 예제가 없고, 시계 `now()` 예제는 큰 벡터
할당을 포함한 벤치마크 형태이므로 드라이버 하네스는 같은 시계/OS 시간 API를
유지하되 벤치마크 할당 본문은 제외합니다.

`std::stack::push` 예제는 cppreference BrainHack 인터프리터를 유지합니다.
모양. x64 드라이버 테스트 하네스는 `ntl::expand_stack`를 통해 이를 호출합니다.
예제에서는 인터프리터 객체에 32KiB 테이프 버퍼를 포함하고 있기 때문입니다.

나열된 C++23 `std::views` 예제는 대응하는 기능 테스트 매크로를 사용할 수 있을 때
드라이버 테스트에 컴파일됩니다. 현재 MSVC STL이 제공하면 다음 C++23 view도 함께
컴파일합니다.
`std::views::chunk_by`, `std::views::cartesian_product`,
`std::views::join_with`, `std::views::adjacent`,
`std::views::adjacent_transform` 및 `std::views::enumerate`.
현재 STL이 `std::string_view`의 C++23 range 생성자를 제공하면
`std::ranges::split_view` 예제는 원문과 같은 소스를 유지합니다. 이전 도구 집합은
cppreference에 없는 우회 코드를 쓰는 대신 건너뛰었다는 메시지를 출력합니다.
`std::ranges::keys_view` 및 `std::ranges::elements_view` 예제는
같은 view 연산을 유지합니다. 테스트 파일 인코딩을 단순하게 유지하도록 Unicode
표와 문자 출력은 드라이버 소스에서 ASCII로 음역합니다. 모든 cppreference 예제가
드라이버 인스턴스 하나에서 실행되므로 `keys_view` 하네스는 예제가 끝난 뒤
`std::cout`의 이전 locale을 복원합니다.
`std::views::chunk_by` 및 `std::views::cartesian_product` 예제는
같은 range 연산과 출력 형태를 유지합니다. 직접 작성한 Unicode 문자열은 이스케이프된
UTF-8 바이트로 표현해 드라이버 소스 파일을 ASCII로 유지합니다.

`std::to_chars` 테스트는 다음을 포함하여 cppreference 예제를 따릅니다.
부동 소수점 오버로드 호출. `std::from_chars` 테스트는 다음을 따릅니다.
cppreference 정수 예제를 추가하고 부동 소수점 오버로드 검사를 추가합니다.
같은 페이지.

`std::expected` 예제는 다음과 같은 경우 드라이버 테스트로 컴파일됩니다.
`__cpp_lib_expected` 기능 테스트 매크로를 사용할 수 있습니다.

`std::noop_coroutine`, `std::generator`, `std::mdspan`, `std::stacktrace` 예제는 현재
MSVC STL이 대응 기능 테스트 매크로를 제공할 때 드라이버 테스트에 컴파일됩니다.
현재 STL이 `std::print`도 제공하면 `std::mdspan` 하네스는 cppreference 예제처럼
이를 사용하고, `print` 없이 `mdspan`만 제공하는 이전 도구 집합에서만 대체 경로를
사용합니다. `std::stacktrace` cppreference 예제는 커널 `module+offset` 형식을
검증하는 드라이버 의미 테스트와 분리돼 있습니다. 커널 ABI shim은 사용자 모드
DbgEng를 사용하지 않고, 일치하는 PDB CodeView 레코드를 직접 읽어 함수와 소스 행
진단을 제공합니다.

실행 정책 예제는 `std::execution::seq`, `unseq`, `par`, `par_unseq` 정렬 호출을
유지합니다. 다만 커널 드라이버 테스트가 시간 측정 벤치마크나 대규모 할당 스트레스
테스트가 되지 않도록 원래 벤치마크 크기의 무작위 입력을 더 작은 결정적 입력으로
줄입니다.

`std::flat_map` 및 `std::flat_set` 페이지는 현재 다음을 제공하지 않습니다.
독립형 "이 코드 실행" 예제이므로 하네스는 작은 직접 API를 사용합니다.
활성 MSVC STL이 해당 헤더를 노출하는 시기를 확인합니다.

`std::format`, `std::range_formatter` 및 `std::print` 예제는 다음에서 실행됩니다.
활성 MSVC STL이 해당 드라이버를 노출할 때 기본 드라이버 빌드
기능 테스트 매크로.

`std::basic_spanstream::span` 예제는 활성 MSVC STL이 노출될 때 실행됩니다.
`__cpp_lib_spanstream`.

파일 스트림 예제는 cppreference 페이로드 및 스트림 작업을 유지합니다.
하지만 `test.bin` 및 `test.txt`와 같은 고정된 예제 파일 이름을 테스트별로 매핑합니다.
커널 드라이버 하네스가 이를 반복적으로 실행할 수 있도록 샌드박스 디렉토리를 만들고
결정론적으로 정리합니다.

`std::basic_filebuf::underflow` 예는 인수 없음을 비교합니다.
`char_traits<char>::eof()`에 대한 `istream::get()` 결과는 명시적으로 나타납니다. MSVC에서
해당 EOF 값은 `int_type`로서 진실이므로 cppreference `while
(stream.get())` 루프는 그렇지 않으면 영원히 실행될 수 있습니다.

`std::shared_timed_mutex` 페이지는 보호된 리소스를 다음과 같이 유지합니다.
`/* data */`; 드라이버 하네스는 소형 `int`를 사용하므로 할당 예
복사된 값을 확인할 수 있습니다.

`std::regex` 적용 범위는 cppreference `regex_search`를 유지합니다.
`regex_match`, 반복자, 토큰 반복자 및 `regex_replace` 흐름.

위에 나열된 파일 시스템 예제는 드라이버 하네스에 이식되었습니다.
`copy_symlink` 페이지에는 현재 cpp참조 예제가 없으므로 하네스
해당 기능에 대해 작은 직접 검사를 사용합니다.
`current_path` 및 `canonical` 예제는 원래 전류 경로를 복원합니다.
cppreference 예제는 독립 실행형 프로그램이기 때문에 돌아오기 전에
드라이버 하니스는 하나의 프로세스에서 많은 예제를 실행합니다.

`std::complex` 테스트는 cppreference의 산술 부분을 유지합니다.
예제에는 `std::exp` 및 `std::pow` 예제도 포함되어 있습니다.

`std::lerp` 예제는 기본 커널 드라이버 빌드에서 활성화됩니다.

`std::pmr::monotonic_buffer_resource` 예제는 기본값으로 컴파일됩니다.
드라이버는 cppreference 반복 및 노드 수로 실행됩니다.

`std::allocator_traits` 및 PMR 풀/리소스 검사는 cppreference API를 포함합니다.
독립형 "이 코드 실행" 예제를 제공하지 않는 페이지.
