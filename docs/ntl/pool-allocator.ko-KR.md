# NTL 풀 할당자

[NTL 문서로 돌아가기](./README.ko-KR.md)

헤더: [`include/ntl/pool_allocator`](../../include/ntl/pool_allocator)

`ntl::pool_allocator`를 사용하면 드라이버 코드에서 STL 컨테이너나 PMR 리소스의
백업 저장소로 쓸 커널 풀을 선택할 수 있습니다. `ntl::pool_ptr`와
`ntl::pool_buffer`는 원시 객체와 바이트 버퍼의 소유권에도 같은 풀/태그 모델을
제공합니다. 기본 `crtsys` 힙의 사용이 지나치게 암묵적이어서 “이 데이터는
nonpaged pool에 있다” 또는 “이 제어 경로 컨테이너는 paged pool을 사용해도 된다”를
코드로 명시해야 할 때 유용합니다.

API는 WDK 개념을 의도적으로 그대로 드러냅니다.

- `ntl::pool_kind`는 풀 종류를 선택합니다.
- `ntl::pool_option`은 WDK 스타일 수정자를 선택합니다.
- 진단이 중요하다면 풀 태그를 항상 명시합니다.

## 빠른 예제

원시 nonpaged 할당:

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

`NTSTATUS`를 전파하는 원시 nonpaged 할당:

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

RAII로 정리하는 원시 nonpaged 할당:

```cpp
auto buffer = ntl::make_pool_buffer(128,
                                    ntl::pool_kind::nonpaged,
                                    ntl::pool_option::none,
                                    "BUF3");
if (!buffer) {
  return STATUS_INSUFFICIENT_RESOURCES;
}
```

`NTSTATUS`를 전파하는 RAII 정리:

```cpp
auto buffer = ntl::try_make_pool_buffer(128,
                                        ntl::pool_kind::nonpaged,
                                        ntl::pool_option::none,
                                        "BUF4");
if (!buffer) {
  return buffer.status();
}
```

생성과 소멸을 포함한 객체 할당:

```cpp
struct packet_state {
  explicit packet_state(int id) : id(id) {}
  int id;
};

auto state = ntl::make_pool<packet_state>("PKTs", 42);
state->id += 1;
```

할당 실패를 `std::bad_alloc`로 바꾸지 않는 객체 할당:

```cpp
auto state = ntl::try_make_pool<packet_state>("PKTr", 42);
if (!state) {
  return state.status();
}

(*state)->id += 1;
```

Nonpaged STL 벡터:

```cpp
using packet_vector =
    std::vector<packet,
                ntl::nonpaged_pool_allocator<packet, ntl::pool_tag("PKTv")>>;

packet_vector packets;
packets.reserve(32);
```

`PASSIVE_LEVEL` 제어 경로용 paged STL 벡터:

```cpp
using name_vector =
    std::vector<std::wstring,
                ntl::paged_pool_allocator<std::wstring, ntl::pool_tag("NAMv")>>;

name_vector names;
names.emplace_back(L"device-0");
```

WDK 스타일의 역순 다중 문자 태그를 사용하는 캐시 정렬 nonpaged 벡터:

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
| `ntl::pool_kind::nonpaged` | NX nonpaged pool | 페이징이 허용되지 않는 문맥에서 접근할 수 있는 데이터 |
| `ntl::pool_kind::paged` | Paged pool | `PASSIVE_LEVEL` 제어 경로 데이터 |
| `ntl::pool_kind::nonpaged_execute` | 실행 가능한 nonpaged pool | 실제로 실행 가능한 풀이 필요한 드문 호환성 사례 |

`nonpaged` 또는 `paged`를 우선 사용하세요. `nonpaged_execute`는 기본값이 아니라
명시적인 WDK 수준의 선택으로 다뤄야 합니다.

## 풀 옵션

| 옵션 | 의미 |
| --- | --- |
| `ntl::pool_option::none` | 추가 수정자 없음 |
| `ntl::pool_option::use_quota` | 대상 WDK API가 지원하면 할당량을 청구 |
| `ntl::pool_option::uninitialized` | `ExAllocatePool2` 대상에서 초기화되지 않은 메모리를 요청 |
| `ntl::pool_option::session` | `ExAllocatePool2` 대상에서 세션 풀을 요청 |
| `ntl::pool_option::cache_aligned` | 캐시 정렬 할당을 요청 |
| `ntl::pool_option::raise_on_failure` | 할당 실패 시 WDK 할당 API가 예외를 발생시키도록 요청 |
| `ntl::pool_option::special_pool` | `ExAllocatePool2` 대상에서 special pool을 요청 |

