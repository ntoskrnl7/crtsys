# Feature Coverage

[Back to README](../README.md)

This page is a driver-tested feature coverage matrix, not an exhaustive support
boundary or a negative compatibility table. It records features and code paths
that are explicitly built into the `crtsys` kernel driver test target and run
under that driver harness.

A feature missing from this list should be read as "not yet explicitly covered
by the driver tests", not as "unsupported", unless it is called out under known
limitations or blockers.

In practice, the usable surface is often wider than the rows below. `crtsys`
uses MSVC CRT/STL source paths and maps their Win32/NTDLL/ICU dependencies
through LDK, so many adjacent header functions and overloads work without a
dedicated row in this matrix. The matrix is intentionally an evidence list:
we add explicit tests for representative cppreference examples, kernel-sensitive
runtime paths, regression-prone behavior, and APIs that exercise the LDK
substrate. Rely on unlisted paths with the same driver-context audit you would
apply to any kernel code, and add a harness test when a path becomes important
to your driver.

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
| Runtime and C++ initialization | Non-local static initialization, dynamic initialization, function-local `static` | `PASSIVE_LEVEL` / driver initialization path | The `crtsys` wrapper runs these paths from `DriverEntry`. MSVC compiler TLS slots are isolated across multiple crtsys-linked drivers. This is runtime slot isolation, not user `thread_local`: kernel GS is processor-local KPCR, not a per-thread user-mode TEB. |
| C++ exceptions | `throw`, `try`, function try blocks, `std::exception_ptr` | `PASSIVE_LEVEL` only | Do not let exceptions cross WDK callback boundaries, spin-lock-held regions, DPC, ISR, or paging I/O paths. |
| C++ RTTI | `typeid`, `dynamic_cast` | Audited caller context; otherwise `PASSIVE_LEVEL` | The consuming driver target must compile with RTTI enabled. Keep the target object and its type metadata resident. |
| STL value-only helpers | Type traits, concepts, `std::array`, `std::span`, `std::string_view`, `std::bitset`, `std::pair`, `std::tuple`, `std::ratio`, `std::source_location`, `std::strong_ordering`, `std::numbers`, simple `std::move` / `std::exchange` / `std::invoke` / `std::reference_wrapper`, integer `std::to_chars` / `std::from_chars`, pure algorithms over resident fixed storage | Audited caller context; otherwise `PASSIVE_LEVEL` | These are the only STL areas that may be reasonable above `PASSIVE_LEVEL`, and only after the exact expression and its callbacks/comparators are audited. |
| STL owning objects, containers, and callback-heavy utilities | `std::string`, containers, `std::any`, `std::function`, smart pointers, `std::optional`, `std::variant`, `std::complex`, `std::valarray`, `std::filesystem::path`, basic `std::filesystem` file operations, `std::format`, `std::print`, random distributions, `std::regex`, `nlohmann::json`, algorithms that allocate, throw, or call arbitrary user code | `PASSIVE_LEVEL` only | Construction, destruction, comparison, hashing, allocation, deallocation, formatting, file-system I/O, and user callbacks can pull in runtime paths. |
| STL synchronization, threading, and async | `std::thread`, `std::mutex`, `std::shared_mutex`, `std::condition_variable`, `std::condition_variable_any`, `std::scoped_lock`, `std::lock`, `std::try_lock`, `std::call_once`, `std::future`, `std::shared_future`, `std::promise`, `std::packaged_task` | `PASSIVE_LEVEL` only | These paths may wait, create worker execution, allocate, or outlive unload if the caller is careless. |
| STL atomic primitives | `std::atomic`, `std::atomic_ref`, and `std::atomic_flag` load, store, exchange, compare-exchange, and C++20 wait/notify over resident storage | `<= DISPATCH_LEVEL` for audited non-waiting operations; `PASSIVE_LEVEL` only for wait/notify | Treat wait/notify as blocking synchronization. Do not combine atomic code with runtime-backed callbacks or allocation at elevated IRQL. |
| I/O streams | `std::cin`, `std::cout`, `std::cerr`, `std::clog`, wide stream variants | `PASSIVE_LEVEL` only | Diagnostic and test support. Avoid production hot paths and stack-sensitive paths. |
| C math and floating-point helpers | Math functions and floating-point classification helpers | `PASSIVE_LEVEL` unless the exact helper is separately audited | Floating-point state and helper dependencies are driver-context sensitive. |
| NTL entry, driver, device, and RPC server helpers | `ntl::main`, `ntl::driver`, `ntl::device`, `ntl::rpc::server` | `PASSIVE_LEVEL` only | Intended for initialization, teardown, device setup, and IOCTL/RPC control paths. |
| NTL IRP view and typed IOCTL helper | `ntl::irp`, `ntl::device_control::in_buffer`, `ntl::device_control::out_buffer`, `ntl::ioctl` | Follows the dispatch path that supplied the IRP | NTL device callbacks should still be treated as `PASSIVE_LEVEL` unless the exact callback body is separately audited. |
| NTL status/result wrapper | `ntl::status`, `ntl::result<T>`, `ntl::result<void>` | Caller context for status checks; contained value and failed `value()` path follow their own contracts | Value/status helpers for preserving `NTSTATUS` while writing C++ control-path code. |
| NTL stack expansion | `ntl::expand_stack` | `PASSIVE_LEVEL` only | Runtime-backed control-path helper, not a hot-path escape hatch. |
| NTL handle/object ownership | `ntl::unique_kernel_handle`, `ntl::unique_object`, `try_reference_object_by_handle` | `PASSIVE_LEVEL` unless the exact WDK primitive documents a wider contract | Separates `ZwClose` handle ownership from `ObDereferenceObject` reference ownership. |
| NTL registry helper | `ntl::registry_key`, `ntl::registry_value`, `ntl::driver_config`, `try_open_driver_parameters`, `open_driver_parameters` | `PASSIVE_LEVEL` only | Wraps Zw registry key handles with RAII and typed `REG_*` value query/set/default helpers for driver configuration paths. |
| NTL pool ownership and allocator | `ntl::pool_ptr`, `ntl::pool_buffer`, `ntl::pool_allocator`, `ntl::nonpaged_pool_allocator`, `ntl::paged_pool_allocator`, `ntl::pmr::pool_resource` | Raw nonpaged pool follows WDK pool rules; object construction/destruction and STL/PMR usage are `PASSIVE_LEVEL` unless separately audited | Exposes WDK pool kind/options to RAII ownership helpers, STL-compatible allocators, and PMR resource types. |
| NTL lookaside list | `ntl::lookaside_list` | Raw nonpaged allocate/free follows WDK lookaside rules; object construction/destruction is `PASSIVE_LEVEL` unless separately audited | Wraps `LOOKASIDE_LIST_EX` for fixed-size kernel object caches while keeping pool kind/tag visible. |
| NTL MDL helper | `ntl::mdl` | Follows the underlying WDK MDL/page-locking primitive | Owns MDLs allocated by `IoAllocateMdl` and exposes nonpaged-pool description, page locking, mapping, and release paths. |
| NTL symbolic link wrapper | `ntl::symbolic_link` | `PASSIVE_LEVEL` | RAII wrapper over `IoCreateSymbolicLink` / `IoDeleteSymbolicLink` for driver setup and teardown paths. |
| NTL PnP device-interface wrapper | `ntl::device_interface_link`, `try_register_device_interface` | `PASSIVE_LEVEL` PnP/control path | Owns the symbolic link returned by `IoRegisterDeviceInterface`; generic tests cover empty-owner safety because real registration requires a valid PDO. |
| NTL event wrapper | `ntl::event` | Follows `KEVENT`; blocking `wait()` is `PASSIVE_LEVEL` in NTL usage | Wraps notification and synchronization event setup, signal/reset/clear, state query, and wait. |
| NTL timer and DPC wrappers | `ntl::timer`, `ntl::kdpc`, `relative_due_time_ms` | Timer setup/cancel follows WDK timer rules; timer waits are `PASSIVE_LEVEL` in NTL usage; DPC callbacks run at `DISPATCH_LEVEL` | Wraps one-shot timers, periodic timers, direct DPC queueing, and timer DPC callbacks. Keep DPC callbacks resident, short, nonblocking, and free of arbitrary STL/CRT work. |
| NTL system thread wrapper | `ntl::system_thread` | `PASSIVE_LEVEL` | Wraps `PsCreateSystemThread` as a native WDK thread-handle owner. Use `std::thread` for standard C++ threading; use this when driver code needs `NTSTATUS`, `OBJECT_ATTRIBUTES`, `CLIENT_ID`, or explicit `ZwClose` ownership. |
| NTL wait helpers | `ntl::try_wait`, `ntl::wait_for`, `ntl::zero_timeout`, `ntl::relative_timeout_ms` | Follows the waited object's native contract; blocking waits are `PASSIVE_LEVEL` in NTL usage | Shared timeout and status-classification helpers for event, timer, and system-thread wrappers. |
| NTL work item / passive executor | `ntl::work_item`, `ntl::passive_work_item`, `ntl::passive_executor` | `queue()` / `post()` `<= DISPATCH_LEVEL`; `wait()` / `queue_and_wait()` are `PASSIVE_LEVEL` | Defers resident work to a system worker thread running at `PASSIVE_LEVEL`; executor can run inline when already passive. |
| NTL remove lock | `ntl::remove_lock`, `ntl::remove_lock_guard` | Follows WDK `IO_REMOVE_LOCK`; waiting teardown path must be able to wait | RAII guard for dispatch/remove/unload synchronization. |
| NTL ERESOURCE wrapper | `ntl::resource`, `ntl::unique_lock<ntl::resource>`, `ntl::shared_lock<ntl::resource>` | `<= APC_LEVEL` | Blocking/resource-style synchronization. Do not use in DPC, ISR, or spin-lock-held paths. |
| NTL spin lock wrapper | `ntl::spin_lock`, `ntl::unique_lock<ntl::spin_lock>` | `<= DISPATCH_LEVEL` | Keep held regions resident, short, nonblocking, and free of allocation, waits, exceptions, streams, and arbitrary STL/runtime helpers. |
| NTL IRQL helpers | `ntl::irql`, `ntl::current_irql`, `ntl::is_passive_level`, `ntl::is_irql_at_most`, `ntl::require_passive_level`, `ntl::require_irql_at_most`, `ntl::raise_irql`, `ntl::raise_irql_to_dpc_level`, `ntl::raise_irql_to_synch_level` | Query helpers follow caller context; require helpers return status; raise helpers explicitly manipulate current IRQL | Keep raised scopes as small as possible. |
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
- [x] Multi-driver compiler TLS slot isolation
  - Multiple crtsys-linked driver images can load at the same time without
    colliding on MSVC compiler TLS `_tls_index` values used by runtime paths
    such as thread-safe function-local `static` initialization. The guarantee
    is module/driver-image isolation: a shared slot vector maps each driver
    image's `_tls_index` to that driver's compiler TLS image buffer. This
    supported path is runtime slot isolation between driver images. It does not
    make user-declared `thread_local T value` safe because kernel-mode GS is
    processor-local KPCR state, not a per-thread user-mode TEB.
  [(multi-driver regression)](../test/cmake/driver/multi_instance/driver.cpp)
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

