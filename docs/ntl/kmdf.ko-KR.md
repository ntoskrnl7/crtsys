# KMDF 도우미

[NTL 문서로 돌아가기](./README.ko-KR.md)

NTL KMDF 지원은 일반 KMDF 객체 모델을 위한 선택적 C++ 인터페이스입니다.
WDF 디스패치, PnP, 전원, 큐, 요청, 상위-하위를 대체하지 않습니다.
수명 또는 동기화 규칙.

[KMDF 드라이버 엔지니어링 체크리스트](./kmdf-driver-checklist.ko-KR.md)를 사용하십시오.
디자인하고 리뷰합니다. 소유권 이전, 취소 상태를 수집합니다.
머신, PnP/전력 주문, ABI 규칙, 릴리스 게이트를 한 곳에서 처리할 수 있습니다.

## 엔트리 모델을 선택하세요

Visual Studio/MSBuild 프로젝트는 `<DriverType>KMDF</DriverType>`를 유지하고 다음을 선택합니다.

```xml
<CrtSysUseNtlKmdfMain>true</CrtSysUseNtlKmdfMain>
```

NuGet 패키지를 사용하는 경우 **Project에서 **드라이버 유형 = KMDF**를 설정합니다.
속성 > 드라이버 설정 > 드라이버 모델**. 그러면 페이지에 다음이 표시됩니다.
**crtsys KMDF 진입점**. `ntl::kmdf::main`를 사용하려면 **NTL KMDF**를 선택합니다. 또는
일반 KMDF 진입 경로를 유지하려면 **NTL 진입점 없음**을 선택하세요.

`ExportDriver`는 별도의 WDK 내보내기 드라이버 모델입니다. 일반 WDM이 아닙니다.
드라이버 진입점 모델이므로 내보내기 드라이버에 대해 NTL 진입점을 선택하지 마십시오.
프로젝트.

CMake 프로젝트는 KMDF 드라이버 선언에 `NTL`를 추가합니다.

```cmake
crtsys_add_driver(my_driver KMDF 1.15 NTL src/main.cpp)
```

해당 옵트인이 없으면 KMDF 프로젝트는 표준 `DriverEntry`를 유지하고 호출합니다.
`WdfDriverCreate` 자체.

## 드라이버 항목

NTL 항목은 기본 `DriverEntry`에 바인딩된 `driver_builder`를 수신합니다.
인수. `try_create()`는 다음에 의해 반환된 `NTSTATUS`를 유지합니다.
`WdfDriverCreate`.

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
`work_item` 및 `child_list`는 비소유 퍼사드입니다.
WDF는 네이티브 객체 소유권을 유지하고 구성된 부모-자식 객체 수명 규칙을
적용합니다. `registry_key`와 `driver_request`는 이동 전용 소유자입니다.
`registry_key`는 `WDFKEY`를 닫고, `driver_request`는 아직 전송하지 않은 드라이버
생성 `WDFREQUEST`를 삭제합니다.

## 플러그 앤 플레이 장치

`EvtDriverDeviceAdd`는 프레임워크 소유 `PWDFDEVICE_INIT`를 받습니다.
`device_init`는 의도적으로 소유하지 않습니다. `WdfDeviceInitFree`를 호출하지 않습니다.
`try_create()`가 성공하면 `WdfDeviceCreate`는 초기화를 사용합니다.
개체. 장치를 생성하기 전에 콜백이 반환되면 KMDF는 장치를 생성한 후 이를 삭제합니다.
콜백이 반환됩니다.

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

단항 `+`는 캡처하지 않는 각 람다를 컴파일 타임 함수로 변환합니다.
포인터. 포인터를 템플릿으로 전달하기 전에 해당 포인터에 `constexpr` 이름을 지정합니다.
포인터에 `constexpr` 이름을 붙여 템플릿 인수로 전달하면 Visual Studio
IntelliSense가 인라인 람다 템플릿 인수를 잘못 해석하는 문제도 피할 수 있습니다.
NTL은 네이티브 핸들을 비소유 `driver`, `device`, `io_queue`, `request` 및
`resource_list` 퍼사드로 변환하는 무할당 WDF thunk를 설치합니다. 콜백은
`noexcept`여야 합니다. WDF 콜백은 C++ 람다 클로저를 보관할 수 없으므로 지속
상태는 WDF 객체 컨텍스트에 두어야 합니다.

`pnp_power_callbacks`는 하드웨어 준비/해제, D0 전환, 자체 관리 I/O, query-stop,
query-remove 및 surprise removal을 지원합니다. `power_policy_callbacks`는 S0/Sx
깨우기 정책 콜백을 지원합니다. 두 형식 모두 네이티브 프레임워크를 숨기지 않고,
덜 일반적인 WDF 필드에 접근할 수 있도록 `native()`를 제공합니다.

