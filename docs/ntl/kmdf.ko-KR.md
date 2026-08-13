# KMDF 도우미

[NTL 문서로 돌아가기](./README.ko-KR.md)

NTL의 KMDF 지원은 일반 KMDF 객체 모델 위에 선택적으로 사용하는 C++ 계층입니다.
WDF의 디스패치, PnP, 전원, 큐, 요청, 부모-자식 수명 또는 동기화 규칙을 대체하지
않습니다.

설계와 검토에는
[KMDF 드라이버 엔지니어링 체크리스트](./kmdf-driver-checklist.ko-KR.md)를
사용하십시오. 소유권 이전, 취소 상태 머신, PnP/전원 순서, ABI 규칙 및 출시
게이트를 한 문서에 정리했습니다.

## 진입점 모델 선택

Visual Studio/MSBuild 프로젝트에서는 `<DriverType>KMDF</DriverType>`를 유지하고
다음 속성으로 NTL을 선택합니다.

```xml
<CrtSysUseNtlKmdfMain>true</CrtSysUseNtlKmdfMain>
```

NuGet 패키지를 사용한다면 **프로젝트 속성 > 드라이버 설정 > 드라이버 모델**에서
**드라이버 유형 = KMDF**로 설정하십시오. 그러면 **crtsys KMDF 진입점** 항목이
나타납니다. `ntl::kmdf::main`을 사용하려면 **NTL KMDF**를, 일반 KMDF 진입 경로를
유지하려면 **NTL 진입점 없음**을 선택합니다.

`ExportDriver`는 별도의 WDK export-driver 모델이며 일반 WDM 드라이버 진입점 모델이
아닙니다. export-driver 프로젝트에는 NTL 진입점을 선택하지 마십시오.

CMake 프로젝트에서는 KMDF 드라이버 선언에 `NTL`을 추가합니다.

```cmake
crtsys_add_driver(my_driver KMDF 1.15 NTL src/main.cpp)
```

이 옵션이 없으면 KMDF 프로젝트는 표준 `DriverEntry`를 유지하고
`WdfDriverCreate`를 직접 호출합니다.

## 드라이버 진입점

NTL 진입점에는 네이티브 `DriverEntry` 인수에 연결된 `driver_builder`가 전달됩니다.
`try_create()`는 `WdfDriverCreate`가 반환한 `NTSTATUS`를 그대로 보존합니다.

```cpp
#include <ntl/kmdf/all>

constexpr auto on_driver_unload =
    +[](kmdf::driver) noexcept {};

ntl::status ntl::kmdf::main(driver_builder& builder,
                            const std::wstring& registry_path) {
  (void)registry_path;

  kmdf::driver_config config;
  config.non_pnp().on_unload<on_driver_unload>();

  auto driver = builder.try_create(config);
  return driver ? ntl::status::ok() : driver.status();
}
```

`driver`, `device`, `file`, `io_queue`, `io_target`, `request`, `memory`,
`dma_enabler`, `common_buffer`, `dma_transaction`, `interrupt`, `timer`,
`work_item` 및 `child_list`는 비소유 래퍼입니다.
WDF는 네이티브 객체 소유권을 유지하고 구성된 부모-자식 객체 수명 규칙을
적용합니다. `registry_key`와 `driver_request`는 이동 전용 소유자입니다.
`registry_key`는 `WDFKEY`를 닫고, `driver_request`는 아직 전송하지 않은 드라이버
생성 `WDFREQUEST`를 삭제합니다.

## 플러그 앤 플레이 장치

`EvtDriverDeviceAdd`에는 프레임워크가 소유하는 `PWDFDEVICE_INIT`이 전달됩니다.
`device_init`은 의도적으로 비소유이며 `WdfDeviceInitFree`를 호출하지 않습니다.
`try_create()`가 성공하면 `WdfDeviceCreate`가 초기화 객체를 소비합니다. 장치를 만들기
전에 콜백이 반환되면 콜백 반환 후 KMDF가 해당 객체를 삭제합니다.

```cpp
constexpr auto on_prepare_hardware =
    +[](kmdf::device, kmdf::resource_list,
        kmdf::resource_list) noexcept -> NTSTATUS {
      return STATUS_SUCCESS;
    };

constexpr auto on_release_hardware =
    +[](kmdf::device,
        kmdf::resource_list) noexcept -> NTSTATUS {
      return STATUS_SUCCESS;
    };

constexpr auto on_d0_entry =
    +[](kmdf::device,
        WDF_POWER_DEVICE_STATE) noexcept -> NTSTATUS {
      return STATUS_SUCCESS;
    };

constexpr auto on_d0_exit =
    +[](kmdf::device,
        WDF_POWER_DEVICE_STATE) noexcept -> NTSTATUS {
      return STATUS_SUCCESS;
    };

constexpr auto on_device_control =
    +[](kmdf::io_queue, kmdf::request request, size_t, size_t,
        ULONG) noexcept {
      request.complete(STATUS_NOT_SUPPORTED);
    };

constexpr auto on_device_add =
    +[](kmdf::driver, kmdf::device_init& init) noexcept -> NTSTATUS {
      ntl::kmdf::pnp_power_callbacks callbacks;
      callbacks
          .on_prepare_hardware<on_prepare_hardware>()
          .on_release_hardware<on_release_hardware>()
          .on_d0_entry<on_d0_entry>()
          .on_d0_exit<on_d0_exit>();

      init.io_type(WdfDeviceIoBuffered).pnp_power(callbacks);

      ntl::kmdf::object_attributes attributes;
      attributes.execution_level(WdfExecutionLevelPassive);
      auto device = init.try_create(&attributes);
      if (!device)
        return device.status();

      ntl::status status =
          device.value().try_create_interface(GUID_DEVINTERFACE_MY_DEVICE);
      if (status.is_err())
        return status;

      ntl::kmdf::io_queue_config queue(WdfIoQueueDispatchSequential);
      queue.on_device_control<on_device_control>();
      queue.power_managed(WdfTrue);
      auto created_queue =
          ntl::kmdf::io_queue::try_create(device.value(), queue);
      return created_queue ? STATUS_SUCCESS : created_queue.status();
    };

ntl::status ntl::kmdf::main(driver_builder& builder,
                            const std::wstring&) {
  ntl::kmdf::driver_config config;
  config.on_device_add<on_device_add>();
  auto driver = builder.try_create(config);
  return driver ? ntl::status::ok() : driver.status();
}
```

단항 `+`는 캡처 없는 각 람다를 컴파일 타임 함수 포인터로 변환합니다. 포인터에
`constexpr` 이름을 붙여 템플릿 인수로 전달하면 Visual Studio
IntelliSense가 인라인 람다 템플릿 인수를 잘못 해석하는 문제도 피할 수 있습니다.
NTL은 네이티브 핸들을 비소유 `driver`, `device`, `io_queue`, `request` 및
`resource_list` 래퍼로 변환하는 무할당 WDF thunk를 설치합니다. 콜백은
`noexcept`여야 합니다. WDF 콜백은 C++ 람다 클로저를 보관할 수 없으므로 지속
상태는 WDF 객체 컨텍스트에 두어야 합니다.

`pnp_power_callbacks`는 하드웨어 준비/해제, D0 전환, 자체 관리 I/O, query-stop,
query-remove 및 surprise removal을 지원합니다. `power_policy_callbacks`는 S0/Sx
깨우기 정책 콜백을 지원합니다. 두 형식 모두 네이티브 프레임워크를 숨기지 않고,
덜 일반적인 WDF 필드에 접근할 수 있도록 `native()`를 제공합니다.

