# NTL 풀 할당자

[NTL 문서로 돌아가기](./README.ko-KR.md)

헤더: [`include/ntl/pool_allocator`](../../include/ntl/pool_allocator)

`ntl::pool_allocator`를 사용하면 드라이버 코드가 STL 컨테이너 또는 PMR 리소스를
지원하는 커널 풀을 선택할 수 있습니다. `ntl::pool_ptr`와 `ntl::pool_buffer`는 원시
객체 및 바이트 버퍼 소유권에도 같은 풀/태그 모델을 제공합니다. 기본 `crtsys` heap이
너무 암묵적이어서 코드에서 “이 데이터는 비페이지 풀에 있다” 또는 “이 제어 경로
컨테이너는 페이지 풀을 사용해도 된다”라고 명시해야 할 때 유용합니다.

API는 의도적으로 WDK 개념을 눈에 띄게 유지합니다.

- `ntl::pool_kind`는 풀 카테고리를 선택합니다.
- `ntl::pool_option`은 WDK 스타일 수정자를 선택합니다.
- 진단에 관심이 있는 경우 풀 태그는 항상 명시적입니다.

## 빠른 예

원시 비페이징 할당:

```cpp
void* buffer = ntl::allocate_pool(128,
                                  ntl::pool_kind::nonpaged,
                                  ntl::pool_option::none,
                                  "BUF1");
if (!buffer) {
  return STATUS_INSUFFICIENT_RESOURCES;
}

ntl::free_pool(buffer, ntl::pool_kind::nonpaged, "BUF1");
```

`NTSTATUS` 전파를 사용한 원시 비페이징 할당:

```cpp
auto buffer = ntl::try_allocate_pool(128,
                                     ntl::pool_kind::nonpaged,
                                     ntl::pool_option::none,
                                     "BUF2");
if (!buffer) {
  return buffer.status();
}

ntl::free_pool(buffer.value(), ntl::pool_kind::nonpaged, "BUF2");
```

RAII 정리를 통한 원시 비페이징 할당:

```cpp
auto buffer = ntl::make_pool_buffer(128,
                                    ntl::pool_kind::nonpaged,
                                    ntl::pool_option::none,
                                    "BUF3");
if (!buffer) {
  return STATUS_INSUFFICIENT_RESOURCES;
}
```

`NTSTATUS` 전파를 통한 RAII 정리:

```cpp
auto buffer = ntl::try_make_pool_buffer(128,
                                        ntl::pool_kind::nonpaged,
                                        ntl::pool_option::none,
                                        "BUF4");
if (!buffer) {
  return buffer.status();
}
```

생성 및 소멸을 통한 객체 할당:

```cpp
struct packet_state {
  explicit packet_state(int id) : id(id) {}
  int id;
};

auto state = ntl::make_pool<packet_state>("PKTs", 42);
state->id += 1;
```

할당 실패를 `std::bad_alloc`로 변환하지 않고 개체 할당:

```cpp
auto state = ntl::try_make_pool<packet_state>("PKTr", 42);
if (!state) {
  return state.status();
}

(*state)->id += 1;
```

비페이징 STL 벡터:

```cpp
using packet_vector =
    std::vector<packet,
                ntl::nonpaged_pool_allocator<packet, ntl::pool_tag("PKTv")>>;

packet_vector packets;
packets.reserve(32);
```

PASSIVE_LEVEL 제어 경로에 대한 페이징된 STL 벡터:

```cpp
using name_vector =
    std::vector<std::wstring,
                ntl::paged_pool_allocator<std::wstring, ntl::pool_tag("NAMv")>>;

name_vector names;
names.emplace_back(L"device-0");
```

WDK 스타일의 역방향 다중 문자 태그를 사용하는 캐시 정렬 비페이지 벡터:

```cpp
using cache_aligned_ints =
    std::vector<int,
                ntl::pool_allocator<int,
                                    ntl::pool_kind::nonpaged,
                                    ntl::pool_option::cache_aligned,
                                    'cLTN'>>;
```

PMR 리소스:

```cpp
ntl::pmr::pool_resource paged_resource{ntl::pool_kind::paged,
                                       ntl::pool_option::none,
                                       "PMRp"};

std::pmr::vector<int> values{&paged_resource};
values.assign({1, 2, 3});
```

## 풀 종류

| 종류 | 의미 | 일반적인 용도 |
| --- | --- | --- |
| `ntl::pool_kind::nonpaged` | 비페이징 NX 풀 | 페이징이 불법인 곳에서 접촉될 수 있는 데이터 |
| `ntl::pool_kind::paged` | 페이징 풀 | PASSIVE_LEVEL 제어 경로 데이터 |
| `ntl::pool_kind::nonpaged_execute` | 실행 가능한 비페이징 풀 | 실제로 실행 가능 풀이 필요한 드문 호환성 사례 |

