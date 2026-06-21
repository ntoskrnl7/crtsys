# 기능 지원 현황

[한국어 문서로 돌아가기](./ko-kr.md)

이 문서는 기존 README의 `Goal` 섹션이 갖고 있던 상세 지원 현황을 보존하기
위한 문서입니다. 체크된 항목은 구현된 항목이며, `tested` 링크가 있는 항목은
테스트 코드 위치를 함께 표시합니다.

여기서 coverage는 해당 기능이 구현되었고 테스트 suite에서 실행된다는 뜻입니다.
모든 driver execution context에서 유효하다는 의미는 아닙니다. API가 더 넓은
계약을 문서화하지 않았다면 기본값은 `PASSIVE_LEVEL`로 보세요.
실행 문맥은 [설계 근거와 운영 경계](./ko-kr-design-rationale.md)와
[NTL API 문서](./ko-kr-ntl-api.md)를 참고하세요.

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
- [ ] [Static local variables](https://en.cppreference.com/w/cpp/language/storage_duration#Static_local_variables)
  - [ ] `thread_local`
    [(disabled cppreference example)](../test/cmake/driver/src/cpp/lang/initialization.cpp#L79)
  - [x] function-local `static`
    [(tested)](../test/cmake/driver/src/cpp/lang/initialization.cpp#L124)
    [(regression)](../test/cmake/driver/src/cpp/lang/initialization.cpp#L229)

### Exceptions

- [x] [throw](https://en.cppreference.com/w/cpp/language/throw)
  [(tested)](../test/cmake/driver/src/cpp/lang/exceptions.cpp#L42)
- [x] [try block](https://en.cppreference.com/w/cpp/language/try_catch)
  [(tested)](../test/cmake/driver/src/cpp/lang/exceptions.cpp#L60)
- [x] [Function try block](https://en.cppreference.com/w/cpp/language/function-try-block)
  [(tested)](../test/cmake/driver/src/cpp/lang/exceptions.cpp#L98)
- [x] [std::exception_ptr](https://en.cppreference.com/w/cpp/error/exception_ptr)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/exception.cpp)

## Microsoft STL

- [x] [std::accumulate](https://en.cppreference.com/w/cpp/algorithm/accumulate)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::adjacent_difference](https://en.cppreference.com/w/cpp/algorithm/adjacent_difference)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::array](https://en.cppreference.com/w/cpp/container/array)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/atomic.cpp)
- [x] [std::atomic_flag](https://en.cppreference.com/w/cpp/atomic/atomic_flag)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/atomic.cpp)
- [x] [std::chrono](https://en.cppreference.com/w/cpp/chrono)
  [(tested)](../test/cmake/driver/src/cpp/stl/chrono.cpp#L15)
- [x] [std::any](https://en.cppreference.com/w/cpp/utility/any)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::bind](https://en.cppreference.com/w/cpp/utility/functional/bind)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/functional.cpp)
- [x] [std::binary_search](https://en.cppreference.com/w/cpp/algorithm/binary_search)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::bitset](https://en.cppreference.com/w/cpp/utility/bitset)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [`<bit>` 유틸리티](https://en.cppreference.com/w/cpp/utility/bit)
  [(cppreference popcount example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::concepts](https://en.cppreference.com/w/cpp/concepts)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::complex](https://en.cppreference.com/w/cpp/numeric/complex)
  [(cppreference arithmetic example)](../test/cmake/driver/src/cpp/stl/numeric.cpp)
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
  [(cppreference integer example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
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
- [x] [std::lcm](https://en.cppreference.com/w/cpp/numeric/lcm)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::list](https://en.cppreference.com/w/cpp/container/list)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
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
- [x] [std::multimap::equal_range](https://en.cppreference.com/w/cpp/container/multimap/equal_range)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::multiset::erase](https://en.cppreference.com/w/cpp/container/multiset/erase)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::next_permutation](https://en.cppreference.com/w/cpp/algorithm/next_permutation)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
- [x] [std::optional](https://en.cppreference.com/w/cpp/utility/optional)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::pair](https://en.cppreference.com/w/cpp/utility/pair)
  [(cppreference make_pair example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
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
- [x] [std::span](https://en.cppreference.com/w/cpp/container/span)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::stack](https://en.cppreference.com/w/cpp/container/stack)
  [(cppreference push/emplace examples)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::source_location](https://en.cppreference.com/w/cpp/utility/source_location)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::strong_ordering](https://en.cppreference.com/w/cpp/utility/compare/strong_ordering)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::string](https://en.cppreference.com/w/cpp/string/basic_string)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/string.cpp)
- [x] [std::string_view](https://en.cppreference.com/w/cpp/string/basic_string_view)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/string.cpp)
- [x] [std::weak_ptr](https://en.cppreference.com/w/cpp/memory/weak_ptr)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/memory.cpp)
- [x] [std::thread](https://en.cppreference.com/w/cpp/thread)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L35)
- [x] [std::tuple](https://en.cppreference.com/w/cpp/utility/tuple)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::to_chars](https://en.cppreference.com/w/cpp/utility/to_chars)
  [(cppreference integer example)](../test/cmake/driver/src/cpp/stl/algorithm.cpp)
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
- [x] [std::vector](https://en.cppreference.com/w/cpp/container/vector)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/containers.cpp)
- [x] [std::numbers](https://en.cppreference.com/w/cpp/numeric/constants)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/utility.cpp)
- [x] [std::uniform_int_distribution](https://en.cppreference.com/w/cpp/numeric/random/uniform_int_distribution)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/numeric.cpp)
- [x] [std::valarray::slice](https://en.cppreference.com/w/cpp/numeric/valarray/slice)
  [(cppreference example)](../test/cmake/driver/src/cpp/stl/numeric.cpp)
- [x] [std::condition_variable](https://en.cppreference.com/w/cpp/thread/condition_variable)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L35)
- [x] [std::mutex](https://en.cppreference.com/w/cpp/thread/mutex)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L81)
- [x] [std::shared_mutex](https://en.cppreference.com/w/cpp/thread/shared_mutex)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L129)
- [x] [std::call_once](https://en.cppreference.com/w/cpp/thread/call_once)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L180)
- [x] [std::future](https://en.cppreference.com/w/cpp/thread/future)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L208)
- [x] [std::promise](https://en.cppreference.com/w/cpp/thread/promise)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L254)
- [x] [std::packaged_task](https://en.cppreference.com/w/cpp/thread/packaged_task)
  [(tested)](../test/cmake/driver/src/cpp/stl/thread.cpp#L318)
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

## 테스트 백로그

아래 미체크 항목은 앞으로 cppreference Example 이식 또는 런타임 지원 작업
후보입니다. 기본 원칙은 `PASSIVE_LEVEL`에서 파일 시스템, locale, 콘솔 입력,
미지원 C++ 런타임 의존성 없이 실행할 수 있는 예제를 우선합니다.

### 남은 후속 후보

- [ ] 활성 toolset 지원 검토 후 추가
      [`std::ranges`](https://en.cppreference.com/w/cpp/ranges) C++23
      view/adaptor 예제

### 추가 조사가 필요한 후보

- [ ] [std::jthread](https://en.cppreference.com/w/cpp/thread/jthread)
  - 현재 driver test에서는 MSVC STL atomic wait/notify ABI helper가 없어
    막혀 있습니다.
- [ ] [std::atomic::wait/notify](https://en.cppreference.com/w/cpp/atomic/atomic/wait)
  - MSVC STL atomic wait/notify ABI helper의 커널 호환 구현이 필요합니다.
- [ ] [std::expected](https://en.cppreference.com/w/cpp/utility/expected)
  - 조건부 cppreference harness는 들어가 있지만, 현재 MSVC 14.32 toolset은
    `<expected>`를 제공하지 않습니다.
- [ ] [std::pmr](https://en.cppreference.com/w/cpp/memory/memory_resource)
  - cppreference `monotonic_buffer_resource` harness는
    `CRTSYS_ENABLE_UNSUPPORTED_PMR_TEST` 뒤에 준비되어 있습니다. 기본 driver
    build는 `_Aligned_get_default_resource` 같은 MSVC PMR resource ABI helper가
    없어 막혀 있습니다.
- [ ] [std::exp](https://en.cppreference.com/w/cpp/numeric/complex/exp),
      [std::pow](https://en.cppreference.com/w/cpp/numeric/complex/pow) 같은
      `std::complex` 초월 함수
  - `_Exp`, `ilogb`, `scalbn`, `copysign` 같은 MSVC math helper 심볼이
    아직 없습니다.
- [ ] [std::lerp](https://en.cppreference.com/w/cpp/numeric/lerp)
  - cppreference harness는 `CRTSYS_ENABLE_UNSUPPORTED_LERP_TEST` 뒤에
    준비되어 있습니다. MSVC의 `std::lerp(float)` 구현은 driver build에서
    아직 없는 `fmaf` helper를 끌어올 수 있습니다.
- [ ] [std::random_device](https://en.cppreference.com/w/cpp/numeric/random/random_device)
  - hosted entropy API 검토가 필요할 가능성이 큽니다. 현재 테스트는
    고정 seed를 사용합니다.
- [ ] [std::latch](https://en.cppreference.com/w/cpp/thread/latch),
      [std::barrier](https://en.cppreference.com/w/cpp/thread/barrier),
      [std::counting_semaphore](https://en.cppreference.com/w/cpp/thread/counting_semaphore)
  - 같은 atomic wait/notify ABI helper 검토가 필요할 가능성이 큽니다.
- [ ] [thread_local](https://en.cppreference.com/w/cpp/language/storage_duration#Thread_storage_duration)
  - compiler TLS 호환은 아직 부분적이며, cppreference storage-duration 예제는
    기본 빌드에서 비활성화되어 있습니다.
- [ ] [RTTI: typeid](https://en.cppreference.com/w/cpp/language/typeid)
  및 [dynamic_cast](https://en.cppreference.com/w/cpp/language/dynamic_cast)
  - driver test에서 RTTI를 켤지 먼저 결정해야 합니다.
- [ ] [std::filesystem](https://en.cppreference.com/w/cpp/filesystem)
  - 파일 시스템 및 hosted ABI 검토가 필요할 가능성이 큽니다.
- [ ] [std::regex](https://en.cppreference.com/w/cpp/regex)
  - allocation, locale, exception path 검토가 필요할 가능성이 큽니다.
- [ ] [std::format](https://en.cppreference.com/w/cpp/utility/format)
  - MSVC STL 지원 여부와 커널에 맞는 의존성 검토가 필요합니다.

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
