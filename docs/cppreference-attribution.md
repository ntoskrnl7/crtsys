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
- `std::optional`: <https://en.cppreference.com/w/cpp/utility/optional>
- `std::tuple`: <https://en.cppreference.com/w/cpp/utility/tuple>
- `std::pair` / `std::make_pair`: <https://en.cppreference.com/w/cpp/utility/pair/make_pair>
- `std::shared_ptr`: <https://en.cppreference.com/w/cpp/memory/shared_ptr>
- `std::atomic`: <https://en.cppreference.com/w/cpp/atomic/atomic>
- `std::atomic_flag`: <https://en.cppreference.com/w/cpp/atomic/atomic_flag>
- `std::weak_ptr`: <https://en.cppreference.com/w/cpp/memory/weak_ptr>
- `std::array`: <https://en.cppreference.com/w/cpp/container/array>
- `std::vector`: <https://en.cppreference.com/w/cpp/container/vector>
- `std::deque`: <https://en.cppreference.com/w/cpp/container/deque>
- `std::list`: <https://en.cppreference.com/w/cpp/container/list>
- `std::forward_list::insert_after`: <https://en.cppreference.com/w/cpp/container/forward_list/insert_after>
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
- `std::ranges` filter/transform views: <https://en.cppreference.com/w/cpp/ranges>
- `std::ranges::sort`: <https://en.cppreference.com/w/cpp/algorithm/ranges/sort>
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
- `std::lerp`: <https://en.cppreference.com/w/cpp/numeric/lerp>
- `std::bitset`: <https://en.cppreference.com/w/cpp/utility/bitset>
- `std::popcount`: <https://en.cppreference.com/w/cpp/numeric/popcount>
- `std::to_chars`: <https://en.cppreference.com/w/cpp/utility/to_chars>
- `std::from_chars`: <https://en.cppreference.com/cpp/utility/from_chars>
- `std::variant`: <https://en.cppreference.com/w/cpp/utility/variant>
- `std::any`: <https://en.cppreference.com/w/cpp/utility/any>
- `std::expected`: <https://en.cppreference.com/w/cpp/utility/expected>
- `std::source_location`: <https://en.cppreference.com/w/cpp/utility/source_location>
- `std::reference_wrapper`: <https://en.cppreference.com/w/cpp/utility/functional/reference_wrapper>
- `std::invoke`: <https://en.cppreference.com/w/cpp/utility/functional/invoke>
- `std::exchange`: <https://en.cppreference.com/w/cpp/utility/exchange>
- `std::move`: <https://en.cppreference.com/w/cpp/utility/move>
- `std::is_same`: <https://en.cppreference.com/w/cpp/types/is_same>
- `std::ratio`: <https://en.cppreference.com/w/cpp/numeric/ratio>
- `std::derived_from` / `std::same_as`: <https://en.cppreference.com/w/cpp/concepts>
- `std::strong_ordering`: <https://en.cppreference.com/w/cpp/utility/compare/strong_ordering>
- `std::numbers`: <https://en.cppreference.com/w/cpp/numeric/constants>
- `std::distance`: <https://en.cppreference.com/w/cpp/iterator/distance>
- `std::advance`: <https://en.cppreference.com/w/cpp/iterator/advance>
- `std::next`: <https://en.cppreference.com/w/cpp/iterator/next>
- `std::back_inserter`: <https://en.cppreference.com/w/cpp/iterator/back_inserter>
- `std::complex`: <https://en.cppreference.com/w/cpp/numeric/complex>
- `std::valarray::slice`: <https://en.cppreference.com/w/cpp/numeric/valarray/slice>
- `std::uniform_int_distribution`: <https://en.cppreference.com/w/cpp/numeric/random/uniform_int_distribution>
- `std::unique_ptr`: <https://en.cppreference.com/w/cpp/memory/unique_ptr>
- `std::pmr::monotonic_buffer_resource`: <https://en.cppreference.com/w/cpp/memory/monotonic_buffer_resource>
- `std::function`: <https://en.cppreference.com/w/cpp/utility/functional/function>
- `std::bind`: <https://en.cppreference.com/w/cpp/utility/functional/bind>

The `std::atomic` example keeps the cppreference worker logic, but the default
kernel-driver build uses `std::thread` with explicit joins instead of
`std::jthread`. The exact `std::jthread` form is available behind
`CRTSYS_ENABLE_EXACT_CPPREFERENCE_JTHREAD_ATOMIC_EXAMPLE`; it currently requires
MSVC STL atomic wait/notify ABI helpers that crtsys does not implement yet.

The `std::atomic_flag` example keeps the cppreference spinlock logic, but the
default kernel-driver build disables the optional C++20 wait/notify path for
the same ABI-helper reason.

The `std::unique_ptr` example keeps the ownership, polymorphism, custom deleter,
array, and linked-list portions. The default kernel-driver build skips the
hosted file I/O demo and uses a smaller linked-list size; the exact linked-list
stress size is available behind
`CRTSYS_ENABLE_EXACT_CPPREFERENCE_UNIQUE_PTR_STRESS_TEST`.

The `std::any` example keeps the `any_cast`, bad-cast, reset, and pointer-access
behavior. The default kernel-driver build prints known sample type names instead
of calling `any::type().name()`, because that path requires MSVC `type_info`
name/cleanup ABI helpers that crtsys does not implement yet.

The `std::iota` example keeps the list/iterator/shuffle structure. The default
kernel-driver build uses a fixed `std::mt19937` seed instead of
`std::random_device`, because the latter can depend on hosted entropy APIs.

The `std::reference_wrapper` example uses the same fixed-seed approach for its
shuffle step, for the same reason.

The `std::merge` and `std::uniform_int_distribution` examples also use fixed
`std::mt19937` seeds instead of `std::random_device`.

The `std::stack::push` example keeps the cppreference BrainHack interpreter
shape. The x64 driver-test harness invokes it through `ntl::expand_stack`
because the example embeds a 32 KiB tape buffer in the interpreter object.

The `std::to_chars` and `std::from_chars` tests currently cover the integer
paths. Floating-point conversion examples are intentionally left out of the
default kernel-driver run until their MSVC STL dependency surface is reviewed.

The `std::expected` example is present as a conditional harness. The current
MSVC 14.32 toolset used by the driver build does not provide `<expected>`, so
the default run reports that the feature is unavailable.

The `std::complex` test keeps only the arithmetic portion of the cppreference
example by default. The transcendental portions using functions such as
`std::pow` and `std::exp` require MSVC math helper symbols that crtsys does not
provide yet.

The `std::lerp` example is available behind
`CRTSYS_ENABLE_UNSUPPORTED_LERP_TEST`. The default kernel-driver build disables
it because MSVC's `std::lerp(float)` implementation can reference the `fmaf`
helper in driver builds, which crtsys does not provide yet.

The `std::pmr::monotonic_buffer_resource` example is available behind
`CRTSYS_ENABLE_UNSUPPORTED_PMR_TEST` with smaller default iteration and node
counts. The default kernel-driver build does not enable it because MSVC's PMR
implementation requires resource ABI helpers such as
`_Aligned_get_default_resource` that crtsys does not provide yet.
