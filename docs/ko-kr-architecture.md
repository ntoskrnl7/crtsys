# 아키텍처

[한국어 문서로 돌아가기](./ko-kr.md)

`crtsys`는 Windows 드라이버를 위한 커널 모드 runtime substrate입니다.
목표는 driver project가 익숙한 MSVC C++ runtime, CRT, STL entry point를
사용하되, 그 runtime dependency를 driver-safe kernel facility와 명시적인
compatibility substrate 위에 매핑하는 것입니다.

## 책임 분리

`crtsys`는 driver binary 안의 MSVC runtime/STL integration을 담당합니다.
테스트된 driver surface에 필요한 Microsoft runtime source path를 선택하고,
hosted runtime이 보통 user-mode facility에 의존하는 부분에는 kernel-mode
runtime adapter를 제공하며, 그 결과를 driver test와 연결합니다.

`LDK`는 runtime과 STL path가 사용하는 Windows/NTDLL-compatible API surface와
ICU ABI substrate를 제공합니다. 여기에는 정상적인 user-mode process 밖에서
MSVC runtime code가 기대하는 lower-level primitive가 포함됩니다.

`NTL`은 driver code에 노출되는 C++ helper layer입니다. C++ entry point
wrapper, driver/device helper, synchronization helper, RPC-style control path,
IRQL helper, stack expansion tool을 제공합니다.

## 계층별 책임

| Layer | Responsibility |
| --- | --- |
| MSVC CRT/STL/VCRT/UCRT source path | Driver code가 사용하는 익숙한 MSVC C++/CRT/STL entry point를 유지합니다. |
| crtsys compatibility layer | Kernel-mode runtime adapter, ABI helper, 선택된 CRT/STL integration, driver-tested coverage contract를 제공합니다. |
| LDK substrate | Runtime/STL path가 요구하는 Windows/NTDLL-compatible API와 ICU ABI entry point를 제공합니다. |
| NTL | 기본 MSVC STL 표면을 바꾸지 않고 driver 형태의 선택적 C++ helper를 제공합니다. |
| WDK / NT kernel | 실제 kernel primitive, object model, IRQL rule, pool allocation, verifier environment를 제공합니다. |

## 사용자 코드에 보이는 C++ 표면

기본 의도는 일반 MSVC C++에 가깝습니다. MSVC 표준 header를 include하고,
표준 CRT/STL type을 사용하며, runtime substrate를 driver에 link합니다.
NTL 같은 kernel-specific helper는 driver 형태의 작업을 돕기 위해 제공되지만,
기본 STL path는 익숙한 MSVC STL path로 유지합니다.

그래서 coverage matrix는 별도의 compatibility 용어가 아니라 driver test에
연결됩니다. Kernel driver harness에서 실제로 실행한 표준 C++/CRT/STL path를
기록합니다.

## 소비 경로

Visual Studio/MSBuild driver project는 보통 `PackageReference`,
`Install-Package crtsys`, `msbuild /restore` 경로로 NuGet package를
사용합니다. 자세한 내용은 [MSBuild/NuGet 빠른 시작](./ko-kr-msbuild-nuget-quickstart.md)을
보세요. CMake project는 GitHub Release prebuilt bundle을
`find_package(crtsys CONFIG REQUIRED)`로 소비하거나, CPM.cmake로 `crtsys`를
source에서 빌드할 수 있습니다.

세 경로 모두 같은 모델을 대상으로 합니다. `crtsys`는 driver에 link되고,
driver는 정상적인 WDK driver로 남습니다.

## 테스트된 표면

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
