# NTL KMDF USB 드라이버 template

[English](./README.md)

이 driver/app template은 NTL KMDF USB 경로를 보여줍니다.

1. `EvtDevicePrepareHardware`에서 `usb_device`를 만듭니다.
2. 첫 번째 USB interface를 선택하고 bulk/interrupt pipe를 열거합니다.
3. 첫 번째 input pipe에 continuous reader를 구성합니다.
4. D0 entry와 exit에서 reader를 시작하고 중지합니다.
5. descriptor, endpoint 및 reader 상태를 사용자 모드 앱에 노출합니다.

INF는 의도적으로 placeholder hardware ID `USB\VID_FFFF&PID_FFFF`를 사용합니다.
예제를 빌드하거나 설치하기 전에 실제 device ID로 바꾸세요. configuration/interface
0을 선택하고 첫 번째 bulk 또는 interrupt input endpoint를 계속 읽는 동작이 device
protocol과 맞는지도 확인해야 합니다. 무관한 USB device에는 설치하지 마세요.

USB target 생성, descriptor 접근 및 configuration 선택은 `PASSIVE_LEVEL`에서
실행됩니다. continuous-reader completion은 보통 `DISPATCH_LEVEL`에서 실행되므로
예제 callback은 lock-free atomic counter만 사용합니다. reader callback에 추가하는
코드는 실제 IRQL을 지원해야 합니다. 일반 CRT/STL 처리는 passive WDF work item이나
queue callback으로 옮기세요.

## CMake 빌드

```powershell
cmake -S examples/kmdf/usb `
      -B artifacts/examples/kmdf-usb `
      -A x64
cmake --build artifacts/examples/kmdf-usb --config Debug
```

## Visual Studio

`crtsys_kmdf_usb_ntl_sample_vs.sln`을 여세요. 두 project 모두 기본적으로 설치된
최신 `crtsys` NuGet package를 사용합니다. 고정된 package version이 필요하면
`CrtSysPackageVersion`을 override하세요.

USB hardware 없이도 build test를 수행할 수 있습니다. runtime 검증에는 수정한
INF와 endpoint protocol에 맞는 USB device가 필요합니다.