`device` 래퍼는 별도의 상태 머신을 만들지 않고도 동작 상태를 제공합니다.
`pnp_state()`, `power_state()`, `power_policy_state()`,
`system_power_action()`, `state()`, `set_failed()` 및
`indicate_wake_status()`를 사용할 수 있습니다. `default_queue()`와
`try_route_requests()`는 기본 및 형식별 디스패치를 지원합니다.
`device_idle_reference::try_acquire()`는 `WdfDeviceStopIdleNoTrack`과
`WdfDeviceResumeIdleNoTrack`을 이동 전용 RAII 방식으로 짝지어 호출합니다. D0
상태가 되기를 기다리려면 `PASSIVE_LEVEL`이 필요합니다.

## 하드웨어 리소스

`resource_list`는 KMDF가 전달한 리소스 목록을 순회하는 비소유 뷰입니다. 각 원소는
`resource_descriptor`입니다. `memory()`, `port()`, `interrupt()`, `dma()`,
`connection()`은 설명자 형식이 일치할 때만 타입이 지정된 값을 반환합니다. 그 밖의
리소스 형식에는 `native()`를 사용할 수 있습니다. 큰 메모리 설명자는 압축된 길이
필드를 직접 해석하지 않고 WDK의 `RtlCmDecodeMemIoResource` 도우미로 디코드합니다.

```cpp
constexpr auto on_prepare_hardware =
    +[](kmdf::device device, kmdf::resource_list raw,
        kmdf::resource_list translated) noexcept -> NTSTATUS {
      (void)raw;

      for (const kmdf::resource_descriptor resource : translated) {
        const auto memory = resource.memory();
        if (!memory)
          continue;

        auto* registers = MmMapIoSpace(memory->start,
                                       static_cast<SIZE_T>(memory->length),
                                       MmNonCached);
        if (!registers)
          return STATUS_INSUFFICIENT_RESOURCES;
        device.context<device_state>().registers = registers;
        return STATUS_SUCCESS;
      }
      return STATUS_DEVICE_CONFIGURATION_ERROR;
    };
```

원시 목록에는 버스 기준 리소스가 들어 있고, 변환된 목록에는 드라이버의 하드웨어
접근 경로에 맞는 시스템 주소, 인터럽트 벡터 및 affinity가 들어 있습니다.
`resource_origin`은 interrupt descriptor로 전달되므로 message-signaled interrupt
필드는 올바른 네이티브 공용체 멤버에서 디코드됩니다. 리소스 래퍼는 WDF 목록의
수명을 소유하거나 연장하지 않으며, `EvtDevicePrepareHardware`부터
`EvtDeviceReleaseHardware`가 반환할 때까지만 유효합니다.

## 유휴 및 절전 모드 해제 정책

`idle_policy`는 WDF 초기화 함수를 사용해 `WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS`를
초기화하고 S0에서의 런타임 유휴 동작을 구성합니다. `wake_policy`는
`WDF_DEVICE_POWER_POLICY_WAKE_SETTINGS`를 구성해 Sx에서 깨우는 정책을 지정합니다.
두 형식 모두 `try_apply()`에서 WDF의 정확한 `NTSTATUS`를 반환합니다.

```cpp
ntl::kmdf::idle_policy idle(IdleCannotWakeFromS0);
idle.timeout(10'000, DriverManagedIdleTimeout)
    .user_control(IdleDoNotAllowUserControl)
    .enabled(true)
    .exclude_d3_cold(WdfUseDefault);

ntl::status status = idle.try_apply(device);
if (status.is_err())
  return status;

ntl::kmdf::wake_policy wake;
wake.device_state(PowerDeviceMaximum)
    .user_control(WakeDoNotAllowUserControl)
    .enabled(true);

status = wake.try_apply(device);
if (status.is_err())
  return status;
```

이 설정은 `WdfDeviceCreate` 이후, 드라이버가 장치 전원 정책을 소유할 때만
적용하십시오. 래퍼는 최대 `DISPATCH_LEVEL`까지 허용하는 네이티브 WDF 계약을
그대로 따릅니다. 이 예제에서는 이미 `PASSIVE_LEVEL`에서 실행 중인
`EvtDeviceAdd`에서 설정합니다. 특정 상태에서 실제로 깨울 수 있는지는 여전히
하드웨어, 버스, 펌웨어 및 INF 계약에 달려 있습니다.

## C++ 객체 컨텍스트

NTL은 WDF가 소유하는 컨텍스트 저장소에 C++ 객체를 직접 생성할 수 있습니다. 이는
`ntl::device<T>`가 관리하는 확장 객체 수명에 대응하는 KMDF 방식입니다. WDF 객체를
성공적으로 만든 뒤 C++ 객체를 생성하고, 컨텍스트 저장소가 해제되기 전에 WDF destroy
콜백에서 소멸자를 실행합니다.

```cpp
struct device_state {
  std::vector<std::uint32_t> completed_values;
  std::atomic<std::uint32_t> open_files{0};

  ~device_state() noexcept = default;
};

ntl::kmdf::object_attributes attributes;
attributes.execution_level(WdfExecutionLevelPassive);

auto created = init.try_create<device_state>(&attributes);
if (!created)
  return created.status();

auto& state = created->context<device_state>();
```

컨텍스트 생성자와 소멸자는 `noexcept`여야 합니다. WDF는 C++ 형식마다 컨텍스트
하나를 저장합니다. `object::try_emplace_context<T>()`를 사용하면 객체 생성 후
추가 형식의 컨텍스트를 붙일 수 있습니다. 해당 형식이 없으면
`try_context<T>()`는 null을 반환하고, `context<T>()`는 형식이 존재한다고
어설션합니다. NTL이 관리하는 C++ 수명을 사용하지 않는 경우에는 네이티브
`WDF_DECLARE_CONTEXT_TYPE*` 컨텍스트도 `object_attributes::context_type()`을 통해
계속 사용할 수 있습니다.

## 제어 장치 및 대기열

`control_device_init`는 `try_create()`가 소비할 때까지 `PWDFDEVICE_INIT`를 소유합니다.
그 전에 설정이 실패하면 소멸자가 `WdfDeviceInitFree`를 호출합니다.
`object_attributes`, `driver_config`, `io_queue_config`는 타입이 지정된 콜백을 설치하면서,
NTL이 감싸지 않은 필드를 위해 해당 WDF 구성 구조의 `native()` 접근도 유지합니다.

```cpp
#include <wdmsec.h>

auto init = ntl::kmdf::control_device_init::try_allocate(
    driver, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R);
if (!init)
  return init.status();

auto device = init.value().try_create();
if (!device)
  return device.status();

ntl::kmdf::io_queue_config queue_config(WdfIoQueueDispatchSequential);
queue_config.on_device_control<on_device_control>();

auto queue = ntl::kmdf::io_queue::try_create(device.value(), queue_config);
if (!queue)
  return queue.status();
```

애플리케이션, 타입이 지정된 요청 버퍼, STL 사용, 심볼릭 링크 생성 및 큐 콜백을
포함한 전체 non-PnP 제어 장치 흐름은
[NTL KMDF 샘플](../../examples/kmdf/basic)을 참고하십시오.

## 파일 객체

`ntl::kmdf::file`은 `WDFFILEOBJECT`를 관찰합니다. `wdm()` 메서드는
하위 `PFILE_OBJECT`의 `ntl::file` 뷰를 반환합니다. 어느 뷰도 객체를
소유하거나 역참조하지 않습니다. 객체 관리자 참조를 소유해야 한다면
`ntl::unique_object<PFILE_OBJECT>`를 사용하고, `ZwCreateFile`로 연 파일에는
핸들 소유자를 사용하십시오.