`device` 퍼사드는 별도의 상태 머신을 만들지 않고도 동작 상태를 제공합니다.
`pnp_state()`, `power_state()`, `power_policy_state()`,
`system_power_action()`, `state()`, `set_failed()` 및
`indicate_wake_status()`를 사용할 수 있습니다. `default_queue()`와
`try_route_requests()`는 기본 및 형식별 디스패치를 지원합니다.
`device_idle_reference::try_acquire()`는
`WdfDeviceStopIdleNoTrack`의 이동 전용 RAII 페어링 및
`WdfDeviceResumeIdleNoTrack`; D0을 기다리려면 `PASSIVE_LEVEL`가 필요합니다.

## 하드웨어 리소스

`resource_list`는 KMDF가 제공한 리소스 목록을 순회하는 비소유 뷰입니다. 각
요소는 `resource_descriptor`이며, `memory()`, `port()`,
`interrupt()`, `dma()` 및 `connection()` 메서드는 형식화된 값만 반환합니다.
설명자에 일치하는 유형이 있는 경우. `native()`는 계속 사용 가능합니다.
덜 일반적인 리소스 유형. 대용량 메모리 설명자는 WDK로 디코딩됩니다.
압축된 내용을 해석하는 대신 `RtlCmDecodeMemIoResource` 도우미
길이 필드를 직접 사용합니다.

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

원시 목록에는 버스 관련 리소스가 포함되어 있습니다. 번역된 목록에는 다음이 포함됩니다.
드라이버에 적합한 시스템 주소, 인터럽트 벡터 및 유사성
하드웨어 액세스 경로. `resource_origin`는 인터럽트 설명자로 전달됩니다.
따라서 메시지 신호 인터럽트 필드는 올바른 기본 통합에서 디코딩됩니다.
회원. 리소스 파사드는 WDF 목록의 수명을 소유하거나 연장하지 않으며
`EvtDevicePrepareHardware`부터 까지만 유효합니다.
`EvtDeviceReleaseHardware`가 반환됩니다.

## 유휴 및 절전 모드 해제 정책

`idle_policy`는 WDF를 사용하여 `WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS`를 초기화합니다.
초기화 프로그램을 실행하고 S0에서 런타임 유휴를 구성합니다. `wake_policy`는 동일한 작업을 수행합니다.
`WDF_DEVICE_POWER_POLICY_WAKE_SETTINGS`를 실행하고 Sx에서 깨어납니다. 둘 다 정확한 값을 반환합니다.
`try_apply()`의 WDF `NTSTATUS`.

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

이 설정은 `WdfDeviceCreate` 이후에 그리고 드라이버가 소유한 경우에만 적용됩니다.
장치 전원 정책. 래퍼는 기본 WDF 계약을 최대로 따릅니다.
`DISPATCH_LEVEL`; 이 예에서는 `EvtDeviceAdd` 중에 이를 구성합니다.
드라이버가 이미 `PASSIVE_LEVEL`에서 실행 중입니다. 장치가 실제로 할 수 있는지 여부
주어진 상태에서 깨어나더라도 하드웨어, 버스, 펌웨어 및 INF 계약은 그대로 유지됩니다.

## C++ 개체 컨텍스트

NTL은 WDF 소유 컨텍스트 저장소에서 직접 C++ 개체를 구성할 수 있습니다. 이것은
`ntl::device<T>`가 관리하는 연장 수명의 KMDF 대응물:
성공적인 WDF 객체 생성 후 생성이 이루어지고 소멸자가 실행됩니다.
컨텍스트 저장소가 해제되기 전에 WDF destroy 콜백에서.

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
`object_attributes`, `driver_config`, `io_queue_config`는 형식화된 콜백을 설치하면서,
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

애플리케이션, 형식화된 요청 버퍼, STL 사용, 심볼릭 링크 생성 및 큐 콜백을
포함한 완전한 비PnP 제어 장치 흐름은
[NTL KMDF 샘플](../../examples/kmdf/basic)을 참조하십시오.

## 파일 객체

`ntl::kmdf::file`는 `WDFFILEOBJECT`를 관찰합니다. `wdm()` 메소드는
기본 `PFILE_OBJECT`의 `ntl::file` 뷰를 반환합니다. 어느 퍼사드도 객체를
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

파일 컨텍스트는 형식화된 생성 콜백 전에 구성되고 삭제됩니다.
기본 정리/닫기 시퀀스 후에 `WDFFILEOBJECT`를 사용합니다. 구성
결합하는 대신 `file_config`를 사용한 파일 객체 정리 동작
`object_attributes::on_cleanup()`를 사용하는 NTL 관리 파일 컨텍스트 또는
`on_destroy()`.

## 요청 버퍼

`request::try_input_buffer<T>()` 및 `try_output_buffer<T>()`는 일치를 호출합니다.
KMDF 버퍼 검색 API를 사용하고 `ntl::result<request_buffer<T>>`를 반환합니다.
`request_buffer<T>`는 기본 WDF 요청에서만 유효한 비소유 뷰입니다.
버퍼 평생 계약.