`nonpaged` 또는 `paged`를 우선하세요. `nonpaged_execute`는 기본값이 아니라 명시적인
WDK 수준 선택으로 취급하세요.

## 풀 옵션

| 옵션 | 의미 |
| --- | --- |
| `ntl::pool_option::none` | 추가 수정자 없음 |
| `ntl::pool_option::use_quota` | 대상 WDK API가 지원할 때 quota를 부과 |
| `ntl::pool_option::uninitialized` | `ExAllocatePool2` 대상에서 초기화되지 않은 메모리 요청 |
| `ntl::pool_option::session` | `ExAllocatePool2` 대상에 대한 세션 풀 요청 |
| `ntl::pool_option::cache_aligned` | 캐시 정렬 할당 요청 |
| `ntl::pool_option::raise_on_failure` | 실패 시 WDK 할당 API에 예외 발생 요청 |
| `ntl::pool_option::special_pool` | `ExAllocatePool2` 대상에 대한 특별 풀 요청 |

옵션은 비트 플래그입니다.

```cpp
auto options = ntl::pool_option::cache_aligned |
               ntl::pool_option::raise_on_failure;
```

`ExAllocatePoolWithTag`를 사용하는 이전 대상은 최신 `POOL_FLAGS` 옵션을 모두
표현할 수 없습니다. 지원하지 않는 조합은 해당 옵션이 존중된 것처럼 조용히 가장하는
대신 할당에 실패합니다.

## 풀 태그

원시 도우미 및 PMR 리소스의 경우 원하는 순서대로 문자열 리터럴을 선호합니다.
코드로 읽으려면 :

```cpp
auto* p = ntl::allocate_pool(64, "BUF2");
ntl::free_pool(p, "BUF2");

auto r = ntl::try_allocate_pool(64, "BUF3");
if (!r) {
  return r.status();
}
ntl::free_pool(r.value(), "BUF3");
```

짧은 `free_pool(pointer, tag)` 형식은 비페이징 풀을 나타냅니다. 때
할당 종류가 동적으로 선택되면 동일한 종류를
`free_pool(pointer, kind, tag)`이므로 NTL은 올바른 릴리스 IRQL을 확인할 수 있습니다.

할당자 템플릿 인수의 경우 C++14 호환 코드는 `"BUF2"`를 다음과 같이 전달할 수 없습니다.
비유형 템플릿 매개변수. `ntl::pool_tag("BUF2")` 또는 기존 방식을 사용하십시오.
WDK는 다중 문자 리터럴을 역전시켰습니다.

```cpp
using a = ntl::nonpaged_pool_allocator<int, ntl::pool_tag("INTv")>;
using b = ntl::pool_allocator<int,
                              ntl::pool_kind::nonpaged,
                              ntl::pool_option::none,
                              'vTNI'>;
```

두 형식 모두 동일한 종류의 원시 `ULONG` 태그 값을 생성합니다. 문자열 형식은 다음과 같습니다.
일반적으로 읽기가 더 쉽습니다. 다중 문자 형식은 WDK 코드에 익숙합니다.
이미 역방향 태그를 사용하고 있습니다.

## IRQL 지침

원시 풀 할당은 기본 WDK 기본 요소를 따릅니다.

- 비페이지 풀의 할당/해제는 `DISPATCH_LEVEL`까지 유효합니다.
- 페이지 풀의 할당/해제는 `APC_LEVEL`까지 유효합니다.
- `ntl::maximum_pool_irql()`, `ntl::is_pool_irql_valid()` 및
  `ntl::require_pool_irql()`는 해당 기본 풀 종류 계약을 노출합니다.

STL 및 PMR 사용은 더 제한적입니다. 컨테이너 작업은 다음을 할당할 수 있습니다.
요소를 파괴하고, 값을 비교하고, 객체를 이동하고, 사용자 코드를 던지거나 호출합니다.
따라서 NTL은 `pool_allocator` 및 `pmr::pool_resource` 작업을 다음과 같이 문서화합니다.
`PASSIVE_LEVEL`. `ntl::is_pool_allocator_irql_valid()` 및
`ntl::require_pool_allocator_irql()`는 해당 계약을 노출하고 디버그 빌드
할당자 또는 PMR 할당 후크가 위에 도달하면 경고를 인쇄합니다.
`PASSIVE_LEVEL`.

해당 진단은 의도적으로 모든 불법 사용이 발생할 수 있다는 주장이 아닙니다.
감지되었습니다. 벡터에 이미 여유 용량이 있는 경우 `push_back()`는 해당 벡터를 호출하지 않을 수 있습니다.
전혀 할당자. 여전히 페이징된 저장소를 건드릴 수 있고, 구성하거나 이동할 수 있습니다.
요소, 사용자 코드 호출 또는 던지기. 비페이징 할당자는 다음 위치만 제어합니다.
컨테이너의 할당 수명; 임의의 STL 객체를 만들지 않거나
`DISPATCH_LEVEL`에서 안전하게 작동합니다.

