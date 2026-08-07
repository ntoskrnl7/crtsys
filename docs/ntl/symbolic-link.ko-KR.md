# NTL 심볼릭 링크 도우미

[NTL 문서로 돌아가기](./README.ko-KR.md)

`ntl::symbolic_link`는 WDK의 `IoCreateSymbolicLink` / `IoDeleteSymbolicLink` 쌍을 위한 작은 RAII 래퍼입니다.

드라이버가 DOS 장치 형식 심볼릭 링크를 통해 이름 있는 장치를 노출하는 초기화와 teardown 경로를 위한 기능입니다.

헤더: [`include/ntl/symbolic_link`](../../include/ntl/symbolic_link)

## 예제

```cpp
#include <ntl/driver>
#include <ntl/symbolic_link>

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;

  ntl::device_options options;
  options.name(L"demo").type(FILE_DEVICE_UNKNOWN);
  auto device = driver.create_device<void>(options);

  auto link = std::make_shared<ntl::symbolic_link>(L"\\DosDevices\\demo",
                                                   L"\\Device\\demo");

  driver.on_unload([device, link] {
    // Capturing device keeps the DEVICE_OBJECT alive until unload.
    link->reset();
  });

  return ntl::status::ok();
}
```

생성자 예외 대신 NTSTATUS 스타일 제어 흐름을 선호한다면 factory 도우미를 사용하십시오.

```cpp
auto link = ntl::try_create_symbolic_link(L"\\DosDevices\\demo",
                                          L"\\Device\\demo");
if (!link) {
  return link.status();
}
```

## API

- `symbolic_link(std::wstring link_name, std::wstring target_name)`
  - 심볼릭 링크를 생성합니다.
  - `IoCreateSymbolicLink` 실패 시 `ntl::exception`을 던집니다.
- `try_create_symbolic_link(link_name, target_name)`
  - 심볼릭 링크를 생성합니다.
  - `IoCreateSymbolicLink` 상태를 포함하는 `ntl::result<symbolic_link>`를 반환합니다.
- `create(link_name, target_name)`
  - 현재 소유한 링크를 삭제한 후 새 링크를 만듭니다.
  - `ntl::status`를 반환합니다.
- `close()`
  - 소유한 링크를 삭제하고 `IoDeleteSymbolicLink` 상태를 보고합니다.
- `reset()`
  - 소유한 링크를 삭제하고 teardown 실패를 무시합니다.
- `release()`
  - 심볼릭 링크를 삭제하지 않고 소유권을 분리합니다.
- `name()`
- `target_name()`
- `valid()` / `operator bool()`

## IRQL

`PASSIVE_LEVEL`의 드라이버 설정 및 teardown 코드에서 사용하십시오. 이 도우미는 `std::wstring` 상태를 소유하고 WDK 객체 관리자 루틴을 호출하므로 DPC 또는 ISR 도우미가 아닙니다.

## 참고

래퍼는 WDK 이름 지정 모델을 숨기지 않습니다. 링크에는 `\\DosDevices\\name`, 대상에는 `\\Device\\name`처럼 `IoCreateSymbolicLink`에 전달할 네이티브 이름을 전달하십시오.

일반적인 "장치를 만들고 DOS 장치 링크로 노출"하는 경우에는 [`ntl::device_endpoint`](./driver-device-irp.ko-KR.md#device-endpoint)를 사용하십시오. 이는 `device_options::name()`을 짧은 이름으로 유지하고 심볼릭 링크용 `\\Device\\...` 대상 경로를 만듭니다. 복사 가능한 소유 핸들은 하나의 idempotent close 상태를 공유하며 링크를 장치보다 먼저 해제합니다. 마지막 endpoint 핸들이 `PASSIVE_LEVEL`보다 높은 수준에서 해제되면 NTL은 최종 상태 정리를 결합된 런타임 worker로 미룹니다.