요청 파사드는 또한 프레임워크 메모리 객체와 MDL을 노출합니다.
`try_input_memory()`, `try_output_memory()`, `try_input_mdl()` 및
`try_output_mdl()`. 명시적으로 명명된 `try_unsafe_user_*()` 함수는 다음과 같습니다.
`EvtIoInCallerContext`에만 해당됩니다. 전에 해당 사용자 주소를 검증하거나 잠그십시오.
그들을 유지합니다. `try_lock_user_buffer_for_read()` 및
`try_lock_user_buffer_for_write()`는 결과 WDF 메모리 개체를 반환합니다.

`parameters()`, `requestor_mode()`, `is_from_32bit_process()`,
`associated_queue()` 및 `wdm_irp()`는 요청 진단 및 명시적 기능을 제공합니다.
WDF의 요청 상태를 복제하지 않고 WDM 상호 운용이 가능합니다.

KMDF 콜백에는 완료할 수 있는 권한이 하나 있으므로 `request`는 이동 전용입니다.
요청을 전달하거나, 다시 대기열에 넣거나, 보냅니다. `request::try_forward_to()` 및
`try_requeue()`는 rvalue 한정이며 WDF가 전송을 수락하면 퍼사드를 비웁니다.
전송:

```cpp
auto status = std::move(request).try_forward_to(destination);
```

`try_mark_cancelable()` 및 `try_unmark_cancelable()`는 KMDF의 네이티브 취소 경쟁
규칙을 보존합니다. 취소 가능 표시 해제에서
`STATUS_CANCELLED`가 반환되면 취소 콜백이 완료 책임을 소유합니다.

## 수동 대기열 및 취소

요청이 대기해야 하는 경우 기본이 아닌 `WdfIoQueueDispatchManual` 대기열 생성
하드웨어, 데이터 또는 기타 요청의 경우. `try_retrieve_next()` 및
`try_retrieve_for()`는 해당 대기열에서 요청을 제거하고 이동 전용을 반환합니다.
`request`, 이를 처리하고 완료할 수 있는 권한을 호출자에게 이전합니다.

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

형식화된 요청을 다른 장치 스택으로 보내려면 무할당 완료 콜백을 등록한 뒤
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
실패하면 원래 요청 퍼사드를 계속 사용할 수 있습니다. `send_options`는 비동기
전송과 동기 대기를 모두 지원합니다. 성공한 비동기 전송은 원본 `request`를
비우지만, 동기 전송은 이를 유지하므로 하위 대상 작업이 끝난 뒤 호출자가 원래 큐
요청을 완료할 수 있습니다. 전송 후 잊기 방식은 소유권 계약이 다른 네이티브 WDF
탈출구로 남겨 둡니다.

## 대기열 전달 진행

`forward_progress_policy`는 필수 I/O가 가능하도록 프레임워크 요청을 예약합니다.
일반 요청 할당이 실패해도 여전히 대기열에 도달합니다. 정책 할당
대기열을 생성한 후 장치가 I/O 처리를 시작하기 전:

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

`always_reserved()`는 일반적인 안정성 정책입니다. `paging_io()` 선택
KMDF의 페이징 I/O 정책인 반면 `examine<Callback>()`는 DISPATCH_LEVEL 안전을 허용합니다.
콜백은 각 IRP가 실패할지 아니면 예약된 요청을 사용할지 선택합니다.
`prepare_reserved_requests()`는 예약된 모든 항목에 대해 일회성 준비를 수행합니다.
`reserved_request_resources`를 요청하고 수신합니다. 그 유형은 다음을 보장합니다.
계약. 이와 대조적으로 KMDF는 모든 새 항목에 대해 `prepare_each_request()`를 호출합니다.
프레임워크 요청을 생성한 후 대기열에 삽입하기 전,
예약되지 않은 일반 요청도 포함됩니다. 그로부터 실패를 반환
콜백이 요청을 거부합니다.

두 콜백은 일반 `request`가 아니라 제한된 리소스 준비 뷰를 받습니다. 따라서 아직
I/O 큐에 들어가지 않은 요청을 실수로 완료·전달·재큐잉할 수 없습니다. 콜백은
`DISPATCH_LEVEL`에서 실행될 수 있으므로 PASSIVE_LEVEL 전용 CRT/STL API를 호출하면
안 됩니다.

## WDF 메모리 및 드라이버 생성 요청

`memory`는 `WDFMEMORY`를 감쌉니다. `try_allocate()`는 WDF 소유 저장소를 만들고
`try_preallocated()`는 호출자 소유 저장소를 감쌉니다. 퍼사드는 비소유이므로 수명이
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

