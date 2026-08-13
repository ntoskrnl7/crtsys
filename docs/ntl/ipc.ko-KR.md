# NTL IPC 공유 메모리

[NTL 문서로 돌아가기](./README.ko-KR.md)

`ntl::ipc`는 NTL RPC와 미니필터 통신 포트 전송이 함께 사용하는, 전송 방식에
종속되지 않는 데이터 평면 기반입니다. WDM 장치, KMDF 큐 또는 Filter Manager
포트를 만들지는 않습니다. 연결, 권한 확인, 취소 및 영역 수명은 각 전송 계층이
책임지고, `ntl::ipc`는 전송 계층이 영역을 안전하게 등록한 뒤 사용할 포인터 없는
메모리 계약을 정의합니다.

헤더:

- [`include/ntl/ipc/common`](../../include/ntl/ipc/common)
- [`include/ntl/ipc/shared_ring`](../../include/ntl/ipc/shared_ring)
- [`include/ntl/ipc/kernel_region`](../../include/ntl/ipc/kernel_region) — 드라이버 빌드 전용
- [`include/ntl/ipc/all`](../../include/ntl/ipc/all)

## 32/64비트 호환 계약

IPC 계약을 통해 포인터, `size_t`, 반복자 또는 프로세스에 종속된 핸들을 보내면 안
됩니다. 전송 계층은 호출자의 메모리를 등록하고 고정 폭 `region_handle`을
반환합니다. 이후 작업에서는 `buffer_token`으로 그 안의 하위 범위를 지정합니다.

```cpp
struct buffer_token {
  ntl::ipc::region_handle region; // uint64 ID + uint64 generation
  std::uint64_t offset;
  std::uint64_t length;
};
```

세대 번호는 연결 해제, 등록 해제 또는 영역 ID 재사용 뒤에 남은 오래된 토큰을
거부하는 데 쓰입니다. 수신 측 전송 계층은 자체 영역 테이블을 기준으로 접근 권한과
범위도 검증해야 합니다. 토큰 자체가 권한을 부여하는 것은 아닙니다.

이 레이아웃은 x86 앱과 x64 드라이버에서 동일합니다. 공유 메모리에 저장하는
레코드 형식에도 같은 규칙을 적용해야 합니다. 고정 폭 필드를 사용하고 네이티브
포인터나 핸들, `size_t`, `std::string`, STL 컨테이너를 저장하지 마세요.

## 고정 레코드 링

`shared_ring<T, Capacity>`는 용량이 제한된 단일 생산자/단일 소비자 큐입니다.
앱→드라이버 레코드와 드라이버→앱 레코드에는 각각 별도의 링을 사용하세요.

```cpp
struct telemetry_record {
  std::uint64_t timestamp;
  std::uint32_t event_id;
  std::uint32_t value;
};

using telemetry_ring = ntl::ipc::shared_ring<telemetry_record, 1024>;

// The allocating side initializes an aligned shared range once.
telemetry_ring producer;
auto status = telemetry_ring::initialize(memory, bytes, producer, generation);

// The other side validates the exact magic/version/record-size/capacity.
telemetry_ring consumer;
status = telemetry_ring::attach(memory, bytes, consumer);

producer.try_write(record); // false applies bounded backpressure when full
consumer.try_read(record);  // false when empty
```

링은 단순 복사가 가능한 레코드를 바이트 슬롯에 복사합니다. 다른 프로세스의
메모리에 C++ 객체를 배치하거나 가변 길이 데이터를 직렬화하지 않습니다. 시퀀스
값은 Windows interlocked 연산으로 공개하므로, 생산자가 복사를 마치기 전에 소비자가
슬롯을 관찰하는 일은 없습니다.

각 링에는 정확히 한 생산자와 한 소비자만 허용됩니다. 여러 생산자 또는 소비자가
필요하면 별도 링이나 상위 전송 정책을 사용하세요.

## IOCTL RPC 어댑터

IOCTL RPC 전송은 세션에 종속된 등록을 구현합니다. 앱은 세션을 만들거나 재개하고,
페이지 기반 영역을 할당한 뒤 기존 세션 제어 IOCTL로 등록합니다.

