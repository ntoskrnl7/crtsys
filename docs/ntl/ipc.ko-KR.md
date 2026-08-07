# NTL IPC 공유 메모리

[NTL 문서로 돌아가기](./README.ko-KR.md)

`ntl::ipc`는 NTL RPC와 미니필터 통신 포트 전송이 공유하는 전송 중립적 데이터 평면
기반입니다. WDM 장치, KMDF 큐, Filter Manager 포트를 만들지 않습니다. 연결, 권한
확인, 취소, 영역 수명은 각 전송이 소유하며, `ntl::ipc`는 전송이 영역을 안전하게
등록한 뒤 사용하는 포인터 없는 메모리 계약을 정의합니다.

헤더:

- [`include/ntl/ipc/common`](../../include/ntl/ipc/common)
- [`include/ntl/ipc/shared_ring`](../../include/ntl/ipc/shared_ring)
- [`include/ntl/ipc/kernel_region`](../../include/ntl/ipc/kernel_region) — 드라이버 빌드 전용
- [`include/ntl/ipc/all`](../../include/ntl/ipc/all)

## 교차 비트 수 계약

IPC 계약으로 포인터, `size_t`, iterator, 프로세스 로컬 handle을 보내지 마세요. 전송은
호출자의 메모리를 등록하고 고정 폭 `region_handle`을 반환합니다. 이후 작업은
`buffer_token`으로 하위 범위를 가리킵니다.

```cpp
struct buffer_token {
  ntl::ipc::region_handle region; // uint64 ID + uint64 generation
  std::uint64_t offset;
  std::uint64_t length;
};
```

generation은 disconnect, unregister, region ID 재사용 뒤의 stale token을 거부합니다.
수신 전송은 자체 region table을 기준으로 access와 bounds도 검증해야 합니다. token만으로
권한을 얻는 것은 아닙니다.

이 레이아웃은 x86 앱과 x64 드라이버에서 같습니다. 공유 메모리에 저장하는 record도
고정 폭 필드를 쓰고 네이티브 포인터·handle, `size_t`, `std::string`, STL container를
저장하지 않아야 합니다.

## 고정 레코드 링

`shared_ring<T, Capacity>`는 제한된 단일 생산자/단일 소비자 큐입니다. 앱→드라이버와
드라이버→앱 레코드에는 각각 다른 링을 사용하세요.

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

링은 trivially copyable record를 byte slot에 복사합니다. 다른 프로세스에 C++ 객체를
배치하거나 가변 크기 데이터를 직렬화하지 않습니다. sequence publish에는 Windows
interlocked 연산을 쓰므로 생산자가 복사를 끝내기 전 소비자가 slot을 보지 않습니다.

각 링에는 정확히 한 생산자와 한 소비자만 허용됩니다. 여러 생산자 또는 소비자가
필요하면 별도 링이나 상위 전송 정책을 사용하세요.

## IOCTL RPC 어댑터

IOCTL RPC 전송은 세션에 묶인 등록을 구현합니다. 앱은 세션을 만들거나 재개하고,
page-backed 영역을 할당한 뒤 기존 세션 제어 IOCTL로 등록합니다.

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

RPC 콜백은 포인터 없는 token만 받고, 콜백 세션에서 필요한 정확한 access로 이를
resolve합니다.

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

`registered_region`은 `VirtualFree` 전에 unregister합니다. 명시적 `close()`는
unregister 오류를 보고합니다. 소멸자는 드라이버가 계속 pin했을 수 있는 할당을
해제하지 않습니다. 최선 노력 unregister가 예외적으로 실패하면 use-after-free를
만드는 대신 프로세스 종료까지 예약 상태로 둡니다. session disconnect, 명시적 close,
retention 만료, server stop은 이후 lookup에서 모든 region을 제거합니다. 이미 token을
resolve한 콜백은 `pinned_buffer`를 해제할 때까지 MDL을 보관합니다.

기본 quota는 session당 region 16개와 64 MiB입니다. 드라이버는
`server_options::max_shared_regions()` 및 `max_shared_region_bytes()`로 이 한계를
낮추거나 올릴 수 있습니다.

## 전송 어댑터

IOCTL RPC 어댑터와 Filter Manager 통신 포트 어댑터는 같은 `region_handle`,
`buffer_token`, 링 레이아웃, generation 검사를 사용합니다. 전송은 연결, 권한 확인,
취소, 영역 수명을 계속 책임지고, 공유 메모리 계층은 검증에 성공한 뒤 검증된 로컬
뷰만 노출합니다.

RPC는 control plane으로 남고 공유 메모리는 대용량 트래픽용 선택 data plane입니다.
registered-region 및 shared-ring 경로는 x64와 x86 앱에서 검증합니다. 사용자 모드
코드는 등록한 region에 `ntl::ipc::shared_buffer_pool`도 만들 수 있습니다.

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

`buffer_lease`는 이동 전용입니다. 해제하면 하위 범위를 pool에 반환하고 인접한 free
range를 병합합니다. pool은 reservation만 소유하므로 lease나 token을 쓰는 동안
`registered_region`을 유지하세요. 드라이버는 여전히 연결 범위 pinned-region registry를
기준으로 모든 token을 검증하므로 pool이 range, generation, access, unload 검사를
약화하지 않습니다. 같은 pool은 미니필터 `registered_port_region`에서도
`make_buffer_pool()`으로 사용할 수 있습니다.
