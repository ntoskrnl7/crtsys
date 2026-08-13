# 파일 객체 뷰

[NTL 문서로 돌아가기](./README.ko-KR.md)

`ntl::file`은 커널 `PFILE_OBJECT`의 비소유 C++ 뷰입니다. 경로를 열거나, `HANDLE`을 닫거나, 객체 관리자 참조를 획득하지 않으며 `std::fstream`과 `std::filesystem`을 대체하지도 않습니다.

I/O 관리자 또는 프레임워크 콜백이 이미 유효한 `FILE_OBJECT`를 제공하고 드라이버에서 공통 필드의 타입이 지정된 뷰가 필요할 때 사용하십시오.

```cpp
void inspect(PFILE_OBJECT native_file) noexcept {
  ntl::file file{native_file};

  const std::wstring_view name = file.name();
  const bool readable = file.can_read();
  const bool writable = file.can_write();
  PDEVICE_OBJECT target = file.device_object();
}
```

이 뷰는 호출자의 네이티브 `FILE_OBJECT` 계약이 유효한 동안에만 유효합니다. 코드가 객체 관리자 참조를 명시적으로 소유한다면 이를 `ntl::unique_object<PFILE_OBJECT>`에 저장하고, 검사하는 동안 `owner.get()`으로 임시 `ntl::file`을 생성하십시오.

## KMDF 브리지

`ntl::kmdf::file`은 `WDFFILEOBJECT`를 관찰합니다. WDF 수준의 파일 이름, 장치와 C++ 문맥에 접근할 수 있으며, `wdm()`은 대응하는 `ntl::file`을 반환합니다.

```cpp
constexpr auto on_file_create =
    +[](ntl::kmdf::device, ntl::kmdf::request request,
        ntl::kmdf::file file) noexcept {
      const ntl::file native_file = file.wdm();
      DbgPrint("read=%u write=%u\n", native_file.can_read(),
               native_file.can_write());
      request.complete(STATUS_SUCCESS);
    };

ntl::kmdf::file_config<> files;
files.on_create<on_file_create>();
```

어느 뷰도 WDF 또는 WDM 객체를 소유하지 않습니다. 네이티브 API와 연동할 때는 `native_object()`를 사용할 수 있습니다.

## 올바른 형식 선택

| 필요한 기능 | 형식 |
| --- | --- |
| 콜백이 제공한 `PFILE_OBJECT` 관찰 | `ntl::file` |
| 콜백이 제공한 `WDFFILEOBJECT` 관찰 | `ntl::kmdf::file` |
| 객체 관리자 참조 소유 | `ntl::unique_object<PFILE_OBJECT>` |
| `ZwCreateFile` 핸들 소유 | `ntl::unique_handle` |
| 표준 C++ 스트림/파일 시스템 연산 수행 | `std::fstream`, `std::filesystem` |