다음 패턴을 피하세요.

```cpp
// Bad: vector growth can allocate and move elements while the spin lock is held.
std::unique_lock lock(spin_lock);
nonpaged_vector.push_back(value);

// Bad: paged allocation is not DPC-safe.
auto* p = ntl::allocate_pool(64, ntl::pool_kind::paged, "BADp");
```

좋은 패턴:

```cpp
// Allocate and prepare at PASSIVE_LEVEL.
packet_vector packets;
packets.reserve(64);

// Later, use preallocated resident storage in a separately audited hot path.
```

이후 작업은 컨테이너 저장소와 모든 개체가 있는 경우에만 유효합니다.
접촉은 상주하며 정확한 작업은 할당을 수행하지 않습니다.
할당 취소, 대기, 예외 또는 감사되지 않은 콜백. 컨테이너 생성 위치
하나의 IRQL은 다른 IRQL에서 이를 변경할 수 있는 권한을 부여하지 않습니다.

## API 요약

-`ntl::pool_tag("TAGx")`
-`ntl::pool_tag(a, b, c, d)`
-`ntl::allocate_pool(bytes, kind, options, tag)`
-`ntl::allocate_pool(bytes, kind, options, "TAGx")`
-`ntl::allocate_pool(bytes, kind, "TAGx")`
-`ntl::allocate_pool(bytes, "TAGx")`
-`ntl::try_allocate_pool(bytes, kind, options, tag)`
-`ntl::try_allocate_pool(bytes, kind, options, "TAGx")`
-`ntl::try_allocate_pool(bytes, kind, "TAGx")`
-`ntl::try_allocate_pool(bytes, "TAGx")`
-`ntl::maximum_pool_irql(kind)`
-`ntl::is_pool_irql_valid(kind)`
-`ntl::require_pool_irql(kind)`
-`ntl::is_pool_allocator_irql_valid()`
-`ntl::require_pool_allocator_irql()`
-`ntl::free_pool(pointer, tag)`
-`ntl::free_pool(pointer, "TAGx")`
-`ntl::free_pool(pointer, kind, tag)`
-`ntl::free_pool(pointer, kind, "TAGx")`
-`ntl::pool_deleter<T>`
-`ntl::pool_ptr<T>`
-`ntl::pool_buffer`
-`ntl::make_pool<T>(kind, options, tag, args...)`
-`ntl::make_pool<T>("TAGx", args...)`
-`ntl::try_make_pool<T>(kind, options, tag, args...)`
-`ntl::try_make_pool<T>("TAGx", args...)`
-`ntl::make_pool_buffer(bytes, kind, options, tag)`
-`ntl::make_pool_buffer(bytes, "TAGx")`
-`ntl::try_make_pool_buffer(bytes, kind, options, tag)`
-`ntl::try_make_pool_buffer(bytes, "TAGx")`
-`ntl::pool_allocator<T, Kind, Options, Tag>`
-`ntl::nonpaged_pool_allocator<T, Tag>`
-`ntl::paged_pool_allocator<T, Tag>`
-`ntl::pmr::pool_resource`
-`ntl::pmr::nonpaged_pool_resource()`
-`ntl::pmr::paged_pool_resource()`

## 구현 참고 사항

`ExAllocatePool2`가 다음의 일부인 `NTDDI_VERSION`를 선택하는 대상
커널 계약은 `ExAllocatePool2` / `ExFreePool2`를 사용합니다.

하위 레벨 대상은 `ExAllocatePoolWithTag` / `ExFreePoolWithTag`를 사용합니다.
레거시 API가 나타낼 수 있는 옵션입니다. 이는 이전 커널 로드를 보존합니다.
새로운 대상이 더 풍부한 풀 플래그를 노출하도록 허용하면서 호환성을 유지합니다.

드라이버 테스트 스위트는 다음을 실행합니다.

- 문자열 태그가 있는 원시 비페이징 할당/무료
- `ntl::result<void*>`를 사용한 원시 비페이징 할당/무료
- WDK 스타일 다중 문자 태그를 사용한 원시 비페이징 할당/무료
- `ntl::pool_buffer` 해제/재설정 정리
- `ntl::result`를 사용한 `ntl::try_make_pool_buffer` 할당
- `ntl::pool_ptr<T>` 구성, 이동, 해제, 재설정 및 파괴
- `ntl::result`를 사용한 `ntl::try_make_pool<T>` 할당
- 비페이징 및 페이징 풀 할당자를 사용하는 `std::vector`
- `ntl::pool_tag("TAGx")` 및 `'xGAT'`를 모두 통한 템플릿 태그
- `std::pmr::vector` 및 `ntl::pmr::pool_resource`
- 제기된 `DISPATCH_LEVEL`의 기본 페이징/비페이징 IRQL 정책
- IRQL이 높아졌을 때 명시적인 PASSIVE_LEVEL 할당자 정책 상태