```cpp
ntl::rpc::client client(L"telemetry_rpc");
(void)client.start_session();

auto region = client.register_shared_region(
    telemetry_ring::required_bytes(),
    ntl::ipc::region_access::driver_read_write);

telemetry_ring app_ring;
if (telemetry_ring::initialize(region.data(), region.size(), app_ring) !=
    ntl::ipc::validation_status::success)
  throw std::runtime_error("could not initialize telemetry ring");

auto token = region.token(0, telemetry_ring::required_bytes());
client.invoke(start_telemetry, token);
```

RPC 콜백은 포인터가 없는 토큰만 받습니다. 그런 다음 콜백의 세션을 통해 필요한
접근 권한을 정확히 지정하여 토큰을 해석합니다.

```cpp
server->on(start_telemetry,
  [](const ntl::rpc::call_context& call, ntl::ipc::buffer_token token) {
    auto pinned = call.try_resolve(
        token, ntl::ipc::region_access::driver_write);
    if (!pinned)
      throw ntl::exception(pinned.status(), "invalid telemetry region");

    telemetry_ring driver_ring;
    if (telemetry_ring::attach(pinned->data(), pinned->size(), driver_ring) !=
        ntl::ipc::validation_status::success)
      throw ntl::exception(STATUS_INVALID_PARAMETER, "invalid ring layout");

    driver_ring.try_write(record);
  });
```

`registered_region`은 `VirtualFree`를 호출하기 전에 등록을 해제합니다. 명시적
`close()`는 등록 해제 오류를 보고합니다. 드라이버가 아직 고정해 두었을 수 있는
할당 영역은 소멸자에서 해제하지 않습니다. 예외적인 상황에서 최선 노력 방식의 등록
해제마저 실패하면, 해제 후 사용 문제를 일으키는 대신 해당 할당을 프로세스 종료
시점까지 예약 상태로 남겨 둡니다. 세션 연결 해제, 명시적 닫기, 보존 기간 만료 및
서버 중지는 이후 조회 대상에서 모든 영역을 제거합니다. 이미 토큰을 해석한 콜백은
`pinned_buffer`를 해제할 때까지 해당 MDL을 유지합니다.

기본 할당량은 세션당 영역 16개와 64 MiB입니다. 드라이버는
`server_options::max_shared_regions()` 및 `max_shared_region_bytes()`로 이 한계를
낮추거나 올릴 수 있습니다.

## 전송 어댑터

IOCTL RPC 어댑터와 Filter Manager 통신 포트 어댑터는 같은 `region_handle`,
`buffer_token`, 링 레이아웃 및 세대 번호 검사를 사용합니다. 전송 계층은 연결, 권한
확인, 취소 및 영역 수명을 계속 책임지고, 공유 메모리 계층은 이러한 검사를 통과한
뒤에만 검증된 로컬 뷰를 노출합니다.

RPC는 제어 평면을 담당하고, 공유 메모리는 대용량 트래픽을 위한 선택적 데이터
평면으로 사용합니다. 등록 영역 및 공유 링 경로는 x64 앱과 x86 앱 모두를 대상으로
검증됩니다. 사용자 모드 코드는 등록된 영역 위에 `ntl::ipc::shared_buffer_pool`도
만들 수 있습니다.

```cpp
auto region = client.register_shared_region(4096);
auto pool = region.make_buffer_pool(16);
auto lease_result = pool.try_acquire(512);
if (!lease_result)
  throw std::runtime_error("shared lease allocation failed");

auto lease = std::move(*lease_result);
std::memset(lease.data(), 0, lease.size());
client.invoke(write_payload, lease.token());
```

`buffer_lease`는 이동 전용 형식입니다. 이를 해제하면 하위 범위가 풀로 반환되고
인접한 빈 범위가 병합됩니다. 풀은 예약 정보만 소유하므로 임대나 토큰을 사용하는
동안에는 `registered_region`을 계속 유지해야 합니다. 드라이버는 여전히 연결별 고정
영역 레지스트리를 기준으로 모든 토큰을 검증합니다. 따라서 풀을 사용해도 범위,
세대 번호, 접근 권한 또는 언로드 검사가 약화되지 않습니다. 미니필터의
`registered_port_region`에서도 `make_buffer_pool()`을 통해 같은 풀을 사용할 수
있습니다.
