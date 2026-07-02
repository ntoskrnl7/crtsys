# Feature Coverage

[Back to README](../README.md)

This page is a driver-tested feature coverage matrix, not an exhaustive support
boundary or a negative compatibility table. It records features and code paths
that are explicitly built into the `crtsys` kernel driver test target and run
under that driver harness.

A feature missing from this list should be read as "not yet explicitly covered
by the driver tests", not as "unsupported", unless it is called out under known
limitations or blockers.

`crtsys` does not reimplement a small, project-specific STL. It builds around
MSVC CRT/STL/VCRT/UCRT source paths and the LDK Windows / NTDLL / ICU-compatible
API substrate used by those paths. That means unlisted STL/header paths often
share the same underlying runtime dependencies as the listed tests. They are
not automatically guaranteed in every kernel context, but absence from this
matrix is not evidence of non-support.

Legend:

- [x] Driver-test covered: explicitly exercised by the `crtsys` kernel driver
  tests.
- [ ] Not yet covered: not yet covered by an explicit driver test, or still
  under review.
- Known limitation: a confirmed missing dependency or semantic difference.

Coverage here means that the feature is implemented and exercised by the driver
test suite. It does not mean the feature is valid in every driver execution
context, and it does not attempt to list every MSVC STL/header path that may be
usable. Unless an API documents a broader contract, assume `PASSIVE_LEVEL`. See
[Design Rationale and Operational Boundaries](./design-rationale.md) and the
[NTL API reference](./ntl-api.md) for execution-context guidance.

## IRQL Coverage Contract

The checklist below is a feature-coverage list, not a blanket permission to use
each feature at any IRQL. The table gives the conservative `crtsys` contract for
the covered usage. "Audited caller context" means the operation itself is
value-only and nonblocking, but the caller must still guarantee resident code
and storage, no allocation, no waits, no exceptions, and a valid WDK context.
The driver tests still exercise these features from `PASSIVE_LEVEL`.

| Feature area | Covered items | Supported IRQL | Notes |
| --- | --- | --- | --- |
| Runtime and C++ initialization | Non-local static initialization, dynamic initialization, function-local `static` | `PASSIVE_LEVEL` / driver initialization path | The `crtsys` wrapper runs these paths from `DriverEntry`. General C++ `thread_local` is not supported as true per-thread TLS. |
| C++ exceptions | `throw`, `try`, function try blocks, `std::exception_ptr` | `PASSIVE_LEVEL` only | Do not let exceptions cross WDK callback boundaries, spin-lock-held regions, DPC, ISR, or paging I/O paths. |
| C++ RTTI | `typeid`, `dynamic_cast` | Audited caller context; otherwise `PASSIVE_LEVEL` | The consuming driver target must compile with RTTI enabled. Keep the target object and its type metadata resident. |
| STL value-only helpers | Type traits, concepts, `std::array`, `std::span`, `std::string_view`, `std::bitset`, `std::pair`, `std::tuple`, `std::ratio`, `std::source_location`, `std::strong_ordering`, `std::numbers`, simple `std::move` / `std::exchange` / `std::invoke` / `std::reference_wrapper`, integer `std::to_chars` / `std::from_chars`, pure algorithms over resident fixed storage | Audited caller context; otherwise `PASSIVE_LEVEL` | These are the only STL areas that may be reasonable above `PASSIVE_LEVEL`, and only after the exact expression and its callbacks/comparators are audited. |
| STL owning objects, containers, and callback-heavy utilities | `std::string`, containers, `std::any`, `std::function`, smart pointers, `std::optional`, `std::variant`, `std::complex`, `std::valarray`, `std::filesystem::path`, basic `std::filesystem` file operations, `std::format`, `std::print`, random distributions, `std::regex`, `nlohmann::json`, algorithms that allocate, throw, or call arbitrary user code | `PASSIVE_LEVEL` only | Construction, destruction, comparison, hashing, allocation, deallocation, formatting, file-system I/O, and user callbacks can pull in runtime paths. |
| STL synchronization, threading, and async | `std::thread`, `std::mutex`, `std::shared_mutex`, `std::condition_variable`, `std::condition_variable_any`, `std::scoped_lock`, `std::lock`, `std::try_lock`, `std::call_once`, `std::future`, `std::shared_future`, `std::promise`, `std::packaged_task` | `PASSIVE_LEVEL` only | These paths may wait, create worker execution, allocate, or outlive unload if the caller is careless. |
| STL atomic primitives | `std::atomic`, `std::atomic_ref`, and `std::atomic_flag` load, store, exchange, compare-exchange, and C++20 wait/notify over resident storage | `<= DISPATCH_LEVEL` for audited non-waiting operations; `PASSIVE_LEVEL` only for wait/notify | Treat wait/notify as blocking synchronization. Do not combine atomic code with runtime-backed callbacks or allocation at elevated IRQL. |
| I/O streams | `std::cin`, `std::cout`, `std::cerr`, `std::clog`, wide stream variants | `PASSIVE_LEVEL` only | Diagnostic and test support. Avoid production hot paths and stack-sensitive paths. |
| C math and floating-point helpers | Math functions and floating-point classification helpers | `PASSIVE_LEVEL` unless the exact helper is separately audited | Floating-point state and helper dependencies are driver-context sensitive. |
| NTL entry, driver, device, and RPC server helpers | `ntl::main`, `ntl::driver`, `ntl::device`, `ntl::rpc::server` | `PASSIVE_LEVEL` only | Intended for initialization, teardown, device setup, and IOCTL/RPC control paths. |
| NTL IRP view | `ntl::irp` | Follows the dispatch path that supplied the IRP | NTL device callbacks should still be treated as `PASSIVE_LEVEL` unless the exact callback body is separately audited. |
| NTL status wrapper | `ntl::status` | Caller context | Value-only wrapper around `NTSTATUS`. |
| NTL stack expansion | `ntl::expand_stack` | `PASSIVE_LEVEL` only | Runtime-backed control-path helper, not a hot-path escape hatch. |
| NTL ERESOURCE wrapper | `ntl::resource`, `ntl::unique_lock<ntl::resource>`, `ntl::shared_lock<ntl::resource>` | `<= APC_LEVEL` | Blocking/resource-style synchronization. Do not use in DPC, ISR, or spin-lock-held paths. |
| NTL spin lock wrapper | `ntl::spin_lock`, `ntl::unique_lock<ntl::spin_lock>` | `<= DISPATCH_LEVEL` | Keep held regions resident, short, nonblocking, and free of allocation, waits, exceptions, streams, and arbitrary STL/runtime helpers. |
| NTL IRQL helpers | `ntl::irql`, `ntl::raise_irql`, `ntl::raise_irql_to_dpc_level`, `ntl::raise_irql_to_synch_level` | Explicitly manipulates current IRQL | Keep raised scopes as small as possible. |
| NTL RPC client | `ntl::rpc::client` | User mode, not kernel IRQL | Client side uses `DeviceIoControl`; server callback contract still controls kernel-side safety. |

