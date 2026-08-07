# NTL KMDF 드라이버 예제

[English](./README.md)

이 예제는 `crtsys`를 통해 MSVC STL을 사용하는 NTL 방식 KMDF 드라이버를
보여줍니다. KMDF는 WDF driver, device, queue, request, PnP, power 및 object 수명
모델을 계속 소유합니다. `crtsys`는 kernel-compatible CRT/STL 시작·종료 경로와
`ntl/kmdf/`의 얇은 C++ facade를 제공합니다.

드라이버는 격리된 테스트 VM에서 service로 load/unload할 수 있도록 non-PnP KMDF
control driver로 구성됩니다. parallel default queue는 명시적으로
`WdfExecutionLevelPassive`를 사용하므로 release IOCTL이 manual dispatch queue에서
기다리던 이전 request를 완료할 수 있습니다. IOCTL callback은 `std::vector`,
`std::accumulate`, `std::format`을 사용하고 WDF callback 경계에서 모든 C++ 예외를
잡으며 관찰한 server IRQL을 앱에 반환합니다.

동일한 open/IOCTL/close 흐름에서 WDF 소유 context storage 안의 non-trivial
`device_state`와 open별 `file_state` 객체를 생성하고 소멸합니다. 형식화된 file
callback은 native `FILE_OBJECT`를 보는 non-owning `ntl::file` view로 연결하는
`ntl::kmdf::file::wdm()` bridge도 보여줍니다. driver setup은 parent가 지정된 KMDF
work item과 passive timer도 만듭니다. work item을 flush해 PASSIVE_LEVEL 실행을
입증하고 timer로 WDF 소유 deferred callback 수명을 검증합니다.

device setup 중에는 parent가 지정된 `WDFMEMORY`도 할당해 buffer copy를 검증하고,
일반 I/O target을 만들고, 전송하지 않은 `ntl::kmdf::driver_request`를 생성한 뒤
자동 삭제합니다. 하나의 runtime 경로에서 `spin_lock`, `wait_lock`, 이동 소유하는
lookaside memory, `collection`, `string`, standalone `dpc` 같은 공통 WDF object
utility도 검증합니다. DPC callback은 `DISPATCH_LEVEL`에서 lock-free counter와
event operation만 수행하고, passive setup 경로가 결과를 기다려 검사합니다.

앱은 실제 overlapped I/O로 manual dispatch queue도 검증합니다. pending IOCTL
하나는 KMDF file object를 사용해 찾아 정상 완료하고, 두 번째는 queue에 남아 있는
동안 `CancelIoEx`로 취소합니다. 드라이버는 이동 전용 request 소유권 전환을
검증하고 framework의 `EvtIoCanceledOnQueue` callback을 정확히 한 번 완료합니다.

default queue에는 KMDF forward-progress reserved request 하나도 할당합니다. 일회성
allocation callback은 device 시작 전에 reserved request 전용
`reserved_request_resources` view를 받습니다. `request_resources` callback은 일반
request도 queue 삽입 전에 모두 기록하며 앱은 counter 증가를 검증합니다. 제한된 두
view는 request를 완료하거나 forward할 수 없고 서로 다른 형식이므로 일반 I/O를
reserved-request fallback traffic으로 잘못 취급할 수 없습니다.

이 예제는 의도적으로 non-PnP control device로 유지합니다. 형식화된 child 열거와
PDO 생성은 별도 [KMDF bus 및 PDO 예제](../bus)를 참고하세요.

## Visual Studio 및 NuGet

`crtsys_kmdf_ntl_sample_vs.sln`을 열고 package를 복원한 뒤 `Debug|x64` 또는
`Release|x64`로 빌드하세요. **Project Properties > Driver Settings > Driver
Model**에서 **Type of driver = KMDF**를 설정하고 **crtsys KMDF entry point**에서
**NTL KMDF**를 선택합니다. package는 crtsys 선택을 다음과 같이 저장합니다.

```xml
<KmdfVersion>1.15</KmdfVersion>
<CrtSysKmdfEntryPoint>NtlKmdf</CrtSysKmdfEntryPoint>
<PackageReference Include="crtsys" Version="$(CrtSysPackageVersion)" />
```

dropdown에서 이 항목을 선택하면 `ntl::kmdf::main`이 활성화됩니다. 일반 KMDF entry
model이 필요하면 **No NTL entry point**를 선택하세요.

## CMake

```powershell
cmake -S examples\kmdf\basic `
      -B artifacts\examples\kmdf-basic -A x64
cmake --build artifacts\examples\kmdf-basic --config Debug
```

기존 driver 도우미에서 KMDF version을 선택합니다.

```cmake
crtsys_add_driver(my_driver KMDF 1.15 NTL driver/main.cpp)
```

표준 KMDF `DriverEntry`를 사용하려면 `NTL`을 생략하세요. `KMDF`를 생략하면 WDM이
기본값입니다.

## VM 스모크 테스트

```bat
sc create CrtSysKmdfNtlSample binpath= "C:\path\crtsys_kmdf_ntl_sample.sys" type= kernel start= demand
sc start CrtSysKmdfNtlSample
crtsys_kmdf_ntl_sample_app.exe 36
sc stop CrtSysKmdfNtlSample
sc delete CrtSysKmdfNtlSample
```

예상 앱 출력은 다음과 같습니다.

```text
NTL KMDF ok: value=36 result=42 server_irql=0 message=KMDF transformed 36 to 42
NTL KMDF manual queue ok: released=1 canceled=1
```