### RTTI

- [x] [typeid](https://en.cppreference.com/w/cpp/language/typeid)
  [(tested)](../test/cmake/driver/src/cpp/lang/rtti.cpp)
- [x] `std::type_info::name`
  - Basic, class, struct, and enum names are covered by a driver semantic test.
  [(driver semantic test)](../test/cmake/driver/src/cpp/lang/rtti.cpp)
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
  [(cppreference example + driver semantic test)](../test/cmake/driver/src/cpp/stl/atomic.cpp)
- [x] [std::atomic_flag](https://en.cppreference.com/w/cpp/atomic/atomic_flag)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/atomic.cpp)
- [x] Atomic fences and free functions:
      [`std::atomic_thread_fence`](https://en.cppreference.com/w/cpp/atomic/atomic_thread_fence),
      [`std::atomic_signal_fence`](https://en.cppreference.com/w/cpp/atomic/atomic_signal_fence),
      [`std::atomic_fetch_add`](https://en.cppreference.com/w/cpp/atomic/atomic_fetch_add),
      [`std::atomic_compare_exchange`](https://en.cppreference.com/w/cpp/atomic/atomic_compare_exchange)
  [(cppreference example/API coverage)](../test/cmake/driver/src/cpp/stl/atomic.cpp)
- [x] [std::chrono](https://en.cppreference.com/w/cpp/chrono)
  [(tested)](../test/cmake/driver/src/cpp/stl/chrono.cpp#L15)
  - C++20 timezone paths for
    [`std::chrono::current_zone`](https://en.cppreference.com/w/cpp/chrono/current_zone),
    [`std::chrono::locate_zone`](https://en.cppreference.com/w/cpp/chrono/locate_zone),
    [`std::chrono::zoned_time`](https://en.cppreference.com/w/cpp/chrono/zoned_time),
    and
    [`std::chrono::time_zone::get_info`](https://en.cppreference.com/w/cpp/chrono/time_zone/get_info)
    also run in the default driver build.
    [`std::chrono::get_tzdb`](https://en.cppreference.com/w/cpp/chrono/get_tzdb)
    / [`std::chrono::get_tzdb_list`](https://en.cppreference.com/w/cpp/chrono/get_tzdb_list)
    and invalid-zone `std::chrono::locate_zone` error paths are covered.
    Additional driver semantic checks cover current-zone locate-by-name,
    `zoned_time` sys-time retention, fixed-offset `choose::earliest` /
    `choose::latest` local-time round trips, and timezone abbreviation bounds.
  - [`std::chrono::system_clock::to_time_t`](https://en.cppreference.com/w/cpp/chrono/system_clock/to_time_t),
    [`std::chrono::system_clock::from_time_t`](https://en.cppreference.com/w/cpp/chrono/system_clock/from_time_t),
    [`std::chrono::file_clock::now`](https://en.cppreference.com/w/cpp/chrono/file_clock/now),
    and `time_zone::to_local` / `time_zone::to_sys` are covered by OS-time
    semantic tests, including `UTC` / `Etc/UTC` alias lookup and
    `time_zone::get_info(local_time)` unique-mapping checks.
  - File timestamp round trips cover `std::chrono::file_clock` /
    `system_clock` conversion through `std::filesystem::last_write_time`
    get/set and missing-file error paths.
  - [`std::chrono::clock_cast`](https://en.cppreference.com/w/cpp/chrono/clock_cast)
    coverage checks `system_clock`, `file_clock`, `utc_clock`, `tai_clock`,
    and `gps_clock` round trips where the active MSVC STL exposes those clocks.
- [x] CRT time semantic checks
  - `_time64`, `_ftime64_s`, `gmtime_s`, `localtime_s`, `_tzset`,
    `_get_timezone`, `_get_daylight`, `_get_tzname`, `strftime`, `wcsftime`,
    `asctime_s`, `_ctime64_s`, `mktime`, `_mkgmtime64`, and `difftime` paths
    are covered against the LDK-backed system time and timezone substrate.
    `TZ=UTC0` and `TZ=KST-9` timezone round trips plus `strftime` /
    `wcsftime` small-buffer failure are also covered.
  [(driver semantic test)](../test/cmake/driver/src/cpp/stl/ctime.cpp)
- [x] [std::any](https://en.cppreference.com/w/cpp/utility/any)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::bind](https://en.cppreference.com/w/cpp/utility/functional/bind)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/functional.cpp)
- [x] [std::bind_front](https://en.cppreference.com/w/cpp/utility/functional/bind_front)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/functional.cpp)
- [x] [std::binary_search](https://en.cppreference.com/w/cpp/algorithm/binary_search)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] Binary-search family:
      [`std::lower_bound`](https://en.cppreference.com/w/cpp/algorithm/lower_bound),
      [`std::upper_bound`](https://en.cppreference.com/w/cpp/algorithm/upper_bound),
      [`std::equal_range`](https://en.cppreference.com/w/cpp/algorithm/equal_range)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::bitset](https://en.cppreference.com/w/cpp/utility/bitset)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [`<bit>` utilities](https://en.cppreference.com/w/cpp/utility/bit)
  [(cppreference popcount example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] `<bit>` standalone examples:
      [`std::rotl`](https://en.cppreference.com/w/cpp/numeric/rotl),
      [`std::rotr`](https://en.cppreference.com/w/cpp/numeric/rotr),
      [`std::countl_zero`](https://en.cppreference.com/w/cpp/numeric/countl_zero),
      [`std::countr_zero`](https://en.cppreference.com/w/cpp/numeric/countr_zero),
      [`std::has_single_bit`](https://en.cppreference.com/w/cpp/numeric/has_single_bit),
      [`std::bit_ceil`](https://en.cppreference.com/w/cpp/numeric/bit_ceil),
      [`std::bit_floor`](https://en.cppreference.com/w/cpp/numeric/bit_floor),
      [`std::bit_width`](https://en.cppreference.com/w/cpp/numeric/bit_width)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
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
- [x] [std::integer_sequence](https://en.cppreference.com/w/cpp/utility/integer_sequence)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::lcm](https://en.cppreference.com/w/cpp/numeric/lcm)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::lerp](https://en.cppreference.com/w/cpp/numeric/lerp)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::list](https://en.cppreference.com/w/cpp/container/list)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::locale](https://en.cppreference.com/w/cpp/locale/locale)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/locale.cpp)
- [x] Locale facet semantic checks
  - Named `std::locale` ctype, collate compare/transform/hash, wide collate,
    codecvt, numpunct, and moneypunct facets are covered against the
    LDK-backed locale/NLS substrate, including grouped numeric parsing through
    the locale facets.
  [(driver semantic test)](../test/cmake/driver/src/cpp/stl/locale.cpp)
- [x] NLS and text conversion semantic checks
  - `MultiByteToWideChar`, `WideCharToMultiByte`, `GetStringTypeA/W`,
    `GetStringTypeExW`, `LCMapStringEx` upper/lower mapping, `CompareStringEx`,
    `CompareStringOrdinal`, CP_ACP / UTF-8 round trips, insufficient-buffer and
    invalid-sequence / invalid-flag error cases, UCRT `mbtowc` / `wctomb` /
    `mbstowcs` / `mbstowcs_s` / `wcstombs` / `wcstombs_s` / `mbrtowc` /
    `wcrtomb`, CRT collation transforms `strcoll` / `strxfrm` / `wcscoll` /
    `wcsxfrm`, C++ UTF conversion functions `mbrtoc16` / `c16rtomb` /
    `mbrtoc32` / `c32rtomb`, and filesystem UTF-8 path
    create/read/copy/rename/enumeration paths are covered.
  [(driver semantic test)](../test/cmake/driver/src/cpp/stl/nls.cpp)
- [x] [std::map](https://en.cppreference.com/w/cpp/container/map)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [`std::flat_map`](https://en.cppreference.com/w/cpp/container/flat_map)
  [(feature-test-gated API coverage)](../test/cmake/driver/src/cpp/stl/cxx_latest.cpp)
- [x] [std::make_heap](https://en.cppreference.com/w/cpp/algorithm/make_heap)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::merge](https://en.cppreference.com/w/cpp/algorithm/merge)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::mem_fn](https://en.cppreference.com/w/cpp/utility/functional/mem_fn)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/functional.cpp)
- [x] [std::midpoint](https://en.cppreference.com/w/cpp/numeric/midpoint)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::move](https://en.cppreference.com/w/cpp/utility/move)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::multimap::equal_range](https://en.cppreference.com/w/cpp/container/multimap/equal_range)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::multiset::erase](https://en.cppreference.com/w/cpp/container/multiset/erase)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::next_permutation](https://en.cppreference.com/w/cpp/algorithm/next_permutation)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] Sorting/selection algorithms:
      [`std::stable_sort`](https://en.cppreference.com/w/cpp/algorithm/stable_sort),
      [`std::nth_element`](https://en.cppreference.com/w/cpp/algorithm/nth_element),
      [`std::partial_sort`](https://en.cppreference.com/w/cpp/algorithm/partial_sort)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::not_fn](https://en.cppreference.com/w/cpp/utility/functional/not_fn)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/functional.cpp)
- [x] [std::optional](https://en.cppreference.com/w/cpp/utility/optional)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::expected](https://en.cppreference.com/w/cpp/utility/expected)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::pair](https://en.cppreference.com/w/cpp/utility/pair)
  [(cppreference make_pair example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::pmr::monotonic_buffer_resource](https://en.cppreference.com/w/cpp/memory/monotonic_buffer_resource)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/memory.cpp)
- [x] PMR pool/resource APIs:
      [`std::pmr::unsynchronized_pool_resource`](https://en.cppreference.com/w/cpp/memory/unsynchronized_pool_resource),
      [`std::pmr::synchronized_pool_resource`](https://en.cppreference.com/w/cpp/memory/synchronized_pool_resource),
      [`std::pmr::polymorphic_allocator`](https://en.cppreference.com/w/cpp/memory/polymorphic_allocator),
      [`std::pmr::null_memory_resource`](https://en.cppreference.com/w/cpp/memory/null_memory_resource),
      [`std::pmr::new_delete_resource`](https://en.cppreference.com/w/cpp/memory/new_delete_resource)
  [(cppreference API page coverage)](../test/cmake/driver/src/cpp/stl/memory.cpp)
- [x] [std::allocator_traits](https://en.cppreference.com/w/cpp/memory/allocator_traits)
  [(cppreference API page coverage)](../test/cmake/driver/src/cpp/stl/memory.cpp)
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
- [x] [std::ranges::sort](https://en.cppreference.com/w/cpp/algorithm/ranges/sort)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] C++20 `std::ranges` adaptors:
      [`std::views::take`](https://en.cppreference.com/w/cpp/ranges/take_view),
      [`std::views::drop`](https://en.cppreference.com/w/cpp/ranges/drop_view),
      [`std::views::reverse`](https://en.cppreference.com/w/cpp/ranges/reverse_view),
      [`std::views::join`](https://en.cppreference.com/w/cpp/ranges/join_view),
      [`std::views::split`](https://en.cppreference.com/w/cpp/ranges/split_view),
      [`std::views::values`](https://en.cppreference.com/w/cpp/ranges/values_view),
      [`std::views::keys`](https://en.cppreference.com/w/cpp/ranges/keys_view),
      and [`std::views::elements`](https://en.cppreference.com/w/cpp/ranges/elements_view)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::views::zip](https://en.cppreference.com/w/cpp/ranges/zip_view),
      [std::views::zip_transform](https://en.cppreference.com/w/cpp/ranges/zip_transform_view),
      [std::views::adjacent](https://en.cppreference.com/w/cpp/ranges/adjacent_view),
      [std::views::adjacent_transform](https://en.cppreference.com/w/cpp/ranges/adjacent_transform_view),
      [std::views::chunk](https://en.cppreference.com/w/cpp/ranges/chunk_view),
      [std::views::chunk_by](https://en.cppreference.com/w/cpp/ranges/chunk_by_view),
      [std::views::slide](https://en.cppreference.com/w/cpp/ranges/slide_view),
      [std::views::stride](https://en.cppreference.com/w/cpp/ranges/stride_view),
      [std::views::repeat](https://en.cppreference.com/w/cpp/ranges/repeat_view),
      [std::views::cartesian_product](https://en.cppreference.com/w/cpp/ranges/cartesian_product_view),
      [std::views::join_with](https://en.cppreference.com/w/cpp/ranges/join_with_view),
      and [std::views::enumerate](https://en.cppreference.com/w/cpp/ranges/enumerate_view)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::ratio](https://en.cppreference.com/w/cpp/numeric/ratio)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::reference_wrapper](https://en.cppreference.com/w/cpp/utility/functional/reference_wrapper)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::remove](https://en.cppreference.com/w/cpp/algorithm/remove)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::set](https://en.cppreference.com/w/cpp/container/set)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [`std::flat_set`](https://en.cppreference.com/w/cpp/container/flat_set)
  [(feature-test-gated API coverage)](../test/cmake/driver/src/cpp/stl/cxx_latest.cpp)
- [x] [std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/memory.cpp)
- [x] [std::sort](https://en.cppreference.com/w/cpp/algorithm/sort)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [Execution policies / parallel algorithms](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag)
  [(feature-test-gated cppreference example)](../test/cmake/driver/src/cpp/stl/cxx_latest.cpp)
- [x] [std::span](https://en.cppreference.com/w/cpp/container/span)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::mdspan](https://en.cppreference.com/w/cpp/container/mdspan)
  [(feature-test-gated cppreference example)](../test/cmake/driver/src/cpp/stl/cxx_latest.cpp)
- [x] [std::stack](https://en.cppreference.com/w/cpp/container/stack)
  [(cppreference push/emplace examples)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::source_location](https://en.cppreference.com/w/cpp/utility/source_location)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::stacktrace](https://en.cppreference.com/w/cpp/utility/basic_stacktrace)
  [(feature-test-gated cppreference example)](../test/cmake/driver/src/cpp/stl/cxx_latest.cpp)
  - The kernel ABI captures frames and formats entries as
    `module!function+offset` when a matching PDB is available at
    `PASSIVE_LEVEL`; otherwise it falls back to `module+offset`. When the
    matching PDB has CodeView line records, `source_file()` and
    `source_line()` report the source location; otherwise they fall back to
    empty/zero. The source driver test also forces missing-PDB and
    mismatched-PDB identity paths to verify that fallback behavior.
- [x] [`std::chrono::year_month_day`](https://en.cppreference.com/w/cpp/chrono/year_month_day),
      [`std::chrono::weekday`](https://en.cppreference.com/w/cpp/chrono/weekday),
      [`std::chrono::hh_mm_ss`](https://en.cppreference.com/w/cpp/chrono/hh_mm_ss)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/chrono.cpp)
- [x] [std::strong_ordering](https://en.cppreference.com/w/cpp/utility/compare/strong_ordering)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::format](https://en.cppreference.com/w/cpp/utility/format)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [`std::range_formatter`](https://en.cppreference.com/w/cpp/utility/format/range_formatter)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [`std::formatter`](https://en.cppreference.com/w/cpp/utility/format/formatter)
      customization
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [`std::range_formatter`](https://en.cppreference.com/w/cpp/utility/format/range_formatter)
      range-format specification snippets
  [(cppreference snippets)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::print](https://en.cppreference.com/w/cpp/io/print)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::regex](https://en.cppreference.com/w/cpp/regex)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/regex.cpp)
- [x] Regex algorithms and iterators:
      [`std::regex_match`](https://en.cppreference.com/w/cpp/regex/regex_match),
      [`std::regex_iterator`](https://en.cppreference.com/w/cpp/regex/regex_iterator),
      [`std::regex_token_iterator`](https://en.cppreference.com/w/cpp/regex/regex_token_iterator)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/regex.cpp)
- [x] [`std::quoted`](https://en.cppreference.com/w/cpp/io/manip/quoted)
      with [`std::stringstream`](https://en.cppreference.com/w/cpp/io/basic_stringstream)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/streams.cpp)
- [x] File stream classes:
      [`std::basic_filebuf`](https://en.cppreference.com/w/cpp/io/basic_filebuf),
      [`std::ifstream`](https://en.cppreference.com/w/cpp/io/basic_ifstream),
      [`std::ofstream`](https://en.cppreference.com/w/cpp/io/basic_ofstream),
      [`std::fstream`](https://en.cppreference.com/w/cpp/io/basic_fstream)
      plus file-oriented examples for `basic_filebuf::open`, `is_open`,
      `seekoff`, `seekpos`, and `underflow`, `basic_ifstream::is_open`,
      and `basic_fstream::open` / `is_open`. Additional driver semantic
      coverage checks `std::fstream` in-place update, append-mode writes,
      exception-mask open failures, and EOF/fail/clear state transitions.
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/streams.cpp)
- [x] [`std::spanstream`](https://en.cppreference.com/w/cpp/io/basic_spanstream)
      / [`basic_spanstream::span`](https://en.cppreference.com/w/cpp/io/basic_spanstream/span)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/streams.cpp)
- [x] [std::filesystem::path lexical operations](https://en.cppreference.com/w/cpp/filesystem/path/lexically_normal)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] `std::filesystem::path` decomposition, modifiers, observers, iteration,
      compare, and hashing
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::directory_iterator](https://en.cppreference.com/w/cpp/filesystem/directory_iterator)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::recursive_directory_iterator](https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::copy_file](https://en.cppreference.com/w/cpp/filesystem/copy_file)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [`std::filesystem::copy_options`](https://en.cppreference.com/w/cpp/filesystem/copy_options)
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
      including `assign`, `replace_filename`, and `refresh`
  [(tested)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
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
- [x] [std::filesystem::permissions](https://en.cppreference.com/w/cpp/filesystem/permissions)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::resize_file](https://en.cppreference.com/w/cpp/filesystem/resize_file)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::remove_all](https://en.cppreference.com/w/cpp/filesystem/remove)
      / `remove`
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::space](https://en.cppreference.com/w/cpp/filesystem/space)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::rename](https://en.cppreference.com/w/cpp/filesystem/rename)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::temp_directory_path](https://en.cppreference.com/w/cpp/filesystem/temp_directory_path)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::absolute](https://en.cppreference.com/w/cpp/filesystem/absolute)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::current_path](https://en.cppreference.com/w/cpp/filesystem/current_path)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::relative / proximate](https://en.cppreference.com/w/cpp/filesystem/relative)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::last_write_time](https://en.cppreference.com/w/cpp/filesystem/last_write_time)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::canonical / weakly_canonical](https://en.cppreference.com/w/cpp/filesystem/canonical)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] `std::filesystem` semantic edge checks
  - Error-code overloads, throwing overload parity, missing/existing/directory
    negative paths, metadata refresh after file changes, and recursive
    traversal pruning are covered by driver semantic tests.
  [(driver semantic test)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] CRT stdio / lowio file I/O semantic checks
  - `fopen` / `fread` / `fwrite` / `fseek` / `ftell` and `_open` / `_read` /
    `_write` / `_lseek` / `_close` and `remove` paths are covered with success,
    missing-file, invalid-descriptor, read-only descriptor, and `errno` /
    `_doserrno` propagation checks. `setvbuf`, `fgetpos` / `fsetpos`,
    `ungetc`, `tmpfile`, `tmpnam_s`, `_tempnam`, `freopen`,
    append/update stdio modes, EOF/error/`clearerr` state transitions, and
    wide stdio (`_wfopen`, `fputwc`, `fputws`, `fgetwc`, `fgetws`) are also
    exercised.
  [(driver semantic test)](../test/cmake/driver/src/cpp/stl/cstdio.cpp)
- [x] CRT file/process-state semantic checks
  - `_stat` / `_stat64` / `_wstat64`, `_fstat` / `_fstat64`,
    `_access` / `_waccess`,
    `_fullpath` / `_wfullpath`, `_getcwd` / `_wgetcwd`, `_chdir` / `_wchdir`,
    `_findfirst` / `_findnext`, `_findfirst64` / `_findnext64`,
    `_dup` / `_dup2`, `_tell`, `_telli64`, `_filelength`, `_filelengthi64`,
    `_lseeki64`, `_commit`, `_chsize`, `_chsize_s`, `_eof`, `_locking`,
    `_setmode`, text/binary newline translation, `_get_osfhandle`, and
    `_umask` paths are covered against the
    LDK-backed current-directory,
    file-handle, enumeration, and metadata substrate. `_O_EXCL`, `_O_APPEND`,
    invalid `_setmode`, invalid-descriptor, and missing-glob failure paths are
    also checked. CRT current-directory state is cross-checked against
    `std::filesystem::current_path`. `GetModuleHandleA/W`,
    `GetModuleFileNameA/W`, and `_get_pgmptr` / `_get_wpgmptr` cover CRT
    program-path and module-handle state.
  [(driver semantic test)](../test/cmake/driver/src/cpp/stl/cstdio_file_state.cpp)
- [x] CRT environment semantic checks
  - `getenv` / `getenv_s` / `_dupenv_s` and wide `_wgetenv` / `_wgetenv_s` /
    `_wdupenv_s` paths are covered through CRT-managed environment variables.
    `_putenv_s` / `_wputenv_s` add, update, and delete paths are also covered,
    including visibility through `GetEnvironmentVariableA/W`.
  [(driver semantic test)](../test/cmake/driver/src/cpp/stl/cstdlib.cpp)
- [x] CRT runtime-state semantic checks
  - `_get_fmode` / `_set_fmode`, `_query_new_mode` / `_set_new_mode`,
    `_get_errno` / `_set_errno`, and `_get_doserrno` / `_set_doserrno` are
    covered as CRT process/runtime state. Invalid-parameter handler contracts
    for `_get_errno`, `_get_doserrno`, and `strerror_s` are also checked. The
    test restores the original state before returning so later driver tests see
    the startup defaults.
  [(driver semantic test)](../test/cmake/driver/src/cpp/stl/cstdlib.cpp)
- [x] Error and diagnostics semantic checks
  - `GetLastError`, `FormatMessageA/W`, `std::system_category`,
    `std::generic_category`, `std::system_error`, `errno`, `_get_errno`,
    `_get_doserrno`, default error-condition mapping, and `FormatMessageA/W`
    failure-edge paths are covered through Win32 and CRT failure cases.
    `FORMAT_MESSAGE_ALLOCATE_BUFFER`, `LocalFree`, `std::filesystem_error`
    message quality, and selected invalid-parameter handler paths are also
    covered.
  [(driver semantic test)](../test/cmake/driver/src/cpp/stl/diagnostics.cpp)
- [x] [std::string](https://en.cppreference.com/w/cpp/string/basic_string)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/string.cpp)
- [x] String member operations:
      [`std::string::find`](https://en.cppreference.com/w/cpp/string/basic_string/find),
      [`std::string::substr`](https://en.cppreference.com/w/cpp/string/basic_string/substr),
      [`std::string::starts_with`](https://en.cppreference.com/w/cpp/string/basic_string/starts_with),
      [`std::string::ends_with`](https://en.cppreference.com/w/cpp/string/basic_string/ends_with),
      [`std::string::contains`](https://en.cppreference.com/w/cpp/string/basic_string/contains), and
      [`std::string::erase`](https://en.cppreference.com/w/cpp/string/basic_string/erase)
  [(cppreference member examples)](../test/cmake/driver/src/cpp/stl/string.cpp)
- [x] [std::string_view](https://en.cppreference.com/w/cpp/string/basic_string_view)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/string.cpp)
- [x] String view member operations:
      [`std::string_view::find`](https://en.cppreference.com/w/cpp/string/basic_string_view/find),
      [`std::string_view::substr`](https://en.cppreference.com/w/cpp/string/basic_string_view/substr),
      [`std::string_view::starts_with`](https://en.cppreference.com/w/cpp/string/basic_string_view/starts_with),
      [`std::string_view::ends_with`](https://en.cppreference.com/w/cpp/string/basic_string_view/ends_with), and
      [`std::string_view::contains`](https://en.cppreference.com/w/cpp/string/basic_string_view/contains)
  [(cppreference member examples)](../test/cmake/driver/src/cpp/stl/string.cpp)
- [x] [std::weak_ptr](https://en.cppreference.com/w/cpp/memory/weak_ptr)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/memory.cpp)
- [x] [std::thread](https://en.cppreference.com/w/cpp/thread)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L35)
- [x] [std::jthread](https://en.cppreference.com/w/cpp/thread/jthread)
  [(cppreference constructor example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] Coroutine library:
      [`std::noop_coroutine`](https://en.cppreference.com/w/cpp/coroutine/noop_coroutine)
      and [`std::generator`](https://en.cppreference.com/w/cpp/coroutine/generator)
  [(feature-test-gated cppreference examples)](../test/cmake/driver/src/cpp/stl/cxx_latest.cpp)
- [x] [std::tuple](https://en.cppreference.com/w/cpp/utility/tuple)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::type_index](https://en.cppreference.com/w/cpp/types/type_index)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::to_chars](https://en.cppreference.com/w/cpp/utility/to_chars)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
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
- [x] [std::visit](https://en.cppreference.com/w/cpp/utility/variant/visit2)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::vector](https://en.cppreference.com/w/cpp/container/vector)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] Container member operations:
      [`std::vector::emplace`](https://en.cppreference.com/w/cpp/container/vector/emplace),
      [`std::vector::erase`](https://en.cppreference.com/w/cpp/container/vector/erase),
      [`std::map::insert_or_assign`](https://en.cppreference.com/w/cpp/container/map/insert_or_assign),
      [`std::map::try_emplace`](https://en.cppreference.com/w/cpp/container/map/try_emplace),
      [`contains`](https://en.cppreference.com/w/cpp/container/map/contains),
      [`extract`](https://en.cppreference.com/w/cpp/container/map/extract), and
      [`merge`](https://en.cppreference.com/w/cpp/container/map/merge)
      paths for ordered and unordered associative containers
  [(cppreference member examples)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] Sequence container member operations:
      [`std::deque::emplace`](https://en.cppreference.com/w/cpp/container/deque/emplace),
      [`std::deque::erase`](https://en.cppreference.com/w/cpp/container/deque/erase),
      [`std::list::emplace`](https://en.cppreference.com/w/cpp/container/list/emplace),
      [`std::list::erase`](https://en.cppreference.com/w/cpp/container/list/erase),
      [`std::list::splice`](https://en.cppreference.com/w/cpp/container/list/splice),
      [`std::list::merge`](https://en.cppreference.com/w/cpp/container/list/merge),
      [`std::list::remove`](https://en.cppreference.com/w/cpp/container/list/remove),
      [`std::list::sort`](https://en.cppreference.com/w/cpp/container/list/sort),
      [`std::list::unique`](https://en.cppreference.com/w/cpp/container/list/unique),
      [`std::forward_list::erase_after`](https://en.cppreference.com/w/cpp/container/forward_list/erase_after),
      [`std::forward_list::splice_after`](https://en.cppreference.com/w/cpp/container/forward_list/splice_after),
      [`std::forward_list::merge`](https://en.cppreference.com/w/cpp/container/forward_list/merge),
      [`std::forward_list::remove`](https://en.cppreference.com/w/cpp/container/forward_list/remove),
      [`std::forward_list::sort`](https://en.cppreference.com/w/cpp/container/forward_list/sort), and
      [`std::forward_list::unique`](https://en.cppreference.com/w/cpp/container/forward_list/unique)
  [(cppreference member examples)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::numbers](https://en.cppreference.com/w/cpp/numeric/constants)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::random_device](https://en.cppreference.com/w/cpp/numeric/random/random_device)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/numeric.cpp)
- [x] CRT `rand_s`
  - `rand_s` and `std::random_device` repeated-call sanity checks cover the
    UCRT random provider path backed by LDK `SystemFunction036`.
  [(driver semantic tests)](../test/cmake/driver/src/cpp/stl/cstdlib.cpp),
  [(random_device semantic test)](../test/cmake/driver/src/cpp/stl/numeric.cpp)
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
- [x] [std::shared_timed_mutex](https://en.cppreference.com/w/cpp/thread/shared_timed_mutex)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::shared_lock](https://en.cppreference.com/w/cpp/thread/shared_lock)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp)
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
- [x] [std::async](https://en.cppreference.com/w/cpp/thread/async)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::future_status](https://en.cppreference.com/w/cpp/thread/future_status)
  [(cppreference wait_until example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::future_error](https://en.cppreference.com/w/cpp/thread/future_error)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::shared_future](https://en.cppreference.com/w/cpp/thread/shared_future)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] Threading/future semantic edge checks
  - Timed shared-lock timeout/reacquire behavior, `std::condition_variable` /
    `std::condition_variable_any` timeout and predicate wake behavior,
    `std::future` / `std::shared_future` timeout/ready states, deferred async
    status, `set_exception`, `set_value_at_thread_exit`, `broken_promise`,
    `promise_already_satisfied`, and latch/barrier/semaphore state transitions
    are covered by driver semantic tests.
  [(driver semantic test)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::promise](https://en.cppreference.com/w/cpp/thread/promise)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L254)
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

### OS-substrate Coverage Priorities

Future coverage should prefer tests that exercise the Win32 / NTDLL / UCRT /
ICU surface that MSVC CRT and STL normally expect from a hosted Windows process.
Pure header algorithms and value utilities remain useful, but they are lower
priority than tests that prove the LDK-backed runtime substrate.

| Priority | Area | What to prefer | Why it matters |
| --- | --- | --- | --- |
| P0 | `std::filesystem` and CRT file I/O | `copy_options`, directory traversal options, symlink and hard-link edge cases, metadata/status transitions, `error_code` overloads, stdio/lowio file handles | Exercises path normalization, file handles, directory enumeration, reparse points, file metadata, current/temp directory state, and Win32 error mapping. |
| P0 | Time and chrono OS paths | `system_clock`, `file_clock`, file timestamp round trips, timezone lookup, invalid-zone/error paths, formatting paths that touch tzdb/ICU | Exercises system time, file time, registry/timezone data, ICU/tzdb shims, and MSVC STL chrono ABI helpers. |
| P0 | Locale, NLS, and text conversion | named/user locales, `GetLocaleInfo`-backed facets, `ctype`/`collate`, UTF-8/multibyte conversion, `time_get`/`time_put`, `money_get`/`money_put` | Exercises NLS tables, code pages, locale data, ICU-backed behavior, and UCRT conversion helpers. |
| P1 | Threading, waits, and async | wait/notify timeout/error paths, condition-variable wake ordering, future/promise broken-promise and thread-exit paths, latch/barrier/semaphore semantics | Exercises LDK `WaitOnAddress`, keyed events, SRW/condition-variable behavior, thread handles, and unload-sensitive lifetime rules. |
| P1 | Error and diagnostics paths | `std::system_error`, `std::error_code`, `FormatMessageA/W`, `GetLastError`/`errno` propagation, filesystem exception messages | Exercises NTSTATUS/Win32 error mapping and message-resource lookup quality. |
| P2 | Additional process/runtime state | Startup-argument edge cases, invalid-parameter policy, `atexit`/`onexit` state, and other CRT startup/shutdown state not already covered above | Useful when a new MSVC CRT path starts depending on a process-global runtime state that is not yet exercised. |
| P2 | Loader/resource integration | resource lookup, DLL/module discovery paths, ICU and message-resource provider behavior | Useful when a CRT/STL path expects hosted loader/resource semantics. |
| P2 | Console/debug output | stream/stdout/stderr failure paths, `std::print`, `OutputDebugString`, output throttling behavior | Exercises console and debug-output policy; keep these out of production hot paths. |

### Future cppreference Coverage Candidates

No concrete standalone cppreference Example candidate remains queued here. Add
specific links here when a new driver-safe example is selected for porting.

### Needs Investigation

- [ ] [thread_local](https://en.cppreference.com/w/cpp/language/storage_duration#Thread_storage_duration)
  - General C++ `thread_local` is not supported as true per-thread TLS in the
    default driver build; the cppreference storage-duration example remains
    disabled. The multi-driver compiler TLS slot isolation above provides
    driver-image slot separation for MSVC runtime paths, not per-thread storage
    semantics for user variables. In other words, it routes each crtsys-linked
    driver image to its own compiler TLS slot so runtime state does not collide.
    It does not make `thread_local int x` thread-local: in kernel mode, the
    GS-based TLS model is tied to processor-local KPCR state rather than a
    user-mode TEB for each thread.
- [ ] [`std::inplace_vector`](https://en.cppreference.com/w/cpp/container/inplace_vector)
  - Not exposed by the active MSVC STL toolsets used by the driver-test matrix
    yet, so no driver test is compiled.
- [ ] [`std::hive`](https://en.cppreference.com/w/cpp/container/hive)
  - Not exposed by the active MSVC STL toolsets used by the driver-test matrix
    yet, so no driver test is compiled.

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
- [x] `ntl::result<T>` / `ntl::result<void>`
  - [x] value result, explicit `unexpected(status)` failure, `value_or`,
        failed `value()` exception, void success/failure paths, pool result
        factories, device result creation, and symbolic-link result factory
        creation
    [(tested)](../test/cmake/driver/src/ntl.cpp)
    [(docs)](./ntl/result.md)
- [x] `ntl::unique_kernel_handle` / `ntl::unique_object`
  - [x] kernel handle close, move, release, and adopt paths
  - [x] object-manager reference ownership from `ObReferenceObjectByHandle`
        through `try_reference_object_by_handle`
    [(tested)](../test/cmake/driver/src/ntl.cpp)
    [(docs)](./ntl/ownership.md)
- [x] `ntl::registry_key`
  - [x] volatile key create/open/delete paths
  - [x] `try_open_driver_parameters` `RegistryPath\\Parameters` open path
  - [x] `ntl::driver_config` optional setting defaults
  - [x] typed `REG_DWORD`, `REG_QWORD`, `REG_SZ`, `REG_EXPAND_SZ`, and
        `REG_BINARY` query/set paths
  - [x] value delete, missing-value status, move, release, adopt, and close
        ownership paths
    [(tested)](../test/cmake/driver/src/ntl.cpp)
    [(docs)](./ntl/registry.md)
- [x] `ntl::pool_ptr` / `ntl::pool_allocator`
  - [x] raw pool allocation and RAII pool buffer ownership
  - [x] pool-backed object construction/destruction with `ntl::pool_ptr<T>`
  - [x] `std::vector` with nonpaged and paged pool allocators
  - [x] `std::pmr::vector` with `ntl::pmr::pool_resource`
    [(tested)](../test/cmake/driver/src/ntl.cpp)
    [(docs)](./ntl/pool-allocator.md)
- [x] `ntl::lookaside_list`
  - [x] raw nonpaged lookaside allocation and explicit construction
  - [x] RAII object creation, `try_make`, move, reset, and destruction
  - [x] paged and cache-aligned nonpaged lookaside list construction
    [(tested)](../test/cmake/driver/src/ntl.cpp)
    [(docs)](./ntl/lookaside-list.md)
- [x] `ntl::mdl`
  - [x] MDL allocation, nonpaged-pool description, system-address lookup,
        move, release/manual free, and empty-owner error path
    [(tested)](../test/cmake/driver/src/ntl.cpp)
    [(docs)](./ntl/mdl.md)
- [x] `ntl::symbolic_link`
  - [x] create, move, close, scope cleanup, and release/manual-delete paths
    [(tested)](../test/cmake/driver/src/ntl.cpp)
    [(docs)](./ntl/symbolic-link.md)
- [x] `ntl::device_interface_link`
  - [x] empty-owner safety path
  - Real `IoRegisterDeviceInterface` registration requires a valid PnP PDO and
    is left to a PnP driver integration test.
    [(tested)](../test/cmake/driver/src/ntl.cpp)
    [(docs)](./ntl/device-interface.md)
- [x] `ntl::event`
  - [x] notification event set/clear/reset/wait state
  - [x] synchronization event auto-reset wait behavior
    [(tested)](../test/cmake/driver/src/ntl.cpp)
    [(docs)](./ntl/event.md)
- [x] `ntl::timer` / `ntl::kdpc`
  - [x] direct DPC queue and callback argument forwarding
  - [x] one-shot synchronization timer wait
  - [x] one-shot timer with DPC callback
  - [x] periodic timer setup/cancel path
    [(tested)](../test/cmake/driver/src/ntl.cpp)
    [(docs)](./ntl/timer.md)
- [x] `ntl::system_thread`
  - [x] `PsCreateSystemThread` creation through `ntl::result`
  - [x] thread routine execution at `PASSIVE_LEVEL`
  - [x] `join()` wait and handle close
  - [x] move, release, adopt ownership paths
    [(tested)](../test/cmake/driver/src/ntl.cpp)
    [(docs)](./ntl/system-thread.md)
- [x] `ntl::wait`
  - [x] zero-timeout `try_wait`
  - [x] `wait_signaled` / `wait_timed_out` classification
  - [x] `wait_for` with timer and system-thread wrappers
    [(tested)](../test/cmake/driver/src/ntl.cpp)
    [(docs)](./ntl/wait.md)
- [x] `ntl::remove_lock`
  - [x] acquire, guard move/reset, multiple acquired references,
        `release_and_wait`, and post-remove acquire rejection
    [(tested)](../test/cmake/driver/src/ntl.cpp)
    [(docs)](./ntl/remove-lock.md)
- [x] `ntl::work_item` / `ntl::passive_work_item`
  - [x] raw context work item queue/wait path
  - [x] callable work item queued from `DISPATCH_LEVEL` and executed at
        `PASSIVE_LEVEL`
  - [x] duplicate queue rejection while a work item is still pending
    [(tested)](../test/cmake/driver/src/ntl.cpp)
    [(docs)](./ntl/work-item.md)
- [x] `ntl::passive_executor`
  - [x] inline execution at `PASSIVE_LEVEL`
  - [x] detached nonpaged work posting
  - [x] raised-IRQL `execute()` deferral
  - [x] DPC callback handoff through `post()`
  - [x] caller-owned work item `queue_and_wait()`
    [(tested)](../test/cmake/driver/src/ntl.cpp)
    [(docs)](./ntl/passive-executor.md)
- [x] `ntl::driver`
  - [x] `driver.try_create_device` result-returning device creation path
  - [x] unload callback
    [(tested)](../test/cmake/driver/src/main.cpp#L73)
  - [x] device creation
    [(tested)](../test/cmake/driver/src/main.cpp#L44)
- [x] `ntl::device`
  - [x] device extension
    [(tested)](../test/cmake/driver/src/main.cpp#L33)
  - [x] `ntl::irp` result helpers and typed device-control buffer helpers
    [(tested)](../test/cmake/driver/src/main.cpp#L64)
  - [x] `ntl::ioctl` typed IOCTL descriptor and input/output helpers
    [(tested)](../test/cmake/driver/src/ntl.cpp)
    [(docs)](./ntl/ioctl.md)
  - [x] practical device-control dispatch pattern combining typed IOCTL,
        remove-lock guard, MDL scratch storage, output byte reporting, and
        teardown rejection
    [(tested)](../test/cmake/driver/src/ntl.cpp)
    [(docs)](./ntl/device-control-pattern.md)
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
  - [x] `ntl::current_irql`
  - [x] `ntl::is_passive_level`
  - [x] `ntl::is_irql_at_most`
  - [x] `ntl::require_passive_level`
  - [x] `ntl::require_irql_at_most`
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