```cpp
struct file_state {
  std::wstring name;
  bool cleaned_up = false;
  ~file_state() noexcept = default;
};

constexpr auto on_file_create =
    +[](kmdf::device device, kmdf::request request,
        kmdf::file file) noexcept {
      try {
        file.context<file_state>().name.assign(file.name());
        ++device.context<device_state>().open_files;
        request.complete(STATUS_SUCCESS);
      } catch (...) {
        request.complete(STATUS_INSUFFICIENT_RESOURCES);
      }
    };

constexpr auto on_file_cleanup = +[](kmdf::file file) noexcept {
  file.context<file_state>().cleaned_up = true;
};

constexpr auto on_file_close = +[](kmdf::file file) noexcept {
  NT_ASSERT(file.context<file_state>().cleaned_up);
};

ntl::kmdf::file_config<file_state> files;
files
    .on_create<on_file_create>()
    .on_cleanup<on_file_cleanup>()
    .on_close<on_file_close>();

init.file_objects(files);
```

파일 컨텍스트는 형식 지정 create 콜백 전에 생성되고, 네이티브 cleanup/close
절차가 끝난 뒤 `WDFFILEOBJECT`와 함께 삭제됩니다. NTL이 관리하는 파일 컨텍스트의
정리 동작은 `object_attributes::on_cleanup()`이나 `on_destroy()`와 섞지 말고
`file_config`에서 구성하십시오.

## 요청 버퍼

`request::try_input_buffer<T>()`와 `try_output_buffer<T>()`는 각각 대응하는 KMDF
버퍼 조회 API를 호출하고 `ntl::result<request_buffer<T>>`를 반환합니다.
`request_buffer<T>`는 하위 WDF 요청의 버퍼 수명 계약 안에서만 유효한 비소유
뷰입니다.

요청 래퍼는 프레임워크 메모리 객체와 MDL도 노출합니다.
`try_input_memory()`, `try_output_memory()`, `try_input_mdl()`,
`try_output_mdl()`을 사용할 수 있습니다. 이름에 `try_unsafe_user_*()`가 명시된
함수는 `EvtIoInCallerContext` 전용입니다. 사용자 주소를 보관하기 전에 검증하거나
잠그십시오. `try_lock_user_buffer_for_read()` 및
`try_lock_user_buffer_for_write()`는 그 결과인 WDF 메모리 객체를 반환합니다.

`parameters()`, `requestor_mode()`, `is_from_32bit_process()`,
`associated_queue()`, `wdm_irp()`는 WDF의 요청 상태를 따로 복제하지 않으면서
요청 진단과 명시적 WDM 상호 운용 기능을 제공합니다.

KMDF 콜백에는 요청을 완료·전달·재큐잉·전송할 권한이 하나뿐이므로 `request`는 이동
전용입니다. `request::try_forward_to()` 및
`try_requeue()`는 rvalue 한정이며 WDF가 이전을 받아들이면 요청 래퍼를 비웁니다.

```cpp
auto status = std::move(request).try_forward_to(destination);
```

`try_mark_cancelable()`과 `try_unmark_cancelable()`은 KMDF의 네이티브 취소 경합
규칙을 보존합니다. 취소 가능 표시를 해제할 때 `STATUS_CANCELLED`가 반환되면 취소
콜백이 완료 책임을 소유합니다.

## 수동 대기열 및 취소

하드웨어, 데이터 또는 다른 요청을 기다려야 한다면 기본 큐와 별도로
`WdfIoQueueDispatchManual` 큐를 만드십시오. `try_retrieve_next()`와
`try_retrieve_for()`는 큐에서 요청을 제거하고 이동 전용 `request`를 반환하여,
호출자에게 요청을 처리하고 완료할 권한을 이전합니다.

```cpp
ntl::kmdf::io_queue_config pending_config(
    WdfIoQueueDispatchManual, false);
pending_config.on_canceled<on_canceled>();

auto pending = ntl::kmdf::io_queue::try_create(
    device, pending_config, &passive_attributes);

auto waiting = pending->try_retrieve_for(release.associated_file());
if (!waiting)
  return waiting.status();

waiting->complete(STATUS_SUCCESS);
```

`try_find()`는 요청을 제거하지 않고 검사할 수 있게 합니다. KMDF가 찾은 요청의
객체 참조를 증가시키지만 이것은 드라이버 요청 소유권을 부여하지 않습니다. 따라서
NTL은 자동으로 역참조하는 이동 전용 `found_request`를 반환합니다. 소유권으로
원자적으로 전환하려면 이를 `try_retrieve()`에 전달하십시오.
`STATUS_NOT_FOUND`는 취소 처리나 다른 소비자가 먼저 요청을 제거했다는 뜻입니다.
`request_parameters`는 찾은 요청의 네이티브 매개변수를 받을 초기화된 저장소를
제공합니다.

요청이 WDF 큐에 남아 있는 동안에는 `try_mark_cancelable()`을 호출하지 마십시오.
큐에 있는 요청의 취소는 프레임워크가 담당합니다. `on_canceled()` 큐 콜백은
취소된 요청을 받아 완료해야 합니다. 드라이버가 요청을 꺼내 보관한 뒤에는 취소
가능으로 표시할 수 있습니다. 취소 콜백 밖에서 드라이버 소유 요청을 완료하기
전에는 `try_unmark_cancelable()`을 호출하십시오. 결과가 `STATUS_CANCELLED`이면
취소 콜백만 해당 요청을 완료할 권한이 있습니다.

타입이 지정된 요청을 다른 장치 스택으로 보내려면 무할당 완료 콜백을 등록한 뒤
`io_target`으로 전송합니다.

```cpp
auto target = device.default_io_target();
ntl::kmdf::send_options options;
options.timeout(WDF_REL_TIMEOUT_IN_MS(1000));

constexpr auto on_request_completion =
    +[](kmdf::request request, kmdf::io_target,
        kmdf::completion_params result, void*) noexcept {
      request.complete(result.status(), result.information());
    };
request.on_completion<on_request_completion>();

auto status = std::move(request).try_send(target, &options);
if (status.is_err()) {
  // WDF did not accept the send; this code still controls request.
  request.complete(status);
}
```

전송에 성공하면 완료 콜백이 끝날 때까지 제어권이 WDF로 넘어갑니다. 전송에
실패하면 원래 요청 래퍼를 계속 사용할 수 있습니다. `send_options`는 비동기
전송과 동기 대기를 모두 지원합니다. 성공한 비동기 전송은 원본 `request`를
비우지만, 동기 전송은 이를 유지하므로 하위 대상 작업이 끝난 뒤 호출자가 원래 큐
요청을 완료할 수 있습니다. 전송 후 잊기 방식은 소유권 계약이 다른 네이티브 WDF
탈출구로 남겨 둡니다.

## 큐 forward progress

`forward_progress_policy`는 일반 요청 할당이 실패해도 필수 I/O가 큐에 도달할 수
있도록 프레임워크 요청을 예비 할당합니다. 큐를 생성한 뒤 장치가 I/O 처리를 시작하기
전에 정책을 할당하십시오.

```cpp
constexpr auto prepare_reserved_request =
    +[](kmdf::io_queue,
        kmdf::reserved_request_resources resources) noexcept -> NTSTATUS {
      configure_reserved_storage(resources.as_object());
      return STATUS_SUCCESS;
    };

constexpr auto prepare_each_request =
    +[](kmdf::io_queue,
        kmdf::request_resources resources) noexcept -> NTSTATUS {
      configure_request_storage(resources.as_object());
      return STATUS_SUCCESS;
    };

auto policy = kmdf::forward_progress_policy::always_reserved(2);
policy.prepare_reserved_requests<prepare_reserved_request>()
    .prepare_each_request<prepare_each_request>();

auto status = policy.try_assign(queue);
if (status.is_err())
  return status;
```

