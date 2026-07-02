# 기능 지원 현황

[한국어 문서로 돌아가기](./ko-kr.md)

이 문서는 crtsys가 지원하는 전체 STL/CRT 기능의 한계표나 부정적인 호환성
목록이 아니라, driver-tested feature coverage matrix입니다. 여기에는
`crtsys` kernel driver test target에 명시적으로 포함되어 실제 driver
harness에서 실행되는 기능과 code path를 기록합니다.

목록에 없는 기능은 곧바로 미지원이라는 뜻이 아니며, 아직 별도 driver test로
고정하지 않았다는 의미입니다. 실제 미지원/제약이 확인된 항목은 known
limitation 또는 blocker에 따로 표시합니다.

`crtsys`는 프로젝트 전용 축소 STL을 새로 흉내 내는 방식이 아닙니다. MSVC
CRT/STL/VCRT/UCRT source path를 사용하고, 그 경로들이 요구하는 Windows /
NTDLL / ICU 호환 API substrate를 LDK가 제공합니다. 따라서 matrix에 아직
이름을 올리지 않은 STL/header 경로도 이미 검증된 항목과 같은 runtime
의존성을 공유하는 경우가 많습니다. 물론 모든 kernel execution context에서
자동 보장된다는 뜻은 아니지만, 이 목록에 없다는 사실만으로 미지원이라고
해석해서는 안 됩니다.

범례:

- [x] Driver-test coverage 있음: `crtsys` kernel driver test에서 명시적으로
  실행됩니다.
- [ ] 아직 coverage 없음: 명시 driver test로 아직 coverage하지 않았거나 검토
  중입니다.
- Known limitation: 실제 미지원 의존성 또는 의미 차이가 확인된 항목입니다.

여기서 coverage는 해당 기능이 구현되었고 driver test suite에서 실행된다는
뜻입니다. 모든 driver execution context에서 유효하다는 의미도 아니고,
사용 가능한 모든 MSVC STL/header path를 전부 나열한다는 뜻도 아닙니다. API가
더 넓은 계약을 문서화하지 않았다면 기본값은 `PASSIVE_LEVEL`로 보세요.
실행 문맥은 [설계 근거와 운영 경계](./ko-kr-design-rationale.md)와
[NTL API 문서](./ko-kr-ntl-api.md)를 참고하세요.

## IRQL 지원 계약

아래 체크리스트는 기능 지원 현황이지, 각 기능을 모든 IRQL에서 써도 된다는
허가가 아닙니다. 이 표는 현재 coverage에 대한 보수적인 `crtsys` 계약입니다.
"감사된 caller context"는 해당 표현식 자체가 값-only이고 block하지 않는다는
뜻입니다. 그래도 caller가 resident code/storage, allocation 없음, wait 없음,
exception 없음, 유효한 WDK 문맥을 직접 보장해야 합니다. driver test는 여전히
이 기능들을 `PASSIVE_LEVEL`에서 실행합니다.