`request`는 WDF 대기열에 의해 전달된 요청을 나타내며 완료되거나
전달. 대신 `driver_request`는 다음을 사용하여 드라이버가 생성한 요청을 소유합니다.
`WdfRequestCreate`. 보내지 않은 `driver_request` 호출 삭제
`WdfObjectDelete`; `request::complete()`를 호출하면 안 됩니다.

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

비동기 전송의 경우 `driver_request::try_send()`는 소유권을 다음으로 이전합니다.
완료 경로. 기본 오버로드는 다음 이후에 요청을 삭제합니다.
완료. 형식화된 완료 콜백은 새로운 `driver_request` 소유자를 수신합니다.
검사하고, 재사용하고, 다시 보낼 수 있습니다. 그렇지 않으면 소멸자가 이를 삭제합니다.
동기식 및 보내기 후 잊어버리기 옵션은 비동기식에 의해 거부됩니다.
과부하가 발생하여 소유권이 자동으로 변경될 수 없습니다.

형식화된 인터페이스는 read, write, IOCTL, internal IOCTL 및 인수가 세 개인
internal IOCTL 형식을 지원합니다. `try_allocate_timer()`는
`WdfRequestAllocateTimer`를 제공하므로 전송 경로 안에서 타이머를 할당하지 않고도
시간 제한 전송을 안정적으로 수행할 수 있습니다.

드라이버는 `io_target::try_create()`를 사용하여 기본이 아닌 대상을 생성하고 열 수 있습니다.
및 `io_target_open_params`. 대상 개체는 WDF 소유로 유지됩니다.

```cpp
auto created_target = ntl::kmdf::io_target::try_create(device);
if (!created_target)
  return created_target.status();

ntl::unicode_string name(L"\\Device\\SampleTarget");
auto open = ntl::kmdf::io_target_open_params::open_by_name(
    &*name, GENERIC_READ | GENERIC_WRITE);
auto status = created_target->try_open(open);
```

PASSIVE_LEVEL 원샷 작업의 경우 `memory_descriptor`는 호출자를 설명합니다.
버퍼, MDL 또는 WDF 메모리 범위. `io_target::try_read()`, `try_write()`,
`try_ioctl()`, `try_internal_ioctl()` 및 `try_internal_ioctl_others()` 사용
별도의 생성이 필요 없는 KMDF의 동기식 타겟 헬퍼
요청. 타겟 파사드는 또한 타겟 WDM 장치/파일 객체를 다음과 같이 노출합니다.
명시적인 `wdm_*()` 상호 운용 방법.

## 일반적인 WDF 개체 유틸리티

KMDF 네임스페이스는 프레임워크 스핀 잠금과 대기 잠금, 고정 크기 룩어사이드
메모리, 객체 컬렉션, 문자열 및 독립형 DPC를 위한 형식화된 퍼사드를 제공합니다.
이는 비슷한 이름의 WDM 지향 NTL 형식과 구별됩니다.
유형: `ntl::kmdf::*` 객체는 WDF의 상위 계층 구조에 참여합니다.
검증, 콜백 직렬화 및 참조 모델.

일반 수명 주기 도우미는 세 가지 다른 책임을 명시적으로 유지합니다.- `object`는 `WDFOBJECT`의 비소유 뷰입니다.
- `owned_object`는 다음에 의해 생성된 일반 객체의 삭제를 소유합니다.
  재설정되거나 삭제되면 `WdfObjectCreate` 및 `WdfObjectDelete`를 호출합니다.
- `object_reference`는 하나의 참조 카운트 증분을 소유하고 균형을 유지합니다.
  `WdfObjectDereferenceActual`; 삭제를 요청하지 않습니다.
  `WdfObjectDelete`.

`owned_object`는 구성된 WDF 상위 버전보다 오래 지속되어서는 안 됩니다. 삭제하면 가능
다른 `object_reference`가 다음인 경우 최종 소멸 콜백 전에 반환됩니다.
아직 개최 중입니다. 이러한 유형은 `ntl::unique_object`와도 구별됩니다.
개체 관리자 포인터 참조를 소유하고 `ObDereferenceObject`를 호출합니다.

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

`spin_lock` 및 `wait_lock`은 표준 C++ Lockable 인터페이스를 구현하므로
`std::lock_guard`와 함께 사용할 수 있습니다. WDF 스핀 잠금은 IRQL을 다음으로 높입니다.
`DISPATCH_LEVEL`; 차단 WDF 대기 잠금 획득에는 다음이 필요합니다.
`PASSIVE_LEVEL`. 스핀 잠금 외부에서 PASSIVE_LEVEL 전용 CRT/STL 작업을 유지하세요.
중요한 섹션. `wait_lock::try_lock()`는 0 시간 초과를 사용하고 반환합니다.
`STATUS_TIMEOUT`에 대한 `false`; 해당 상태는 정보 제공용이므로 일반적인
`NT_SUCCESS` 검사는 잠금 획득 여부를 결정하는 데 충분하지 않습니다.

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

