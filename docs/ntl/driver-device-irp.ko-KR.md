# NTL 드라이버, 장치 및 IRP 도우미

[NTL 문서로 돌아가기](./README.ko-KR.md)

이 페이지에서는 일반 WDK를 연결하는 데 사용되는 드라이버 관련 도우미 클래스를 다룹니다.
C++ 콜백이 포함된 드라이버.

## 진입점

`CRTSYS_NTL_MAIN`가 활성화되면 다음을 구현합니다.

```cpp
ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path);
```

`crtsys`는 WDK 드라이버 진입점을 이 함수로 라우팅합니다. 포장지에도
`ntl::main`를 호출하기 전에 스택 확장 도우미를 사용합니다.

예:

```cpp
#include <ntl/driver>
#include <ntl/registry>

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  ULONG flags = 0;

  auto parameters = ntl::try_open_driver_parameters(registry_path);
  if (parameters) {
    auto configured_flags = parameters->query_dword(L"Flags");
    if (configured_flags) {
      flags = *configured_flags;
    }
  } else if (static_cast<NTSTATUS>(parameters.status()) !=
             STATUS_OBJECT_NAME_NOT_FOUND) {
    return parameters.status();
  }

  (void)flags;

  driver.on_unload([] {
    // Release driver-owned objects here.
  });

  return ntl::status::ok();
}
```

IRQL: `PASSIVE_LEVEL`.

`registry_path`는 I/O 관리자가 제공하는 서비스 키 경로입니다. 사용
[`ntl::try_open_driver_parameters`](./registry.ko-KR.md) 드라이버에 옵션이 있는 경우
표준 `Parameters` 하위 키 아래의 구성 값입니다.

## 드라이버 객체

헤더: [`include/ntl/driver`](../../include/ntl/driver)

`ntl::driver`는 `DRIVER_OBJECT`를 래핑합니다.

API:

-`create_device<Extension>(device_options&)`
  - `ntl::device<Extension>`를 생성합니다.
  - 장치 확장 영역에 확장 개체를 구성합니다.
-`try_create_device<Extension>(device_options&)`
  - `ntl::device<Extension>`를 생성합니다.
  - 다음과 함께 `ntl::result<std::shared_ptr<ntl::device<Extension>>>`를 반환합니다.
    생성 실패로 인해 발생하는 대신 `IoCreateDevice` 상태
-`on_unload(callback)`
  - C++ 언로드 콜백을 등록합니다.
-`name() const`
  - 드라이버 이름을 `std::wstring`로 반환합니다.

예:

```cpp
struct device_extension {
  ULONG open_count = 0;
};

ntl::device_options options;
options.name(L"demo").type(FILE_DEVICE_UNKNOWN);

auto device = driver.create_device<device_extension>(options);
device.extension().open_count = 0;

driver.on_unload([device = std::move(device)]() mutable {
  device.detach();
});
```

초기화 경로를 유지해야 하는 경우 `try_create_device`를 사용하세요.
`IoCreateDevice` 실패를 예외로 변환하지 않고 `NTSTATUS`:

```cpp
auto device = driver.try_create_device<device_extension>(options);
if (!device) {
  return device.status();
}

(*device)->extension().open_count = 0;
```

IRQL: `PASSIVE_LEVEL`. 도우미는 C++ 개체와 컨테이너를 사용하며
드라이버 초기화, 언로드 등록 및 설정 경로용입니다.

## 장치 엔드포인트

헤더: [`include/ntl/device_endpoint`](../../include/ntl/device_endpoint)

`ntl::device_endpoint<Extension>`는 복사 가능한 소유 핸들입니다.
`ntl::device<Extension>` 및 이를 노출하는 DOS 장치 심볼릭 링크.
복사본은 하나의 끝점 상태를 공유합니다. 복사본을 닫으면 모든 복사본의 상태가 닫힙니다.
복사하고 작업은 멱등성을 갖습니다. 링크는 항상 삭제되기 전에 삭제됩니다.
장치 개체가 해제되었습니다.

드라이버가 공통 쌍을 원할 때 사용하십시오.

-`\\Device\\name`
-`\\DosDevices\\name`

예:

```cpp
#include <ntl/device_endpoint>

struct device_extension {
  ULONG open_count = 0;
};

ntl::device_options options;
options.name(L"demo").type(FILE_DEVICE_UNKNOWN);

auto endpoint_result = ntl::try_create_device_endpoint<device_extension>(
    driver, options);
if (!endpoint_result) {
  return endpoint_result.status();
}

auto endpoint = std::move(*endpoint_result);
auto device = endpoint.device();
if (!device)
  return STATUS_INVALID_DEVICE_STATE;
device->extension().open_count = 0;

driver.on_unload([endpoint]() noexcept {
  const ntl::status result = endpoint.close();
  NT_ASSERT(result.is_ok());
});
```

API:

-`try_create_device_endpoint<Extension>(driver, options)`
  - `driver.try_create_device`를 통해 장치를 생성합니다.
  - `\\DosDevices\\` + `options.name()` 타겟팅을 생성합니다.
    `\\Device\\` + `options.name()`
  - `ntl::result<ntl::device_endpoint<Extension>>`를 반환합니다.
