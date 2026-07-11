# 기능 지원 현황

[한국어 문서로 돌아가기](./ko-kr.md)

이 문서는 crtsys가 지원하는 전체 STL/CRT 기능의 한계표나 부정적인 호환성
목록이 아니라, driver-tested feature coverage matrix입니다. 여기에는
`crtsys` kernel driver test target에 명시적으로 포함되어 실제 driver
harness에서 실행되는 기능과 code path를 기록합니다.

목록에 없는 기능은 곧바로 미지원이라는 뜻이 아니며, 아직 별도 driver test로
고정하지 않았다는 의미입니다. 실제 미지원/제약이 확인된 항목은 known
limitation 또는 blocker에 따로 표시합니다.

실제로 사용할 수 있는 표면은 아래 표보다 넓은 경우가 많습니다. `crtsys`는
MSVC CRT/STL source path를 사용하고, 그 경로가 필요로 하는 Win32/NTDLL/ICU
의존성은 LDK를 통해 kernel-mode substrate로 연결합니다. 그래서 matrix에
개별 행이 없는 인접 header 함수나 overload도 별도 테스트 없이 동작하는
경우가 많습니다. 이 matrix는 의도적으로 증거 목록입니다. 대표적인
cppreference 예제, kernel-sensitive runtime path, 회귀가 쉬운 동작, LDK
substrate를 실제로 밟는 API를 명시 테스트로 고정합니다. 목록에 없는 경로를
사용할 때도 일반 kernel code와 같은 driver-context 감사를 적용하고, 해당
경로가 드라이버에서 중요해지면 harness test를 추가하는 방식이 좋습니다.

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
| Runtime 및 C++ 초기화 | non-local static initialization, dynamic initialization, function-local `static` | `PASSIVE_LEVEL` / driver initialization path | `crtsys` wrapper가 `DriverEntry`에서 실행합니다. MSVC compiler TLS slot은 여러 crtsys-linked driver 사이에서 격리됩니다. 이것은 runtime slot 격리이지 사용자 `thread_local` 지원이 아닙니다. kernel GS는 thread별 user-mode TEB가 아니라 processor-local KPCR입니다. |
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
- [x] Multi-driver compiler TLS slot isolation
  - 여러 crtsys-linked driver image가 동시에 load되어도 thread-safe
    function-local `static` initialization 같은 runtime path에서 사용하는
    MSVC compiler TLS `_tls_index`가 서로 충돌하지 않습니다. 보장하는 것은
    module/driver-image 격리입니다. shared slot vector가 각 driver image의
    `_tls_index`를 그 driver의 compiler TLS image buffer에 매핑합니다. 이것은
    driver image 사이의 runtime slot 격리입니다. 사용자 `thread_local T value`
    를 안전하게 만드는 기능은 아닙니다. kernel mode에서 GS 기반 TLS 가정은
    thread별 user-mode TEB가 아니라 processor-local KPCR 쪽에 걸립니다.
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
  - 기본 타입, class, struct, enum 이름을 driver semantic test로
    검증합니다.
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
- [x] Atomic fence 및 free function:
      [`std::atomic_thread_fence`](https://en.cppreference.com/w/cpp/atomic/atomic_thread_fence),
      [`std::atomic_signal_fence`](https://en.cppreference.com/w/cpp/atomic/atomic_signal_fence),
      [`std::atomic_fetch_add`](https://en.cppreference.com/w/cpp/atomic/atomic_fetch_add),
      [`std::atomic_compare_exchange`](https://en.cppreference.com/w/cpp/atomic/atomic_compare_exchange)
  [(cppreference example/API coverage)](../test/cmake/driver/src/cpp/stl/atomic.cpp)
- [x] [std::chrono](https://en.cppreference.com/w/cpp/chrono)
  [(tested)](../test/cmake/driver/src/cpp/stl/chrono.cpp#L15)
  - C++20 timezone 경로인
    [`std::chrono::current_zone`](https://en.cppreference.com/w/cpp/chrono/current_zone),
    [`std::chrono::locate_zone`](https://en.cppreference.com/w/cpp/chrono/locate_zone),
    [`std::chrono::zoned_time`](https://en.cppreference.com/w/cpp/chrono/zoned_time),
    [`std::chrono::time_zone::get_info`](https://en.cppreference.com/w/cpp/chrono/time_zone/get_info)도
    기본 driver build에서 실행됩니다.
    [`std::chrono::get_tzdb`](https://en.cppreference.com/w/cpp/chrono/get_tzdb) /
    [`std::chrono::get_tzdb_list`](https://en.cppreference.com/w/cpp/chrono/get_tzdb_list)와
    invalid-zone `std::chrono::locate_zone` error path도 검증합니다.
    추가 driver semantic check는 current-zone name lookup, `zoned_time`의
    sys-time 유지, fixed-offset `choose::earliest` / `choose::latest`
    local-time round trip, timezone abbreviation 경계를 검증합니다.
  - [`std::chrono::system_clock::to_time_t`](https://en.cppreference.com/w/cpp/chrono/system_clock/to_time_t),
    [`std::chrono::system_clock::from_time_t`](https://en.cppreference.com/w/cpp/chrono/system_clock/from_time_t),
    [`std::chrono::file_clock::now`](https://en.cppreference.com/w/cpp/chrono/file_clock/now),
    `time_zone::to_local` / `time_zone::to_sys`는 OS time semantic test로
    검증합니다. `UTC` / `Etc/UTC` alias lookup 및
    `time_zone::get_info(local_time)` unique-mapping도 확인합니다.
  - File timestamp round trip은 `std::filesystem::last_write_time` get/set 및
    missing-file error path를 통해 `std::chrono::file_clock` /
    `system_clock` 변환을 검증합니다.
  - [`std::chrono::clock_cast`](https://en.cppreference.com/w/cpp/chrono/clock_cast)
    coverage는 active MSVC STL에서 해당 clock을 노출하는 경우
    `system_clock`, `file_clock`, `utc_clock`, `tai_clock`, `gps_clock`
    round trip을 확인합니다.
- [x] CRT time semantic check
  - `_time64`, `_ftime64_s`, `gmtime_s`, `localtime_s`, `_tzset`,
    `_get_timezone`, `_get_daylight`, `_get_tzname`, `strftime`, `wcsftime`,
    `asctime_s`, `_ctime64_s`, `mktime`, `_mkgmtime64`, `difftime` 경로를
    LDK가 제공하는 system time 및 timezone substrate 기준으로 검증합니다.
    `TZ=UTC0` 및 `TZ=KST-9` timezone round trip과 `strftime` / `wcsftime`
    small-buffer failure도 확인합니다.
  [(driver semantic test)](../test/cmake/driver/src/cpp/stl/ctime.cpp)
- [x] [std::any](https://en.cppreference.com/w/cpp/utility/any)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::bind](https://en.cppreference.com/w/cpp/utility/functional/bind)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/functional.cpp)
- [x] [std::bind_front](https://en.cppreference.com/w/cpp/utility/functional/bind_front)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/functional.cpp)
- [x] [std::binary_search](https://en.cppreference.com/w/cpp/algorithm/binary_search)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] Binary-search 계열:
      [`std::lower_bound`](https://en.cppreference.com/w/cpp/algorithm/lower_bound),
      [`std::upper_bound`](https://en.cppreference.com/w/cpp/algorithm/upper_bound),
      [`std::equal_range`](https://en.cppreference.com/w/cpp/algorithm/equal_range)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::bitset](https://en.cppreference.com/w/cpp/utility/bitset)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [`<bit>` 유틸리티](https://en.cppreference.com/w/cpp/utility/bit)
  [(cppreference popcount example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] `<bit>` 개별 예제:
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
- [x] Locale facet semantic check
  - named `std::locale`의 ctype, collate compare/transform/hash, wide
    collate, codecvt, numpunct, moneypunct facet을 LDK 기반 locale/NLS
    substrate 기준으로 검증하며, locale facet 기반 grouped numeric
    parsing도 확인합니다.
  [(driver semantic test)](../test/cmake/driver/src/cpp/stl/locale.cpp)
- [x] NLS and text conversion semantic check
  - `MultiByteToWideChar`, `WideCharToMultiByte`, `GetStringTypeA/W`,
    `GetStringTypeExW`, `LCMapStringEx` upper/lower mapping, `CompareStringEx`,
    `CompareStringOrdinal`, CP_ACP / UTF-8 round trip, insufficient-buffer 및
    invalid-sequence / invalid-flag error case, UCRT `mbtowc` / `wctomb` /
    `mbstowcs` / `mbstowcs_s` / `wcstombs` / `wcstombs_s` / `mbrtowc` /
    `wcrtomb`, C++ UTF conversion 함수 `mbrtoc16` / `c16rtomb` /
    `mbrtoc32` / `c32rtomb`, filesystem UTF-8 path
    create/read/copy/rename/enumeration 경로를 검증합니다.
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
- [x] Sorting/selection 계열:
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
- [x] PMR pool/resource API:
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
- [x] C++20 `std::ranges` adaptor:
      [`std::views::take`](https://en.cppreference.com/w/cpp/ranges/take_view),
      [`std::views::drop`](https://en.cppreference.com/w/cpp/ranges/drop_view),
      [`std::views::reverse`](https://en.cppreference.com/w/cpp/ranges/reverse_view),
      [`std::views::join`](https://en.cppreference.com/w/cpp/ranges/join_view),
      [`std::views::split`](https://en.cppreference.com/w/cpp/ranges/split_view),
      [`std::views::values`](https://en.cppreference.com/w/cpp/ranges/values_view),
      [`std::views::keys`](https://en.cppreference.com/w/cpp/ranges/keys_view),
      [`std::views::elements`](https://en.cppreference.com/w/cpp/ranges/elements_view)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::views::zip](https://en.cppreference.com/w/cpp/ranges/zip_view),
      [std::views::chunk](https://en.cppreference.com/w/cpp/ranges/chunk_view),
      [std::views::slide](https://en.cppreference.com/w/cpp/ranges/slide_view),
      [std::views::stride](https://en.cppreference.com/w/cpp/ranges/stride_view),
      [std::views::repeat](https://en.cppreference.com/w/cpp/ranges/repeat_view)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] C++23 `std::ranges` 추가 adaptor:
      [std::views::chunk_by](https://en.cppreference.com/w/cpp/ranges/chunk_by_view),
      [std::views::cartesian_product](https://en.cppreference.com/w/cpp/ranges/cartesian_product_view),
      [std::views::join_with](https://en.cppreference.com/w/cpp/ranges/join_with_view),
      [std::views::adjacent](https://en.cppreference.com/w/cpp/ranges/adjacent_view),
      [std::views::adjacent_transform](https://en.cppreference.com/w/cpp/ranges/adjacent_transform_view),
      [std::views::enumerate](https://en.cppreference.com/w/cpp/ranges/enumerate_view)
  [(feature-test-gated cppreference examples)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
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
  - kernel ABI는 frame을 캡처하고, 매칭되는 PDB가 `PASSIVE_LEVEL`에서
    접근 가능하면 entry를 `module!function+offset` 형태로 포맷합니다.
    PDB가 없거나 맞지 않으면 `module+offset`으로 fallback합니다.
    source file/line은 아직 empty/zero로 보고합니다.
- [x] [`std::chrono::year_month_day`](https://en.cppreference.com/w/cpp/chrono/year_month_day),
      [`std::chrono::weekday`](https://en.cppreference.com/w/cpp/chrono/weekday),
      [`std::chrono::hh_mm_ss`](https://en.cppreference.com/w/cpp/chrono/hh_mm_ss)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/chrono.cpp)
- [x] [std::strong_ordering](https://en.cppreference.com/w/cpp/utility/compare/strong_ordering)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::format](https://en.cppreference.com/w/cpp/utility/format)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [`std::formatter`](https://en.cppreference.com/w/cpp/utility/format/formatter)
      customization
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [`std::range_formatter`](https://en.cppreference.com/w/cpp/utility/format/range_formatter)
      range-format specification snippet
  [(cppreference snippets)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::print](https://en.cppreference.com/w/cpp/io/print)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::regex](https://en.cppreference.com/w/cpp/regex)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/regex.cpp)
- [x] Regex algorithm 및 iterator:
      [`std::regex_match`](https://en.cppreference.com/w/cpp/regex/regex_match),
      [`std::regex_iterator`](https://en.cppreference.com/w/cpp/regex/regex_iterator),
      [`std::regex_token_iterator`](https://en.cppreference.com/w/cpp/regex/regex_token_iterator)
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/regex.cpp)
- [x] [`std::quoted`](https://en.cppreference.com/w/cpp/io/manip/quoted)
      및 [`std::stringstream`](https://en.cppreference.com/w/cpp/io/basic_stringstream)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/streams.cpp)
- [x] 파일 스트림 클래스:
      [`std::basic_filebuf`](https://en.cppreference.com/w/cpp/io/basic_filebuf),
      [`std::ifstream`](https://en.cppreference.com/w/cpp/io/basic_ifstream),
      [`std::ofstream`](https://en.cppreference.com/w/cpp/io/basic_ofstream),
      [`std::fstream`](https://en.cppreference.com/w/cpp/io/basic_fstream)
      및 `basic_filebuf::open`, `is_open`, `seekoff`, `seekpos`,
      `underflow`, `basic_ifstream::is_open`, `basic_fstream::open` /
      `is_open` 파일 지향 예제. 추가 driver semantic coverage는
      `std::fstream` in-place update, append-mode write, exception-mask open
      failure, EOF/fail/clear state transition을 확인합니다.
  [(cppreference examples)](../test/cmake/driver/src/cpp/stl/streams.cpp)
- [x] [`std::spanstream`](https://en.cppreference.com/w/cpp/io/basic_spanstream)
      / [`basic_spanstream::span`](https://en.cppreference.com/w/cpp/io/basic_spanstream/span)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/streams.cpp)
- [x] [std::filesystem::path lexical operation](https://en.cppreference.com/w/cpp/filesystem/path/lexically_normal)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] `std::filesystem::path` decomposition, modifier, observer, iterator,
      compare, hashing
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
      및 `assign`, `replace_filename`, `refresh`
  [(tested)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
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
- [x] `std::filesystem` semantic edge check
  - Error-code overload, throwing overload parity, missing/existing/directory
    negative path, file 변경 후 metadata refresh, recursive traversal pruning은
    driver semantic test로 검증합니다.
  [(driver semantic test)](../test/cmake/driver/src/cpp/stl/filesystem.cpp)
- [x] CRT stdio / lowio file I/O semantic check
  - `fopen` / `fread` / `fwrite` / `fseek` / `ftell` 및 `_open` / `_read` /
    `_write` / `_lseek` / `_close`, `remove` 경로를 success,
    missing-file, invalid-descriptor, read-only descriptor, `errno` /
    `_doserrno` propagation 기준으로 검증합니다. `setvbuf`,
    `fgetpos` / `fsetpos`, `ungetc`, `tmpfile`, `tmpnam_s`, `_tempnam`,
    `freopen`, append/update stdio mode, EOF/error/`clearerr` state transition,
    wide stdio(`_wfopen`, `fputwc`, `fputws`, `fgetwc`, `fgetws`)도 driver
    semantic test에서 실행합니다.
  [(driver semantic test)](../test/cmake/driver/src/cpp/stl/cstdio.cpp)
- [x] CRT file/process-state semantic check
  - `_stat` / `_stat64` / `_wstat64`, `_fstat` / `_fstat64`,
    `_access` / `_waccess`,
    `_fullpath` / `_wfullpath`, `_getcwd` / `_wgetcwd`, `_chdir` / `_wchdir`,
    `_findfirst` / `_findnext`, `_findfirst64` / `_findnext64`,
    `_dup` / `_dup2`, `_tell`, `_telli64`, `_filelength`, `_filelengthi64`,
    `_lseeki64`, `_commit`, `_chsize`, `_chsize_s`, `_eof`, `_locking`,
    `_setmode`, text/binary newline translation, `_get_osfhandle`, `_umask`
    경로를 LDK가 제공하는
    current-directory, file-handle, enumeration, metadata substrate 기준으로
    검증합니다. `_O_EXCL`,
    `_O_APPEND`, invalid `_setmode`, invalid-descriptor,
    missing-glob failure path도 확인합니다.
    CRT current-directory state는 `std::filesystem::current_path`와 교차 검증합니다.
    `GetModuleHandleA/W`, `GetModuleFileNameA/W`와 `_get_pgmptr` /
    `_get_wpgmptr`는 CRT program-path 및 module-handle state를 검증합니다.
  [(driver semantic test)](../test/cmake/driver/src/cpp/stl/cstdio_file_state.cpp)
- [x] CRT environment semantic check
  - `getenv` / `getenv_s` / `_dupenv_s` 및 wide `_wgetenv` / `_wgetenv_s` /
    `_wdupenv_s` 경로를 CRT-managed environment variable 기준으로
    검증합니다. `_putenv_s` / `_wputenv_s`의 add, update, delete 경로와
    `GetEnvironmentVariableA/W`를 통한 Win32 environment visibility도
    함께 검증합니다.
  [(driver semantic test)](../test/cmake/driver/src/cpp/stl/cstdlib.cpp)
- [x] CRT runtime-state semantic check
  - `_get_fmode` / `_set_fmode`, `_query_new_mode` / `_set_new_mode`,
    `_get_errno` / `_set_errno`, `_get_doserrno` / `_set_doserrno`를 CRT
    process/runtime state 기준으로 검증합니다. `_get_errno`,
    `_get_doserrno`, `strerror_s`의 invalid-parameter handler contract도
    확인합니다. 테스트가 끝나기 전에 원래 상태를 복구하여 뒤의 driver
    test가 startup default를 보도록 합니다.
  [(driver semantic test)](../test/cmake/driver/src/cpp/stl/cstdlib.cpp)
- [x] Error and diagnostics semantic check
  - `GetLastError`, `FormatMessageA/W`, `std::system_category`,
    `std::generic_category`, `std::system_error`, `errno`, `_get_errno`,
    `_get_doserrno`, default error-condition mapping,
    `FormatMessageA/W` failure-edge, invalid-parameter handler 경로를 Win32
    및 CRT failure case 기준으로 검증합니다. `FORMAT_MESSAGE_ALLOCATE_BUFFER`,
    `LocalFree`, `std::filesystem_error` message quality도 확인합니다.
  [(driver semantic test)](../test/cmake/driver/src/cpp/stl/diagnostics.cpp)
- [x] [std::string](https://en.cppreference.com/w/cpp/string/basic_string)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/string.cpp)
- [x] String member operation:
      [`std::string::find`](https://en.cppreference.com/w/cpp/string/basic_string/find),
      [`std::string::substr`](https://en.cppreference.com/w/cpp/string/basic_string/substr),
      [`std::string::starts_with`](https://en.cppreference.com/w/cpp/string/basic_string/starts_with),
      [`std::string::ends_with`](https://en.cppreference.com/w/cpp/string/basic_string/ends_with),
      [`std::string::contains`](https://en.cppreference.com/w/cpp/string/basic_string/contains), and
      [`std::string::erase`](https://en.cppreference.com/w/cpp/string/basic_string/erase)
  [(cppreference member examples)](../test/cmake/driver/src/cpp/stl/string.cpp)
- [x] [std::string_view](https://en.cppreference.com/w/cpp/string/basic_string_view)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/string.cpp)
- [x] String view member operation:
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
- [x] Container member operation:
      [`std::vector::emplace`](https://en.cppreference.com/w/cpp/container/vector/emplace),
      [`std::vector::erase`](https://en.cppreference.com/w/cpp/container/vector/erase),
      [`std::map::insert_or_assign`](https://en.cppreference.com/w/cpp/container/map/insert_or_assign),
      [`std::map::try_emplace`](https://en.cppreference.com/w/cpp/container/map/try_emplace),
      [`contains`](https://en.cppreference.com/w/cpp/container/map/contains),
      [`extract`](https://en.cppreference.com/w/cpp/container/map/extract), and
      [`merge`](https://en.cppreference.com/w/cpp/container/map/merge)
      ordered/unordered associative container 경로
  [(cppreference member examples)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] Sequence container member operation:
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
  - `rand_s`와 `std::random_device` repeated-call sanity check로 LDK
    `SystemFunction036` 기반 UCRT random provider 경로를 검증합니다.
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
- [x] Threading/future semantic edge check
  - timed shared-lock timeout/reacquire 동작,
    `std::condition_variable` / `std::condition_variable_any` timeout 및
    predicate wake 동작, `std::future` / `std::shared_future` timeout/ready
    상태, deferred async 상태, `set_exception`, `set_value_at_thread_exit`,
    `broken_promise`, `promise_already_satisfied`, latch/barrier/semaphore
    state transition을 driver semantic test로 검증합니다.
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
- [x] `nlohmann::json` 연동
  [(tested)](../test/cmake/driver/src/libs/nlohmann_json.cpp)

## NTL

- [x] `ntl::seh::try_except`
  [(tested)](../test/cmake/driver/src/ntl.cpp)
  - 내부적으로 MSVC `__try` / `__except`를 사용하고, 잡은 exception code를
    반환합니다.

## 테스트 백로그

아래 미체크 항목은 앞으로 cppreference Example 이식 또는 런타임 지원 작업
후보입니다. 명시적으로 limitation이라고 적지 않은 항목은 실패가 확인된
목록이 아닙니다. 기본 원칙은 `PASSIVE_LEVEL`에서 파일 시스템, locale,
콘솔 입력, 미지원 C++ 런타임 의존성 없이 실행할 수 있는 예제를
우선합니다.

### OS substrate coverage 우선순위

앞으로의 coverage는 MSVC CRT/STL이 hosted Windows process에서 기대하는
Win32 / NTDLL / UCRT / ICU 표면을 실제로 밟는 테스트를 우선합니다. 순수
header algorithm이나 value utility도 의미는 있지만, LDK가 받쳐주는
runtime substrate를 증명하는 테스트보다 우선순위는 낮습니다.

| 우선순위 | 영역 | 우선 볼 항목 | 의미 |
| --- | --- | --- | --- |
| P0 | `std::filesystem` 및 CRT file I/O | `copy_options`, directory traversal option, symlink/hard-link edge case, metadata/status transition, `error_code` overload, stdio/lowio file handle | path normalization, file handle, directory enumeration, reparse point, file metadata, current/temp directory 상태, Win32 error mapping을 검증합니다. |
| P0 | Time 및 chrono OS path | `system_clock`, `file_clock`, file timestamp round trip, timezone lookup, invalid-zone/error path, tzdb/ICU를 밟는 formatting path | system time, file time, registry/timezone data, ICU/tzdb shim, MSVC STL chrono ABI helper를 검증합니다. |
| P0 | Locale, NLS, text conversion | named/user locale, `GetLocaleInfo` 기반 facet, `ctype`/`collate`, UTF-8/multibyte conversion, `time_get`/`time_put`, `money_get`/`money_put` | NLS table, code page, locale data, ICU 기반 동작, UCRT conversion helper를 검증합니다. |
| P1 | Threading, wait, async | wait/notify timeout/error path, condition-variable wake ordering, future/promise broken-promise 및 thread-exit path, latch/barrier/semaphore semantics | LDK `WaitOnAddress`, keyed event, SRW/condition-variable 동작, thread handle, unload-sensitive lifetime rule을 검증합니다. |
| P1 | Error 및 diagnostics path | `std::system_error`, `std::error_code`, `FormatMessageA/W`, `GetLastError`/`errno` propagation, filesystem exception message | NTSTATUS/Win32 error mapping과 message-resource lookup 품질을 검증합니다. |
| P2 | Additional process/runtime state | startup argument edge case, invalid-parameter policy, `atexit`/`onexit` state, 위에서 아직 검증하지 않은 CRT startup/shutdown state | 새 MSVC CRT path가 아직 검증하지 않은 process-global runtime state에 의존하기 시작할 때 유용합니다. |
| P2 | Loader/resource integration | resource lookup, DLL/module discovery path, ICU 및 message-resource provider 동작 | CRT/STL path가 hosted loader/resource semantics를 기대하는 경우에 유용합니다. |
| P2 | Console/debug output | stream/stdout/stderr failure path, `std::print`, `OutputDebugString`, output throttling behavior | console/debug-output policy를 검증합니다. production hot path와는 분리해서 봅니다. |

### 앞으로 보강할 cppreference coverage 후보

현재 이 문서에 큐로 남겨둔 구체적인 cppreference standalone Example 후보는
없습니다. 새로 이식할 driver-safe 예제가 정해지면 이곳에 링크 단위로
추가합니다.

### 추가 조사가 필요한 후보

- [ ] [thread_local](https://en.cppreference.com/w/cpp/language/storage_duration#Thread_storage_duration)
  - 일반 C++ `thread_local`은 기본 driver build에서 true per-thread TLS로
    지원하지 않습니다. cppreference storage-duration 예제는 비활성화되어
    있습니다. 위의 multi-driver compiler TLS slot isolation은 MSVC runtime
    path를 위한 driver-image slot 분리이지, user variable에 대한 per-thread
    storage semantics가 아닙니다. 즉, 제공하는 것은 여러 crtsys-linked driver가
    같은 compiler TLS slot index를 공유해서 서로의 runtime state를 덮어쓰지
    않게 하는 routing입니다. `thread_local int x`를 thread-local로 만들어주는
    기능은 아닙니다. kernel mode에서 GS 기반 TLS 모델은 각 thread의
    user-mode TEB가 아니라 processor-local KPCR에 묶입니다.
- [ ] [`std::inplace_vector`](https://en.cppreference.com/w/cpp/container/inplace_vector)
  - 현재 driver-test matrix에서 사용하는 active MSVC STL toolset은 아직
    이 header를 노출하지 않으므로 driver test를 컴파일하지 않습니다.
- [ ] [`std::hive`](https://en.cppreference.com/w/cpp/container/hive)
  - 현재 driver-test matrix에서 사용하는 active MSVC STL toolset은 아직
    이 header를 노출하지 않으므로 driver test를 컴파일하지 않습니다.

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
