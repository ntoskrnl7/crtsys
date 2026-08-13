# crtsys NuGet 패키지

`crtsys`는 Windows 커널 드라이버용 경량 C/C++ 런타임 + 도우미 세트입니다.
선택한 MSVC/C++ 런타임, CRT/STL 스타일 API 및
NTL 추상화를 통해 WDK 드라이버 코드를 보다 자연스러운 C++ 흐름으로 작성할 수 있습니다.

이 NuGet 패키지는 **Visual Studio/MSBuild** 소비자(`crtsys.<version>.nupkg`)용입니다.

## 빠른 시작

최신 MSBuild 프로젝트의 경우 `PackageReference`를 추가합니다.

```xml
<ItemGroup>
  <PackageReference Include="crtsys" Version="<version>" />
</ItemGroup>
```

그런 다음 MSBuild를 사용하여 복원하고 빌드합니다.

```powershell
msbuild .\my_driver.vcxproj /restore /p:Configuration=Debug /p:Platform=x64
```

x86 드라이버 프로젝트의 경우 MSBuild `Win32` 플랫폼 이름을 사용합니다.

```powershell
msbuild .\my_driver.vcxproj /restore /p:Configuration=Debug /p:Platform=Win32
```

Visual Studio 패키지 관리자 콘솔의 경우:

```powershell
Install-Package crtsys
```

- 앱 프로젝트에는 호환성 헤더와 include 경로가 제공됩니다.
- 사용자 모드 앱 프로젝트에는 NTL의 HTTP, WebSocket 및 gRPC 변환에서
  사용하는 gzip, RFC 1950 `deflate`, Brotli 헤더와 정적 라이브러리가
  자동으로 제공됩니다. 상태를 유지하며 점진적으로 처리하는
  `Content-Encoding` 스트림 API도 포함됩니다. 앱에서 이러한 표준 코덱을
  사용하지 않거나 자체 레지스트리를 제공하는 경우에만
  `<CrtSysUseNtlContentCodecs>false</CrtSysUseNtlContentCodecs>`를 설정하세요.
- 사용자 모드 및 드라이버 프로젝트에는 NTL의 선택적 QUIC 백엔드가 사용하는
  정확한 공개 `msquic.h` 리비전이 제공됩니다. 패키지는 컴파일 시 필요한
  ABI만 제공하며 `msquic.dll`이나 커널 NMR 공급자는 설치하지 않습니다.
  MsQuic 기반 NTL 헤더를 컴파일하지 않거나 다른 include 경로에서 같은 고정
  ABI를 직접 제공하는 경우에만
  `<CrtSysUseNtlMsQuicHeaders>false</CrtSysUseNtlMsQuicHeaders>`를 설정하세요.
- WDK 드라이버 프로젝트에는 선택한 MSVC 툴셋과 아키텍처에 맞는
  `crtsys.lib` 및 `Ldk.lib` 링크 설정이 자동으로 적용됩니다.
- 커널 MsQuic은 명시적으로 선택해야 하는 배포 기능입니다. 드라이버 모델
  속성 페이지에서 **NTL kernel MsQuic backend**를 켜거나 드라이버 프로젝트에
  `<CrtSysUseNtlKernelMsQuic>true</CrtSysUseNtlKernelMsQuic>`를 설정하면 Windows
  10 버전 2004 이상 계약이 선택되고 `netio.lib`가 링크됩니다. 이 라이브러리는
  `ntl::net::kernel::msquic_provider`가 사용하는 NMR 클라이언트 호출을
  제공합니다. 고정된 헤더가 제공된다는 이유만으로 모든 드라이버의 최소 OS
  버전이 올라가지는 않습니다.

### 드라이버 모델 선택

**프로젝트 속성 > 드라이버 설정 > 드라이버 모델**에서 사용할 진입점 모델을
선택합니다.

| 프로젝트 모델 | 선택 항목 | 구현할 함수 | 패키지가 자동으로 적용하는 항목 |
| --- | --- | --- | --- |
| WDM | **NTL WDM** | `ntl::main` | NTL WDM 진입점 래퍼 사용 |
| KMDF | **NTL KMDF** | `ntl::kmdf::main` | NTL KMDF 진입점 래퍼 사용. PnP, 전원 및 디스패치는 계속 WDF가 담당 |
| 미니필터 | **NTL Minifilter** | `ntl::flt::main` | Filter Manager 진입점 래퍼 사용 및 `fltmgr.lib` 링크 |
| WFP 콜아웃 | **NTL WFP** | `ntl::main` | WFP/NDIS 대상 정의 적용, `fwpkclnt.lib` 및 커널 콘텐츠 코덱 링크 |
| NDIS lightweight filter | **NTL NDIS LWF** | `ntl::main` | NDIS 6.30 LWF 정의 적용 및 `ndis.lib` 링크 |