-`try_create_device_endpoint<Extension>(driver, options, link_name)`
  - `driver.try_create_device`를 통해 장치를 생성합니다.
  - `\\Device\\` + `options.name()`를 대상으로 `link_name`를 생성합니다.
  - `ntl::result<ntl::device_endpoint<Extension>>`를 반환합니다.
- `create_device_endpoint<Extension>(driver, options)`
  - 생성 실패 시 `ntl::exception`가 발생합니다.
- `create_device_endpoint<Extension>(driver, options, link_name)`
  - 생성 실패 시 `ntl::exception`가 발생합니다.
- `dos_device_name(short_name)`
- `device_target_name(short_name)`
- `device_endpoint<Extension>::device()`
  - 공유된 `ntl::device<Extension>` 소유자를 반환합니다.
- `device_endpoint<Extension>::unpublish()`
  - 장치 객체를 유지하면서 새로운 사용자 모드 열기를 거부합니다.
    구성한 종료 절차를 실행할 수 있게 합니다.
- `device_endpoint<Extension>::close()`
  - 링크를 멱등적으로 삭제하고 모든 복사본에 대해 장치를 해제합니다.
- `device_endpoint<Extension>::link_name()`
- `device_endpoint<Extension>::target_name()`
- `device_endpoint<Extension>::valid()` / `operator bool()`

`device_options::name()`은 `\\Device\\` 접두사가 없는 짧은 장치 이름입니다.
엔드포인트 팩터리는 이 이름으로 네이티브 대상 경로를 만듭니다. 인수를 두 개 받는
엔드포인트 팩터리는 같은 짧은 이름으로 일반적인 DOS 링크 이름도 만들기 때문에,
보통은 장치 이름을 한 번만 지정하면 됩니다.

엔드포인트를 값으로 캡처하면 공유 소유 상태가 유지됩니다. 호출자가 다시
`std::shared_ptr`로 감쌀 필요는 없습니다. 외부 작업을 비우기 전에 새 open을
거부해야 하는 하위 시스템은 `unpublish()`를 호출하고 작업을 비운 다음 `close()`를
호출합니다. 일반 호출자가 런타임의 `close()` 작업 하나만 호출하도록 이 순서를
하위 시스템을 소유하는 런타임에 넣으세요.

`device()`가 반환한 소유 장치는 엔드포인트 facade보다 오래 살 수 있습니다.
`close()` 뒤에는 모든 엔드포인트 복사본이 닫힘 상태를 보고하고 새 장치 소유자를
반환하지 않지만, 이미 보관한 장치 소유자는 해제될 때까지 유효합니다. 따라서 아직
사용 중인 자식이 facade 소멸 순서 때문에 무효화되지 않습니다.

생성, 접근자, `unpublish()` 및 `close()`에는 `PASSIVE_LEVEL`가 필요합니다.
마지막 엔드포인트 핸들을 파기하는 것은 `DISPATCH_LEVEL`: NTL을 통해 안전합니다.
엔드포인트 상태의 최종 정리를 조인된 `PASSIVE_LEVEL`로 연기합니다.
필요한 경우 런타임 작업자. 호출자는 작업 항목을 대기열에 넣거나 비우지 않습니다.

### 형식화된 IOCTL 라우팅

`on_ioctl<Contract>()`는 추론되거나 숨겨진 요청이 포함된 콜백이 아닙니다.
레이아웃. `Contract::input_type` 및 `Contract::output_type`는 정확한 값을 정의합니다.
공유 앱/드라이버 와이어 구조 및 계약은 `CTL_CODE`도 소유합니다.
필드. 예를 들면:

```cpp
struct configure_proxy_request {
  std::uint16_t port = 0;
};

struct configure_proxy_reply {
  std::uint32_t generation = 0;
};

struct configure_proxy_contract {
  static constexpr ULONG device_type = FILE_DEVICE_UNKNOWN;
  static constexpr ULONG function = 0x900;
  static constexpr ULONG method = METHOD_BUFFERED;
  static constexpr ULONG access = FILE_READ_DATA | FILE_WRITE_DATA;
  using input_type = configure_proxy_request;
  using output_type = configure_proxy_reply;
};

const ntl::status routed =
    endpoint.on_ioctl<configure_proxy_contract>(
        [](const configure_proxy_request &request,
           configure_proxy_reply &reply) noexcept -> ntl::status {
          if (request.port == 0)
            return STATUS_INVALID_PARAMETER;
          reply.generation = static_cast<std::uint32_t>(request.port);
          return ntl::status::ok();
        });
if (!routed.is_ok())
  return routed;
```

