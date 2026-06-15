# NTL 사용 예제

[한국어 문서로 돌아가기](./ko-kr.md)

이 문서는 NTL을 드라이버와 companion user-mode 앱에서 함께 사용하는 작은
골격을 보여줍니다. 예제는 의도적으로 작게 유지했습니다. 목적은 WDK 설계를
대체하는 것이 아니라, ownership과 app/driver 경계를 빠르게 이해시키는
것입니다.

실제로 빌드되는 코드는 다음 위치를 참고하세요.

- 공유 RPC schema: [`test/cmake/common/rpc.hpp`](../test/cmake/common/rpc.hpp)
- 드라이버 쪽: [`test/cmake/driver/src/main.cpp`](../test/cmake/driver/src/main.cpp)
- 앱 쪽: [`test/cmake/app/src/main.cpp`](../test/cmake/app/src/main.cpp)
- NuGet 소비자 프로젝트: [`test/nuget`](../test/nuget)

## 빌드 구조

`crtsys`는 보통 두 위치에서 함께 사용합니다.

- 커널 드라이버는 device, RPC server, IOCTL callback, lifetime, unload
  cleanup을 소유합니다.
- user-mode 앱은 드라이버가 만든 device를 열고 `DeviceIoControl`을 통해
  요청을 보냅니다. 직접 보내거나 `ntl::rpc::client`를 사용할 수 있습니다.

NuGet 패키지도 같은 구분을 따릅니다.

- App 프로젝트는 header-only app mode를 사용합니다. `ntl/rpc/client` 사용에
  충분합니다.
- WDK driver 프로젝트는 driver mode를 사용합니다. driver library,
  entry-point wiring, include ordering, WDK 호환 설정이 추가됩니다.

## 최소 드라이버 진입점

`CRTSYS_NTL_MAIN`을 켜면 `DriverEntry`를 직접 작성하지 않고 `ntl::main`을
구현합니다.

```cpp
#include <string>
#include <ntl/driver>

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;

  driver.on_unload([] {
    // driver-owned object를 여기에서 정리합니다.
  });

  return ntl::status::ok();
}
```

`ntl::main`, `ntl::driver`, helper callback은 드라이버 제어 경로를 위한
것입니다. API가 더 넓은 계약을 문서화하지 않았다면 `PASSIVE_LEVEL`로
취급하세요.

## RPC 골격

RPC helper는 하나의 공유 schema 헤더에서 kernel server와 user-mode client를
같이 생성합니다. 드라이버는 schema 전에 `<ntl/rpc/server>`를 include하고,
앱은 같은 schema 전에 `<ntl/rpc/client>`를 include합니다.

callback ID는 현재 `__LINE__`을 기반으로 합니다. 양쪽은 같은 schema 파일을
그대로 include해야 하며, 서로 다른 line number가 생성되면 안 됩니다.

### 공유 Schema

```cpp
// shared/demo_rpc.hpp
#pragma once

// 드라이버에서는 <ntl/rpc/server> 뒤에 include하고,
// user-mode 앱에서는 <ntl/rpc/client> 뒤에 include합니다.

NTL_RPC_BEGIN(demo_rpc)

NTL_ADD_CALLBACK_2(demo_rpc, int, add, int, a, int, b, {
  return a + b;
})

NTL_ADD_CALLBACK_1(demo_rpc, int, negate, int, value, {
  return -value;
})

NTL_RPC_END(demo_rpc)
```

`std::map<int, int>`처럼 type에 comma가 들어가면 `NTL_RPC_ARG_PACK(...)`으로
감싸세요. custom object는 `zpp::serializer` serialization function을 제공해야
합니다. 테스트의 `point` class가 그 패턴을 보여줍니다.

### 커널 Server

```cpp
#include <memory>
#include <string>
#include <ntl/driver>
#include <ntl/rpc/server>
#include "shared/demo_rpc.hpp"

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;

  auto rpc_server = demo_rpc::init(driver);

  driver.on_unload([rpc_server]() mutable {
    rpc_server.reset();
  });

  return ntl::status::ok();
}
```