## C++ Standard

The tests are based on examples and behavior described by
[cppreference](https://en.cppreference.com). Ports of cppreference Example code
are tracked in the [cppreference attribution note](./cppreference-attribution.md).

### Initialization

- [x] [Non-local variables](https://en.cppreference.com/w/cpp/language/initialization#Non-local_variables)
  - [x] [Static initialization](https://en.cppreference.com/w/cpp/language/initialization#Static_initialization)
    - [x] [Constant initialization](https://en.cppreference.com/w/cpp/language/constant_initialization)
      [(tested)](../test/cmake/driver/src/cpp/lang/initialization.cpp#L13)
    - [x] [Zero initialization](https://en.cppreference.com/w/cpp/language/zero_initialization)
      [(tested)](../test/cmake/driver/src/cpp/lang/initialization.cpp#L41)
  - [x] [Dynamic initialization](https://en.cppreference.com/w/cpp/language/initialization#Dynamic_initialization)
    [(tested)](../test/cmake/driver/src/cpp/lang/initialization.cpp#L56)
- [x] [Function-local `static` variables](https://en.cppreference.com/w/cpp/language/storage_duration#Static_local_variables)
  [(tested)](../test/cmake/driver/src/cpp/lang/initialization.cpp#L124)
  [(regression)](../test/cmake/driver/src/cpp/lang/initialization.cpp#L229)
- [ ] [`thread_local`](https://en.cppreference.com/w/cpp/language/storage_duration#Thread_storage_duration)
  [(disabled cppreference example)](../test/cmake/driver/src/cpp/lang/initialization.cpp#L79)

### Exceptions

- [x] [throw](https://en.cppreference.com/w/cpp/language/throw)
  [(tested)](../test/cmake/driver/src/cpp/lang/exceptions.cpp#L42)
- [x] [try block](https://en.cppreference.com/w/cpp/language/try_catch)
  [(tested)](../test/cmake/driver/src/cpp/lang/exceptions.cpp#L60)
- [x] [Function try block](https://en.cppreference.com/w/cpp/language/function-try-block)
  [(tested)](../test/cmake/driver/src/cpp/lang/exceptions.cpp#L98)
- [x] [std::exception_ptr](https://en.cppreference.com/w/cpp/error/exception_ptr)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/exception.cpp)
- [x] Diagnostics and system-error helpers:
      [std::uncaught_exceptions](https://en.cppreference.com/w/cpp/error/uncaught_exception),
      [std::nested_exception](https://en.cppreference.com/w/cpp/error/nested_exception),
      [std::throw_with_nested](https://en.cppreference.com/w/cpp/error/throw_with_nested),
      [std::rethrow_if_nested](https://en.cppreference.com/w/cpp/error/rethrow_if_nested),
      [std::error_code](https://en.cppreference.com/w/cpp/error/error_code),
      [std::error_condition](https://en.cppreference.com/w/cpp/error/error_condition),
      and [std::system_error](https://en.cppreference.com/w/cpp/error/system_error)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/exception.cpp)

### RTTI

- [x] [typeid](https://en.cppreference.com/w/cpp/language/typeid)
  [(tested)](../test/cmake/driver/src/cpp/lang/rtti.cpp)
- [x] [dynamic_cast](https://en.cppreference.com/w/cpp/language/dynamic_cast)
  [(tested)](../test/cmake/driver/src/cpp/lang/rtti.cpp)

## Microsoft STL

- [x] [std::accumulate](https://en.cppreference.com/w/cpp/algorithm/accumulate)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::adjacent_difference](https://en.cppreference.com/w/cpp/algorithm/adjacent_difference)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::array](https://en.cppreference.com/w/cpp/container/array)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/atomic.cpp)
- [x] [std::atomic_ref](https://en.cppreference.com/w/cpp/atomic/atomic_ref)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/atomic.cpp)
- [x] [std::atomic::wait/notify](https://en.cppreference.com/w/cpp/atomic/atomic/wait)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/atomic.cpp)
- [x] [std::atomic_flag](https://en.cppreference.com/w/cpp/atomic/atomic_flag)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/atomic.cpp)
- [x] Atomic fences and free atomic operations:
      [`std::atomic_thread_fence`](https://en.cppreference.com/w/cpp/atomic/atomic_thread_fence),
      [`std::atomic_signal_fence`](https://en.cppreference.com/w/cpp/atomic/atomic_signal_fence),
      [`std::atomic_fetch_add`](https://en.cppreference.com/w/cpp/atomic/atomic_fetch_add),
      [`std::atomic_compare_exchange`](https://en.cppreference.com/w/cpp/atomic/atomic_compare_exchange)
  [(cppreference examples and direct API coverage)](../test/cmake/driver/src/cpp/stl/atomic.cpp)
- [x] [std::chrono](https://en.cppreference.com/w/cpp/chrono)
  [(tested)](../test/cmake/driver/src/cpp/stl/chrono.cpp#L15)
  - C++20 timezone paths for
    [`std::chrono::current_zone`](https://en.cppreference.com/w/cpp/chrono/current_zone),
    [`std::chrono::locate_zone`](https://en.cppreference.com/w/cpp/chrono/locate_zone),
    [`std::chrono::zoned_time`](https://en.cppreference.com/w/cpp/chrono/zoned_time),
    and
    [`std::chrono::time_zone::get_info`](https://en.cppreference.com/w/cpp/chrono/time_zone/get_info)
    also run in the default driver build.
  - C++20 calendar/time-of-day coverage includes
    [`std::chrono::year_month_day`](https://en.cppreference.com/w/cpp/chrono/year_month_day),
    [`std::chrono::weekday`](https://en.cppreference.com/w/cpp/chrono/weekday), and
    [`std::chrono::hh_mm_ss`](https://en.cppreference.com/w/cpp/chrono/hh_mm_ss).
  - C++20 clock conversion coverage includes
    [`std::chrono::file_clock`](https://en.cppreference.com/w/cpp/chrono/file_clock),
    [`std::chrono::utc_clock`](https://en.cppreference.com/w/cpp/chrono/utc_clock),
    [`std::chrono::tai_clock`](https://en.cppreference.com/w/cpp/chrono/tai_clock),
    [`std::chrono::gps_clock`](https://en.cppreference.com/w/cpp/chrono/gps_clock),
    and [`std::chrono::clock_cast`](https://en.cppreference.com/w/cpp/chrono/clock_cast).
  - C++20 chrono stream parsing coverage includes
    [`std::chrono::parse`](https://en.cppreference.com/w/cpp/chrono/parse)
    for `std::chrono::year_month_day`.
  - Duration rounding coverage includes
    [`std::chrono::floor`](https://en.cppreference.com/w/cpp/chrono/duration/floor),
    [`std::chrono::round`](https://en.cppreference.com/w/cpp/chrono/duration/round),
    and [`std::chrono::ceil`](https://en.cppreference.com/w/cpp/chrono/duration/ceil).
- [x] [std::any](https://en.cppreference.com/w/cpp/utility/any)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::bind](https://en.cppreference.com/w/cpp/utility/functional/bind)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/functional.cpp)
- [x] [std::not_fn](https://en.cppreference.com/w/cpp/utility/functional/not_fn),
      [std::mem_fn](https://en.cppreference.com/w/cpp/utility/functional/mem_fn),
      [std::bind_front](https://en.cppreference.com/w/cpp/utility/functional/bind_front)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/functional.cpp)
- [x] [std::binary_search](https://en.cppreference.com/w/cpp/algorithm/binary_search)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::lower_bound](https://en.cppreference.com/w/cpp/algorithm/lower_bound),
      [std::upper_bound](https://en.cppreference.com/w/cpp/algorithm/upper_bound),
      [std::equal_range](https://en.cppreference.com/w/cpp/algorithm/equal_range)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::bitset](https://en.cppreference.com/w/cpp/utility/bitset)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [`<bit>` utilities](https://en.cppreference.com/w/cpp/utility/bit)
  [(cppreference popcount and bit operation examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::bit_cast](https://en.cppreference.com/w/cpp/numeric/bit_cast)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::endian](https://en.cppreference.com/w/cpp/types/endian)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::byteswap](https://en.cppreference.com/w/cpp/numeric/byteswap)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::concepts](https://en.cppreference.com/w/cpp/concepts)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::complex](https://en.cppreference.com/w/cpp/numeric/complex)
  arithmetic plus [std::exp](https://en.cppreference.com/w/cpp/numeric/complex/exp)
  and [std::pow](https://en.cppreference.com/w/cpp/numeric/complex/pow)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/numeric.cpp)
- [x] [std::deque](https://en.cppreference.com/w/cpp/container/deque)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::exchange](https://en.cppreference.com/w/cpp/utility/exchange)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::find](https://en.cppreference.com/w/cpp/algorithm/find)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::forward_list::insert_after](https://en.cppreference.com/w/cpp/container/forward_list/insert_after)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::function](https://en.cppreference.com/w/cpp/utility/functional/function)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/functional.cpp)
- [x] [std::gcd](https://en.cppreference.com/w/cpp/numeric/gcd)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::from_chars](https://en.cppreference.com/cpp/utility/from_chars)
  [(cppreference example + floating-point checks)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::inclusive_scan](https://en.cppreference.com/w/cpp/algorithm/inclusive_scan)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] Numeric reduce/scan algorithms:
      [std::reduce](https://en.cppreference.com/w/cpp/algorithm/reduce),
      [std::transform_reduce](https://en.cppreference.com/w/cpp/algorithm/transform_reduce),
      [std::exclusive_scan](https://en.cppreference.com/w/cpp/algorithm/exclusive_scan),
      [std::transform_inclusive_scan](https://en.cppreference.com/w/cpp/algorithm/transform_inclusive_scan),
      and [std::transform_exclusive_scan](https://en.cppreference.com/w/cpp/algorithm/transform_exclusive_scan)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::inner_product](https://en.cppreference.com/w/cpp/algorithm/inner_product)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::advance](https://en.cppreference.com/w/cpp/iterator/advance),
      [std::distance](https://en.cppreference.com/w/cpp/iterator/distance),
      [std::next](https://en.cppreference.com/w/cpp/iterator/next), and
      [std::back_inserter](https://en.cppreference.com/w/cpp/iterator/back_inserter)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/numeric.cpp)
- [x] [std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::iota](https://en.cppreference.com/w/cpp/algorithm/iota)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::is_same](https://en.cppreference.com/w/cpp/types/is_same)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::type_identity](https://en.cppreference.com/w/cpp/types/type_identity),
      [std::type_index](https://en.cppreference.com/w/cpp/types/type_index),
      [std::integer_sequence](https://en.cppreference.com/w/cpp/utility/integer_sequence)
  [(cppreference examples and documented-note coverage)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::lcm](https://en.cppreference.com/w/cpp/numeric/lcm)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::lerp](https://en.cppreference.com/w/cpp/numeric/lerp)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::list](https://en.cppreference.com/w/cpp/container/list)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::locale](https://en.cppreference.com/w/cpp/locale/locale)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/locale.cpp)
- [x] [std::map](https://en.cppreference.com/w/cpp/container/map)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::make_heap](https://en.cppreference.com/w/cpp/algorithm/make_heap)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::merge](https://en.cppreference.com/w/cpp/algorithm/merge)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::midpoint](https://en.cppreference.com/w/cpp/numeric/midpoint)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::move](https://en.cppreference.com/w/cpp/utility/move)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
  [(feature-test-gated cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::multimap::equal_range](https://en.cppreference.com/w/cpp/container/multimap/equal_range)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::multiset::erase](https://en.cppreference.com/w/cpp/container/multiset/erase)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::next_permutation](https://en.cppreference.com/w/cpp/algorithm/next_permutation)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::optional](https://en.cppreference.com/w/cpp/utility/optional)
  and monadic operations
  [`and_then`](https://en.cppreference.com/w/cpp/utility/optional/and_then),
  [`transform`](https://en.cppreference.com/w/cpp/utility/optional/transform),
  and `or_else`
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::expected](https://en.cppreference.com/w/cpp/utility/expected)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::pair](https://en.cppreference.com/w/cpp/utility/pair)
  [(cppreference make_pair example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::pmr::monotonic_buffer_resource](https://en.cppreference.com/w/cpp/memory/monotonic_buffer_resource)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/memory.cpp)
- [x] [std::allocator_traits::construct](https://en.cppreference.com/w/cpp/memory/allocator_traits/construct),
      [std::pmr::null_memory_resource](https://en.cppreference.com/w/cpp/memory/null_memory_resource)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/memory.cpp)
- [x] Allocator and pointer utility helpers:
      [std::allocator](https://en.cppreference.com/w/cpp/memory/allocator),
      [std::uses_allocator](https://en.cppreference.com/w/cpp/memory/uses_allocator),
      [std::uses_allocator_construction_args](https://en.cppreference.com/w/cpp/memory/uses_allocator_construction_args),
      [std::make_obj_using_allocator](https://en.cppreference.com/w/cpp/memory/make_obj_using_allocator),
      [std::uninitialized_construct_using_allocator](https://en.cppreference.com/w/cpp/memory/uninitialized_construct_using_allocator),
      [std::scoped_allocator_adaptor](https://en.cppreference.com/w/cpp/memory/scoped_allocator_adaptor),
      [std::enable_shared_from_this](https://en.cppreference.com/w/cpp/memory/enable_shared_from_this),
      [std::owner_less](https://en.cppreference.com/w/cpp/memory/owner_less),
      [std::to_address](https://en.cppreference.com/w/cpp/memory/to_address),
      [std::addressof](https://en.cppreference.com/w/cpp/memory/addressof),
      [std::align](https://en.cppreference.com/w/cpp/memory/align),
      and [std::assume_aligned](https://en.cppreference.com/w/cpp/memory/assume_aligned)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/memory.cpp)
- [x] [std::polymorphic_allocator](https://en.cppreference.com/w/cpp/memory/polymorphic_allocator),
      [std::pmr::unsynchronized_pool_resource](https://en.cppreference.com/w/cpp/memory/unsynchronized_pool_resource),
      [std::pmr::synchronized_pool_resource](https://en.cppreference.com/w/cpp/memory/synchronized_pool_resource),
      [std::pmr::new_delete_resource](https://en.cppreference.com/w/cpp/memory/new_delete_resource)
  [(direct API coverage)](../test/cmake/driver/src/cpp/stl/memory.cpp)
- [x] [std::partial_sum](https://en.cppreference.com/w/cpp/algorithm/partial_sum)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::partition](https://en.cppreference.com/w/cpp/algorithm/partition)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::priority_queue](https://en.cppreference.com/w/cpp/container/priority_queue)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::queue](https://en.cppreference.com/w/cpp/container/queue)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [`std::ranges` filter/transform views](https://en.cppreference.com/w/cpp/ranges)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] Additional C++20 range adaptors:
      [`std::views::take`](https://en.cppreference.com/w/cpp/ranges/take_view),
      [`std::views::drop`](https://en.cppreference.com/w/cpp/ranges/drop_view),
      [`std::views::reverse`](https://en.cppreference.com/w/cpp/ranges/reverse_view),
      [`std::views::join`](https://en.cppreference.com/w/cpp/ranges/join_view),
      [`std::views::split`](https://en.cppreference.com/w/cpp/ranges/split_view),
      [`std::views::keys`](https://en.cppreference.com/w/cpp/ranges/keys_view),
      [`std::views::values`](https://en.cppreference.com/w/cpp/ranges/values_view),
      [`std::views::elements`](https://en.cppreference.com/w/cpp/ranges/elements_view)
  [(compact driver coverage)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::ranges::sort](https://en.cppreference.com/w/cpp/algorithm/ranges/sort)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::views::zip](https://en.cppreference.com/w/cpp/ranges/zip_view),
      [std::views::chunk](https://en.cppreference.com/w/cpp/ranges/chunk_view),
      [std::views::slide](https://en.cppreference.com/w/cpp/ranges/slide_view),
      [std::views::stride](https://en.cppreference.com/w/cpp/ranges/stride_view),
      [std::views::repeat](https://en.cppreference.com/w/cpp/ranges/repeat_view)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] Additional C++23 range adaptors:
      [std::views::enumerate](https://en.cppreference.com/w/cpp/ranges/enumerate_view),
      [std::views::cartesian_product](https://en.cppreference.com/w/cpp/ranges/cartesian_product_view),
      [std::views::chunk_by](https://en.cppreference.com/w/cpp/ranges/chunk_by_view),
      [std::views::join_with](https://en.cppreference.com/w/cpp/ranges/join_with_view),
      [std::views::as_rvalue](https://en.cppreference.com/w/cpp/ranges/as_rvalue_view),
      [std::views::as_const](https://en.cppreference.com/w/cpp/ranges/as_const_view)
  [(feature-test-gated cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] Additional C++23 ranges algorithms:
      [`std::ranges::contains`](https://en.cppreference.com/w/cpp/algorithm/ranges/contains),
      [`std::ranges::contains_subrange`](https://en.cppreference.com/w/cpp/algorithm/ranges/contains),
      [`std::ranges::starts_with`](https://en.cppreference.com/w/cpp/algorithm/ranges/starts_with),
      [`std::ranges::ends_with`](https://en.cppreference.com/w/cpp/algorithm/ranges/ends_with),
      [`std::ranges::find_last`](https://en.cppreference.com/w/cpp/algorithm/ranges/find_last),
      and [`std::ranges::fold_left`](https://en.cppreference.com/w/cpp/algorithm/ranges/fold_left)
  [(feature-test-gated cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::ratio](https://en.cppreference.com/w/cpp/numeric/ratio)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::reference_wrapper](https://en.cppreference.com/w/cpp/utility/functional/reference_wrapper)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::remove](https://en.cppreference.com/w/cpp/algorithm/remove)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::set](https://en.cppreference.com/w/cpp/container/set)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/memory.cpp)
- [x] [std::sort](https://en.cppreference.com/w/cpp/algorithm/sort)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::stable_sort](https://en.cppreference.com/w/cpp/algorithm/stable_sort),
      [std::nth_element](https://en.cppreference.com/w/cpp/algorithm/nth_element),
      [std::partial_sort](https://en.cppreference.com/w/cpp/algorithm/partial_sort)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] Additional non-modifying and modifying algorithms:
      [std::all_of / any_of / none_of](https://en.cppreference.com/w/cpp/algorithm/all_any_none_of),
      [std::count](https://en.cppreference.com/w/cpp/algorithm/count),
      [std::mismatch](https://en.cppreference.com/w/cpp/algorithm/mismatch),
      [std::equal](https://en.cppreference.com/w/cpp/algorithm/equal),
      [std::copy](https://en.cppreference.com/w/cpp/algorithm/copy),
      [std::copy_n](https://en.cppreference.com/w/cpp/algorithm/copy_n),
      [std::copy_backward](https://en.cppreference.com/w/cpp/algorithm/copy_backward),
      [std::move_backward](https://en.cppreference.com/w/cpp/algorithm/move_backward),
      [std::replace](https://en.cppreference.com/w/cpp/algorithm/replace),
      [std::fill](https://en.cppreference.com/w/cpp/algorithm/fill),
      [std::generate](https://en.cppreference.com/w/cpp/algorithm/generate),
      [std::unique](https://en.cppreference.com/w/cpp/algorithm/unique),
      [std::reverse](https://en.cppreference.com/w/cpp/algorithm/reverse),
      [std::rotate](https://en.cppreference.com/w/cpp/algorithm/rotate),
      [std::shift_left / shift_right](https://en.cppreference.com/w/cpp/algorithm/shift),
      and [std::sample](https://en.cppreference.com/w/cpp/algorithm/sample)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] Additional partition/set/heap/min-max/permutation algorithms:
      [std::partition_copy](https://en.cppreference.com/w/cpp/algorithm/partition_copy),
      [std::stable_partition](https://en.cppreference.com/w/cpp/algorithm/stable_partition),
      [std::is_partitioned](https://en.cppreference.com/w/cpp/algorithm/is_partitioned),
      [std::partition_point](https://en.cppreference.com/w/cpp/algorithm/partition_point),
      [std::includes](https://en.cppreference.com/w/cpp/algorithm/includes),
      [std::set_union](https://en.cppreference.com/w/cpp/algorithm/set_union),
      [std::set_intersection](https://en.cppreference.com/w/cpp/algorithm/set_intersection),
      [std::set_difference](https://en.cppreference.com/w/cpp/algorithm/set_difference),
      [std::set_symmetric_difference](https://en.cppreference.com/w/cpp/algorithm/set_symmetric_difference),
      [std::inplace_merge](https://en.cppreference.com/w/cpp/algorithm/inplace_merge),
      [std::is_heap](https://en.cppreference.com/w/cpp/algorithm/is_heap),
      [std::minmax](https://en.cppreference.com/w/cpp/algorithm/minmax),
      [std::clamp](https://en.cppreference.com/w/cpp/algorithm/clamp),
      [std::minmax_element](https://en.cppreference.com/w/cpp/algorithm/minmax_element),
      [std::prev_permutation](https://en.cppreference.com/w/cpp/algorithm/prev_permutation),
      and [std::is_permutation](https://en.cppreference.com/w/cpp/algorithm/is_permutation)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::span](https://en.cppreference.com/w/cpp/container/span)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::stack](https://en.cppreference.com/w/cpp/container/stack)
  [(cppreference push/emplace examples)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::source_location](https://en.cppreference.com/w/cpp/utility/source_location)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::strong_ordering](https://en.cppreference.com/w/cpp/utility/compare/strong_ordering)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] Comparison and utility helpers:
      [std::weak_ordering](https://en.cppreference.com/w/cpp/utility/compare/weak_ordering),
      [std::partial_ordering](https://en.cppreference.com/w/cpp/utility/compare/partial_ordering),
      [std::cmp_* / std::in_range](https://en.cppreference.com/w/cpp/utility/intcmp),
      [std::as_const](https://en.cppreference.com/w/cpp/utility/as_const),
      [std::apply](https://en.cppreference.com/w/cpp/utility/apply),
      [std::make_from_tuple](https://en.cppreference.com/w/cpp/utility/make_from_tuple),
      and [std::forward_like](https://en.cppreference.com/w/cpp/utility/forward_like)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::format](https://en.cppreference.com/w/cpp/utility/format)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::print](https://en.cppreference.com/w/cpp/io/print)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::formatter](https://en.cppreference.com/w/cpp/utility/format/formatter)
  customization
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] C++23 range formatting:
      [`std::formatter<range>`](https://en.cppreference.com/w/cpp/utility/format/ranges_formatter),
      [`std::range_formatter`](https://en.cppreference.com/w/cpp/utility/format/range_formatter)
  [(cppreference assertions)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::stringstream](https://en.cppreference.com/w/cpp/io/basic_stringstream),
      [std::quoted](https://en.cppreference.com/w/cpp/io/manip/quoted)
  [(cppreference example and direct stream coverage)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] C++23 span-backed streams:
      [`std::basic_ispanstream`](https://en.cppreference.com/w/cpp/io/basic_ispanstream),
      [`std::basic_ospanstream`](https://en.cppreference.com/w/cpp/io/basic_ospanstream),
      [`std::basic_spanstream`](https://en.cppreference.com/w/cpp/io/basic_spanstream)
  [(direct API coverage)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::regex](https://en.cppreference.com/w/cpp/regex)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/regex.cpp)
- [x] [std::regex_match](https://en.cppreference.com/w/cpp/regex/regex_match),
      [std::regex_iterator](https://en.cppreference.com/w/cpp/regex/regex_iterator),
      [std::regex_token_iterator](https://en.cppreference.com/w/cpp/regex/regex_token_iterator)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/regex.cpp)
- [x] [std::filesystem::path lexical operations](https://en.cppreference.com/w/cpp/filesystem/path/lexically_normal)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::absolute](https://en.cppreference.com/w/cpp/filesystem/absolute),
      [std::filesystem::relative / proximate](https://en.cppreference.com/w/cpp/filesystem/relative),
      [std::filesystem::current_path](https://en.cppreference.com/w/cpp/filesystem/current_path)
  [(direct path-relation coverage)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::directory_iterator](https://en.cppreference.com/w/cpp/filesystem/directory_iterator)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::recursive_directory_iterator](https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
  - Includes `disable_recursion_pending()` and `increment(error_code&)`
    coverage.
- [x] [std::filesystem::copy_file](https://en.cppreference.com/w/cpp/filesystem/copy_file)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::copy](https://en.cppreference.com/w/cpp/filesystem/copy)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::create_directory / create_directories](https://en.cppreference.com/w/cpp/filesystem/create_directory)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::create_hard_link](https://en.cppreference.com/w/cpp/filesystem/create_hard_link)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::create_symlink / create_directory_symlink](https://en.cppreference.com/w/cpp/filesystem/create_symlink)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::read_symlink](https://en.cppreference.com/w/cpp/filesystem/read_symlink)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::copy_symlink](https://en.cppreference.com/w/cpp/filesystem/copy_symlink)
  [(tested)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::directory_entry](https://en.cppreference.com/w/cpp/filesystem/directory_entry)
  [(tested)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
  - Includes [`directory_entry::refresh(error_code&)`](https://en.cppreference.com/w/cpp/filesystem/directory_entry/refresh)
    after a metadata transition.
- [x] [std::filesystem::equivalent](https://en.cppreference.com/w/cpp/filesystem/equivalent)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::file_size](https://en.cppreference.com/w/cpp/filesystem/file_size)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::hard_link_count](https://en.cppreference.com/w/cpp/filesystem/hard_link_count)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::status / symlink_status](https://en.cppreference.com/w/cpp/filesystem/status)
  and file type queries such as
  [exists](https://en.cppreference.com/w/cpp/filesystem/exists),
  [is_directory](https://en.cppreference.com/w/cpp/filesystem/is_directory),
  [is_regular_file](https://en.cppreference.com/w/cpp/filesystem/is_regular_file),
  and [is_empty](https://en.cppreference.com/w/cpp/filesystem/is_empty)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
  - Includes selected non-throwing `error_code&` overload coverage.
- [x] [std::filesystem::permissions](https://en.cppreference.com/w/cpp/filesystem/permissions)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::resize_file](https://en.cppreference.com/w/cpp/filesystem/resize_file)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::remove_all](https://en.cppreference.com/w/cpp/filesystem/remove)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::space](https://en.cppreference.com/w/cpp/filesystem/space)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::rename](https://en.cppreference.com/w/cpp/filesystem/rename)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::temp_directory_path](https://en.cppreference.com/w/cpp/filesystem/temp_directory_path)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::last_write_time](https://en.cppreference.com/w/cpp/filesystem/last_write_time)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::canonical / weakly_canonical](https://en.cppreference.com/w/cpp/filesystem/canonical)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] Selected non-throwing filesystem `error_code&` overloads for
      [`remove`](https://en.cppreference.com/w/cpp/filesystem/remove),
      [`resize_file`](https://en.cppreference.com/w/cpp/filesystem/resize_file),
      [`last_write_time`](https://en.cppreference.com/w/cpp/filesystem/last_write_time),
      and [`canonical`](https://en.cppreference.com/w/cpp/filesystem/canonical)
  [(direct negative-path coverage)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::string](https://en.cppreference.com/w/cpp/string/basic_string)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/string.cpp)
- [x] [std::string_view](https://en.cppreference.com/w/cpp/string/basic_string_view)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/string.cpp)
- [x] [std::weak_ptr](https://en.cppreference.com/w/cpp/memory/weak_ptr)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/memory.cpp)
- [x] [std::thread](https://en.cppreference.com/w/cpp/thread)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L35)
- [x] [std::jthread](https://en.cppreference.com/w/cpp/thread/jthread)
  [(cppreference constructor example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::tuple](https://en.cppreference.com/w/cpp/utility/tuple)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::to_chars](https://en.cppreference.com/w/cpp/utility/to_chars)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::to_underlying](https://en.cppreference.com/w/cpp/utility/to_underlying)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::transform](https://en.cppreference.com/w/cpp/algorithm/transform)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::unique_ptr](https://en.cppreference.com/w/cpp/memory/unique_ptr)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/memory.cpp)
- [x] [std::unordered_map](https://en.cppreference.com/w/cpp/container/unordered_map)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::unordered_multimap::equal_range](https://en.cppreference.com/w/cpp/container/unordered_multimap/equal_range)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::unordered_multiset::count](https://en.cppreference.com/w/cpp/container/unordered_multiset/count)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::unordered_set](https://en.cppreference.com/w/cpp/container/unordered_set)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::variant](https://en.cppreference.com/w/cpp/utility/variant)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::visit](https://en.cppreference.com/w/cpp/utility/variant/visit)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::vector](https://en.cppreference.com/w/cpp/container/vector)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::numbers](https://en.cppreference.com/w/cpp/numeric/constants)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::random_device](https://en.cppreference.com/w/cpp/numeric/random/random_device)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/numeric.cpp)
- [x] [std::uniform_int_distribution](https://en.cppreference.com/w/cpp/numeric/random/uniform_int_distribution)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/numeric.cpp)
- [x] [std::valarray::slice](https://en.cppreference.com/w/cpp/numeric/valarray/slice)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/numeric.cpp)
- [x] [std::condition_variable](https://en.cppreference.com/w/cpp/thread/condition_variable)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L35)
- [x] [std::condition_variable_any](https://en.cppreference.com/w/cpp/thread/condition_variable_any)
  [(cppreference wait example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::mutex](https://en.cppreference.com/w/cpp/thread/mutex)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L81)
- [x] [std::lock_guard](https://en.cppreference.com/w/cpp/thread/lock_guard)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::shared_mutex](https://en.cppreference.com/w/cpp/thread/shared_mutex)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L129)
- [x] [std::shared_lock](https://en.cppreference.com/w/cpp/thread/shared_lock)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::shared_timed_mutex](https://en.cppreference.com/w/cpp/thread/shared_timed_mutex)
  timed exclusive/shared lock paths
  [(cppreference incomplete example plus timed edge coverage)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::timed_mutex](https://en.cppreference.com/w/cpp/thread/timed_mutex)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::recursive_mutex](https://en.cppreference.com/w/cpp/thread/recursive_mutex)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::recursive_timed_mutex](https://en.cppreference.com/w/cpp/thread/recursive_timed_mutex)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::scoped_lock](https://en.cppreference.com/w/cpp/thread/scoped_lock)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::lock](https://en.cppreference.com/w/cpp/thread/lock)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::unique_lock](https://en.cppreference.com/w/cpp/thread/unique_lock)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::try_lock](https://en.cppreference.com/w/cpp/thread/try_lock)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::call_once](https://en.cppreference.com/w/cpp/thread/call_once)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L180)
- [x] [std::future](https://en.cppreference.com/w/cpp/thread/future)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L208)
- [x] [std::future::valid](https://en.cppreference.com/w/cpp/thread/future/valid)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::async](https://en.cppreference.com/w/cpp/thread/async)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::future::wait_for](https://en.cppreference.com/w/cpp/thread/future/wait_for)
  and future exception propagation
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::future_status](https://en.cppreference.com/w/cpp/thread/future_status)
  [(cppreference wait_until example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::future_error](https://en.cppreference.com/w/cpp/thread/future_error)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::shared_future](https://en.cppreference.com/w/cpp/thread/shared_future)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::shared_future::wait_for](https://en.cppreference.com/w/cpp/thread/shared_future/wait_for)
  and future/shared-future timeout edges
  [(cppreference example plus timeout edge coverage)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::promise](https://en.cppreference.com/w/cpp/thread/promise)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L254)
- [x] [std::promise::set_exception](https://en.cppreference.com/w/cpp/thread/promise/set_exception)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::packaged_task](https://en.cppreference.com/w/cpp/thread/packaged_task)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L318)
- [x] [std::latch](https://en.cppreference.com/w/cpp/thread/latch)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::barrier](https://en.cppreference.com/w/cpp/thread/barrier)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::counting_semaphore](https://en.cppreference.com/w/cpp/thread/counting_semaphore)
  and [std::binary_semaphore](https://en.cppreference.com/w/cpp/thread/counting_semaphore)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::stop_source](https://en.cppreference.com/w/cpp/thread/stop_source)
  and [std::stop_token](https://en.cppreference.com/w/cpp/thread/stop_token)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::stop_callback](https://en.cppreference.com/w/cpp/thread/stop_callback)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::cin](https://en.cppreference.com/w/cpp/io/cin)
- [x] [std::cout](https://en.cppreference.com/w/cpp/io/cout)
- [x] [std::cerr](https://en.cppreference.com/w/cpp/io/cerr)
- [x] [std::clog](https://en.cppreference.com/w/cpp/io/clog)
- [x] [std::wcin](https://en.cppreference.com/w/cpp/io/cin)
- [x] [std::wcout](https://en.cppreference.com/w/cpp/io/cout)
- [x] [std::wcerr](https://en.cppreference.com/w/cpp/io/cerr)
- [x] [std::wclog](https://en.cppreference.com/w/cpp/io/clog)
- [x] `nlohmann::json` integration
  [(tested)](../test/cmake/driver/src/libs/nlohmann_json.cpp)

## NTL

- [x] `ntl::seh::try_except`
  [(tested)](../test/cmake/driver/src/ntl.cpp)
  - Uses MSVC `__try` / `__except` internally and returns the captured
    exception code.

## Test Backlog

These unchecked items are candidates for future cppreference Example ports or
runtime support work. They are not known failures unless explicitly described
as limitations. Prefer examples that can run at `PASSIVE_LEVEL` without hosted
file-system, locale, console input, or other driver-test-hostile runtime
dependencies.

### Future cppreference Coverage Candidates

- [ ] Core language examples
  - [`lambda`](https://en.cppreference.com/w/cpp/language/lambda),
    [`structured binding`](https://en.cppreference.com/w/cpp/language/structured_binding),
    [`range-for`](https://en.cppreference.com/w/cpp/language/range-for),
    [`constexpr`](https://en.cppreference.com/w/cpp/language/constexpr),
    [`consteval`](https://en.cppreference.com/w/cpp/language/consteval),
    [`constinit`](https://en.cppreference.com/w/cpp/language/constinit),
    [`fold expression`](https://en.cppreference.com/w/cpp/language/fold),
    [`CTAD`](https://en.cppreference.com/w/cpp/language/class_template_argument_deduction),
    [`requires expression`](https://en.cppreference.com/w/cpp/language/requires),
    [`noexcept`](https://en.cppreference.com/w/cpp/language/noexcept),
    and [`coroutines`](https://en.cppreference.com/w/cpp/language/coroutines)
    where the example can run without hosted assumptions.
- [ ] Diagnostics and system-error examples
  - C++23/C++26 stacktrace/debugging APIs when MSVC STL exposes them.
- [ ] Utility, comparison, and type-support examples
  - Additional type-traits examples and C++26 comparison/type-order APIs when
    MSVC STL exposes them.
- [ ] Memory and allocator examples
  - C++23 [`std::out_ptr`](https://en.cppreference.com/w/cpp/memory/out_ptr)
    and [`std::inout_ptr`](https://en.cppreference.com/w/cpp/memory/inout_ptr)
    when the active MSVC STL exposes them.
- [ ] Containers and views
  - [`std::vector<bool>`](https://en.cppreference.com/w/cpp/container/vector_bool),
    C++23 flat containers (`flat_set`, `flat_map`, `flat_multiset`,
    `flat_multimap`) if available, [`std::mdspan`](https://en.cppreference.com/w/cpp/container/mdspan),
    and C++26 `inplace_vector` / `hive` when MSVC STL exposes them.
  - More member-operation examples for already-covered containers:
    `emplace`, `erase`, `extract`, `merge`, `contains`, `insert_range`,
    node-handle, heterogeneous lookup, and allocator-aware construction paths.
- [ ] Algorithms and numeric algorithms
  - Remaining search/mutation examples such as `find_end`, `find_first_of`,
    `adjacent_find`, `search`, `search_n`, `iter_swap`, `swap_ranges`,
    `shuffle`, `push_heap`, `pop_heap`, and `sort_heap`.
  - Remaining numeric examples for ranges `iota`, special math functions,
    random engines/distributions, floating-point environment, and C++26
    checked/saturation arithmetic when available.
- [ ] Ranges examples
  - Range access/concepts and utility examples: `begin`/`end`, `size`,
    `data`, `empty`, `subrange`, `view_interface`, `ref_view`,
    `owning_view`, `from_range`, and `ranges::to`.
  - Remaining/adaptive views: `empty_view`, `single_view`, `iota_view`,
    `istream_view`, `take_while`, `drop_while`, `common_view`,
    `views::counted`, `lazy_split`, `zip_transform`, `adjacent`,
    `pairwise`, `adjacent_transform`, plus C++26 `indices`, `as_input`,
    `concat`, `cache_latest`, and `reserve_hint` paths when available.
- [ ] I/O, text, and locale edges
  - [`std::basic_syncbuf`](https://en.cppreference.com/w/cpp/io/basic_syncbuf),
    [`std::basic_osyncstream`](https://en.cppreference.com/w/cpp/io/basic_osyncstream),
    `basic_stringbuf`, `basic_spanbuf`, `ios_base` / `basic_ios`,
    `istream` / `ostream` manipulators, `iostream_category` / `io_errc`,
    and file-stream examples where the driver harness can safely back them with
    LDK file APIs.
  - C++26 [`std::text_encoding`](https://en.cppreference.com/w/cpp/locale/text_encoding)
    if exposed by the active MSVC STL.
- [ ] Filesystem edge examples
  - More `path` iterator/formatting cases, `copy_options`, `directory_options`,
    `recursive_directory_iterator::depth/pop/recursion_pending`,
    permission-option combinations, hard negative paths for `equivalent`,
    `read_symlink`, `copy`, `remove`, and metadata/time/error-code overloads.
- [ ] Concurrency edge examples
  - `std::this_thread::get_id`, `yield`, `sleep_until`,
    [`std::notify_all_at_thread_exit`](https://en.cppreference.com/w/cpp/thread/notify_all_at_thread_exit),
    `condition_variable` / `condition_variable_any` `wait_for` and
    `wait_until`, additional `timed_mutex` / `recursive_timed_mutex` timed
    edges, `std::launch`, `future_category`, `future_errc`, hardware
    interference-size constants, and C++26 stop-token / safe-reclamation APIs
    when available.
- [ ] Track new MSVC STL feature-test macros and newly published cppreference
      examples as they appear.

### Needs Investigation

- [ ] [thread_local](https://en.cppreference.com/w/cpp/language/storage_duration#Thread_storage_duration)
  - General C++ `thread_local` is not supported as true per-thread TLS in the
    default driver build; the cppreference storage-duration example remains
    disabled.

## C Standard

- [x] Math functions
  - adapted where needed from small portable implementations
  - references:
    [RetrievAL](https://github.com/SpoilerScriptsGroup/RetrievAL),
    [musl](https://github.com/bminor/musl)
- [x] Floating-point classification helpers
  [(source)](../src/custom/crt/math/fpclassify.c)

## NTL: NT Template Library

NTL provides C++ helpers for driver code. See the
[NTL API reference](./ntl-api.md) for API-level details.

- [x] `ntl::expand_stack`
  [(tested)](../test/cmake/driver/src/ntl.cpp#L6)
- [x] `ntl::status`
- [x] `ntl::driver`
  - [x] unload callback
    [(tested)](../test/cmake/driver/src/main.cpp#L73)
  - [x] device creation
    [(tested)](../test/cmake/driver/src/main.cpp#L44)
- [x] `ntl::device`
  - [x] device extension
    [(tested)](../test/cmake/driver/src/main.cpp#L33)
  - [x] `IRP_MJ_CREATE`
    [(app test)](../test/cmake/app/src/main.cpp#L77)
  - [x] `IRP_MJ_CLOSE`
    [(app test)](../test/cmake/app/src/main.cpp#L77)
  - [x] `IRP_MJ_DEVICE_CONTROL`
    [(app test)](../test/cmake/app/src/main.cpp#L77)
    [(driver test)](../test/cmake/driver/src/main.cpp#L55)
- [x] `ntl::main`
  [(tested)](../test/cmake/driver/src/main.cpp#L25)
- [x] `ntl::rpc`
  - [x] `ntl::rpc::server`
    [(tested)](../test/cmake/driver/src/main.cpp#L19)
    [(schema)](../test/cmake/common/rpc.hpp)
  - [x] `ntl::rpc::client`
    [(tested)](../test/cmake/app/src/main.cpp#L4)
    [(schema)](../test/cmake/common/rpc.hpp)
- [x] `ntl::irql`
  - [x] `ntl::irql`
    [(tested)](../test/cmake/driver/src/ntl.cpp#L47)
  - [x] `ntl::raise_irql`
    [(tested)](../test/cmake/driver/src/ntl.cpp#L50)
  - [x] `ntl::raise_irql_to_dpc_level`
    [(tested)](../test/cmake/driver/src/ntl.cpp#L63)
  - [x] `ntl::raise_irql_to_synch_level`
    [(tested)](../test/cmake/driver/src/ntl.cpp#L72)
- [x] `ntl::spin_lock`
  - [x] `ntl::spin_lock`
    [(tested)](../test/cmake/driver/src/ntl.cpp#L81)
  - [x] `ntl::unique_lock<ntl::spin_lock>`
    [(normal lock tested)](../test/cmake/driver/src/ntl.cpp#L94)
    [(at-DPC tested)](../test/cmake/driver/src/ntl.cpp#L140)
- [x] `ntl::resource`
  - [x] `ntl::resource`
    [(tested)](../test/cmake/driver/src/ntl.cpp#L181)
  - [x] `ntl::unique_lock<ntl::resource>`
    [(tested)](../test/cmake/driver/src/ntl.cpp#L220)
  - [x] `ntl::shared_lock<ntl::resource>`
    [(tested)](../test/cmake/driver/src/ntl.cpp#L230)
