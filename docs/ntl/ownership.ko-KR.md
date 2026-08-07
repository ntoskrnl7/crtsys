# NTL 핸들 및 객체 소유권

[NTL 문서로 돌아가기](./README.ko-KR.md)

NTL은 두 빌드 모드에서 네이티브 핸들의 소유권에 같은 `ntl::unique_handle` 이름을
사용합니다.

- 사용자 모드 빌드에서 `ntl::unique_handle`는 `CloseHandle`로 핸들을 닫습니다.
- 커널 빌드에서 `ntl::unique_handle`는 `ZwClose`로 핸들을 닫습니다.
- `ntl::unique_object<Pointer>`는 객체 관리자 참조를 소유하며
  `ObDereferenceObject`로 해제합니다.

앱 쪽 코드와 드라이버 쪽 코드 모두 네이티브 `HANDLE` 값을 사용하므로 이름은
공유하지만, 실제 닫기 함수는 빌드 환경에 따라 선택됩니다. 참조를 획득한 객체
포인터는 그 수명이 핸들 소유권과 다르므로 별도로 다룹니다.

헤더: [`include/ntl/handle`](../../include/ntl/handle)

## 사용자 모드 핸들

`CreateFileW`로 드라이버 장치를 열거나 `DeviceIoControl`을 호출하는 등,
`CloseHandle`로 닫아야 하는 핸들을 반환하는 Win32 API를 사용하는 사용자 모드
동반 앱에서는 `ntl::unique_handle`를 사용합니다.

```cpp
ntl::unique_handle device{
    CreateFileW(LR"(\\.\MyDevice)",
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr)};
if (!device) {
  return GetLastError();
}

// device closes automatically with CloseHandle.
```

API:

- `unique_handle()`
- `unique_handle(HANDLE handle)`
- 이동 생성 / 이동 대입
- `get()`
- `put()`
  - 현재 소유권을 비우고 Win32 출력 매개 변수용 `HANDLE*`를 반환합니다.
- `close()`
  - `CloseHandle`로 닫고 성공하면 `true`를 반환합니다.
- `reset(handle = nullptr)`
  - 기존 핸들을 닫고, 닫기 실패는 무시하며, 필요하면 다른 핸들을 인계받습니다.
- `release()`
  - 닫지 않고 소유권을 분리합니다.
- `valid()` / `operator bool()`

## 커널 핸들

Zw/Nt 계열 커널 API가 반환한 핸들을 `ZwClose`로 닫아야 할 때는
`ntl::unique_handle`를 사용합니다.

```cpp
ntl::unique_handle event_handle;
auto status = ZwCreateEvent(event_handle.put(),
                            EVENT_MODIFY_STATE | SYNCHRONIZE,
                            nullptr,
                            NotificationEvent,
                            FALSE);
if (!NT_SUCCESS(status)) {
  return ntl::status{status};
}

// event_handle closes automatically with ZwClose.
```

API:

- `unique_handle()`
- `unique_handle(HANDLE handle)`
- 이동 생성 / 이동 대입
- `get()`
- `put()`
  - 현재 소유권을 비우고 WDK 출력 매개 변수용 `PHANDLE`을 반환합니다.
- `close()`
  - `ZwClose`로 닫고 `ntl::status`를 반환합니다.
- `reset(handle = nullptr)`
  - 기존 핸들을 닫고, 닫기 실패는 무시하며, 필요하면 다른 핸들을 인계받습니다.
- `release()`
  - 닫지 않고 소유권을 분리합니다.
- `valid()` / `operator bool()`

드라이버가 소유하는 커널 핸들은 `OBJ_KERNEL_HANDLE`로 만든 것을 우선 사용하세요.
핸들이 사용자 모드에서 왔다면, 먼저 드라이버가 그 핸들을 실제로 소유하는지
판단해야 합니다. 많은 IOCTL 경로는 사용자가 준 핸들을 닫는 대신
`ObReferenceObjectByHandle`로 객체를 참조하고, 그 객체 참조를
`ntl::unique_object<Pointer>`로 관리해야 합니다.

## 객체 참조

`ObReferenceObjectByHandle` 같은 객체 관리자 참조 API가 반환한 포인터에는
`ntl::unique_object<Pointer>`를 사용합니다.

```cpp
auto event = ntl::try_reference_object_by_handle<PKEVENT>(
    event_handle.get(),
    EVENT_MODIFY_STATE,
    *ExEventObjectType);
if (!event) {
  return event.status();
}

KeSetEvent(event->get(), IO_NO_INCREMENT, FALSE);

// The referenced event object is released with ObDereferenceObject.
```

API:

- `unique_object<Pointer>()`
- `unique_object<Pointer>(Pointer object)`
- 이동 생성 / 이동 대입
- `get()`
- `reset(object = nullptr)`
  - 기존 객체 참조를 `ObDereferenceObject`로 해제합니다.
- `release()`
  - 역참조하지 않고 소유권을 분리합니다.
- `valid()` / `operator bool()`
- void가 아닌 포인티드 형식에 대한 `operator->()` 및 `operator*()`

`Pointer`는 `PFILE_OBJECT`, `PDEVICE_OBJECT`, `PKEVENT` 같은 네이티브 WDK
포인터 형식입니다.

## 팩터리 도우미

`try_reference_object_by_handle<Pointer>(...)`는 `ObReferenceObjectByHandle`를
감싸며 `ntl::result<ntl::unique_object<Pointer>>`를 반환합니다.

```cpp
auto object = ntl::try_reference_object_by_handle<PFILE_OBJECT>(
    file_handle.get(),
    FILE_READ_DATA,
    *IoFileObjectType);
if (!object) {
  return object.status();
}
```

## IRQL

정확한 WDK 프리미티브가 더 넓은 계약을 문서화하지 않는 한, 이 도우미들은
`PASSIVE_LEVEL`에서 사용하세요. `ZwClose`와 `ObReferenceObjectByHandle`는
DPC/ISR용 도우미가 아니라 제어 경로 객체 관리자 작업입니다.

## 참고

`HANDLE` 값에는 `unique_object`를 사용하지 말고, 객체 관리자 참조 포인터에는
`unique_handle`를 사용하지 마세요. 래퍼는 이 두 소유권 모델을 의도적으로
분리합니다.