`demo_rpc::init(driver)`는 `\Device\demo_rpc` device를 만듭니다. 반환되는
`std::shared_ptr<ntl::rpc::server>`는 RPC endpoint가 살아 있어야 하는 동안
유지해야 합니다. 보통 unload callback에 capture해서 lifetime을 맞춥니다.

server callback은 driver device-control dispatch path에서 실행됩니다. 짧게
유지하고, 입력을 검증하며, runtime-backed driver control code와 같은 IRQL
규칙을 적용하세요.

### User-Mode Client

```cpp
#include <exception>
#include <iostream>
#include <ntl/rpc/client>
#include "shared/demo_rpc.hpp"

int wmain() {
  try {
    std::wcout << L"40 + 2 = " << demo_rpc::add(40, 2) << L"\n";

    ntl::rpc::client client(L"demo_rpc");
    auto value = client.invoke<int>(demo_rpc::negate_1_index, 7);
    std::wcout << L"negate(7) = " << value << L"\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "RPC failed: " << e.what() << "\n";
    return 1;
  }
}
```

`demo_rpc::add` 같은 generated wrapper 함수는 임시 client를 만들어서 device를
호출합니다. handle을 재사용하거나 output buffer 크기를 조절하려면 명시적으로
`ntl::rpc::client`를 만들면 됩니다.

앱이 RPC device를 열려면 드라이버가 먼저 로드되어 있어야 합니다.

## Raw IOCTL 골격

RPC는 편의 계층일 뿐입니다. 일반 device를 만들고 IOCTL을 직접 처리할 수도
있습니다.

### 공유 IOCTL Contract

```cpp
// shared/demo_ioctl.hpp
#pragma once

#define DEMO_DEVICE_NAME L"demo_device"

#define DEMO_IOCTL_ECHO \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
```

이 contract는 양쪽에서 WDK 또는 Windows SDK 헤더가 준비된 뒤 include하세요.

### 커널 Device

```cpp
#include <string>
#include <wdm.h>
#include <ntl/driver>
#include "shared/demo_ioctl.hpp"

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
    if (code == DEMO_IOCTL_ECHO && in.ptr && out.ptr) {
      const auto bytes = in.size < out.size ? in.size : out.size;
      RtlCopyMemory(out.ptr, in.ptr, bytes);
      out.size = bytes;
    }
  });

  driver.on_unload([device]() mutable {
    device.reset();
  });

  return ntl::status::ok();
}
```

callback에서 extension state가 필요하다면, 같은 device에 저장되는 callback
안에서 owning `std::shared_ptr`를 직접 capture하지 마세요. repository test
driver처럼 `std::weak_ptr`를 capture하는 편이 안전합니다.

### User-Mode App

```cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include "shared/demo_ioctl.hpp"

int wmain() {
  HANDLE device = CreateFileW(
      L"\\\\?\\Global\\GLOBALROOT\\Device\\" DEMO_DEVICE_NAME,
      GENERIC_READ | GENERIC_WRITE,
      0,
      nullptr,
      OPEN_EXISTING,
      0,
      nullptr);

  if (device == INVALID_HANDLE_VALUE)
    return 1;

  char request[] = "hello";
  char reply[sizeof request] = {};
  DWORD returned = 0;

  const BOOL ok = DeviceIoControl(device,
                                  DEMO_IOCTL_ECHO,
                                  request,
                                  sizeof request,
                                  reply,
                                  sizeof reply,
                                  &returned,
                                  nullptr);

  CloseHandle(device);
  return ok ? 0 : 1;
}
```

## 실사용 메모

- 공유 RPC/IOCTL contract 헤더는 작고 안정적으로 유지하세요.
- raw kernel pointer, handle, process-local address를 app/driver boundary 밖으로
  노출하지 마세요.
- 앱에서 온 데이터는 신뢰하지 마세요. 크기, 범위, enum 값, serialized object
  형태를 검증해야 합니다.
- driver-side callback은 정확히 audit한 경로가 아니라면 `PASSIVE_LEVEL`
  control-path model 안에서 다루세요.
- unload ownership을 명확히 하세요. device, worker thread, RPC server,
  callback을 소유하는 객체는 명확한 teardown 지점이 있어야 합니다.