`lookaside`는 고정 크기 `WDFMEMORY` 할당을 생성합니다. `try_allocate()`
이동 전용 `lookaside_memory`를 반환합니다. 소멸자가 호출합니다.
`WdfObjectDelete`는 백업 블록을 WDF 색인 목록으로 반환합니다.
이 명시적 소유자는 성공적인 할당이 자동으로 수행되는 것을 방지합니다.
유출.

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

`collection`는 WDF 객체에 대한 프레임워크 참조를 유지합니다.
제거되거나 컬렉션이 삭제됩니다. `string`는 `UNICODE_STRING`를 복사하거나
`std::wstring_view`를 WDF 소유 문자열 개체로 변환합니다. 창조와 가치 접근
`PASSIVE_LEVEL`가 필요합니다.

```cpp
auto label = ntl::kmdf::string::try_create(L"sample device");
auto objects = ntl::kmdf::collection::try_create();
if (!label || !objects)
  return STATUS_INSUFFICIENT_RESOURCES;
auto status = objects->try_add(*label);
```

독립 실행형 `dpc`는 다른 WDF 객체의 부모이며 해당 유형의 개체를 호출합니다.
`DISPATCH_LEVEL`에서 콜백합니다. `enqueue()`는 `HIGH_LEVEL`를 통해 유효합니다.
`cancel(true)`는 실행 중인 콜백을 기다리므로 다음이 필요합니다.
`PASSIVE_LEVEL`. 콜백에는 페이징할 수 없고 IRQL 안전 작업만 사용하세요.
일반 CRT/STL 작업을 `ntl::kmdf::work_item`로 연기합니다.

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

`dma_enabler`, `common_buffer` 및 `dma_transaction`는 KMDF의 기본을 유지합니다.
반복적인 구성을 대체하면서 DMA 객체와 콜백 모델
유형화된 파사드로 변환을 처리합니다. Enabler 또는 공통 버퍼를 생성하는 것은
`PASSIVE_LEVEL` 작업. 프로그램 DMA 콜백은 일반적으로 다음에서 실행됩니다.
`DISPATCH_LEVEL`이므로 PASSIVE_LEVEL CRT/STL 표면을 사용하면 안 됩니다.

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

요청 지원 DMA 트랜잭션은 할당이 없는 유형의 프로그램을 설치합니다.
콜백. 콜백은 분산/수집 목록을 수신합니다.
장치에 프로그래밍됨:

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

Facade는 의도적으로 활성 트랜잭션을 삭제하지 않습니다. 이후
인터럽트/DPC 경로는 `complete_transfer()`를 사용한 마지막 전송을 보고합니다.
`complete_final()`, 동일한 WDF를 다시 초기화하기 전에 `try_release()`를 호출하세요.
재사용하지 않을 경우에는 `destroy()`를 호출하세요. `try_execute()`인 경우
성공적으로 초기화된 후 실패하면 재사용하기 전에 트랜잭션을 해제하세요.
WDF 상위 항목은 최종 분해 폴백으로 남아 있습니다.

패키지 빌드는 지원되는 모든 항목에서 이러한 유형의 DMA 콜백을 인스턴스화합니다.
도구 세트 및 아키텍처. 런타임 DMA 실행에는 일치하는 하드웨어가 필요합니다.
따라서 소프트웨어 전용 VM 스모크 테스트에서는 요구되지 않습니다.
빌드 가능한 [KMDF DMA 드라이버 템플릿](../../examples/kmdf/dma)전체 요청, 분산/수집 프로그래밍, 인터럽트-DPC를 보여줍니다.
완료, 트랜잭션 릴리스 및 요청-완료 흐름.

## USB

`usb_device`, `usb_interface` 및 `usb_pipe`는 비소유 유형의 파사드입니다.
KMDF USB 개체. 다음에서 대상을 생성하고 구성합니다.
`EvtDevicePrepareHardware`, 구성된 연속 리더를 시작합니다.
`EvtDeviceD0Entry`를 실행하고 `EvtDeviceD0Exit`에서 중지합니다.

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

USB 대상 생성, 설명자 검색, 구성 선택 및
동기 전송 도우미에는 `PASSIVE_LEVEL`가 필요합니다. 비동기식
형식 지정 도우미는 WDF 요청만 준비하며 다음을 통해 유효합니다.
`DISPATCH_LEVEL`; `usb_device::target()`를 통해 형식화된 요청을 보내거나
`usb_pipe::target()`.

연속 읽기 콜백은 `DISPATCH_LEVEL`에서 실행될 수 있습니다. 만 사용해야 합니다.
실제 콜백 IRQL에서 유효한 작업이며 일반 콜백을 호출해서는 안 됩니다.
PASSIVE_LEVEL CRT/STL 표면. 이러한 작업을 KMDF 작업 항목이나 다른 항목으로 연기
수동 콜백. 리더 오류 콜백은 `PASSIVE_LEVEL`에서 실행됩니다.

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

