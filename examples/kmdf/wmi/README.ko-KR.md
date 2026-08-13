# NTL KMDF WMI 예제

[English](./README.md)

이 PnP 드라이버는 타입이 지정된 `ntl::kmdf` WMI API를 처음부터 끝까지 보여줍니다.

- query, whole-instance set, set-item 및 method callback을 제공하는 MOF 기반 data
  provider
- event-only provider와 사용자 모드 WMI event subscription
- device와 instance용 NTL 관리 C++ context
- `ROOT\\WMI`와 device interface를 검증하는 사용자 모드 verifier

Visual Studio에서 `crtsys_kmdf_wmi_ntl_sample_vs.sln`을 빌드하거나 CMake로 이
directory를 구성하세요. WDK MOF tool은 binary MOF를 `CrtSysKmdfWmi` resource로
embed하기 전에 생성하고 검증합니다.

```powershell
cmake -S examples\kmdf\wmi `
      -B artifacts\examples\kmdf-wmi -A x64
cmake --build artifacts\examples\kmdf-wmi --config Debug
```

관리자 PowerShell session에서 root-enumerated test device를 설치합니다.

```powershell
.\install.ps1 -PackageDirectory .\x64\Debug
```

그다음 `crtsys_kmdf_wmi_ntl_sample_app.exe`를 실행하세요. 앱은 WMI instance를
조회하고 갱신하며 `Transform`을 호출하고 event class를 subscribe한 뒤 sample
IOCTL로 event를 발생시키고 반환된 모든 값을 검증합니다.

`remove.ps1`로 sample device를 제거합니다. query, set, set-item 및 method callback은
native KMDF WMI 계약에 따라 `PASSIVE_LEVEL`에서 실행되므로 예제의 일반 CRT/STL
작업을 허용합니다. WMI provider 및 instance object의
`WDF_OBJECT_ATTRIBUTES::ExecutionLevel`은
`WdfExecutionLevelInheritFromParent`로 유지해야 합니다. WDF는 이 object type에
명시적인 execution level을 지정하면 거부합니다. event-only provider는 KMDF의
single-instance 경로를 따르며 `wmi_provider_config`에서 instance와 함께 생성됩니다.