`always_reserved()`는 일반적인 안정성 정책입니다. `paging_io()`는 KMDF의 paging
I/O 정책을 선택하고, `examine<Callback>()`은 DISPATCH_LEVEL-safe 콜백이 각 IRP를
실패시킬지 예비 요청을 사용할지 정하게 합니다. `prepare_reserved_requests()`는 모든
예비 요청을 한 번씩 준비하며 `reserved_request_resources`를 받습니다. 이 제한된
형식이 수명 계약을 보장합니다. 이와 달리 KMDF는 예비 요청이 아닌 일반 요청을 포함해
새 프레임워크 요청을 만들 때마다, 큐에 넣기 전에 `prepare_each_request()`를
호출합니다. 이 콜백이 실패를 반환하면 요청을 거부합니다.

두 콜백은 일반 `request`가 아니라 제한된 리소스 준비 뷰를 받습니다. 따라서 아직
I/O 큐에 들어가지 않은 요청을 실수로 완료·전달·재큐잉할 수 없습니다. 콜백은
`DISPATCH_LEVEL`에서 실행될 수 있으므로 PASSIVE_LEVEL 전용 CRT/STL API를 호출하면
안 됩니다.

## WDF 메모리 및 드라이버 생성 요청

`memory`는 `WDFMEMORY`를 감쌉니다. `try_allocate()`는 WDF 소유 저장소를 만들고
`try_preallocated()`는 호출자 소유 저장소를 감쌉니다. 래퍼는 비소유이므로 수명이
긴 메모리에는 명시적인 WDF 부모를 지정해야 합니다. 사전 할당 버퍼는 이를 참조하는
WDF 메모리 객체보다 오래 살아 있어야 합니다.

```cpp
ntl::kmdf::object_attributes attributes;
attributes.parent(device);

auto transfer = ntl::kmdf::memory::try_allocate(
    4096, NonPagedPoolNx, "Xfer", &attributes);
if (!transfer)
  return transfer.status();

std::array<std::byte, 16> header{};
auto status = transfer->try_copy_from(0, header.data(), header.size());
```

`request`는 WDF 큐에서 전달되어 완료하거나 다른 곳으로 전달할 요청을 나타냅니다.
반면 `driver_request`는 드라이버가 `WdfRequestCreate`로 만든 요청을 소유합니다.
전송하지 않은 `driver_request`가 소멸하면 `WdfObjectDelete`를 호출합니다.
이 객체에는 `request::complete()`를 호출하면 안 됩니다.

```cpp
auto target = device.default_io_target();
auto created = ntl::kmdf::driver_request::try_create(target);
if (!created)
  return created.status();

auto outgoing = std::move(created).value();
auto status = outgoing.try_format_ioctl(
    target, IOCTL_SAMPLE_QUERY, input_memory, output_memory);
if (status.is_err())
  return status;

ntl::kmdf::send_options options;
options.timeout(WDF_REL_TIMEOUT_IN_MS(1000));
status = outgoing.try_allocate_timer();
if (status.is_err())
  return status;
return outgoing.try_send_and_wait(target, &options);
```

비동기 전송에서는 `driver_request::try_send()`가 완료 경로로 소유권을 이전합니다.
기본 오버로드는 완료 후 요청을 삭제합니다. 타입이 지정된 완료 콜백은 새로운
`driver_request` 소유자를 받으므로 요청을 검사하고 재사용하거나 다시 전송할 수
있습니다. 그렇게 하지 않으면 소멸자가 요청을 삭제합니다. 비동기 오버로드는 동기
전송 옵션과 전송 후 완료를 기다리지 않는 옵션을 거부하므로, 소유권이 암묵적으로
바뀌지 않습니다.

타입이 지정된 인터페이스는 read, write, IOCTL, internal IOCTL 및 인수가 세 개인
internal IOCTL 형식을 지원합니다. `try_allocate_timer()`는
`WdfRequestAllocateTimer`를 제공하므로 전송 경로 안에서 타이머를 할당하지 않고도
시간 제한 전송을 안정적으로 수행할 수 있습니다.

드라이버는 `io_target::try_create()`와 `io_target_open_params`를 사용해 기본 대상이
아닌 I/O 대상을 만들고 열 수 있습니다. 대상 객체의 소유권은 WDF에 남습니다.

```cpp
auto created_target = ntl::kmdf::io_target::try_create(device);
if (!created_target)
  return created_target.status();

ntl::unicode_string name(L"\\Device\\SampleTarget");
auto open = ntl::kmdf::io_target_open_params::open_by_name(
    &*name, GENERIC_READ | GENERIC_WRITE);
auto status = created_target->try_open(open);
```

`PASSIVE_LEVEL`에서 한 번 실행하는 작업에는 `memory_descriptor`로 호출자 버퍼,
MDL 또는 WDF 메모리 범위를 기술합니다. `io_target::try_read()`, `try_write()`,
`try_ioctl()`, `try_internal_ioctl()`, `try_internal_ioctl_others()`는 별도 요청을
만들지 않고 KMDF의 동기 대상 도우미를 사용합니다. 대상 래퍼는 대상 WDM 장치 및
파일 객체를 명시적인 `wdm_*()` 상호 운용 메서드로도 노출합니다.

## 공통 WDF 객체 유틸리티

KMDF 네임스페이스는 프레임워크 스핀 락과 대기 락, 고정 크기 룩어사이드 메모리,
객체 컬렉션, 문자열 및 독립형 DPC에 타입이 지정된 래퍼를 제공합니다. 이는 이름이 비슷한
WDM 지향 NTL 형식과는 다릅니다. `ntl::kmdf::*` 객체는 WDF의 부모 계층, 검증,
콜백 직렬화 및 참조 모델에 참여합니다.

일반 수명 주기 도우미는 서로 다른 세 가지 책임을 명확히 구분합니다.

- `object`는 `WDFOBJECT`의 비소유 뷰입니다.
- `owned_object`는 `WdfObjectCreate`로 만든 일반 객체의 삭제 책임을 소유하며,
  재설정하거나 소멸할 때 `WdfObjectDelete`를 호출합니다.
- `object_reference`는 참조 횟수 증가분 하나를 소유하고
  `WdfObjectDereferenceActual`로 균형을 맞춥니다. `WdfObjectDelete`로 삭제를
  요청하지는 않습니다.

`owned_object`는 지정된 WDF 부모보다 오래 살아서는 안 됩니다. 다른
`object_reference`가 남아 있으면 객체 삭제가 최종 소멸 콜백보다 먼저 반환될 수
있습니다. 이 형식들은 객체 관리자 포인터 참조를 소유하고 `ObDereferenceObject`를
호출하는 `ntl::unique_object`와도 다릅니다.

```cpp
struct operation_state {
  explicit operation_state(ULONG id) noexcept : id(id) {}
  ULONG id;
};

ntl::kmdf::object_attributes attributes;
attributes.parent(device);

auto operation = ntl::kmdf::owned_object::try_create<operation_state>(
    &attributes, 42);
if (!operation)
  return operation.status();

auto pending_reference = operation->reference();
auto id = pending_reference.context<operation_state>().id;

// The same reference owner accepts typed WDF facades.
ntl::kmdf::object_reference device_reference{device};
```

`spin_lock`과 `wait_lock`은 표준 C++ Lockable 인터페이스를 구현하므로
`std::lock_guard`와 함께 사용할 수 있습니다. WDF 스핀 락은 IRQL을
`DISPATCH_LEVEL`로 올리며, 블로킹 방식으로 WDF 대기 락을 획득하려면
`PASSIVE_LEVEL`이어야 합니다. PASSIVE_LEVEL 전용 CRT/STL 작업은 스핀 락의 임계
구역 밖에서 수행하십시오. `wait_lock::try_lock()`은 제한 시간을 0으로 지정하고
`STATUS_TIMEOUT`이면 `false`를 반환합니다. 이 상태 코드는 정보 상태이므로 일반적인
`NT_SUCCESS` 검사만으로는 락 획득 여부를 판단할 수 없습니다.