패키지 빌드는 USB 장치, 인터페이스, 파이프, 동기 및
지원되는 모든 항목에 대한 형식화된 전송 및 연속 판독기 표면
도구 세트 및 아키텍처. 런타임 USB 검증에는 다음과 같은 장치가 필요합니다.
설명자와 끝점 프로토콜이 드라이버와 일치합니다. 건설 가능한
[KMDF USB 드라이버 템플릿](../../examples/kmdf/usb) 의도적으로
실수로 설치되는 것을 방지하기 위해 자리 표시자 하드웨어 ID를 사용합니다.
관련 없는 USB 장치.

## 인터럽트

`interrupt_config`는 동적 할당 없이 컴파일 타임 콜백을 설치합니다.
`interrupt::try_create()`는 `WdfInterruptCreate` 상태를 유지하며 다음과 같은 작업도 수행할 수 있습니다.
NTL 관리 C++ 인터럽트 컨텍스트를 구성합니다.

| 콜백 | 타이핑된 서명 | 실행 수준 |
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

`interrupt_lock`는 `WdfInterruptAcquireLock`와 쌍을 이룹니다.
`WdfInterruptReleaseLock`; `interrupt::synchronize()` 랩
`WdfInterruptSynchronize`. 이러한 도우미는 기본 인터럽트 IRQL을 유지하고
교착상태 계약. `info()`, `policy()` 및 `extended_policy()` 벡터 노출
및 선호도 구성, `enable()`, `disable()`, `report_active()`,
및 `report_inactive()`는 KMDF의 명시적인 인터럽트 수명 주기를 보존합니다.

## 타이머 및 작업 항목

`ntl::kmdf::timer` 및 `ntl::kmdf::work_item`는 상위 개체인 WDF 개체입니다.
장치, 대기열 또는 다른 WDF 개체. WDM 지향과는 다릅니다.
`ntl::timer` 및 `ntl::work_item`: KMDF는 해당 수명을 소유하고 직렬화할 수 있습니다.
해당 콜백을 상위 개체의 콜백과 함께 사용합니다.

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

작업 항목 콜백은 항상 `PASSIVE_LEVEL`에서 실행됩니다. `flush()`에는 또한 다음이 필요합니다.
`PASSIVE_LEVEL`이며 해당 작업 항목의 콜백에서 호출하면 안 됩니다. 타이머
일반적으로 WDF 타이머 실행 규칙을 따릅니다. 선택
본체가 CRT/STL을 사용할 때 `WdfExecutionLevelPassive`; WDF에는 다음과 같은 것이 필요합니다.
패시브 타이머는 일회성입니다(`Period == 0`). `timer::stop(true)`는 다음을 기다립니다.
활성 콜백이므로 `PASSIVE_LEVEL`가 필요합니다. 고해상도 타이머
WDF에서 요구하는 대로 `TolerableDelay`를 0으로 유지해야 합니다.

## 동적 하위 목록

`child_list_config<Identification, Address>`는 KMDF child description을 형식화된
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

Facade는 또한 스캔 시작/종료, 현재/누락된 업데이트, 형식화된 PDO를 노출합니다.
조회, 주소 조회, 꺼내기 요청 및 RAII 반복. `pdo_init`는 유지합니다.
명시적인 소유권 구별: 동적에 제공되는 값
child-create 콜백은 프레임워크 소유이고 `pdo_init::try_allocate()`
`try_create()`가 소비할 때까지 정적 PDO 초기화 객체를 소유합니다. 형식화된
인터페이스는 장치, 인스턴스, 하드웨어, 호환 ID 및 컨테이너 ID, 지역화된 장치
텍스트, 원시 장치 지정과 부모 장치로의 전달을 지원합니다.

생성된 `pdo` 퍼사드는 부모 조회, 식별/주소
왕복, 누락/꺼내기 요청, 방출 관계, PnP/전원
능력. 정적으로 할당된 자식은 다음과 같이 연결할 수 있습니다.
`device::try_add_static_child()`. 건설 가능한
[KMDF 버스 샘플](../../examples/kmdf/bus)은 동적 플러그를 실행합니다.
실제 하위 기능 드라이버에 대한 누락 및 꺼내기 전환.

### PDO 이벤트 및 리소스 요구 사항

`pdo_event_callbacks`는 PDO 관련 리소스, 꺼내기, 잠금, 깨우기 및
`WDF_PDO_EVENT_CALLBACKS`에서 보고된 누락 이벤트. 테이블을 등록하세요
`try_create()` 이전에 `pdo_init`. WDF는 이러한 콜백을 호출합니다.
`PASSIVE_LEVEL`.

리소스 보고에는 세 가지 단계가 있습니다.

- `on_resource_requirements_query()`는 `io_resource_requirements`를 수신하고
  PnP가 중재할 수 있는 논리적 구성을 보고합니다.
