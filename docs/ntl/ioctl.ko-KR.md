# NTL 형식화된 IOCTL 도우미

[NTL 문서로 돌아가기](./README.ko-KR.md)

헤더: [`include/ntl/ioctl`](../../include/ntl/ioctl)

`ntl::ioctl`은 `CTL_CODE` 값을 위한 작은 컴파일 타임 descriptor입니다. 네이티브 IOCTL 번호를 드러낸 채로, 해당 번호를 장치 제어 handler가 사용하는 요청과 응답 payload 형식에 연결합니다.

## 예제

```cpp
struct ping_request {
  ULONG value;
};

struct ping_reply {
  ULONG value;
};

struct ping_contract {
  using input_type = ping_request;
  using output_type = ping_reply;

  static constexpr ULONG device_type = FILE_DEVICE_UNKNOWN;
  static constexpr ULONG function = 0x801;
  static constexpr ULONG method = METHOD_BUFFERED;
  static constexpr ULONG access = FILE_READ_DATA | FILE_WRITE_DATA;
};

using ioctl_ping = ntl::ioctl_from_contract<ping_contract>;

device.on_device_control([](const ntl::device_control::code& code,
                            const ntl::device_control::in_buffer& in,
                            ntl::device_control::out_buffer& out) {
  if (!ntl::is_ioctl<ioctl_ping>(code)) {
    out.clear();
    return;
  }

  const auto* request = ntl::ioctl_input_as<ioctl_ping>(in);
  if (!request) {
    out.clear();
    return;
  }

  const ping_reply reply{request->value + 1};
  if (!ntl::ioctl_write_output<ioctl_ping>(out, reply)) {
    out.clear();
  }
});
```

## API 요약

- `ntl::ioctl<DeviceType, Function, Method, Access, Input, Output>`
- `ntl::ioctl_from_contract<Contract>`
- `Ioctl::code`
- `Ioctl::control_code()`
- `ntl::is_ioctl<Ioctl>(code)`
- `ntl::ioctl_input_as<Ioctl>(in_buffer)`
- `ntl::ioctl_write_output<Ioctl>(out_buffer, value)`

입력과 출력 도우미 함수에는 trivially copyable payload 형식이 필요합니다. 해당 방향에 형식화된 payload가 없는 IOCTL은 입력 또는 출력 형식으로 `void`를 사용하십시오.

`ioctl_from_contract`는 app/driver가 공유하는 계약 헤더를 위한 기능입니다. 원시 `CTL_CODE` 필드와 payload 형식을 하나의 계약 형식에 두고, 드라이버에서 그 형식으로 NTL descriptor를 파생시키십시오.

## IRQL

도우미 자체는 형식과 버퍼 연산만 수행합니다. IRP를 제공한 dispatch 경로의 실행 문맥을 따르며, 실제 IRQL 계약은 콜백 본문에서 계속 제어합니다.

## 드라이버 테스트 범위

드라이버 테스트 모음은 다음을 검사합니다.

- 컴파일 타임 `CTL_CODE` 일치
- 공유 계약에서 형식화된 descriptor로의 매핑
- 실행 시 코드 일치 및 불일치
- 형식화된 입력 뷰
- 형식화된 출력 쓰기
- 짧은 출력 버퍼 거부