```cpp
auto spin = ntl::kmdf::spin_lock::try_create();
if (!spin)
  return spin.status();
{
  std::lock_guard guard(*spin);
  ++shared_counter; // nonpageable, DISPATCH_LEVEL-safe work only
}

auto wait = ntl::kmdf::wait_lock::try_create();
if (!wait)
  return wait.status();
{
  std::lock_guard guard(*wait); // blocking acquisition: PASSIVE_LEVEL
  update_passive_state();
}
```

`lookaside`는 고정 크기의 `WDFMEMORY` 할당을 만듭니다. `try_allocate()`는 이동 전용
`lookaside_memory`를 반환합니다. 이 객체의 소멸자는 `WdfObjectDelete`를 호출하여
기반 메모리 블록을 WDF 룩어사이드 목록에 반환합니다. 이 명시적 소유자 덕분에
성공적으로 할당한 객체를 실수로 누락해 유출하는 일을 막을 수 있습니다.

```cpp
ntl::kmdf::object_attributes attributes;
attributes.parent(device);

auto packets = ntl::kmdf::lookaside::try_create(
    sizeof(packet), NonPagedPoolNx, "Pkt0", &attributes);
if (!packets)
  return packets.status();

auto packet_memory = packets->try_allocate();
if (!packet_memory)
  return packet_memory.status();
auto* packet_view = packet_memory->data<packet>();
```

`collection`은 객체가 컬렉션에서 제거되거나 컬렉션 자체가 삭제될 때까지 WDF
객체에 대한 프레임워크 참조를 유지합니다. `string`은 `UNICODE_STRING` 또는
`std::wstring_view`를 WDF 소유 문자열 객체로 복사합니다. 생성과 값 접근에는
`PASSIVE_LEVEL`이 필요합니다.

```cpp
auto label = ntl::kmdf::string::try_create(L"sample device");
auto objects = ntl::kmdf::collection::try_create();
if (!label || !objects)
  return STATUS_INSUFFICIENT_RESOURCES;
auto status = objects->try_add(*label);
```

독립형 `dpc`는 다른 WDF 객체를 부모로 두며 타입이 지정된 콜백을
`DISPATCH_LEVEL`에서 호출합니다. `enqueue()`는 `HIGH_LEVEL`까지 호출할 수 있습니다.
`cancel(true)`는 실행 중인 콜백이 끝날 때까지 기다리므로 `PASSIVE_LEVEL`이
필요합니다. 콜백에서는 비페이지 코드와 IRQL에 안전한 작업만 사용하고, 일반적인
CRT/STL 작업은 `ntl::kmdf::work_item`으로 미루십시오.

```cpp
constexpr auto on_dpc = +[](ntl::kmdf::dpc work) noexcept {
  work.context<dpc_state>().pending.store(true,
                                          std::memory_order_relaxed);
};

auto config = ntl::kmdf::dpc_config::with_callback<on_dpc>();
config.automatic_serialization(false);
auto deferred = ntl::kmdf::dpc::try_create<dpc_state>(
    device, config, nullptr);
if (!deferred)
  return deferred.status();
deferred->enqueue();
```

## DMA

`dma_enabler`, `common_buffer`, `dma_transaction`은 KMDF 고유의 DMA 객체와 콜백
모델을 유지하면서 반복적인 구성 및 핸들 변환을 타입이 지정된 래퍼로 대체합니다.
DMA enabler나 common buffer 생성은 `PASSIVE_LEVEL` 작업입니다. program-DMA 콜백은
보통 `DISPATCH_LEVEL`에서 실행되므로 PASSIVE_LEVEL 전용 CRT/STL API를 사용하면
안 됩니다.

```cpp
ntl::kmdf::dma_enabler_config dma_config(
    WdfDmaProfileScatterGather64, 1024 * 1024);

auto dma = ntl::kmdf::dma_enabler::try_create(device, dma_config);
if (!dma)
  return dma.status();

ntl::kmdf::common_buffer_config aligned(15); // 16-byte alignment
auto descriptors = ntl::kmdf::common_buffer::try_create(
    dma.value(), 4096, aligned);
if (!descriptors)
  return descriptors.status();

auto* table = descriptors->data<device_descriptor>();
const auto device_address = descriptors->logical_address();
```

요청을 기반으로 하는 DMA 트랜잭션은 추가 할당이 필요 없는 타입이 지정된 program
콜백을 설치합니다. 이 콜백은 장치에 설정해야 할 scatter/gather 목록을 받습니다.

```cpp
constexpr auto program_dma =
    +[](ntl::kmdf::dma_transaction transaction,
        ntl::kmdf::device,
        void* context,
        WDF_DMA_DIRECTION direction,
        ntl::kmdf::scatter_gather_list fragments) noexcept {
      auto& registers = *static_cast<device_registers*>(context);
      for (ULONG i = 0; i != fragments.size(); ++i) {
        const auto* fragment = fragments.at(i);
        registers.program(direction, fragment->Address,
                          fragment->Length);
      }
      return true;
    };

auto transaction = ntl::kmdf::dma_transaction::try_create(dma.value());
if (!transaction)
  return transaction.status();

auto status = transaction->try_initialize_request<program_dma>(
    request, WdfDmaDirectionWriteToDevice);
if (status.is_err())
  return status;

return transaction->try_execute(&registers);
```

래퍼는 활성 트랜잭션을 의도적으로 삭제하지 않습니다. 인터럽트/DPC 경로에서
`complete_transfer()` 또는 `complete_final()`로 마지막 전송을 보고한 뒤, 같은 WDF
객체를 다시 초기화하려면 먼저 `try_release()`를 호출하십시오. 재사용하지 않을
객체에는 `destroy()`를 호출하십시오. 초기화 성공 후 `try_execute()`가 실패한
경우에도 재사용하기 전에 트랜잭션을 해제해야 합니다. 최종 정리 수단으로는 WDF
부모 객체가 남아 있습니다.

패키지 빌드는 지원하는 모든 toolset과 architecture에서 이 형식의 DMA 콜백을
인스턴스화합니다. 런타임 DMA 실행에는 해당 하드웨어가 필요하므로 소프트웨어 전용 VM
스모크 테스트에서는 요구하지 않습니다. 빌드 가능한
[KMDF DMA 드라이버 템플릿](../../examples/kmdf/dma)은 전체 요청,
scatter/gather 프로그래밍, interrupt-DPC 완료, transaction 해제 및 요청 완료
흐름을 보여 줍니다.

## USB

`usb_device`, `usb_interface`, `usb_pipe`는 KMDF USB 객체를 감싸는 타입이 지정된
비소유 래퍼입니다. `EvtDevicePrepareHardware`에서 대상을 생성하고 구성하고,
`EvtDeviceD0Entry`에서 구성한 continuous reader를 시작한 뒤
`EvtDeviceD0Exit`에서 중지하십시오.

```cpp
auto usb = ntl::kmdf::usb_device::try_create(device);
if (!usb)
  return usb.status();

auto selection = ntl::kmdf::usb_select_config::single_interface();
auto status = usb->try_select(selection);
if (status.is_err())
  return status;

auto interface = selection.configured_interface();
ntl::kmdf::usb_pipe_information info;
auto pipe = interface.pipe_at(0, &info);
```

USB 대상 생성, 설명자 조회, 구성 선택 및 동기 전송 도우미에는
`PASSIVE_LEVEL`이 필요합니다. 비동기 형식화 도우미는 WDF 요청만 준비하므로
`DISPATCH_LEVEL`까지 호출할 수 있습니다. 형식화한 요청은
`usb_device::target()`이나 `usb_pipe::target()`을 통해 전송하십시오.

