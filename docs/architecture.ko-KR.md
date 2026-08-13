# 아키텍처

[한국어 문서로 돌아가기](./README.ko-KR.md)

`crtsys`는 Windows 드라이버용 커널 모드 런타임 기반 계층입니다.
드라이버 프로젝트가 익숙한 MSVC C++ 런타임, CRT, STL 진입점을 사용하면서도,
그 런타임 의존성을 드라이버에서 안전한 커널 기능과 명시적인 호환성 계층에
연결하는 것이 목표입니다.

## 책임 분리

`crtsys`는 드라이버 바이너리 안의 MSVC 런타임/STL 통합을 담당합니다.
검증한 드라이버 기능 범위에 필요한 Microsoft 런타임 소스 경로를 선택하고,
호스팅된 런타임이 보통 사용자 모드 기능에 의존하는 부분에는 커널 모드
런타임 어댑터를 제공하며, 그 동작을 드라이버 테스트로 검증합니다.

`LDK`는 런타임과 STL 경로가 사용하는 Windows/NTDLL 호환 API 계층과 ICU ABI
기반을 제공합니다. 여기에는 일반적인 사용자 모드 프로세스 밖에서 MSVC 런타임
코드가 호출할 것으로 기대하는 저수준 기본 기능이 포함됩니다.

`NTL`은 드라이버 코드에 제공하는 C++ 도우미 계층입니다. C++ 진입점 래퍼,
드라이버/디바이스 도우미, 동기화 도우미, RPC 방식 제어 경로, IRQL 도우미 및
스택 확장 도구를 제공합니다.

## 계층별 책임

| 계층 | 역할 |
| --- | --- |
| MSVC CRT/STL/VCRT/UCRT 소스 경로 | 드라이버 코드가 사용하는 익숙한 MSVC C++/CRT/STL 진입점을 유지합니다. |
| crtsys 호환성 계층 | 커널 모드 런타임 어댑터, ABI 도우미, 선택된 CRT/STL 통합, 드라이버 검증 범위 계약을 제공합니다. |
| LDK 기반 계층 | 런타임/STL 경로가 요구하는 Windows/NTDLL 호환 API와 ICU ABI 진입점을 제공합니다. |
| NTL | 기본 MSVC STL API를 바꾸지 않고 드라이버용 선택적 C++ 도우미를 제공합니다. |
| WDK / NT 커널 | 실제 커널 기본 기능, 객체 모델, IRQL 규칙, 풀 할당, Verifier 환경을 제공합니다. |

## 사용자 코드에 노출되는 C++ 영역

기본 사용 방식은 일반 MSVC C++와 같습니다. MSVC 표준 헤더를 include하고,
표준 CRT/STL 타입을 사용하며, 런타임 기반 계층을 드라이버에 link합니다.
NTL 같은 커널 전용 도우미는 드라이버 작업을 돕기 위해 제공되지만,
기본 STL 경로는 익숙한 MSVC STL 경로로 유지합니다.

그래서 지원 범위 표는 별도의 호환성 용어가 아니라 드라이버 테스트에
연결됩니다. 커널 드라이버 하니스에서 실제로 실행한 표준 C++/CRT/STL 경로를
기록합니다.

## 여러 드라이버의 런타임 상태

`crtsys`는 정적 런타임 기반 계층으로 link됩니다. 따라서 서로 다른 두
드라이버가 같은 커널 세션 안에서 서로 다른 crtsys 런타임 사본을 가질 수
있습니다. 사용자 모드의 프로세스 전체 또는 로더 전체 동작을 흉내 내는 런타임
상태는 이런 이미지별 충돌을 피해야 합니다.

MSVC 컴파일러 TLS 상태에 대해서는 crtsys가 공유 커널 섹션 안에 시스템 공간
TLS 슬롯 벡터와 슬롯 할당기를 둡니다. 각 crtsys 연결 드라이버는 고유한 MSVC
`_tls_index`를 받고, 자신의 컴파일러 TLS 이미지 버퍼를 그 공유 벡터에
등록합니다. 그래서 스레드 안전한 함수 지역 `static` 초기화 같은 런타임 경로가
여러 crtsys 연결 드라이버를 동시에 로드했을 때 서로 충돌하지 않습니다.