프로젝트의 기존 `DriverEntry`, `WdfDriverCreate`, 미니필터, WFP 또는 NDIS 진입
경로를 유지하려면 **No NTL entry point**를 선택합니다. 모델별 API와 완전한
예제는 [KMDF 가이드](../docs/ntl/kmdf.ko-KR.md),
[미니필터 가이드](../docs/ntl/minifilter.ko-KR.md),
[WFP 가이드](../docs/ntl/wfp-guide.ko-KR.md), [NDIS 예제](../examples/ndis) 및
[예제 모음](../examples)을 참조하세요.

이 NuGet 패키지는 다음 용도에 적합합니다.

- 제어 경로 코드의 현대적인 C++ 소유권 관리(`ntl::driver`, `ntl::device`,
  언로드 콜백)
- `ntl::status`를 사용한 작고 읽기 쉬운 상태/오류 흐름
- 하나의 헤더에서 공유하는 사용자/커널 계약(`shared/*.hpp`)
- 드라이버 리소스 및 정리를 위한 안정적인 RAII 스타일 수명 주기

최소 드라이버 진입점 예제:

```cpp
#include <ntl/driver>

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;
  driver.on_unload([]() {});
  return ntl::status::ok();
}
```

### IOCTL 예제(커널 + 앱)

공유 헤더(`shared/demo_ioctl.hpp`):

```cpp
// shared/demo_ioctl.hpp
#pragma once

#define DEMO_DEVICE_NAME L"demo_device"
#define DEMO_IOCTL_ECHO \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
```

커널 측:

```cpp
#include <string>
#include <wdm.h>
#include <ntl/driver>
#include "shared/demo_ioctl.hpp" // DEMO_DEVICE_NAME/DEMO_IOCTL_*

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;

  auto options = ntl::device_options()
    .name(DEMO_DEVICE_NAME)
    .type(FILE_DEVICE_UNKNOWN)
    .exclusive(false);

  auto device = driver.create_device<void>(options);
  device->on_device_control([](const ntl::device_control::code& code,
                               const ntl::device_control::in_buffer& in,
                               ntl::device_control::out_buffer& out) {
    // register IRP_MJ_DEVICE_CONTROL logic without switch-heavy boilerplate
    if (code == DEMO_IOCTL_ECHO && in.ptr && out.ptr) {
      const auto bytes = in.size < out.size ? in.size : out.size;
      RtlCopyMemory(out.ptr, in.ptr, bytes);
      out.size = bytes;
    }
  });

  driver.on_unload([device]() mutable {
    // keep cleanup in one place; shared_ptr reset happens on unload
    device.reset();
  });

  return ntl::status::ok();
}
```

앱 측:

```cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <iostream>
#include "shared/demo_ioctl.hpp"

int wmain() {
  const HANDLE device = CreateFileW(
      L"\\\\?\\Global\\GLOBALROOT\\Device\\" DEMO_DEVICE_NAME,
      GENERIC_READ | GENERIC_WRITE,
      0, nullptr, OPEN_EXISTING, 0, nullptr);

  if (device == INVALID_HANDLE_VALUE) {
    std::cerr << "failed to open device\n";
    return 1;
  }

  char request[] = "hello";
  char reply[sizeof request] = {};
  DWORD returned = 0;
  const BOOL ok = DeviceIoControl(device,
                                  DEMO_IOCTL_ECHO,
                                  request,
                                  static_cast<DWORD>(sizeof request),
                                  reply,
                                  static_cast<DWORD>(sizeof reply),
                                  &returned,
                                  nullptr);
  CloseHandle(device);
  if (!ok) {
    std::cerr << "DeviceIoControl failed\n";
    return 1;
  }
  return 0;
}
```

### RPC 예제(커널 + 앱)

공유 헤더(`shared/demo_rpc.hpp`):