| 기능 영역 | 포함 항목 | 지원 IRQL | 비고 |
| --- | --- | --- | --- |
| Runtime 및 C++ 초기화 | non-local static initialization, dynamic initialization, function-local `static` | `PASSIVE_LEVEL` / driver initialization path | `crtsys` wrapper가 `DriverEntry`에서 실행합니다. 일반 C++ `thread_local`은 true per-thread TLS로 지원하지 않습니다. |
| C++ 예외 | `throw`, `try`, function try block, `std::exception_ptr` | `PASSIVE_LEVEL` only | WDK callback 경계, spin lock 보유 구간, DPC, ISR, paging I/O 경로를 넘어 예외가 흐르게 하지 마세요. |
| C++ RTTI | `typeid`, `dynamic_cast` | 감사된 caller context; 그 외에는 `PASSIVE_LEVEL` | 사용하는 driver target은 RTTI를 켜고 빌드해야 합니다. 대상 객체와 type metadata는 resident 상태여야 합니다. |
| STL value-only helper | type traits, concepts, `std::array`, `std::span`, `std::string_view`, `std::bitset`, `std::pair`, `std::tuple`, `std::ratio`, `std::source_location`, `std::strong_ordering`, `std::numbers`, 단순 `std::move` / `std::exchange` / `std::invoke` / `std::reference_wrapper`, integer `std::to_chars` / `std::from_chars`, resident fixed storage 위의 순수 algorithm | 감사된 caller context; 그 외에는 `PASSIVE_LEVEL` | STL 중 `PASSIVE_LEVEL`보다 높은 곳에서 고려할 수 있는 영역은 이 정도뿐입니다. exact expression과 comparator/callback까지 감사해야 합니다. |
| STL owning object, container, callback-heavy utility | `std::string`, container, `std::any`, `std::function`, smart pointer, `std::optional`, `std::variant`, `std::complex`, `std::valarray`, `std::filesystem::path`, 기본 `std::filesystem` file operation, `std::format`, `std::print`, random distribution, `std::regex`, `nlohmann::json`, allocation/throw/arbitrary user code 호출 가능성이 있는 algorithm | `PASSIVE_LEVEL` only | 생성, 소멸, 비교, hashing, allocation, deallocation, formatting, file-system I/O, user callback이 runtime path를 끌어올 수 있습니다. |
| STL synchronization, threading, async | `std::thread`, `std::mutex`, `std::shared_mutex`, `std::condition_variable`, `std::condition_variable_any`, `std::scoped_lock`, `std::lock`, `std::try_lock`, `std::call_once`, `std::future`, `std::shared_future`, `std::promise`, `std::packaged_task` | `PASSIVE_LEVEL` only | wait, worker execution 생성, allocation, unload 이후 생존 문제가 생길 수 있습니다. |
| STL atomic primitive | resident storage 위의 `std::atomic`, `std::atomic_ref`, `std::atomic_flag` load, store, exchange, compare-exchange 및 C++20 wait/notify | 감사된 non-waiting operation은 `<= DISPATCH_LEVEL`; wait/notify는 `PASSIVE_LEVEL` only | wait/notify는 blocking synchronization으로 취급하세요. elevated IRQL에서 runtime-backed callback이나 allocation과 섞지 마세요. |
| I/O stream | `std::cin`, `std::cout`, `std::cerr`, `std::clog`, wide stream 계열 | `PASSIVE_LEVEL` only | diagnostic/test 용도입니다. production hot path와 stack-sensitive path에서는 피하세요. |
| C math 및 floating-point helper | math function, floating-point classification helper | exact helper를 별도 감사하지 않았다면 `PASSIVE_LEVEL` | floating-point state와 helper dependency는 driver context에 민감합니다. |
| NTL entry, driver, device, RPC server helper | `ntl::main`, `ntl::driver`, `ntl::device`, `ntl::rpc::server` | `PASSIVE_LEVEL` only | initialization, teardown, device setup, IOCTL/RPC control path 용도입니다. |
| NTL IRP view | `ntl::irp` | IRP를 넘겨준 dispatch path를 따름 | NTL device callback은 exact callback body를 별도 감사하지 않았다면 `PASSIVE_LEVEL`로 보세요. |
| NTL status wrapper | `ntl::status` | caller context | `NTSTATUS`를 감싼 value-only wrapper입니다. |
| NTL stack expansion | `ntl::expand_stack` | `PASSIVE_LEVEL` only | runtime-backed control-path helper입니다. hot path 회피 수단이 아닙니다. |
| NTL ERESOURCE wrapper | `ntl::resource`, `ntl::unique_lock<ntl::resource>`, `ntl::shared_lock<ntl::resource>` | `<= APC_LEVEL` | blocking/resource-style synchronization입니다. DPC, ISR, spin-lock-held path에서 쓰지 마세요. |
| NTL spin lock wrapper | `ntl::spin_lock`, `ntl::unique_lock<ntl::spin_lock>` | `<= DISPATCH_LEVEL` | 보유 구간은 resident, 짧고, nonblocking이어야 하며 allocation, wait, exception, stream, 임의 STL/runtime helper 호출이 없어야 합니다. |
| NTL IRQL helper | `ntl::irql`, `ntl::raise_irql`, `ntl::raise_irql_to_dpc_level`, `ntl::raise_irql_to_synch_level` | 현재 IRQL을 명시적으로 조작 | raised scope는 최대한 작게 유지하세요. |
| NTL RPC client | `ntl::rpc::client` | user mode, kernel IRQL 아님 | client side는 `DeviceIoControl`을 씁니다. kernel-side 안전성은 server callback 계약을 따릅니다. |

## C++ Standard