연속 읽기 콜백은 `DISPATCH_LEVEL`에서 실행될 수 있습니다. 실제 콜백 IRQL에서
허용되는 작업만 수행해야 하며, 일반적인 PASSIVE_LEVEL 전용 CRT/STL API를 호출하면
안 됩니다. 그런 작업은 KMDF 작업 항목이나 다른 패시브 콜백으로 미루십시오.
reader-failure 콜백은 `PASSIVE_LEVEL`에서 실행됩니다.

```cpp
constexpr auto on_packet =
    +[](ntl::kmdf::usb_pipe, ntl::kmdf::memory buffer,
        size_t transferred, void* context) noexcept {
      auto& count = *static_cast<std::atomic_uint32_t*>(context);
      count.fetch_add(1, std::memory_order_relaxed);
      // Do not perform PASSIVE_LEVEL-only STL/CRT work here.
    };

auto reader =
    ntl::kmdf::usb_continuous_reader_config::with_completion<on_packet>(
        packet_size, &packet_count);
status = input_pipe.try_configure_reader(reader);
```

패키지 빌드는 지원하는 모든 도구 집합과 아키텍처에서 USB 장치, 인터페이스,
파이프, 동기 및 타입이 지정된 전송, 연속 reader API를 인스턴스화합니다. 런타임 USB
검증에는 설명자와 엔드포인트 프로토콜이 드라이버와 일치하는 장치가 필요합니다.
빌드 가능한 [KMDF USB 드라이버 템플릿](../../examples/kmdf/usb)은 관련 없는 USB
장치에 실수로 설치하지 않도록 의도적으로 임시 하드웨어 ID를 사용합니다.

## 인터럽트

`interrupt_config`는 동적 할당 없이 컴파일 타임 콜백을 설치합니다.
`interrupt::try_create()`는 `WdfInterruptCreate`의 상태를 그대로 보존하며 NTL이
관리하는 C++ 인터럽트 컨텍스트도 생성할 수 있습니다.

| 콜백 | 타입이 지정된 시그니처 | 실행 수준 |
| --- | --- | --- |
| ISR | `bool(interrupt, ULONG) noexcept` | 수동 인터럽트의 경우 DIRQL 또는 `PASSIVE_LEVEL` |
| DPC | `void(interrupt, object) noexcept` | `DISPATCH_LEVEL` |
| 작업 항목 | `void(interrupt, object) noexcept` | `PASSIVE_LEVEL` |
| 활성화/비활성화 | `NTSTATUS(interrupt, device) noexcept` | 인터럽트 서비스 수준 |

```cpp
constexpr auto on_interrupt =
    +[](kmdf::interrupt interrupt, ULONG) noexcept {
      // Inspect and acknowledge hardware here.
      return interrupt.queue_dpc();
    };
constexpr auto on_interrupt_dpc =
    +[](kmdf::interrupt interrupt, kmdf::object) noexcept {
      // DISPATCH_LEVEL: defer CRT/STL work if necessary.
      interrupt.queue_work_item();
    };
constexpr auto on_interrupt_work_item =
    +[](kmdf::interrupt, kmdf::object) noexcept {
      // PASSIVE_LEVEL work may use the audited CRT/STL surface.
    };

auto config =
    ntl::kmdf::interrupt_config::with_isr<on_interrupt>();
config
    .on_dpc<on_interrupt_dpc>()
    .on_work_item<on_interrupt_work_item>();

auto created = ntl::kmdf::interrupt::try_create(device, config);
if (!created)
  return created.status();
```

`interrupt_lock`은 `WdfInterruptAcquireLock`과 `WdfInterruptReleaseLock`의 짝을
보장하며, `interrupt::synchronize()`는 `WdfInterruptSynchronize`를 감쌉니다. 이
도우미들은 네이티브 인터럽트 IRQL 및 교착 상태 관련 계약을 그대로 유지합니다.
`info()`, `policy()`, `extended_policy()`는 벡터와 선호도 구성을 노출하고,
`enable()`, `disable()`, `report_active()`, `report_inactive()`는 KMDF의 명시적
인터럽트 수명 주기를 유지합니다.

## 타이머 및 작업 항목

`ntl::kmdf::timer`와 `ntl::kmdf::work_item`은 장치, 큐 또는 다른 WDF 객체를 부모로
두는 WDF 객체입니다. WDM 지향 `ntl::timer`, `ntl::work_item`과는 달리 KMDF가
수명을 소유하며 부모 객체의 콜백과 함께 해당 콜백을 직렬화할 수 있습니다.

```cpp
constexpr auto on_work_item = +[](kmdf::work_item item) noexcept {
  auto& state = item.parent().context<device_state>();
  state.refresh_at_passive_level();
};
constexpr auto on_timer = +[](kmdf::timer timer) noexcept {
  timer.parent().context<device_state>().poll();
};

auto work_config =
    ntl::kmdf::work_item_config::with_callback<on_work_item>();
work_config.automatic_serialization(false);

auto work = ntl::kmdf::work_item::try_create(device, work_config);
if (!work)
  return work.status();
work->enqueue();

auto timer_config =
    ntl::kmdf::timer_config::periodic<on_timer>(1000);

ntl::kmdf::object_attributes timer_attributes;
timer_attributes.execution_level(WdfExecutionLevelPassive);
auto timer = ntl::kmdf::timer::try_create(
    device, timer_config, &timer_attributes);
if (!timer)
  return timer.status();
timer->start_after_ms(1000);
```

작업 항목 콜백은 항상 `PASSIVE_LEVEL`에서 실행됩니다. `flush()` 역시
`PASSIVE_LEVEL`이 필요하며 해당 작업 항목의 콜백 안에서 호출하면 안 됩니다.
타이머에는 일반적으로 WDF 타이머 실행 규칙이 적용됩니다. 타이머 본문에서 CRT/STL을
사용한다면 `WdfExecutionLevelPassive`를 선택하십시오. WDF 규칙상 이러한 패시브
타이머는 일회성이어야 합니다(`Period == 0`). `timer::stop(true)`는 실행 중인 콜백이
끝나기를 기다리므로 `PASSIVE_LEVEL`이 필요합니다. 고해상도 타이머의
`TolerableDelay`는 WDF 요구 사항에 따라 0으로 유지해야 합니다.

## 동적 하위 목록

`child_list_config<Identification, Address>`는 KMDF child description을 타입이 지정된
payload로 mapping합니다. KMDF의 기본 description 동작은 payload byte를 복사하고
비교하므로 NTL은 의도적으로 trivially copyable한 standard-layout payload를
요구합니다. 할당된 문자열이나 다른 복잡한 identity가 필요한 드라이버는 계속
`child_list_config::native()`를 통해 native WDF callback을 사용할 수 있습니다.

```cpp
struct child_id { ULONG serial; };
struct child_address { ULONG slot; };

using children = ntl::kmdf::child_list_config<child_id, child_address>;
constexpr auto on_child_create =
    +[](kmdf::child_list,
        const kmdf::child_identification<child_id>& id,
        kmdf::pdo_init init) noexcept -> NTSTATUS {
      auto status = init.assign_device_id(L"Sample\\Child");
      if (status.is_err())
        return status;
      status = init.assign_instance_id(L"7");
      if (status.is_err())
        return status;
      status = init.add_hardware_id(L"Sample\\Child");
      if (status.is_err())
        return status;

      auto child = init.try_create();
      return child ? STATUS_SUCCESS : child.status();
    };
auto config = children::with_create<on_child_create>();

// Configure the FDO's framework-owned default list before device creation.
init.default_child_list(config);

// Additional child lists can be created after the parent device exists.
auto list = ntl::kmdf::child_list::try_create(parent_device, config);
if (!list)
  return list.status();

const kmdf::child_identification<child_id> id{{7}};
const kmdf::child_address<child_address> address{{2}};
return list->add_or_update(id, address);
```