```cpp
// shared/demo_rpc.hpp
#pragma once

NTL_RPC_BEGIN(demo_rpc)

NTL_ADD_CALLBACK_ID_2(demo_rpc, 0x801, int, add, int, left, int, right, {
  return left + right;
})

NTL_ADD_CALLBACK_ID_1(demo_rpc, 0x802, int, negate, int, value, {
  return -value;
})

NTL_RPC_END(demo_rpc)
```

커널 측:

```cpp
#include <memory>
#include <ntl/driver>
#include <ntl/rpc/server>
#include "shared/demo_rpc.hpp"

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;

  auto rpc_server = demo_rpc::init(driver);

  driver.on_unload([rpc_server]() mutable {
    rpc_server.reset(); // remove endpoint before driver unload completes
  });

  return ntl::status::ok();
}
```

앱 측:

```cpp
#include <exception>
#include <iostream>
#include <ntl/rpc/client>
#include "shared/demo_rpc.hpp"

int wmain() {
  try {
    ntl::rpc::client client(L"demo_rpc");
    std::wcout << L"40 + 2 = " << demo_rpc::add(40, 2) << L"\n";
    auto value = client.invoke(demo_rpc::negate_1_method, 7);
    std::wcout << L"negate(7) = " << value << L"\n";
  } catch (const std::exception& e) {
    std::cerr << "RPC call failed: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
```

이 패키지는 WDK/SDK 자체를 설치하지 않으며, 일반 C++ 프로젝트를 드라이버
프로젝트로 변환하지도 않습니다.

## 내용

- `include/` 헤더
- 네이티브 MSBuild props/targets(`build/native`)
- 고정된 MsQuic 공개 ABI 헤더(`build/native/msquic/include/msquic.h`)
- MSVC 도구 세트, 아키텍처 및 구성으로 사전 구축된 라이브러리:
  `build/native/lib/native/<toolset>/{x86,x64,ARM,ARM64}/{Debug,Release}/(crtsys.lib|Ldk.lib)`.
  예를 들어 VS2019는 `build/native/lib/native/v142/x64/Release`, VS2022는
  `build/native/lib/native/v143/x64/Release`, VS2026은
  `build/native/lib/native/v145/x64/Release`를 사용합니다. ARM은 v142/v143에
  제공되고, v145에는 x86/x64/ARM64가 제공됩니다.

패키지 CI는 모든 패키지 툴셋·아키텍처·Debug/Release 조합에서 실제 codec
소비자를 컴파일하고 링크합니다. x86 및 x64 소비자는 gzip, deflate, Brotli,
연결된 gzip+Brotli의 한 바이트씩 분할한 증분 왕복도 실행합니다. ARM 및 ARM64는
호스팅된 Windows runner에서 교차 링크 검증을 수행합니다.

패키지 CI는 재배포하는 SHA-256 검증 MsQuic 헤더를 대상으로 사용자 HTTP/3
백엔드와 커널 NMR 래퍼도 모두 컴파일합니다. 해당 사용자 DLL이나 커널 provider의
런타임 배포는 여전히 제품이 결정할 문제입니다.

NTL 미니필터 진입점은 미리 빌드한 라이브러리 자체가 Windows 8 Filter Manager
선언으로 컴파일되었더라도 Windows 7 이상 소비자를 지원합니다. 공개 소유 객체의
레이아웃은 대상 버전과 무관하며, 네이티브 `FLT_REGISTRATION`은 소비자 번역
단위에서 생성·소멸됩니다. 따라서 그 크기와 버전은 프로젝트의 `NTDDI_VERSION`과
일치합니다.

## 릴리스 아티팩트

- `crtsys-<version>-prebuilt.zip`
  헤더, 라이브러리, 문서 및 CMake 도우미가 포함된 사전 빌드된 번들입니다.
  CMake 기반 소비자를 위한 `CrtSys.cmake` 및 `find_package(crtsys CONFIG)` 지원과
  동일한 네이티브 MSBuild 빌드 지원 파일도 포함합니다.
- `crtsys-<version>-SHA256SUMS.txt`
  오프라인/수동 확인을 위한 체크섬 파일입니다.

이 패키지 README는 nuget.org에서 자체 완결되도록 작성했습니다. 프로젝트 URL,
저장소 URL, 라이선스, 릴리스 자산 링크는 패키지 메타데이터가 별도로 제공하므로,
이 문서에서는 패키지 페이지에서 해석되지 않는 저장소 상대 문서 링크를 사용하지
않습니다.
