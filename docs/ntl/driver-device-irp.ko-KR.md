# NTL 드라이버, 장치 및 IRP 도우미

[NTL 문서로 돌아가기](./README.ko-KR.md)

이 문서에서는 일반 WDK 드라이버를 C++ 콜백으로 구성할 때 사용하는 드라이버 측
도우미 클래스를 설명합니다.

## 진입점

`CRTSYS_NTL_MAIN`을 활성화했다면 다음 함수를 구현합니다.

```cpp
ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path);
```

`crtsys`는 WDK 드라이버 진입점에서 이 함수를 호출합니다. 이 진입점 래퍼는
`ntl::main`을 호출하기 전에 스택 확장 도우미도 적용합니다.

예제:

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

`registry_path`는 I/O 관리자가 전달한 서비스 키 경로입니다. 드라이버가 표준
`Parameters` 하위 키에 선택적 구성 값을 둔다면
[`ntl::try_open_driver_parameters`](./registry.ko-KR.md)를 사용하십시오.

## 드라이버 객체

헤더: [`include/ntl/driver`](../../include/ntl/driver)

`ntl::driver`는 `DRIVER_OBJECT`를 래핑합니다.

API:

- `create_device<Extension>(device_options&)`
  - `ntl::device<Extension>`를 생성합니다.
  - 장치 확장 영역에 확장 객체를 생성합니다.
- `try_create_device<Extension>(device_options&)`
  - `ntl::device<Extension>`를 생성합니다.
  - 생성 실패 시 예외를 던지는 대신 `IoCreateDevice`의 상태가 담긴
    `ntl::result<std::shared_ptr<ntl::device<Extension>>>`를 반환합니다.
- `on_unload(callback)`
  - C++ 언로드 콜백을 등록합니다.
- `name() const`
  - 드라이버 이름을 `std::wstring`로 반환합니다.

예제:

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

초기화 경로에서 `IoCreateDevice` 실패를 예외로 바꾸지 않고 `NTSTATUS` 그대로
보존해야 한다면 `try_create_device`를 사용하십시오.

```cpp
auto device = driver.try_create_device<device_extension>(options);
if (!device) {
  return device.status();
}

(*device)->extension().open_count = 0;
```

IRQL: `PASSIVE_LEVEL`. 이 도우미는 C++ 객체와 컨테이너를 사용하므로 드라이버
초기화, unload 등록 및 설정 경로에서 사용하도록 설계되었습니다.

## 장치 엔드포인트

헤더: [`include/ntl/device_endpoint`](../../include/ntl/device_endpoint)

`ntl::device_endpoint<Extension>`는 `ntl::device<Extension>`와 그 장치를 노출하는
DOS 장치 심볼릭 링크를 함께 관리하는, 복사 가능한 소유 핸들입니다. 모든 복사본은
하나의 엔드포인트 상태를 공유합니다. 어느 복사본에서든 닫으면 모든 복사본이 닫힌
상태가 되며 이 작업은 멱등적입니다. 장치 객체를 해제하기 전에 항상 링크부터
삭제합니다.

드라이버에 다음과 같은 일반적인 이름 쌍이 필요할 때 사용합니다.

- `\\Device\\name`
- `\\DosDevices\\name`

예제:

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

- `try_create_device_endpoint<Extension>(driver, options)`
  - `driver.try_create_device`를 통해 장치를 생성합니다.
  - `\\Device\\` + `options.name()`을 대상으로 하는
    `\\DosDevices\\` + `options.name()` 링크를 생성합니다.
  - `ntl::result<ntl::device_endpoint<Extension>>`를 반환합니다.
- `try_create_device_endpoint<Extension>(driver, options, link_name)`
  - `driver.try_create_device`를 통해 장치를 생성합니다.
  - `\\Device\\` + `options.name()`을 대상으로 하는 `link_name` 링크를 생성합니다.
  - `ntl::result<ntl::device_endpoint<Extension>>`를 반환합니다.
- `create_device_endpoint<Extension>(driver, options)`
  - 생성에 실패하면 `ntl::exception`을 던집니다.
- `create_device_endpoint<Extension>(driver, options, link_name)`
  - 생성에 실패하면 `ntl::exception`을 던집니다.
- `dos_device_name(short_name)`
- `device_target_name(short_name)`
- `device_endpoint<Extension>::device()`
  - 공유된 `ntl::device<Extension>` 소유자를 반환합니다.
- `device_endpoint<Extension>::unpublish()`
  - 장치 객체는 유지하면서 새 사용자 모드 open을 거부하여, 여러 단계로 구성한
    drain 절차를 수행할 수 있게 합니다.
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

`device()`가 반환한 소유 장치는 엔드포인트 래퍼보다 오래 살 수 있습니다.
`close()` 뒤에는 모든 엔드포인트 복사본이 닫힘 상태를 보고하고 새 장치 소유자를
반환하지 않지만, 이미 보관한 장치 소유자는 해제될 때까지 유효합니다. 따라서 아직
사용 중인 자식이 래퍼 소멸 순서 때문에 무효화되지 않습니다.

생성, 접근자, `unpublish()`, `close()`에는 `PASSIVE_LEVEL`이 필요합니다. 마지막
엔드포인트 핸들의 소멸은 `DISPATCH_LEVEL`에서도 안전합니다. 필요하면 NTL이
엔드포인트 상태의 최종 정리를 합류 가능한 `PASSIVE_LEVEL` 런타임 worker로
미룹니다. 호출자가 별도로 work item을 큐에 넣거나 drain할 필요는 없습니다.

