# NTL minifilter control-device 예제

[English](./README.md)

이 예제는 WDK `cdo` 예제에서 재사용 가능한 부분을 형식화된 NTL 소유권으로
구현합니다. 애플리케이션 드라이버 코드에 raw WDM dispatch나 `fltKernel.h`를
노출하지 않고 minifilter 옆에 legacy control device를 두는 방법을 보여줍니다.

| WDK 기능 | NTL 표현 |
| --- | --- |
| `IoCreateDevice` 및 symbolic link | `driver.add_control_device<T>()` |
| `DriverObject->MajorFunction[]` | 형식화된 `device.on_*()` handler |
| 수동 `METHOD_BUFFERED` 검증 | `ioctl_from_contract` 도우미 |
| 열린 참조가 있을 때 unload 거부 | 형식화된 `on_unload(unload_flags)` |
| 장치/link 삭제 | `ntl::flt::driver`가 자동으로 teardown |

## 동작 흐름

1. `ntl::flt::main`이 filtering 시작 전에 control device를 기술합니다.
2. NTL이 장치를 생성·구성한 뒤 DOS 이름을 게시합니다.
3. 앱이 `\\.\CrtSysMinifilterControlDevice`를 열고 형식화된 ping을 보냅니다.
4. 선택적 `FilterUnload`는 handle이 열린 동안 거부됩니다.
5. unload 거부 후에도 dispatch는 계속 동작하며, handle을 닫으면 unload할 수
   있습니다.

드라이버 source는 `#include <ntl/flt/all>` 하나로 시작합니다. native Filter
Manager 및 WDM 진입점/dispatch table은 NTL 내부에 유지됩니다.

## 빌드 및 실행

```powershell
cmake -S examples\minifilter\control-device `
      -B out\minifilter-control-device-x64 -A x64
cmake --build out\minifilter-control-device-x64 --config Debug
```

Visual Studio/WDK에서 직접 빌드하려면
`crtsys_minifilter_control_device_sample_vs.sln`을 열거나 다음 명령을 실행하세요.

```powershell
msbuild crtsys_minifilter_control_device_sample_vs.sln /restore `
        /p:Configuration=Debug /p:Platform=x64
```

폐기 가능한 VM에 test-signed 드라이버를 설치한 다음 실행합니다.

```powershell
fltmc load CrtSysMinifilterControlDeviceSample
crtsys_minifilter_control_device_sample_app.exe
fltmc unload CrtSysMinifilterControlDeviceSample
```

앱은 의도적으로 `FilterUnload`를 시도하므로 관리자 권한으로 실행해야 합니다.
개발용 altitude `370030.130`은 배포에 적합하지 않습니다.

x64/WOW64 및 Driver Verifier 전체 fixture는
[`test/flt/runtime/cdo_*`](../../../test/flt/runtime)에 유지됩니다.