- `on_resources_query()`는 펌웨어에 대해 변경 가능한 `resource_list`를 수신합니다.
  부팅 리소스가 이미 하위 항목에 할당되어 있습니다.
- `pnp_power_callbacks::on_prepare_hardware()`는 최종 원시 데이터를 수신하고
  PnP에 의해 선택된 변환된 `resource_list` 값.

`io_resource_descriptor`는 `IO_RESOURCE_DESCRIPTOR`를 소유하고 있으며
메모리, 포트, 인터럽트, 레거시 및 v3 DMA, 연결을 위한 팩토리
장치 개인 요구 사항. `io_resource_list`는 하나의 논리를 나타냅니다.
구성; `io_resource_requirements`에는 하나 이상의 대안이 포함되어 있습니다.
Facades는 디스크립터를 WDF 목록에 복사하고 `native()` 또는 유지합니다.
버스별 필드에 대한 `native_handle()` 액세스.

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

단지 데이터를 채우기 위해 메모리 범위, 인터럽트 또는 DMA 채널을 만들어내지 마십시오.
테스트 목록. 하드웨어 리소스가 필요하지 않은 가상 하위 항목을 추가해야 합니다.
버스 샘플과 마찬가지로 빈 논리적 구성입니다. 하드웨어 버스 드라이버
버스가 실제로 계약한 범위와 대안만 보고해야 합니다.
지원합니다.

### 드라이버 정의 쿼리 인터페이스

공급자는 일반 Windows `INTERFACE` 헤더로 시작하고 형식화된 헤더를 추가합니다.
운영. `make_query_interface()`는 공통 헤더를 초기화하는 반면
`query_interface_config`는 WDF 장치에 계약을 등록합니다.

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

기능 드라이버는 장치 스택을 통해 인터페이스를 쿼리합니다. 는
반환된 `queried_interface<T>`는 이동 전용이며 호출합니다.
`InterfaceDereference`는 재설정되거나 삭제될 때 정확히 한 번:

```cpp
auto queried = device.try_query_interface<counter_interface>(
    counter_interface_guid, 1);
if (!queried)
  return queried.status();

auto interface = std::move(queried).value();
ULONG value = 0;
auto status = interface->next(interface->header.Context, &value);
```

GUID와 구조 레이아웃은 크로스 드라이버 ABI를 형성합니다. 첫 번째 멤버 유지
`INTERFACE`로 고정 너비 페이로드 유형을 사용하고 인터페이스를 증가시킵니다.
계약이 변경되면 버전이 변경됩니다. 다음 도우미는 WDF 소유권을 숨기지 않습니다.
공급자는 참조 콜백을 선택하고 소비자는
`queried_interface<T>`를 통해 참조를 획득했습니다.

## 레지스트리 및 장치 속성

`device::try_open_registry_key()`는 이동 전용을 반환합니다.
`ntl::kmdf::registry_key`. 상대 하위 키 열기/생성, 원시 값을 제공합니다.
쿼리/할당, DWORD/QWORD/문자열/다중 문자열 도우미 및 제거기본 `WDFKEY` 및 WDM 핸들 탈출구를 유지합니다.

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

레거시 `DEVICE_REGISTRY_PROPERTY` 쿼리는 원시 바이트를 반환합니다. `DEVPROPKEY`
오버로드는 `DEVPROPTYPE`를 포함하여 `device_property_value`를 반환합니다.
`as_string()` 및 `as_uint32()` 변환을 확인했습니다. 이러한 NTL 도우미에는 다음이 필요합니다.
`PASSIVE_LEVEL`는 할당된 STL 저장소를 반환하기 때문입니다. 기본
드라이버가 필요할 때 기본 `WdfDeviceQueryPropertyEx` API를 계속 사용할 수 있습니다.
더 넓은 `APC_LEVEL` 계약을 체결하고 자체 스토리지를 제공합니다.

## WMI 공급자 및 인스턴스

`wmi_provider` 및 `wmi_instance`는 KMDF의 비소유 퍼사드입니다.
프레임워크 소유 WMI 개체. 공급자는 PnP 장치에 속하며 공급자가 될 수 없습니다.
제어 장치용으로 생성되었습니다. `wmi_provider_config`는 하나의 데이터 블록을 선언합니다.
GUID; `wmi_instance_config`는 형식화된 쿼리, 집합, 집합 항목 및 메서드를 연결합니다.
해당 공급자의 인스턴스에 대한 콜백.

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

`wmi_input_buffer`, `wmi_output_buffer` 및 `wmi_method_buffer` 검증
고정 크기의 사소하게 복사 가능한 페이로드 및 WMI의 필수 크기 보존
보고. `use_native_context_for_query()`는 기본 WDF 단축키이며
NTL 관리 C++ 컨텍스트와 함께 사용하면 안 됩니다.
구성 메타데이터도 포함되어 있습니다.

