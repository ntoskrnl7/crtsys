# NTL 예제 드라이버

[English](./README.md)

이 예제는 `crtsys`에 포함된 NTL 도우미 계층을 사용하는 작은 WDK 드라이버입니다.
단순한 테스트 장치보다는 실용적인 드라이버 구조를 보여주는 데 목적이 있습니다.

이 드라이버는 다음 기능을 보여줍니다.

- C++ 진입점으로 사용하는 `ntl::main`
- 선택적 `Parameters` 레지스트리 설정을 읽는 `ntl::driver_config`
- 장치와 DOS 심볼릭 링크의 수명을 관리하는 `ntl::device_endpoint`
- 타입이 지정된 `CTL_CODE` 계약을 정의하는 `ntl::ioctl`
- 디스패치와 언로드를 동기화하는 `ntl::remove_lock`
- 작업을 PASSIVE_LEVEL로 넘기는 `ntl::passive_executor`
- 커널 풀에서 STL/PMR 메모리를 할당하는 `ntl::pmr::pool_resource`

## Visual Studio / NuGet

WDK 워크로드가 설치된 Visual Studio에서
[`crtsys_ntl_sample_vs.sln`](./crtsys_ntl_sample_vs.sln)을 여세요. 솔루션에는
다음 프로젝트가 있습니다.

- `crtsys_ntl_sample`: 커널 드라이버
- `crtsys_ntl_sample_app`: 사용자 모드 ping 앱

NuGet 패키지를 복원한 다음 `Debug|x64` 또는 `Release|x64`로 빌드하세요. 프로젝트
파일에서는 다음 참조를 사용합니다.

```xml
<PackageReference Include="crtsys" Version="$(CrtSysPackageVersion)" />
```

`CrtSysPackageVersion`의 기본값은 `*`이므로 NuGet 복원 시 구성된 패키지
소스에서 최신 안정 버전의 `crtsys` 패키지를 선택합니다. 재현 가능한 빌드가
필요하면 MSBuild에서 정확한 패키지 버전을 지정하세요.

```bat
msbuild crtsys_ntl_sample_vs.sln /restore /p:Configuration=Debug /p:Platform=x64 /p:CrtSysPackageVersion=0.1.32
```

## CMake 빌드

저장소 루트에서 다음 명령을 실행합니다.

```bat
cmake -S examples\ntl-driver -B examples\ntl-driver\build_x64 -A x64
cmake --build examples\ntl-driver\build_x64 --config Debug
```

Debug 빌드 결과는 다음과 같습니다.

```text
examples\ntl-driver\build_x64\Debug\crtsys_ntl_sample.sys
examples\ntl-driver\build_x64\Debug\crtsys_ntl_sample_app.exe
```

기능을 시험하는 동안 진단 중단점을 비활성화하려면 다음과 같이 구성하세요.

```bat
cmake -S examples\ntl-driver -B examples\ntl-driver\build_x64 -A x64 -DCRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS=OFF
```

## IOCTL 계약

공유 계약은
[`shared/ntl_sample_ioctl.hpp`](./shared/ntl_sample_ioctl.hpp)에 있습니다.
드라이버에서는 `<wdm.h>` 뒤에, 사용자 모드 동반 앱에서는 `<winioctl.h>` 뒤에
이 헤더를 포함할 수 있습니다.

예제 드라이버는 `ntl_sample::ping_ioctl_code`를 노출합니다. 요청에는 정수 값 하나가
들어갑니다. 응답에는 그 값을 1 증가시킨 값, 단조 증가하는 시퀀스 번호, 선택적
레지스트리 `Flags` 설정, 그리고 PASSIVE_LEVEL 작업 항목에서 계산한 작은 체크섬이
포함됩니다.

## 로드

평소 사용하는 격리된 드라이버 테스트 VM에서 실행하세요. 예를 들면 다음과 같습니다.

```bat
sc create CrtSysNtlSample binpath= "C:\path\to\crtsys_ntl_sample.sys" type= kernel start= demand
sc start CrtSysNtlSample
sc stop CrtSysNtlSample
sc delete CrtSysNtlSample
```

## 사용자 모드 ping 앱

예제에는 `\\.\CrtSysNtlSample`을 열고 공유
`ntl_sample::ping_ioctl_code` 요청을 보내는 작은 동반 앱이 포함되어 있습니다.

```bat
examples\ntl-driver\build_x64\Debug\crtsys_ntl_sample_app.exe 41
```

예상 출력은 다음과 같습니다.

```text
ping ok: request=41 reply=42 sequence=1 flags=0 checksum=42
```

시퀀스 번호는 성공한 IOCTL마다 증가합니다. 서비스 레지스트리 키 아래에
`Parameters\Flags` DWORD가 있으면 드라이버가 해당 값을 응답과 체크섬에 반영합니다.