옵션은 비트 플래그입니다.

```cpp
auto options = ntl::pool_option::cache_aligned |
               ntl::pool_option::raise_on_failure;
```

`ExAllocatePoolWithTag`를 쓰는 하위 호환 대상에서는 최신 `POOL_FLAGS` 옵션을
모두 표현할 수 없습니다. 지원하지 않는 조합은 해당 옵션이 적용된 것처럼 조용히
처리하지 않고 할당을 실패시킵니다.

## 풀 태그

원시 도우미와 PMR 리소스에는 코드에서 읽고 싶은 순서의 문자열 리터럴을
사용하는 편이 좋습니다.

```cpp
auto* p = ntl::allocate_pool(64, "BUF2");
ntl::free_pool(p, "BUF2");

auto r = ntl::try_allocate_pool(64, "BUF3");
if (!r) {
  return r.status();
}
ntl::free_pool(r.value(), "BUF3");
```

짧은 `free_pool(pointer, tag)` 형식은 nonpaged pool을 뜻합니다. 할당 종류를
동적으로 골랐다면 `free_pool(pointer, kind, tag)`에 같은 종류를 전달하세요.
그래야 NTL이 해제 시의 올바른 IRQL을 검증할 수 있습니다.

할당자 템플릿 인수에서는 C++14 호환 코드가 `"BUF2"`를 non-type 템플릿 인수로
전달할 수 없습니다. `ntl::pool_tag("BUF2")` 또는 전통적인 WDK 역순 다중 문자
리터럴을 사용하세요.

```cpp
using a = ntl::nonpaged_pool_allocator<int, ntl::pool_tag("INTv")>;
using b = ntl::pool_allocator<int,
                              ntl::pool_kind::nonpaged,
                              ntl::pool_option::none,
                              'vTNI'>;
```

두 형식은 같은 종류의 원시 `ULONG` 태그 값을 만듭니다. 문자열 형식은 대체로
읽기 쉽고, 다중 문자 형식은 이미 역순 태그를 쓰는 WDK 코드에 익숙합니다.

## IRQL 지침

원시 풀 할당은 기본 WDK 프리미티브의 규칙을 따릅니다.

- Nonpaged pool의 할당과 해제는 `DISPATCH_LEVEL`까지 유효합니다.
- Paged pool의 할당과 해제는 `APC_LEVEL`까지 유효합니다.
- `ntl::maximum_pool_irql()`, `ntl::is_pool_irql_valid()`,
  `ntl::require_pool_irql()`은 이 네이티브 풀 종류 계약을 노출합니다.

STL과 PMR 사용에는 더 엄격한 제약이 있습니다. 컨테이너 작업은 할당, 요소 소멸,
값 비교, 객체 이동, 예외 발생, 사용자 코드 호출을 할 수 있습니다. 따라서 NTL은
`pool_allocator`와 `pmr::pool_resource` 작업을 `PASSIVE_LEVEL` 계약으로
문서화합니다. `ntl::is_pool_allocator_irql_valid()`와
`ntl::require_pool_allocator_irql()`은 이 계약을 노출하며, Debug 빌드에서는
할당자나 PMR 할당 후크가 `PASSIVE_LEVEL`보다 높은 IRQL에서 호출되면 경고를
출력합니다.

이 진단은 모든 부적절한 사용을 검출할 수 있다는 뜻이 아닙니다. 예를 들어 벡터에
여유 용량이 있으면 `push_back()`이 할당자를 호출하지 않을 수 있습니다. 그래도
paged 저장소를 건드리거나, 요소를 생성·이동하거나, 사용자 코드를 호출하거나,
예외를 발생시킬 수 있습니다. Nonpaged 할당자는 컨테이너의 할당 저장소 위치만
제어할 뿐, 임의의 STL 객체나 작업을 `DISPATCH_LEVEL`에서 안전하게 만들지는
않습니다.

다음 패턴은 피하세요.

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