첫 번째 D0 항목에서 자동 등록이 발생합니다. 수동 인스턴스는 다음을 사용할 수 있습니다.
`PASSIVE_LEVEL`의 `try_register()` 및 `deregister()`. 이벤트 전용 제공업체
`wmi_instance_config(wmi_provider_config&)`를 통해 생성됩니다.
단일 인스턴스 경로를 사용하고 IRQL에서 다음보다 높지 않은 `try_fire_event()`를 사용합니다.
`APC_LEVEL`. 쿼리, 설정, 설정항목, 메소드, 제공자 기능 제어
콜백은 `PASSIVE_LEVEL`에서 실행됩니다. 함수 제어 콜백은 무시됩니다.
이벤트 전용 제공업체의 경우. WDF는 명시적인 것을 허용하지 않습니다.
WMI 공급자 또는 인스턴스 개체에 대한 `ExecutionLevel`이므로
`object_attributes`는 `WdfExecutionLevelInheritFromParent`를 유지해야 합니다. 설정
장치 또는 대기열과 같은 지원되는 상위 개체에 대한 수동적 계약
항상 각 기본 KMDF WMI 콜백의 문서화된 IRQL 계약을 따르세요.

빌드 가능한 [KMDF WMI 샘플](../../examples/kmdf/wmi)이 컴파일됩니다.
바이너리 MOF 리소스의 유효성을 검사하고, 형식화된 쿼리/설정/메서드 콜백을 실행합니다.
사용자 모드에서 이벤트를 구독하고 장치 인터페이스를 통해 이벤트를 트리거합니다.
`ROOT\\WMI`를 통해 이벤트 페이로드를 확인합니다.

## 대기열 제어

`io_queue`는 start, stop, drain, purge 및 stop-and-purge 작업을 제공합니다.
`*_and_wait()` 형식은 동기 WDF 메서드를 호출하므로 네이티브 API와 같은
`PASSIVE_LEVEL` 및 교착 상태 예방 규칙을 따라야 합니다. 비동기 형식에는 선택적으로
컴파일 시간
`void(io_queue, void*) noexcept` 콜백 및 불투명 컨텍스트 포인터.

`native()`와 `native_handle()`은 명시적인 상호 운용 탈출구입니다. 파일 및
인터럽트 객체는 객체 의미를 드러내도록 `native_object()`를 사용합니다. 그렇다고
일반 NTL 콜백이 `WDFDEVICE`, `WDFQUEUE` 또는 `WDFREQUEST`를 노출해야 하는 것은
아닙니다. 형식화된 콜백 인터페이스는 동적 디스패치나 저장소를 추가하지 않으면서
동일한 WDF 객체 수명과 동기화 규칙을 적용합니다.

## FDO, 제어 및 객체 유틸리티

`fdo_event_callbacks`는 형식화된 `device`, `io_resource_requirements` 및
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
`control_device_init::on_shutdown()`을 통해 형식화된 종료 알림을 등록할 수
있습니다. `object_lock_guard`는 none이 아닌 동기화 범위로 생성한 WDF 객체에만
사용하십시오. 이 가드는 두 번째 잠금을 할당하지 않고
`WdfObjectAcquireLock`과 `WdfObjectReleaseLock`의 호출 균형을 맞춥니다.

## KMDF 인터페이스 경계

NTL 인터페이스는 일반적인 제어, PnP, 필터, 기능 및 버스
드라이버 경로: 항목 및 장치 생성, 형식화된 컨텍스트 및 콜백, 대기열,
앞으로 진행 및 취소, 요청 및 대상, 파일, PnP/전원,
리소스, 인터럽트, 타이머/작업 항목/DPC, 하위 목록/PDO, 쿼리
인터페이스, FDO 리소스 필터링, 레지스트리/속성, DMA, USB, WMI,
제어 장치 종료 및 프레임워크 개체 동기화.

NTL은 의도적으로 모든 WDF 함수의 이름을 바꾸지 않습니다. 원시 IRP 파견 및
전처리, 미니포트 통합, 검증자 버그체크 도우미, 장치 제품군
프로토콜 구조 및 일반적이지 않은 버전별 필드는 계속 사용할 수 있습니다.
`native()`, `native_handle()`, `native_object()`, `wdm_*()` 및
일반 WDK 헤더. 이러한 경로는 명시적인 프레임워크 상호 운용이며
WDF의 두 번째 구현이 누락되었습니다.

## 실행 컨텍스트

항목 기능과 샘플 대기열은 `PASSIVE_LEVEL` 실행을 사용합니다. WDF
콜백은 대기열/객체 구성이 허용하는 경우 더 높은 IRQL에서 실행될 수 있습니다.
따라서 콜백 내에서 CRT/STL을 사용하려면 여전히 수동 실행이 필요합니다.
`object_attributes::execution_level(WdfExecutionLevelPassive)`와 같은 계약.
모든 기본 KMDF 콜백, 취소, 동기화 및 수명 규칙
계속 유효합니다.