라우터는 이 두 가지 유형에서 콜백 서명을 파생합니다. `void`
입력은 입력 매개변수를 제거하고, `void` 출력은 출력을 제거합니다.
매개변수이며 `void`가 아닌 엔드포인트 확장이 첫 번째로 전달됩니다.
매개변수. 페이로드 유형은 쉽게 복사할 수 있어야 합니다. 소유한 경로는 다음과 같습니다.
`METHOD_BUFFERED`로 제한되고 정확한 입력 크기를 검증하고 초기화합니다.
정확한 출력 크기를 보고하고 콜백 캡처를 소유하며 중복을 거부합니다.
엔드포인트가 닫히는 동안 코드를 생성하고 콜백을 배출합니다. 직접 I/O,
`METHOD_NEITHER` 또는 의도적으로 보류 중인 IRP는 명시적으로 낮은 수준을 사용합니다.
대신 `on_borrowed_pending_ioctl()` 경로를 사용하세요.

## IRP 보기

헤더: [`include/ntl/irp`](../../include/ntl/irp)

`ntl::irp`는 디스패치 시간 `PIRP`에 대한 비소유 뷰입니다. 그렇지 않다
IRP를 완성, 참조 또는 보관합니다.

API:

-`get() const`
-`operator->() const`
-`stack_location() const`
-`major_function() const`
-`status() const` / `status(NTSTATUS)`
-`information() const` / `information(ULONG_PTR)`
-`set_result(NTSTATUS, ULONG_PTR = 0)`
-`succeed(ULONG_PTR = 0)`
-`fail(NTSTATUS)`

예:

```cpp
device.on_create([](ntl::irp& request) {
  request.succeed();
});
```

`set_result`, `succeed`, `fail`은 `IoStatus.Status`와
`IoStatus.Information`을 설정합니다. 이 함수들은 `IoCompleteRequest`를 호출하지
않으며, 콜백이 반환된 뒤 NTL 디스패치 호출기가 IRP를 완료합니다.

IRQL: IRP를 제공한 디스패치 루틴을 따릅니다.

## 장치 개체

헤더: [`include/ntl/device`](../../include/ntl/device)

`ntl::device_options`는 장치 생성을 구성합니다.

API:

-`name(std::wstring)`
-`type(DEVICE_TYPE)`
-`exclusive(bool = true)`
-`name() const`
-`type() const`
-`is_exclusive() const`

`ntl::device<Extension>`는 `PDEVICE_OBJECT`를 소유하고 있습니다.

API:

-`extension()`
-`on_create(callback)`
-`on_close(callback)`
-`on_device_control(callback)`
-`name() const`
-`type() const`
-`detach()`

장치 제어 도우미 유형:

-`ntl::device_control::code`
-`ntl::device_control::in_buffer`
-`ntl::device_control::out_buffer`
-`ntl::device_control::dispatch_fn`

`in_buffer`는 다음에 대해 `can_read(bytes)` 및 `as<T>()`를 제공합니다.
간단하게 복사 가능한 요청 페이로드. `out_buffer`는 다음을 제공합니다.
`can_write(bytes)`, `clear()`, `as<T>()`, `write_bytes(ptr, bytes)` 및
다음을 통해 정확한 출력 바이트 수를 보고하는 `write(value)`
`IoStatus.Information`.

간단하게 복사 가능한 고정된 요청 및 응답 페이로드가 있는 IOCTL의 경우 다음을 사용하세요.
[`ntl::ioctl`](./ioctl.ko-KR.md) - `CTL_CODE` 값을 해당 페이로드 유형에 연결합니다.
반복되는 크기 확인을 줄이면서도 디스패치 코드에는 원시 IOCTL 번호가 그대로
드러납니다. 형식화된 IOCTL과 다음 요소를 결합하는 완전한 디스패치 본문 패턴은
`ntl::remove_lock`, `ntl::mdl` 및 출력 바이트 수 보고는 다음을 참조하세요.
[`Device-control pattern`](./device-control-pattern.ko-KR.md).

예:

```cpp
struct demo_reply {
  ULONG value;
};

device.on_device_control([](const ntl::device_control::code& code,
                            const ntl::device_control::in_buffer& in,
                            ntl::device_control::out_buffer& out) {
  if (code != DEMO_IOCTL_PING) {
    out.clear();
    return;
  }

  const auto* request = in.as<ULONG>();
  if (!request) {
    out.clear();
    return;
  }

  demo_reply reply{*request + 1};
  if (!out.write(reply)) {
    out.clear();
  }
});
```

IRQL: 특정 디스패치 경로가 감사되지 않은 경우 `PASSIVE_LEVEL`
달리 문서화되어 있습니다. 래퍼는 C++ 콜백과 소유권 도우미를 사용합니다.

## 심볼릭 링크

헤더: [`include/ntl/symbolic_link`](../../include/ntl/symbolic_link)

`ntl::symbolic_link`는 다음에 의해 생성된 WDK 심볼릭 링크를 소유합니다.
`IoCreateSymbolicLink`를 사용하고 `IoDeleteSymbolicLink`를 사용하여 삭제합니다.

예:

```cpp
ntl::symbolic_link link(L"\\DosDevices\\demo", L"\\Device\\demo");
```

링크가 연결될 때 장치 개체와 함께 언로드 콜백으로 이동합니다.
드라이버 평생 동안 살아야합니다.

IRQL: `PASSIVE_LEVEL`.
