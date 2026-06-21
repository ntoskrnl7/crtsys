# crtsys

Windows 커널 드라이버(`.sys`)를 위한 **C**/C++ **R**un**t**ime 지원
라이브러리입니다.

[![CMake](https://github.com/ntoskrnl7/crtsys/actions/workflows/cmake.yml/badge.svg)](https://github.com/ntoskrnl7/crtsys/actions/workflows/cmake.yml)
![GitHub](https://img.shields.io/github/license/ntoskrnl7/crtsys)
![GitHub release](https://img.shields.io/github/v/release/ntoskrnl7/crtsys)
![Windows 7+](https://img.shields.io/badge/Windows-7%2B-blue?logo=windows&logoColor=white)
![Visual Studio 2017+](https://img.shields.io/badge/Visual%20Studio-2017%2B-682270?logo=visualstudio&logoColor=white)
![CMake 3.14+](https://img.shields.io/badge/CMake-3.14%2B-064f8c?logo=cmake&logoColor=white)
![C++14+](https://img.shields.io/badge/C%2B%2B-14%2B-00599c?logo=cplusplus&logoColor=white)

[English documentation](../README.md)

`crtsys`는 Windows 커널 드라이버에서 C++ 언어 기능, 일부 Microsoft STL
기능, 그리고 커널에 맞춘 작은 C++ 헬퍼들을 사용할 수 있도록 돕는 실험적
런타임 레이어입니다. WDK와 CMake 기반 빌드 흐름은 유지하면서, 드라이버
코드에서 더 익숙한 C++ 개발 경험을 제공하는 것을 목표로 합니다.

프로젝트는 크게 세 부분으로 구성됩니다.

- 커널 모드 빌드를 위한 CRT/STL 호환 코드.
- 런타임과 STL이 필요로 하는 일부 Win32/NTDLL 유사 API를 제공하는
  [`Ldk`](https://github.com/ntoskrnl7/ldk).
- 드라이버 객체와 동기화 코드를 C++ 스타일로 감싸는 NTL 헬퍼.

## 설계 모델

`crtsys`는 커널 모드 C++ 런타임 기반 계층입니다. 목적은 MSVC C++ 런타임,
CRT, STL, NTL 기능의 통제된 일부를 Windows 드라이버 안에서 사용할 수
있게 하되, WDK 드라이버 모델을 벗어나지 않는 것입니다.

주요 대상은 드라이버 제어 경로입니다. 예를 들면 초기화, unload, device
setup, IOCTL/RPC 처리, worker thread coordination, ownership cleanup,
error handling 같은 경로입니다. 이런 코드는 보통 `PASSIVE_LEVEL`에서
실행되며, underlying WDK API가 허용하는 일부 경우에만 `APC_LEVEL`까지
수용합니다.

이 프로젝트는 임의의 user-mode C++ 코드, 임의의 STL 사용, 임의의 Win32
가정이 커널 모드에서 안전하다고 약속하지 않습니다. API가 더 넓은 계약을
명시하지 않는 한 기본값은 `PASSIVE_LEVEL`로 보아야 합니다. DPC, ISR,
paging I/O, spin lock을 잡은 상태, 기타 elevated-IRQL 경로에서는 해당
API가 그 문맥을 명시적으로 문서화하고 테스트한 경우에만 사용하세요.

전체 설계 의도는 [설계 근거와 운영 경계](./ko-kr-design-rationale.md)에
정리되어 있습니다.

## 상태

이 프로젝트는 완전한 사용자 모드 CRT도, 완전한 Windows 호환 레이어도,
드라이버 설계를 대신해주는 도구도 아닙니다. 커널 모드에서 유용한 C++ 및
STL 시나리오를 가능하게 해주지만, 실제 드라이버는 배포 대상 Windows,
WDK, SDK, Visual Studio, 아키텍처, Driver Verifier 설정에서 반드시 따로
검증해야 합니다.

현재 알려진 제약은 다음과 같습니다.

- 런타임 기반 C++/CRT/STL 기능은 API 문서가 다르게 말하지 않는 한
  `PASSIVE_LEVEL` 기능으로 취급해야 합니다.
- MSVC thread-safe function-local static initialization은 지원합니다.
  일반 C++ `thread_local` storage는 true per-thread TLS로 지원하지
  않습니다.
- 커널 스택은 작습니다. 예외 처리나 STL 사용으로 스택을 많이 쓰는 경로는
  `ntl::expand_stack` 사용을 고려하세요.
- SDK와 WDK 버전이 다르면 빌드가 실패할 수 있습니다. 가능하면 같은 버전의
  SDK와 WDK를 사용하세요.
- Windows 11 24H2 WDK부터는 x86 커널 모드 드라이버 개발을 지원하지
  않습니다. x86 드라이버 대상이 필요하면 WDK 23H2 이하를 사용해야 합니다.

## 기능

README 계열 문서에서는 기능 목록을 짧게 유지합니다. 어떤 항목이 정확히
테스트되었는지는 별도 문서에 테스트 링크와 함께 정리했습니다.

- [설계 근거와 운영 경계](./ko-kr-design-rationale.md)
- [상세 기능 지원 현황](./ko-kr-feature-coverage.md)
- [NTL API 문서](./ko-kr-ntl-api.md)
- [NTL 사용 예제](./ko-kr-usage-examples.md)

상위 수준의 지원 범위는 다음과 같습니다.

- C++ 런타임 지원
  - 비지역 static 초기화
  - function-local static 초기화
  - 동적 초기화
  - 예외 처리
- Microsoft STL 지원
  - 시간 유틸리티
  - 스레딩 primitive
  - 동기화 primitive
  - 비동기 primitive
  - stream 객체
- C 런타임 지원
  - 지원되는 STL 경로에 필요한 CRT glue
  - math helper
  - floating-point classification helper
- NTL 지원
  - C++ 드라이버 진입점 래퍼
  - driver/device helper
  - RPC helper
  - IRQL 및 동기화 helper

## 요구 사항

- Windows 7 이상
- Visual Studio 또는 Build Tools 2017 이상
- 선택한 Visual Studio toolset과 호환되는 Windows SDK 및 WDK
- CMake 3.14 이상
- Git

테스트된 toolchain에는 Visual Studio 2017, 2019, 2022와 `10.0.17763.0`,
`10.0.18362.0`, `10.0.22000.0`, `10.0.22621.0` SDK/WDK 계열이 포함됩니다.

Visual Studio 2017은 일부 CRT 소스/헤더 구성이 부족한 경로가 있어서,
해당 toolset에서는 일부 UCXXRT 호환 코드를 사용합니다.

## 빠른 시작

드라이버 프로젝트에 `cmake/CPM.cmake`를 배치한 뒤 `crtsys`를 추가합니다.

```cmake
cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(my_driver LANGUAGES C CXX)

include(cmake/CPM.cmake)

set(CRTSYS_NTL_MAIN ON)
CPMAddPackage("gh:ntoskrnl7/crtsys@<version>")
include(${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake)

crtsys_add_driver(my_driver src/main.cpp)
```

`CRTSYS_NTL_MAIN`을 켜면 C++ 진입점 래퍼를 사용합니다. 이 경우 직접
`DriverEntry`를 작성하지 않고 `ntl::main`을 정의합니다.

```cpp
#include <iostream>
#include <string>
#include <ntl/driver>

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  std::wcout << L"load: " << registry_path << L"\n";

  driver.on_unload([registry_path]() {
    std::wcout << L"unload: " << registry_path << L"\n";
  });

  return ntl::status::ok();
}
```

`CRTSYS_NTL_MAIN`을 끄면 일반 WDK 드라이버처럼 `DriverEntry`를 직접
작성하고 초기화를 수동으로 처리하면 됩니다.

Visual Studio generator로 빌드합니다.

```bat
cmake -S . -B build_x64 -A x64
cmake --build build_x64 --config Debug
```

`crtsys`는 기본적으로 진단용 `KdBreakPoint()` 호출을 유지합니다. 진단용
breakpoint 없이 빌드하려면 다음처럼 설정합니다.

```bat
cmake -S . -B build_x64 -A x64 -DCRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS=OFF
```

## NuGet 패키지

`crtsys`의 NuGet 배포는 다음 하나뿐입니다.

- `crtsys.<version>.nupkg`: Visual Studio/MSBuild 프로젝트용

MSBuild 설치:

```powershell
Install-Package crtsys
```

## GitHub Release prebuilt 번들

GitHub Release는 별도 오프라인 번들을 배포합니다.

- `crtsys-<version>-prebuilt.zip`: 헤더, 문서, CMake 헬퍼,
  x64/ARM64의 `Debug`/`Release` 사전 빌드 라이브러리.
- `crtsys-<version>-SHA256SUMS.txt`

WDK CMake/오프라인 부트스트랩은 prebuilt 번들을 사용합니다.

```powershell
Expand-Archive .\crtsys-<version>-prebuilt.zip .

```

```cmake
# 기존 드라이버 CMakeLists.txt
find_package(crtsys CONFIG REQUIRED PATHS path/to/crtsys-<version>)
crtsys_add_driver(my_driver src/main.cpp)
```

`prebuilt.zip`은 GitHub Release 전용 번들이며 NuGet 패키지가 아닙니다.

## CMake install

소스 트리 소비자는 로컬 CMake 패키지를 설치해서 사용할 수 있습니다.

```bat
cmake -S . -B build_x64 -A x64 -DCMAKE_INSTALL_PREFIX=%CD%\artifacts\install\crtsys
cmake --build build_x64 --config Release --target crtsys
cmake --install build_x64 --config Release
```

설치된 패키지는 소비자 프로젝트에서 다음처럼 찾습니다.

```cmake
find_package(crtsys CONFIG REQUIRED PATHS path/to/install-prefix)
crtsys_add_driver(my_driver src/main.cpp)
```

설치 트리는 GitHub Release prebuilt 번들과 같은 native library 레이아웃인
`lib/native/<arch>/<config>`를 사용합니다.

install 흐름은 다음 스모크 테스트로 확인할 수 있습니다.

```powershell
.\scripts\cmake\Test-CrtSysInstall.ps1 -Architecture x64 -Configuration Release
```

릴리스/게시 방법, release helper, Trusted Publishing 설정은 `nuget/README.md`에
모아서 정리해두었습니다. 자세한 앱/드라이버 동작 방식도 같은 문서에서 확인하세요.

## 이 저장소 빌드

저장소를 clone한 뒤, 현재 호스트 아키텍처 기준으로 테스트 앱과 드라이버를
빌드합니다.

```bat
git clone https://github.com/ntoskrnl7/crtsys
cd crtsys
test\build.bat
```

특정 대상을 직접 빌드하려면 다음처럼 실행합니다.

```bat
build.bat test\cmake\app x64 Debug
build.bat test\cmake\driver x64 Debug
build.bat test\cmake\app x64 Release
build.bat test\cmake\driver x64 Release
```

지원하는 모든 아키텍처와 구성 조합을 빌드하려면 다음 명령을 사용합니다.

```bat
build_all.bat test\cmake\app
build_all.bat test\cmake\driver
```

`build_all.bat`은 빌드를 순차 실행하고, 첫 실패의 exit code를 반환합니다.
두 번째 인자로 `Debug` 또는 `Release`를 넘기면 해당 구성만 빌드합니다.

일반적인 Debug 출력 경로는 다음과 같습니다.

```text
test\cmake\driver\build_x64\Debug\crtsys_test.sys
test\cmake\app\build_x64\Debug\crtsys_test_app.exe
```

## 테스트 실행

`crtsys_test.sys`는 커널 드라이버입니다. CI에서는 빌드 검증만 할 수 있고,
실제 로드와 실행은 Windows 드라이버 테스트 환경에서 수행해야 합니다.

CI 빌드 workflow와 선택적 self-hosted 드라이버 로드 테스트 경로는
[CI driver load tests](./ci-driver-load-tests.md)에 정리되어 있습니다.

```bat
sc create CrtSysTest binpath= "C:\path\to\crtsys_test.sys" displayname= "crtsys test" start= demand type= kernel
sc start CrtSysTest

C:\path\to\crtsys_test_app.exe

sc stop CrtSysTest
sc delete CrtSysTest
```

테스트 드라이버는 내부적으로 Google Test를 사용합니다. 결과는 DebugView,
WinDbg 또는 일반적인 커널 디버깅 환경에서 확인하세요.

## 저장소 구조

```text
cmake/             CrtSys.cmake를 포함한 CMake 헬퍼
include/ntl/       NTL C++ 헬퍼 헤더
include/.internal/ 내부 버전 및 toolchain 호환 헤더
src/               crtsys 런타임 및 CRT/STL 호환 코드
test/cmake/app/    CMake 사용자 모드 테스트 companion 앱
test/cmake/driver/ CMake 커널 모드 테스트 드라이버
test/nuget/        Visual Studio WDK NuGet 소비자 테스트 프로젝트
docs/              추가 문서
```

## 배경

`crtsys`는 UCXXRT와 KTL 같은 커널 C++ 런타임 프로젝트를 실험한 뒤 만들어진
프로젝트입니다. 목표는 CMake/WDK 흐름을 실용적으로 유지하면서 실제 드라이버
실험에 충분한 Microsoft CRT/STL 동작을 지원하는 것입니다.

프로젝트는 Microsoft CRT/STL 소스를 vendored library처럼 다루지 않으려는
방향으로 설계되었습니다. 대신 로컬에 설치된 Visual Studio/Build Tools의
구성과 작은 커널 모드 호환 코드를 함께 사용합니다. Microsoft가 제공하는
소스/헤더 구성이 부족한 오래된 toolset에서는 일부 호환 코드를 사용합니다.

아래 프로젝트의 구현도 필요한 곳에서 참고했습니다.

- [RetrievAL](https://github.com/SpoilerScriptsGroup/RetrievAL)
- [musl](https://github.com/bminor/musl)
- [zpp serializer](https://github.com/eyalz800/serializer)

## 로드맵

- 아직 지원하지 않는 C++ 및 STL 기능을 확장합니다. 특히 true
  `thread_local` storage가 남아 있습니다.
- Visual Studio 2017 호환성 간격을 줄이고 toolset별 호환 코드를 더
  작게 유지합니다.
- 적절한 테스트 환경이 준비되는 범위에서 실제 드라이버 로드/실행 CI
  coverage를 넓힙니다.
