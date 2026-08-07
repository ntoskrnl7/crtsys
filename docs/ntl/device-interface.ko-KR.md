# NTL 장치 인터페이스

[NTL 문서로 돌아가기](./README.ko-KR.md)

헤더: [`include/ntl/device_interface`](../../include/ntl/device_interface)

`ntl::device_interface_link`는 `IoRegisterDeviceInterface`가 반환한 심볼릭 링크를 소유합니다. 이는 PnP 장치 인터페이스용이며 이름이 지정된 레거시 제어 장치용이 아닙니다. `\\Device\\name`과 `\\DosDevices\\name` 쌍에는 [`ntl::device_endpoint`](./driver-device-irp.ko-KR.md#device-endpoint)를 사용하십시오.

## 예제

```cpp
auto iface = ntl::try_register_device_interface(
    physical_device_object,
    GUID_DEVINTERFACE_DEMO);
if (!iface) {
  return iface.status();
}

auto status = iface->enable();
if (!status) {
  return status;
}
```

`physical_device_object`는 `IoRegisterDeviceInterface`가 허용하는 PDO여야 합니다. 임의의 제어 장치 객체를 전달하는 것은 올바른 PnP 장치 인터페이스 사용법이 아닙니다.

## API 요약

- `ntl::try_register_device_interface(pdo, guid, reference_string)`
- `device_interface_link::enable()`
- `device_interface_link::disable()`
- `device_interface_link::set_enabled(bool)`
- `device_interface_link::native_name()`
- `device_interface_link::name()`
- `device_interface_link::release()`
- `device_interface_link::reset()`

`device_interface_link`는 소멸자에서 활성화된 인터페이스를 비활성화하고 네이티브 심볼릭 링크 문자열을 해제합니다.

## IRQL

등록과 상태 변경은 `PASSIVE_LEVEL` PnP/제어 경로 작업으로 취급하십시오.

## 드라이버 테스트 범위

일반 드라이버 테스트 모음은 가짜 PDO를 만들지 않으며 빈 소유자의 안전 경로만 검사합니다. 실제 등록은 유효한 물리 장치 객체를 소유한 PnP 드라이버에서 테스트해야 합니다.