이 래퍼는 scan 시작/종료, present/missing 상태 갱신, 형식 지정 PDO 조회, 주소
조회, eject 요청 및 RAII 순회도 제공합니다. `pdo_init`은 소유권 차이를 분명하게
유지합니다. dynamic child-create 콜백에 전달되는 값은 프레임워크가 소유하고,
`pdo_init::try_allocate()`가 반환하는 static PDO 초기화 객체는 `try_create()`가
소비할 때까지 호출자가 소유합니다. 형식 지정 인터페이스는 device, instance,
hardware, compatible 및 container ID, 지역화된 장치 텍스트, raw-device 지정과
부모로의 전달을 지원합니다.

생성된 `pdo` 래퍼는 부모 조회, identification/address 왕복, missing/eject 요청,
ejection relation 및 PnP/power capability를 지원합니다. 정적으로 할당한 자식은
`device::try_add_static_child()`로 연결할 수 있습니다. 빌드 가능한
[KMDF 버스 샘플](../../examples/kmdf/bus)은 실제 하위 function driver에 대해 동적
연결, missing 및 eject 전환을 실행합니다.

### PDO 이벤트 및 리소스 요구 사항

`pdo_event_callbacks`는 `WDF_PDO_EVENT_CALLBACKS`의 PDO 전용 리소스, 꺼내기,
잠금, 깨우기 및 누락 보고 이벤트를 지원합니다. `try_create()`를 호출하기 전에
콜백 테이블을 `pdo_init`에 등록하십시오. WDF는 이 콜백들을 `PASSIVE_LEVEL`에서
호출합니다.

리소스 보고에는 세 가지 단계가 있습니다.

- `on_resource_requirements_query()`는 `io_resource_requirements`를 수신하고
  PnP가 중재할 수 있는 논리적 구성을 보고합니다.
- `on_resources_query()`는 자식 장치에 이미 할당된 펌웨어 또는 부팅 리소스를
  나타내는 수정 가능한 `resource_list`를 받습니다.
- `pnp_power_callbacks::on_prepare_hardware()`는 PnP가 최종 선택한 원시 및 변환
  `resource_list` 값을 받습니다.

`io_resource_descriptor`는 `IO_RESOURCE_DESCRIPTOR`를 소유하며 메모리, 포트,
인터럽트, 레거시 및 v3 DMA, 연결, 장치 전용 요구 사항을 만드는 팩터리를
제공합니다. `io_resource_list`는 하나의 논리 구성을 나타내고,
`io_resource_requirements`에는 하나 이상의 대안이 들어 있습니다. 래퍼는
설명자를 WDF 목록에 복사하며 버스별 필드에는 `native()` 또는
`native_handle()`로 접근할 수 있게 합니다.

```cpp
constexpr auto query_requirements =
    +[](kmdf::pdo child,
        kmdf::io_resource_requirements requirements) noexcept -> NTSTATUS {
  requirements.interface_type(Internal).slot_number(0);

  auto configuration = requirements.try_create_list();
  if (!configuration)
    return configuration.status();

  PHYSICAL_ADDRESS minimum{};
  PHYSICAL_ADDRESS maximum{};
  maximum.QuadPart = 0xffff;
  auto status = configuration->try_append(
      kmdf::io_resource_descriptor::port(
          minimum, maximum, 8, 1, CM_RESOURCE_PORT_IO));
  if (status.is_err())
    return status;
  return requirements.try_append(configuration.value());
};

constexpr auto query_boot_resources =
    +[](kmdf::pdo, kmdf::resource_list resources) noexcept -> NTSTATUS {
  // A software-enumerated child can legitimately have no boot resources.
  return resources.size() == 0 ? STATUS_SUCCESS : STATUS_DATA_ERROR;
};

ntl::kmdf::pdo_event_callbacks events;
events.on_resource_requirements_query<query_requirements>()
    .on_resources_query<query_boot_resources>();
init.events(events);
```

테스트 목록을 채우겠다는 이유로 메모리 범위, 인터럽트 또는 DMA 채널을 지어내면
안 됩니다. 하드웨어 리소스가 필요 없는 가상 자식은 버스 샘플처럼 빈 논리 구성을
추가해야 합니다. 하드웨어 버스 드라이버는 해당 버스 계약에서 실제로 지원하는
범위와 대안만 보고해야 합니다.

### 드라이버 정의 쿼리 인터페이스

공급자는 일반 Windows `INTERFACE` 헤더로 시작하여 타입이 지정된 연산을 추가합니다.
`make_query_interface()`는 공통 헤더를 초기화하고, `query_interface_config`는
WDF 장치에 이 계약을 등록합니다.

```cpp
struct counter_interface {
  INTERFACE header;
  NTSTATUS(NTAPI *next)(void* context, ULONG* value) noexcept;
};

auto interface = ntl::kmdf::make_query_interface<counter_interface>(
    1, child.native_handle(),
    ntl::kmdf::reference_query_interface_object,
    ntl::kmdf::dereference_query_interface_object);
interface.next = next_counter;

ntl::kmdf::query_interface_config config{interface, counter_interface_guid};
auto status = child.try_add_query_interface(config);
```

기능 드라이버는 장치 스택을 통해 인터페이스를 조회합니다. 반환되는
`queried_interface<T>`는 이동 전용이며 재설정되거나 소멸할 때
`InterfaceDereference`를 정확히 한 번 호출합니다.

```cpp
auto queried = device.try_query_interface<counter_interface>(
    counter_interface_guid, 1);
if (!queried)
  return queried.status();

auto interface = std::move(queried).value();
ULONG value = 0;
auto status = interface->next(interface->header.Context, &value);
```

GUID와 구조체 레이아웃은 드라이버 간 ABI를 이룹니다. 첫 멤버는 `INTERFACE`로
유지하고, 페이로드에는 고정 너비 형식을 사용하며, 계약을 변경할 때는 인터페이스
버전을 올리십시오. 이 도우미들은 WDF 소유권을 숨기지 않습니다. 공급자가 참조
콜백을 선택하고 소비자는 `queried_interface<T>`를 통해 획득한 참조를 소유합니다.

## 레지스트리 및 장치 속성

`device::try_open_registry_key()`는 이동 전용 `ntl::kmdf::registry_key`를 반환합니다.
이 객체는 상대 하위 키 열기/생성, 원시 값 조회/할당, DWORD/QWORD/문자열/다중
문자열 도우미 및 값 제거 기능을 제공하며, 네이티브 `WDFKEY`와 WDM 핸들에 접근하는
명시적 경로도 유지합니다.

```cpp
auto key = device.try_open_registry_key(
    PLUGPLAY_REGKEY_DEVICE, KEY_READ | KEY_WRITE);
if (!key)
  return key.status();

auto enabled = key->query_dword(L"Enabled");
if (!enabled)
  return enabled.status();

auto status = key->assign_string(L"Mode", L"safe");
if (status.is_err())
  return status;

status = key->assign_multi_string(L"Fallbacks", {L"primary", L"safe"});
if (status.is_err())
  return status;

auto description =
    device.try_query_property(DevicePropertyDeviceDescription);
```

레거시 `DEVICE_REGISTRY_PROPERTY` 조회는 원시 바이트를 반환합니다. `DEVPROPKEY`
오버로드는 `DEVPROPTYPE`과 함께 `device_property_value`를 반환하며, 검사된
`as_string()` 및 `as_uint32()` 변환을 제공합니다. 이 NTL 도우미들은 할당된 STL
저장소를 반환하므로 `PASSIVE_LEVEL`이 필요합니다. 더 넓은 `APC_LEVEL` 계약이
필요하고 저장소를 직접 제공할 수 있다면 네이티브 `WdfDeviceQueryPropertyEx` API를
사용할 수 있습니다.

