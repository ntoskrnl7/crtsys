# NTL 장치 제어 패턴

[NTL 문서로 돌아가기](./README.ko-KR.md)

이 문서는 하위 수준 NTL 장치 제어 도우미를 실제 dispatch 본문에서 함께 사용하는 방법을 보여 줍니다. 목표는 WDK 모델을 숨기는 것이 아닙니다. IOCTL 번호, 입출력 바이트 수, remove-lock 수명과 MDL 매핑 규칙은 그대로 드러납니다.

## 패턴

고정 크기 buffered IOCTL에는 다음 형태를 사용하십시오.

1. dispatch 경로에 대한 `ntl::remove_lock` guard를 획득합니다.
2. 실행 시점의 `ntl::device_control::code`를 형식화된 `ntl::ioctl` descriptor와 비교합니다.
3. `ntl::ioctl_input_as`로 요청을 읽습니다.
4. 코드에 MDL 소유자가 필요하면 `ntl::mdl`로 커널 버퍼를 준비하거나 매핑합니다.
5. `ntl::ioctl_write_output`으로 응답을 씁니다.
6. `NTSTATUS`를 반환하고 `out_buffer::size`는 정확한 `IoStatus.Information` 바이트 수로 둡니다.

```cpp
struct ping_request {
  ULONG value;
};

struct ping_reply {
  ULONG value;
  ULONG checksum;
};

using ioctl_ping =
    ntl::ioctl<FILE_DEVICE_UNKNOWN, 0x812, METHOD_BUFFERED,
               FILE_READ_DATA | FILE_WRITE_DATA, ping_request, ping_reply>;

class device_state {
public:
  NTSTATUS on_device_control(const ntl::device_control::code& code,
                             const ntl::device_control::in_buffer& in,
                             ntl::device_control::out_buffer& out,
                             PIRP irp) noexcept {
    auto guard = remove_lock_.acquire(irp);
    if (!guard) {
      out.clear();
      return static_cast<NTSTATUS>(guard.status());
    }

    if (!ntl::is_ioctl<ioctl_ping>(code)) {
      out.clear();
      return STATUS_INVALID_DEVICE_REQUEST;
    }

    const auto* request = ntl::ioctl_input_as<ioctl_ping>(in);
    if (!request) {
      out.clear();
      return STATUS_INVALID_PARAMETER;
    }

    ping_reply reply{request->value + 1, request->value ^ 0xa5a5a5a5u};
    if (!ntl::ioctl_write_output<ioctl_ping>(out, reply)) {
      out.clear();
      return STATUS_BUFFER_TOO_SMALL;
    }

    return STATUS_SUCCESS;
  }

  void unload() noexcept {
    remove_lock_.release_and_wait(this);
  }

private:
  ntl::remove_lock remove_lock_{ntl::pool_tag("NTPp")};
};
```

`ntl::device` 콜백에서는 장치 상태를 캡처하고 handler를 호출합니다.

```cpp
device.on_device_control([state](const ntl::device_control::code& code,
                                 const ntl::device_control::in_buffer& in,
                                 ntl::device_control::out_buffer& out) {
  const NTSTATUS status = state->on_device_control(code, in, out, nullptr);
  if (!NT_SUCCESS(status)) {
    // Throwing ntl::exception lets the NTL dispatch invoker copy the status
    // into IRP->IoStatus.Status and complete the IRP.
    throw ntl::exception(status, "device-control request failed");
  }
});
```

hot 또는 pageable 경로에서는 네이티브 WDK 규칙이 우선하도록 두십시오. `ntl::mdl`은 `IoAllocateMdl` 저장소를 소유하고 reset/소멸 시 매핑 해제와 unlock을 수행하지만, 버퍼를 nonpaged로 둘지, probe-and-lock할지, `MmGetSystemAddressForMdlSafe`로 매핑할지는 호출자가 계속 선택합니다.

## 테스트된 범위

드라이버 테스트 모음은 이 패턴을 `ntl_device_control_pipeline_test`로 검사합니다. 성공, 알 수 없는 IOCTL, 짧은 입력, 짧은 출력, MDL 기반 scratch 저장소와 remove-lock 해제 이후의 거부를 포함합니다.