테스트는 [cppreference](https://en.cppreference.com)의 예제와 동작 설명을
기준으로 작성되었습니다.
cppreference Example 코드를 이식한 항목은
[cppreference attribution note](./cppreference-attribution.md)에 별도로 정리합니다.

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
- [x] Diagnostics 및 system-error helper:
      [std::uncaught_exceptions](https://en.cppreference.com/w/cpp/error/uncaught_exception),
      [std::nested_exception](https://en.cppreference.com/w/cpp/error/nested_exception),
      [std::throw_with_nested](https://en.cppreference.com/w/cpp/error/throw_with_nested),
      [std::rethrow_if_nested](https://en.cppreference.com/w/cpp/error/rethrow_if_nested),
      [std::error_code](https://en.cppreference.com/w/cpp/error/error_code),
      [std::error_condition](https://en.cppreference.com/w/cpp/error/error_condition),
      [std::system_error](https://en.cppreference.com/w/cpp/error/system_error)
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
- [x] Atomic fence 및 free atomic operation:
      [`std::atomic_thread_fence`](https://en.cppreference.com/w/cpp/atomic/atomic_thread_fence),
      [`std::atomic_signal_fence`](https://en.cppreference.com/w/cpp/atomic/atomic_signal_fence),
      [`std::atomic_fetch_add`](https://en.cppreference.com/w/cpp/atomic/atomic_fetch_add),
      [`std::atomic_compare_exchange`](https://en.cppreference.com/w/cpp/atomic/atomic_compare_exchange)
  [(cppreference examples 및 direct API coverage)](../test/cmake/driver/src/cpp/stl/atomic.cpp)
- [x] [std::chrono](https://en.cppreference.com/w/cpp/chrono)
  [(tested)](../test/cmake/driver/src/cpp/stl/chrono.cpp#L15)
  - C++20 timezone 경로인
    [`std::chrono::current_zone`](https://en.cppreference.com/w/cpp/chrono/current_zone),
    [`std::chrono::locate_zone`](https://en.cppreference.com/w/cpp/chrono/locate_zone),
    [`std::chrono::zoned_time`](https://en.cppreference.com/w/cpp/chrono/zoned_time),
    [`std::chrono::time_zone::get_info`](https://en.cppreference.com/w/cpp/chrono/time_zone/get_info)도
    기본 driver build에서 실행됩니다.
  - C++20 calendar/time-of-day coverage는
    [`std::chrono::year_month_day`](https://en.cppreference.com/w/cpp/chrono/year_month_day),
    [`std::chrono::weekday`](https://en.cppreference.com/w/cpp/chrono/weekday),
    [`std::chrono::hh_mm_ss`](https://en.cppreference.com/w/cpp/chrono/hh_mm_ss)를 포함합니다.
  - C++20 clock conversion coverage는
    [`std::chrono::file_clock`](https://en.cppreference.com/w/cpp/chrono/file_clock),
    [`std::chrono::utc_clock`](https://en.cppreference.com/w/cpp/chrono/utc_clock),
    [`std::chrono::tai_clock`](https://en.cppreference.com/w/cpp/chrono/tai_clock),
    [`std::chrono::gps_clock`](https://en.cppreference.com/w/cpp/chrono/gps_clock),
    [`std::chrono::clock_cast`](https://en.cppreference.com/w/cpp/chrono/clock_cast)를
    포함합니다.
  - C++20 chrono stream parsing coverage는
    [`std::chrono::parse`](https://en.cppreference.com/w/cpp/chrono/parse)를
    `std::chrono::year_month_day` 대상으로 실행합니다.
  - duration rounding coverage는
    [`std::chrono::floor`](https://en.cppreference.com/w/cpp/chrono/duration/floor),
    [`std::chrono::round`](https://en.cppreference.com/w/cpp/chrono/duration/round),
    [`std::chrono::ceil`](https://en.cppreference.com/w/cpp/chrono/duration/ceil)을
    포함합니다.
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
- [x] [`<bit>` 유틸리티](https://en.cppreference.com/w/cpp/utility/bit)
  [(cppreference popcount 및 bit operation examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::bit_cast](https://en.cppreference.com/w/cpp/numeric/bit_cast)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::endian](https://en.cppreference.com/w/cpp/types/endian)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::byteswap](https://en.cppreference.com/w/cpp/numeric/byteswap)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::concepts](https://en.cppreference.com/w/cpp/concepts)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::complex](https://en.cppreference.com/w/cpp/numeric/complex)
  산술 연산 및 [std::exp](https://en.cppreference.com/w/cpp/numeric/complex/exp),
  [std::pow](https://en.cppreference.com/w/cpp/numeric/complex/pow)
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
- [x] Numeric reduce/scan algorithm:
      [std::reduce](https://en.cppreference.com/w/cpp/algorithm/reduce),
      [std::transform_reduce](https://en.cppreference.com/w/cpp/algorithm/transform_reduce),
      [std::exclusive_scan](https://en.cppreference.com/w/cpp/algorithm/exclusive_scan),
      [std::transform_inclusive_scan](https://en.cppreference.com/w/cpp/algorithm/transform_inclusive_scan),
      [std::transform_exclusive_scan](https://en.cppreference.com/w/cpp/algorithm/transform_exclusive_scan)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::inner_product](https://en.cppreference.com/w/cpp/algorithm/inner_product)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::advance](https://en.cppreference.com/w/cpp/iterator/advance),
      [std::distance](https://en.cppreference.com/w/cpp/iterator/distance),
      [std::next](https://en.cppreference.com/w/cpp/iterator/next),
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
  [(cppreference examples 및 documented-note coverage)](../test/cmake/driver/src/cpp/stl/utility.cpp)
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
  및 monadic operation
  [`and_then`](https://en.cppreference.com/w/cpp/utility/optional/and_then),
  [`transform`](https://en.cppreference.com/w/cpp/utility/optional/transform),
  `or_else`
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
- [x] Allocator 및 pointer utility helper:
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
      [std::assume_aligned](https://en.cppreference.com/w/cpp/memory/assume_aligned)
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
- [x] 추가 C++20 range adaptor:
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
- [x] 추가 C++23 range adaptor:
      [std::views::enumerate](https://en.cppreference.com/w/cpp/ranges/enumerate_view),
      [std::views::cartesian_product](https://en.cppreference.com/w/cpp/ranges/cartesian_product_view),
      [std::views::chunk_by](https://en.cppreference.com/w/cpp/ranges/chunk_by_view),
      [std::views::join_with](https://en.cppreference.com/w/cpp/ranges/join_with_view),
      [std::views::as_rvalue](https://en.cppreference.com/w/cpp/ranges/as_rvalue_view),
      [std::views::as_const](https://en.cppreference.com/w/cpp/ranges/as_const_view)
  [(feature-test-gated cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] 추가 C++23 ranges algorithm:
      [`std::ranges::contains`](https://en.cppreference.com/w/cpp/algorithm/ranges/contains),
      [`std::ranges::contains_subrange`](https://en.cppreference.com/w/cpp/algorithm/ranges/contains),
      [`std::ranges::starts_with`](https://en.cppreference.com/w/cpp/algorithm/ranges/starts_with),
      [`std::ranges::ends_with`](https://en.cppreference.com/w/cpp/algorithm/ranges/ends_with),
      [`std::ranges::find_last`](https://en.cppreference.com/w/cpp/algorithm/ranges/find_last),
      [`std::ranges::fold_left`](https://en.cppreference.com/w/cpp/algorithm/ranges/fold_left)
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
- [x] 추가 non-modifying 및 modifying algorithm:
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
      [std::sample](https://en.cppreference.com/w/cpp/algorithm/sample)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] 추가 partition/set/heap/min-max/permutation algorithm:
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
      [std::is_permutation](https://en.cppreference.com/w/cpp/algorithm/is_permutation)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::span](https://en.cppreference.com/w/cpp/container/span)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::stack](https://en.cppreference.com/w/cpp/container/stack)
  [(cppreference push/emplace examples)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::source_location](https://en.cppreference.com/w/cpp/utility/source_location)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::strong_ordering](https://en.cppreference.com/w/cpp/utility/compare/strong_ordering)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] Comparison 및 utility helper:
      [std::weak_ordering](https://en.cppreference.com/w/cpp/utility/compare/weak_ordering),
      [std::partial_ordering](https://en.cppreference.com/w/cpp/utility/compare/partial_ordering),
      [std::cmp_* / std::in_range](https://en.cppreference.com/w/cpp/utility/intcmp),
      [std::as_const](https://en.cppreference.com/w/cpp/utility/as_const),
      [std::apply](https://en.cppreference.com/w/cpp/utility/apply),
      [std::make_from_tuple](https://en.cppreference.com/w/cpp/utility/make_from_tuple),
      [std::forward_like](https://en.cppreference.com/w/cpp/utility/forward_like)
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
  [(cppreference example 및 direct stream coverage)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] C++23 span-backed stream:
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
- [x] [std::filesystem::path lexical operation](https://en.cppreference.com/w/cpp/filesystem/path/lexically_normal)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::absolute](https://en.cppreference.com/w/cpp/filesystem/absolute),
      [std::filesystem::relative / proximate](https://en.cppreference.com/w/cpp/filesystem/relative),
      [std::filesystem::current_path](https://en.cppreference.com/w/cpp/filesystem/current_path)
  [(direct path-relation coverage)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::directory_iterator](https://en.cppreference.com/w/cpp/filesystem/directory_iterator)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::recursive_directory_iterator](https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
  - `disable_recursion_pending()` 및 `increment(error_code&)` coverage를
    포함합니다.
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
  - metadata 전환 뒤
    [`directory_entry::refresh(error_code&)`](https://en.cppreference.com/w/cpp/filesystem/directory_entry/refresh)
    경로를 포함합니다.
- [x] [std::filesystem::equivalent](https://en.cppreference.com/w/cpp/filesystem/equivalent)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::file_size](https://en.cppreference.com/w/cpp/filesystem/file_size)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::hard_link_count](https://en.cppreference.com/w/cpp/filesystem/hard_link_count)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] [std::filesystem::status / symlink_status](https://en.cppreference.com/w/cpp/filesystem/status)
  및 [exists](https://en.cppreference.com/w/cpp/filesystem/exists),
  [is_directory](https://en.cppreference.com/w/cpp/filesystem/is_directory),
  [is_regular_file](https://en.cppreference.com/w/cpp/filesystem/is_regular_file),
  [is_empty](https://en.cppreference.com/w/cpp/filesystem/is_empty) 같은 file type query
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
  - 선택된 non-throwing `error_code&` overload coverage를 포함합니다.
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
- [x] 선택된 non-throwing filesystem `error_code&` overload:
      [`remove`](https://en.cppreference.com/w/cpp/filesystem/remove),
      [`resize_file`](https://en.cppreference.com/w/cpp/filesystem/resize_file),
      [`last_write_time`](https://en.cppreference.com/w/cpp/filesystem/last_write_time),
      [`canonical`](https://en.cppreference.com/w/cpp/filesystem/canonical)
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
  timed exclusive/shared lock 경로
  [(cppreference incomplete example 및 timed edge coverage)](../test/cmake/driver/src/cpp/stl/thread.cpp)
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
  및 future exception propagation
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::future_status](https://en.cppreference.com/w/cpp/thread/future_status)
  [(cppreference wait_until example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::future_error](https://en.cppreference.com/w/cpp/thread/future_error)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::shared_future](https://en.cppreference.com/w/cpp/thread/shared_future)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::shared_future::wait_for](https://en.cppreference.com/w/cpp/thread/shared_future/wait_for)
  및 future/shared-future timeout edge
  [(cppreference example 및 timeout edge coverage)](../test/cmake/driver/src/cpp/stl/thread.cpp)
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
  및 [std::binary_semaphore](https://en.cppreference.com/w/cpp/thread/counting_semaphore)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/thread.cpp)
- [x] [std::stop_source](https://en.cppreference.com/w/cpp/thread/stop_source)
  및 [std::stop_token](https://en.cppreference.com/w/cpp/thread/stop_token)
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

## 테스트 백로그

아래 미체크 항목은 앞으로 cppreference Example 이식 또는 런타임 지원 작업
후보입니다. 명시적으로 limitation이라고 적힌 항목이 아니면 실패가 확인된
목록이 아닙니다. 기본 원칙은 `PASSIVE_LEVEL`에서 파일 시스템, locale, 콘솔
입력, 미지원 hosted C++ 환경 의존 없이 실행할 수 있는 예제를 우선합니다.

### 앞으로 보강할 cppreference coverage 후보

- [ ] Core language 예제
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
    [`coroutines`](https://en.cppreference.com/w/cpp/language/coroutines) 중
    hosted 가정 없이 driver harness에서 안전하게 실행할 수 있는 예제
- [ ] Diagnostics 및 system-error 예제
  - MSVC STL이 노출하는 경우 C++23/C++26 stacktrace/debugging API
- [ ] Utility, comparison, type-support 예제
  - 추가 type-traits 예제와 MSVC STL이 노출하는 경우 C++26 comparison /
    type-order API
- [ ] Memory 및 allocator 예제
  - active MSVC STL이 노출하는 경우 C++23
    [`std::out_ptr`](https://en.cppreference.com/w/cpp/memory/out_ptr),
    [`std::inout_ptr`](https://en.cppreference.com/w/cpp/memory/inout_ptr)
- [ ] Container 및 view 예제
  - [`std::vector<bool>`](https://en.cppreference.com/w/cpp/container/vector_bool),
    active MSVC STL에서 열리는 경우 C++23 flat container(`flat_set`,
    `flat_map`, `flat_multiset`, `flat_multimap`),
    [`std::mdspan`](https://en.cppreference.com/w/cpp/container/mdspan),
    C++26 `inplace_vector` / `hive`
  - 이미 coverage된 container의 추가 member operation:
    `emplace`, `erase`, `extract`, `merge`, `contains`, `insert_range`,
    node-handle, heterogeneous lookup, allocator-aware construction
- [ ] Algorithm 및 numeric algorithm 예제
  - 아직 남은 search/mutation 예제:
    `find_end`, `find_first_of`, `adjacent_find`, `search`, `search_n`,
    `iter_swap`, `swap_ranges`, `shuffle`, `push_heap`, `pop_heap`, `sort_heap`
  - 아직 남은 numeric 예제:
    ranges `iota`, special math functions, random engine/distribution,
    floating-point environment, C++26 checked/saturation arithmetic
- [ ] Ranges 예제
  - range access/concepts 및 utility:
    `begin`/`end`, `size`, `data`, `empty`, `subrange`,
    `view_interface`, `ref_view`, `owning_view`, `from_range`, `ranges::to`
  - 남은/adaptive view:
    `empty_view`, `single_view`, `iota_view`, `istream_view`,
    `take_while`, `drop_while`, `common_view`, `views::counted`,
    `lazy_split`, `zip_transform`, `adjacent`, `pairwise`,
    `adjacent_transform`, 가능하면 C++26 `indices`, `as_input`,
    `concat`, `cache_latest`, `reserve_hint`
- [ ] I/O, text, locale edge 예제
  - [`std::basic_syncbuf`](https://en.cppreference.com/w/cpp/io/basic_syncbuf),
    [`std::basic_osyncstream`](https://en.cppreference.com/w/cpp/io/basic_osyncstream),
    `basic_stringbuf`, `basic_spanbuf`, `ios_base` / `basic_ios`,
    `istream` / `ostream` manipulator, `iostream_category` / `io_errc`,
    LDK file API로 안전하게 받칠 수 있는 file-stream 예제
  - active MSVC STL에서 노출하는 경우 C++26
    [`std::text_encoding`](https://en.cppreference.com/w/cpp/locale/text_encoding)
- [ ] Filesystem edge 예제
  - 더 많은 `path` iterator/formatting case, `copy_options`,
    `directory_options`, `recursive_directory_iterator::depth/pop/recursion_pending`,
    permission option 조합, `equivalent`, `read_symlink`, `copy`, `remove`,
    metadata/time/error-code overload의 negative path
- [ ] Concurrency edge 예제
  - `std::this_thread::get_id`, `yield`, `sleep_until`,
    [`std::notify_all_at_thread_exit`](https://en.cppreference.com/w/cpp/thread/notify_all_at_thread_exit),
    `condition_variable` / `condition_variable_any`의 `wait_for`와
    `wait_until`, 추가 `timed_mutex` / `recursive_timed_mutex` timed edge,
    `std::launch`, `future_category`, `future_errc`, hardware interference
    size constant, active MSVC STL에서 열리는 C++26 stop-token /
    safe-reclamation API
- [ ] 새 MSVC STL feature-test macro 또는 새 cppreference 예제가 공개되면
      계속 추적합니다.

### 추가 조사가 필요한 후보

- [ ] [thread_local](https://en.cppreference.com/w/cpp/language/storage_duration#Thread_storage_duration)
  - 일반 C++ `thread_local`은 기본 driver build에서 true per-thread TLS로
    지원하지 않습니다. cppreference storage-duration 예제는 비활성화되어
    있습니다.

## C Standard

- [x] Math functions
  - 필요한 경우 작은 portable 구현을 참고해 보강했습니다.
  - 참고:
    [RetrievAL](https://github.com/SpoilerScriptsGroup/RetrievAL),
    [musl](https://github.com/bminor/musl)
- [x] Floating-point classification helpers
  [(source)](../src/custom/crt/math/fpclassify.c)

## NTL: NT Template Library

NTL은 드라이버 코드를 위한 C++ helper를 제공합니다. API 수준 설명은
[NTL API 문서](./ko-kr-ntl-api.md)를 참고하세요.

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
