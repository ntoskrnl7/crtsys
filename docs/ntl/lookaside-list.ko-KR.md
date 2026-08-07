# NTL 룩어사이드 리스트

[NTL 문서로 돌아가기](./README.ko-KR.md)

헤더: [`include/ntl/lookaside_list`](../../include/ntl/lookaside_list)

`ntl::lookaside_list<T>`는 고정 크기 커널 객체용 WDK `LOOKASIDE_LIST_EX`를
감싼 래퍼입니다. 드라이버가 같은 형식의 객체를 반복해서 많이 할당하고 해제하면서,
C++ 객체의 생성과 정리를 함께 처리하고 일반적인 WDK 룩어사이드 캐시 동작을 원할 때
사용합니다.

이 클래스는 의도적으로 커널 API의 성격을 그대로 유지합니다. 풀 종류, 풀 태그 및
룩어사이드 리스트의 수명은 모두 호출자에게 드러납니다.

## 빠른 예제

비페이지 객체 캐시:

```cpp
struct packet_context {
  explicit packet_context(unsigned id) : id(id) {}
  unsigned id;
};

using packet_cache =
    ntl::lookaside_list<packet_context,
                        ntl::pool_kind::nonpaged,
                        ntl::pool_option::none,
                        ntl::pool_tag("PKTl")>;

packet_cache packets;

auto packet = packets.make(42);
packet->id += 1;

auto packet_result = packets.try_make(43);
if (!packet_result) {
  return packet_result.status();
}
```

원시 할당과 명시적 생성:

```cpp
auto* packet = packets.allocate();
if (!packet) {
  return STATUS_INSUFFICIENT_RESOURCES;
}

new (packet) packet_context{7};

packet->~packet_context();
packets.free(packet);
```

페이지 가능 메모리를 쓰는 제어 경로 캐시:

```cpp
using name_cache =
    ntl::lookaside_list<name_record,
                        ntl::pool_kind::paged,
                        ntl::pool_option::none,
                        ntl::pool_tag("NAMl")>;

name_cache names;
auto name = names.make(L"control-path-only");
```

캐시 줄에 맞춘 비페이지 캐시:

```cpp
using aligned_cache =
    ntl::lookaside_list<cache_line_state,
                        ntl::pool_kind::nonpaged,
                        ntl::pool_option::cache_aligned,
                        'lCAC'>;
```

## API 요약

- `ntl::lookaside_list<T, Kind, Options, Tag>`
- `lookaside_list(depth)`
- `allocate()`
- `free(pointer)`
- `make(args...)`
- `try_make(args...)`
- `destroy(pointer)`
- `flush()`
- `native_handle()`

`make(args...)`는 이동 전용 RAII 포인터를 반환합니다. 포인터의 삭제자가 객체를
해당 리스트로 되돌리므로, 이 리스트는 여기서 만든 모든 포인터보다 오래 살아 있어야
합니다.

`try_make(args...)`는 예외를 던지지 않고 `ntl::result<pointer>`를 반환합니다.
드라이버 초기화 경로에서 `NTSTATUS` 흐름을 보존해야 할 때 사용합니다.

WDK는 `depth`에 `0`을 전달할 것을 권장합니다. 명시적으로 0이 아닌 깊이를
전달한다면 WDK의 `EX_MAXIMUM_LOOKASIDE_DEPTH_*` 범위 안이어야 합니다. `4`나
`32`처럼 작은 값은 유효한 명시적 깊이가 아닙니다.

## 옵션

`ntl::lookaside_list`는 다음 `ntl::pool_option` 값을 지원합니다.

- `ntl::pool_option::none`
- `ntl::pool_option::cache_aligned`
- `ntl::pool_option::raise_on_failure`

그 밖의 `pool_option` 값은 컴파일 시간에 거부됩니다. 이 래퍼는
`ExInitializeLookasideListEx`로 표현할 수 없는 플래그까지 지원하는 것처럼
행동하지 않습니다.

## IRQL 지침

내부 WDK 룩어사이드 할당/해제 프리미티브는 `<= DISPATCH_LEVEL`에서 사용할 수
있도록 문서화되어 있습니다. 하지만 객체의 생성·소멸, 예외 처리, 임의의 C++ 코드는
자동으로 DPC 안전해지지 않습니다.

다음 기준을 따르세요.

- 원시 비페이지 `allocate()` / `free()`는 WDK 룩어사이드 계약을 따를 수 있습니다.
- 객체 형식 및 생성자/소멸자를 별도로 검토하지 않았다면 `make()` / `destroy()`는
  `PASSIVE_LEVEL`에서만 사용해야 합니다.
- 페이지 가능 룩어사이드 리스트는 페이지 가능 메모리를 사용할 수 있는 곳에서만
  사용해야 합니다.

객체를 만든 `lookaside_list`의 수명이 끝난 뒤에도 RAII 객체를 보관해서는 안 됩니다.

## 테스트된 범위

드라이버 테스트 모음은 다음을 검증합니다.

- 원시 비페이지 할당/생성/소멸/해제
- RAII 객체 생성, `try_make`, 이동, `reset`, 소멸
- 페이지 가능 룩어사이드 리스트 생성
- 캐시 줄 정렬 비페이지 룩어사이드 리스트 생성
- 명시적 `flush()`