### 타입이 지정된 IOCTL 라우팅

`on_ioctl<Contract>()`는 요청 레이아웃을 추론하거나 숨기는 콜백이 아닙니다.
`Contract::input_type`과 `Contract::output_type`이 앱과 드라이버가 공유하는 정확한
wire 구조를 정의하며, 계약 자체에 `CTL_CODE` 필드도 들어 있습니다. 예를 들면
다음과 같습니다.

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

라우터는 이 두 형식에서 콜백 서명을 도출합니다. 입력이 `void`이면 입력 매개변수가,
출력이 `void`이면 출력 매개변수가 사라집니다. 엔드포인트 확장 형식이 `void`가
아니면 확장 객체가 첫 번째 매개변수로 전달됩니다. payload 형식은 trivially
copyable이어야 합니다. 이 소유형 라우팅은 `METHOD_BUFFERED`로 제한되며, 정확한 입력
크기를 검증하고 정확한 출력 크기를 초기화해 보고합니다. 콜백의 capture를 소유하고,
중복 코드를 거부하며, 엔드포인트를 닫을 때 콜백이 끝나기를 기다립니다. Direct I/O,
`METHOD_NEITHER` 또는 의도적으로 보류할 IRP에는 명시적인 저수준 경로인
`on_borrowed_pending_ioctl()`을 사용하십시오.

## IRP 보기

헤더: [`include/ntl/irp`](../../include/ntl/irp)

`ntl::irp`는 디스패치 중인 `PIRP`를 감싸는 비소유 뷰입니다. IRP를 완료하거나
참조를 획득하거나 보관하지 않습니다.

API:

- `get() const`
- `operator->() const`
- `stack_location() const`
- `major_function() const`
- `status() const` / `status(NTSTATUS)`
- `information() const` / `information(ULONG_PTR)`
- `set_result(NTSTATUS, ULONG_PTR = 0)`
- `succeed(ULONG_PTR = 0)`
- `fail(NTSTATUS)`

예제:

```cpp
device.on_create([](ntl::irp& request) {
  request.succeed();
});
```

`set_result`, `succeed`, `fail`은 `IoStatus.Status`와
`IoStatus.Information`을 설정합니다. 이 함수들은 `IoCompleteRequest`를 호출하지
않으며, 콜백이 반환된 뒤 NTL 디스패치 호출기가 IRP를 완료합니다.

IRQL: IRP를 제공한 디스패치 루틴을 따릅니다.

## 장치 객체

헤더: [`include/ntl/device`](../../include/ntl/device)

`ntl::device_options`는 장치 생성을 구성합니다.

API:

- `name(std::wstring)`
- `type(DEVICE_TYPE)`
- `exclusive(bool = true)`
- `name() const`
- `type() const`
- `is_exclusive() const`

`ntl::device<Extension>`는 `PDEVICE_OBJECT`를 소유합니다.

API:

- `extension()`
- `on_create(callback)`
- `on_close(callback)`
- `on_device_control(callback)`
- `name() const`
- `type() const`
- `detach()`

장치 제어 도우미 형식:

- `ntl::device_control::code`
- `ntl::device_control::in_buffer`
- `ntl::device_control::out_buffer`
- `ntl::device_control::dispatch_fn`

`in_buffer`는 trivially-copyable 요청 payload를 읽는 `can_read(bytes)`와
`as<T>()`를 제공합니다. `out_buffer`는 `can_write(bytes)`, `clear()`, `as<T>()`,
`write_bytes(ptr, bytes)`, `write(value)`를 제공하며 정확한 출력 바이트 수를
`IoStatus.Information`으로 보고합니다.

trivially-copyable 고정 요청 및 응답 payload를 사용하는 IOCTL이라면
[`ntl::ioctl`](./ioctl.ko-KR.md)로 `CTL_CODE` 값을 해당 payload 형식에 연결하십시오.
반복되는 크기 확인을 줄이면서도 디스패치 코드에는 원시 IOCTL 번호가 그대로
드러납니다. 형식 지정 IOCTL, `ntl::remove_lock`, `ntl::mdl` 및 출력 바이트 수
보고를 조합한 전체 디스패치 본문 패턴은
[`장치 제어 패턴`](./device-control-pattern.ko-KR.md)을 참고하십시오.

예제:

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

IRQL: 특정 디스패치 경로를 별도로 검토해 문서화하지 않았다면 `PASSIVE_LEVEL`입니다.
이 래퍼는 C++ 콜백과 소유권 도우미를 사용합니다.

## 심볼릭 링크

헤더: [`include/ntl/symbolic_link`](../../include/ntl/symbolic_link)

`ntl::symbolic_link`는 `IoCreateSymbolicLink`로 만든 WDK 심볼릭 링크를 소유하고
`IoDeleteSymbolicLink`로 삭제합니다.

예제:

```cpp
ntl::symbolic_link link(L"\\DosDevices\\demo", L"\\Device\\demo");
```

링크가 드라이버 수명 동안 유지되어야 한다면 장치 객체와 함께 unload 콜백으로
이동하십시오.

IRQL: `PASSIVE_LEVEL`.
