# NTL minifilter control-device 예제

[English](./README.md)

이 예제는 WDK `cdo` 예제에서 재사용 가능한 부분을 타입이 지정된 NTL 소유권으로
구현합니다. 앱 드라이버 코드에 원시 WDM dispatch나 `fltKernel.h`를 노출하지 않고,
minifilter와 함께 기존 control device를 두는 방법을 보여 줍니다.

| WDK 방식 | NTL 표현 |
| --- | --- |
| `IoCreateDevice` 및 심볼릭 링크 | `driver.add_control_device<T>()` |
| `DriverObject->MajorFunction[]` | 타입이 지정된 `device.on_*()` 핸들러 |
| 수동 `METHOD_BUFFERED` 검증 | `ioctl_from_contract` 도우미 |
| 열린 참조가 있을 때 언로드 거부 | 타입이 지정된 `on_unload(unload_flags)` |
| 장치/링크 삭제 | 자동 `ntl::flt::driver` 정리 |

## 흐름

1. `ntl::flt::main`은 필터링을 시작하기 전에 control device를 기술합니다.
2. NTL이 장치를 생성·구성한 다음 DOS 이름을 게시합니다.
3. 앱은 `\\.\CrtSysMinifilterControlDevice`를 열고 타입이 지정된 ping을 보냅니다.
4. 선택적인 `FilterUnload`는 핸들이 열린 동안 거부됩니다.
5. 거부 뒤에도 dispatch는 계속 사용할 수 있고, 핸들을 닫으면 언로드할 수 있습니다.

드라이버 소스는 `#include <ntl/flt/all>` 하나로 시작합니다. 네이티브 Filter Manager와
WDM 진입/dispatch 테이블은 NTL 안에 머뭅니다.

## 빌드 및 실행

```powershell
cmake -S examples\minifilter\control-device `
      -B out\minifilter-control-device-x64 -A x64
cmake --build out\minifilter-control-device-x64 --config Debug
```

Visual Studio/WDK에서 직접 빌드하려면
`crtsys_minifilter_control_device_sample_vs.sln`을 열거나 다음을 실행합니다.

```powershell
msbuild crtsys_minifilter_control_device_sample_vs.sln /restore `
        /p:Configuration=Debug /p:Platform=x64
```

폐기 가능한 VM에 테스트 서명 드라이버를 설치한 뒤 실행합니다.

```powershell
fltmc load CrtSysMinifilterControlDeviceSample
crtsys_minifilter_control_device_sample_app.exe
fltmc unload CrtSysMinifilterControlDeviceSample
```

앱은 의도적으로 `FilterUnload`를 시도하므로 관리자 권한으로 실행해야 합니다.
개발용 고도 `370030.130`은 배포에 적합하지 않습니다.

철저한 x64/WOW64 및 Driver Verifier fixture는
[`test/flt/runtime/cdo_*`](../../../test/flt/runtime)에 있습니다.