이후 작업이 유효하려면 컨테이너 저장소와 접근하는 모든 객체가 상주 상태여야 하며,
정확히 그 작업이 할당·해제·대기·예외·검증되지 않은 콜백을 수행하지 않아야 합니다.
한 IRQL에서 컨테이너를 만들었다고 해서 다른 IRQL에서 변경해도 되는 것은 아닙니다.

## API 요약

- `ntl::pool_tag("TAGx")`
- `ntl::pool_tag(a, b, c, d)`
- `ntl::allocate_pool(bytes, kind, options, tag)`
- `ntl::allocate_pool(bytes, kind, options, "TAGx")`
- `ntl::allocate_pool(bytes, kind, "TAGx")`
- `ntl::allocate_pool(bytes, "TAGx")`
- `ntl::try_allocate_pool(bytes, kind, options, tag)`
- `ntl::try_allocate_pool(bytes, kind, options, "TAGx")`
- `ntl::try_allocate_pool(bytes, kind, "TAGx")`
- `ntl::try_allocate_pool(bytes, "TAGx")`
- `ntl::maximum_pool_irql(kind)`
- `ntl::is_pool_irql_valid(kind)`
- `ntl::require_pool_irql(kind)`
- `ntl::is_pool_allocator_irql_valid()`
- `ntl::require_pool_allocator_irql()`
- `ntl::free_pool(pointer, tag)`
- `ntl::free_pool(pointer, "TAGx")`
- `ntl::free_pool(pointer, kind, tag)`
- `ntl::free_pool(pointer, kind, "TAGx")`
- `ntl::pool_deleter<T>`
- `ntl::pool_ptr<T>`
- `ntl::pool_buffer`
- `ntl::make_pool<T>(kind, options, tag, args...)`
- `ntl::make_pool<T>("TAGx", args...)`
- `ntl::try_make_pool<T>(kind, options, tag, args...)`
- `ntl::try_make_pool<T>("TAGx", args...)`
- `ntl::make_pool_buffer(bytes, kind, options, tag)`
- `ntl::make_pool_buffer(bytes, "TAGx")`
- `ntl::try_make_pool_buffer(bytes, kind, options, tag)`
- `ntl::try_make_pool_buffer(bytes, "TAGx")`
- `ntl::pool_allocator<T, Kind, Options, Tag>`
- `ntl::nonpaged_pool_allocator<T, Tag>`
- `ntl::paged_pool_allocator<T, Tag>`
- `ntl::pmr::pool_resource`
- `ntl::pmr::nonpaged_pool_resource()`
- `ntl::pmr::paged_pool_resource()`

## 구현 참고 사항

`ExAllocatePool2`가 커널 계약에 포함되는 `NTDDI_VERSION`을 선택한 대상은
`ExAllocatePool2`와 `ExFreePool2`를 사용합니다.

하위 호환 대상은 레거시 API로 표현할 수 있는 옵션에 한해
`ExAllocatePoolWithTag`와 `ExFreePoolWithTag`를 사용합니다. 이 방식은 오래된
커널에서의 로드 호환성을 유지하면서, 새로운 대상에서는 더 풍부한 풀 플래그를
노출할 수 있게 합니다.

드라이버 테스트 모음은 다음을 검증합니다.

- 문자열 태그를 사용한 원시 nonpaged 할당과 해제
- `ntl::result<void*>`를 사용한 원시 nonpaged 할당과 해제
- WDK 스타일 다중 문자 태그를 사용한 원시 nonpaged 할당과 해제
- `ntl::pool_buffer`의 release/reset 정리
- `ntl::result`를 사용하는 `ntl::try_make_pool_buffer` 할당
- `ntl::pool_ptr<T>`의 생성, 이동, release, reset, 소멸
- `ntl::result`를 사용하는 `ntl::try_make_pool<T>` 할당
- Nonpaged 및 paged pool allocator를 사용하는 `std::vector`
- `ntl::pool_tag("TAGx")`와 `'xGAT'`를 모두 사용한 템플릿 태그
- `ntl::pmr::pool_resource`를 사용하는 `std::pmr::vector`
- 올린 `DISPATCH_LEVEL`에서의 네이티브 paged/nonpaged IRQL 정책
- 높은 IRQL에서 명시적으로 적용되는 `PASSIVE_LEVEL` allocator 정책 상태