## WMI 공급자 및 인스턴스

`wmi_provider`와 `wmi_instance`는 KMDF 프레임워크가 소유하는 WMI 객체의 비소유
래퍼입니다. provider는 PnP 장치에 속하며 제어 장치에는 만들 수 없습니다.
`wmi_provider_config`는 하나의 data-block GUID를 선언하고,
`wmi_instance_config`는 해당 provider 인스턴스의 형식 지정 query, set,
set-item 및 method 콜백을 연결합니다.

```cpp
struct telemetry_state {
  std::uint32_t value = 7;
};

constexpr auto query_telemetry =
    +[](ntl::kmdf::wmi_instance instance,
        ntl::kmdf::wmi_output_buffer output) noexcept -> NTSTATUS {
  return output.try_write(instance.context<telemetry_state>());
};

ntl::kmdf::wmi_provider_config provider(telemetry_guid);
provider.minimum_instance_buffer_size(sizeof(telemetry_state));

auto created_provider =
    ntl::kmdf::wmi_provider::try_create(device, provider);
if (!created_provider)
  return created_provider.status();

ntl::kmdf::wmi_instance_config instance(created_provider.value());
instance.register_automatically().on_query<query_telemetry>();

auto telemetry = ntl::kmdf::wmi_instance::try_create<telemetry_state>(
    device, instance, nullptr);
```

장치가 시작되기 전에 컴파일된 MOF 리소스 이름을 할당합니다.

```cpp
auto status = device.try_assign_mof_resource(L"DriverMofResource");
```

`wmi_input_buffer`, `wmi_output_buffer`, `wmi_method_buffer`는 고정 크기의 단순 복사
가능한 페이로드를 검증하고 WMI가 요구하는 크기 보고 방식을 유지합니다.
`use_native_context_for_query()`는 네이티브 WDF 단축 경로입니다. NTL 관리 C++
컨텍스트에는 생성 메타데이터도 들어 있으므로 이 경로와 함께 사용하면 안 됩니다.

자동 등록은 첫 D0 진입 때 이루어집니다. 수동 인스턴스는 `PASSIVE_LEVEL`에서
`try_register()`와 `deregister()`를 사용할 수 있습니다. 이벤트 전용 공급자는
단일 인스턴스 경로인 `wmi_instance_config(wmi_provider_config&)`로 생성하고,
`APC_LEVEL` 이하에서 `try_fire_event()`를 사용합니다. 조회, 설정, 항목 설정,
메서드 및 공급자 기능 제어 콜백은 `PASSIVE_LEVEL`에서 실행됩니다. 이벤트 전용
공급자에서는 기능 제어 콜백이 무시됩니다. WDF는 WMI 공급자나 인스턴스 객체에
명시적인 `ExecutionLevel`을 허용하지 않으므로, 해당 `object_attributes`는
`WdfExecutionLevelInheritFromParent`를 유지해야 합니다. 장치나 큐처럼 지원되는
부모 객체에 패시브 실행 계약을 설정하고, 각 네이티브 KMDF WMI 콜백에 문서화된
IRQL 계약을 항상 따르십시오.

빌드 가능한 [KMDF WMI 샘플](../../examples/kmdf/wmi)은 바이너리 MOF 리소스를
컴파일하고 검증하며, 타입이 지정된 조회/설정/메서드 콜백을 실행합니다. 또한 사용자
모드에서 이벤트를 구독하고 장치 인터페이스를 통해 트리거한 뒤 `ROOT\\WMI`에서
이벤트 페이로드를 검증합니다.

## 대기열 제어

`io_queue`는 시작, 중지, 비우기, 제거 및 중지 후 제거 작업을 제공합니다.
`*_and_wait()` 형식은 동기 WDF 메서드를 호출하므로 네이티브 API와 같은
`PASSIVE_LEVEL` 및 교착 상태 예방 규칙을 따라야 합니다. 비동기 형식에는 선택적으로
컴파일 시간 `void(io_queue, void*) noexcept` 콜백과 불투명 컨텍스트 포인터를 받을
수 있습니다.

`native()`와 `native_handle()`은 명시적인 상호 운용 탈출구입니다. 파일 및
인터럽트 객체는 객체 의미를 드러내도록 `native_object()`를 사용합니다. 그렇다고
일반 NTL 콜백이 `WDFDEVICE`, `WDFQUEUE` 또는 `WDFREQUEST`를 노출해야 하는 것은
아닙니다. 타입이 지정된 콜백 인터페이스는 동적 디스패치나 저장소를 추가하지 않으면서
동일한 WDF 객체 수명과 동기화 규칙을 적용합니다.

## FDO, 제어 및 객체 유틸리티

`fdo_event_callbacks`는 타입이 지정된 `device`, `io_resource_requirements` 및
`resource_list` 인수로 프레임워크의 FDO 리소스 필터 단계를 지원합니다.
콜백은 `PASSIVE_LEVEL`에서 실행되며 장치 생성 전에 설치됩니다.

```cpp
ntl::kmdf::fdo_event_callbacks events;
events.on_add_resource_requirements<filter_requirements>()
      .on_remove_resource_requirements<remove_requirements>()
      .on_remove_added_resources<remove_added_resources>();
init.fdo_events(events);
```

`device::try_name()` 및 `try_interface_string()`은 프레임워크가 소유하는
`ntl::kmdf::string` 객체를 반환합니다. 제어 장치는
`control_device_init::on_shutdown()`을 통해 타입이 지정된 종료 알림을 등록할 수
있습니다. `object_lock_guard`는 none이 아닌 동기화 범위로 생성한 WDF 객체에만
사용하십시오. 이 가드는 두 번째 잠금을 할당하지 않고
`WdfObjectAcquireLock`과 `WdfObjectReleaseLock`의 호출 균형을 맞춥니다.

## KMDF 인터페이스 경계

NTL 인터페이스는 일반적인 제어, PnP, 필터, 기능 및 버스 드라이버 경로를
지원합니다. 진입점과 장치 생성, 타입이 지정된 컨텍스트 및 콜백, 큐, forward progress와
취소, 요청과 대상, 파일, PnP/전원, 리소스, 인터럽트, 타이머/작업 항목/DPC, 자식
목록/PDO, 쿼리 인터페이스, FDO 리소스 필터링, 레지스트리/속성, DMA, USB, WMI,
제어 장치 종료 및 프레임워크 객체 동기화가 여기에 포함됩니다.

NTL은 모든 WDF 함수의 이름을 의도적으로 바꾸지는 않습니다. 원시 IRP 디스패치와
전처리, 미니포트 통합, verifier bugcheck 도우미, 장치군별 프로토콜 구조체 및
드물게 쓰는 버전별 필드는 `native()`, `native_handle()`, `native_object()`,
`wdm_*()`와 일반 WDK 헤더를 통해 계속 사용할 수 있습니다. 이 경로들은
프레임워크와 명시적으로 상호 운용하기 위한 것이며, WDF의 별도 구현이 빠진 것이
아닙니다.

## 실행 컨텍스트

진입 함수와 예제 큐는 `PASSIVE_LEVEL`에서 실행됩니다. WDF 콜백은 큐나 객체 구성이
허용하면 더 높은 IRQL에서 실행될 수 있습니다. 따라서 콜백에서 CRT/STL을
사용하려면 `object_attributes::execution_level(WdfExecutionLevelPassive)` 같은
패시브 실행 계약이 필요합니다. 네이티브 KMDF의 콜백, 취소, 동기화 및 수명 규칙은
모두 그대로 적용됩니다.