이 보장은 드라이버 이미지 격리이지 스레드별 저장소 의미가 아닙니다. 두 crtsys
연결 드라이버가 같은 컴파일러 TLS 슬롯을 공유하는 문제를 막지만, 컴파일러 TLS
값을 커널 스레드마다 다르게 만들어 주지는 않습니다. 다시 말해 이 구조는 여러
드라이버 이미지 사이의 `_tls_index` 충돌 방지 장치입니다. GS/TEB 기반 사용자
모드 TLS처럼 같은 변수 선언이 스레드별로 다른 저장소를 갖도록 만드는 기능은
아닙니다. 커널 모드에서 GS 기반 TLS 가정은 각 스레드의 사용자 모드 TEB가 아니라
프로세서 로컬 KPCR에 걸립니다. 따라서 `thread_local T value`를 C++ 스레드별
저장소로 사용하면 안 됩니다.

## 드라이버 모델 통합

crtsys는 하나의 static runtime library 안에 진입 경로를 별도 object로
분리합니다. 따라서 모든 project가 같은 driver model을 강제로 사용할 필요가
없습니다.

| 프로젝트 모델 | OS image 진입점 | 드라이버 코드 진입점 |
| --- | --- | --- |
| NTL WDM | `CrtSysDriverEntry` | `ntl::main` |
| 일반 WDM | `CrtSysWdmDriverEntry` | 일반 `DriverEntry` |
| 일반 KMDF | `CrtSysKmdfDriverEntry` | 일반 `DriverEntry`에서 `WdfDriverCreate` 호출 |
| NTL KMDF | `CrtSysNtlKmdfDriverEntry` | `driver_builder`를 받는 `ntl::kmdf::main` |

KMDF 진입 경로는 crtsys runtime을 초기화한 뒤 WDF가 제공하는 `FxDriverEntry`를
호출합니다. WDF는 driver의 일반 `DriverEntry`를 호출하고 dispatch, PnP, power,
queue, request 처리를 계속 소유합니다. crtsys는 WDF와 사용자 unload callback이
반환된 다음 C++ runtime을 해제할 수 있도록 unload 경로만 감쌉니다. KMDF의
major-function entry를 교체하지 않습니다. 선택적인 NTL KMDF 경로는 driver가
보는 초기화 API만 바꾸며, 내부적으로 `WdfDriverCreate`를 호출하고 WDF object
ownership과 major-function 처리는 그대로 유지합니다.

## 소비 경로

Visual Studio/MSBuild driver project는 보통 `PackageReference`,
`Install-Package crtsys`, `msbuild /restore` 경로로 NuGet package를
사용합니다. 자세한 내용은 [MSBuild/NuGet 빠른 시작](./msbuild-nuget-quickstart.ko-KR.md)을
보세요. CMake project는 GitHub Release prebuilt bundle을
`find_package(crtsys CONFIG REQUIRED)`로 소비하거나, CPM.cmake와
`CPMAddPackage("gh:ntoskrnl7/crtsys@<version>")`로 GitHub의 `crtsys`를
직접 소비할 수 있습니다.

세 경로 모두 같은 모델을 대상으로 합니다. `crtsys`는 driver에 link되고,
driver는 정상적인 WDK driver로 남습니다.

## 검증된 영역

Feature coverage matrix는 증거 기반 문서입니다. 목록에 있는 C++/CRT/STL
path는 kernel driver test target에 포함되어 그 harness에서 실행됩니다.
목록에 없는 header나 code path는 자동으로 미지원이라는 뜻이 아니라, 아직
명시적인 driver-tested matrix에 포함하지 않았다는 뜻입니다.

Driver-tested 영역에는 C++ initialization, C++ exception handling, SEH
handling, RTTI, core STL container와 utility, filesystem, format/print, regex,
locale, random, chrono/timezone, synchronization, threading, async, atomic,
stream, PMR, 일부 CRT/math path가 포함됩니다.

## 경계

`crtsys`는 임의의 user-mode 가정이 kernel mode에서 안전하다고 만들어주는
프로젝트가 아닙니다. IRQL, pageable code, pool allocation, stack depth,
unload safety, verifier behavior, HVCI, target OS validation은 여전히 driver가
책임져야 합니다.

기능이 더 넓은 계약을 명시하지 않는 한, runtime-backed C++/CRT/STL path는
`PASSIVE_LEVEL` control-path 기능으로 취급하세요.
