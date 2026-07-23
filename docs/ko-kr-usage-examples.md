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

RPC helper는 하나의 공유 macro 선언에서 kernel callback dispatcher와
user-mode wrapper를 같이 생성합니다. 드라이버는 schema 전에
`<ntl/rpc/server>`를 include하고, 앱은 같은 schema 전에
`<ntl/rpc/client>`를 include합니다.

`NTL_ADD_CALLBACK_0`부터 `NTL_ADD_CALLBACK_5`까지의 숫자는 callback 인자
개수입니다. macro가 이 정보를 이용해 양쪽의 함수 인자와 직렬화 코드를
생성합니다. 기본형은 공유 schema의 line에서 양쪽에 같은 ID를 만들므로,
드라이버와 앱이 같은 계약 헤더를 사용하는 일반적인 경우에 적합합니다.
schema 재배치 뒤에도 같은 method ID를 유지해야 할 때
`NTL_ADD_CALLBACK_ID_N`으로 `0x800`부터 `0xFFF` 범위의 ID를 명시하세요.

### 공유 Schema

```cpp
// shared/demo_rpc.hpp
#pragma once

#include <vector>

NTL_RPC_BEGIN(demo_rpc)

NTL_ADD_CALLBACK_2(demo_rpc, int, add, int, left, int, right, {
  return left + right;
})

NTL_ADD_CALLBACK_1(
    demo_rpc,
    NTL_RPC_BOUNDED_RESPONSE(64 * 1024, std::vector<int>), values, int, count, {
      if (count < 0 || count > 4096)
        throw ntl::exception(STATUS_INVALID_PARAMETER, "invalid count");
      return std::vector<int>(static_cast<std::size_t>(count), 42);
    })

NTL_RPC_END(demo_rpc)
```

custom object는 `zpp::serializer` serialization function을 제공해야 합니다.
테스트의 `point` class가 그 패턴을 보여줍니다.
NTL은 같은 필드 목록에서 method의 wire schema fingerprint를 자동으로
계산하므로 별도의 숫자 hash를 지정할 필요가 없습니다.

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

`demo_rpc::init(driver)`는 `\Device\demo_rpc` device를 만들고 공유 선언의
callback을 모두 등록합니다. 반환되는 server는 RPC endpoint가 살아 있어야
하는 동안 유지하세요.

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
    ntl::rpc::client client(L"demo_rpc");
    std::cout << demo_rpc::add(40, 2) << "\n";
    const auto values = client.invoke(demo_rpc::values_1_method, 16);
    std::cout << "values=" << values.size() << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "RPC failed: " << e.what() << "\n";
    return 1;
  }
}
```

`demo_rpc::add` 같은 generated wrapper는 간헐적인 호출에 편합니다. 반복
호출에서는 `ntl::rpc::client`와 `<name>_<arity>_method` 객체를 함께 사용해
device handle을 재사용할 수 있습니다. 가변 크기 결과는
`NTL_RPC_BOUNDED_RESPONSE`로 최대 직렬화 크기를 선언하며, server는 callback을
실행하기 전에 client output buffer가 그 한도를 제공하는지 확인합니다.

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
#include <ntl/handle>
#include "shared/demo_ioctl.hpp"

int wmain() {
  ntl::unique_handle device{CreateFileW(
      L"\\\\?\\Global\\GLOBALROOT\\Device\\" DEMO_DEVICE_NAME,
      GENERIC_READ | GENERIC_WRITE,
      0,
      nullptr,
      OPEN_EXISTING,
      0,
      nullptr)};

  if (!device)
    return 1;

  char request[] = "hello";
  char reply[sizeof request] = {};
  DWORD returned = 0;

  const BOOL ok = DeviceIoControl(device.get(),
                                  DEMO_IOCTL_ECHO,
                                  request,
                                  sizeof request,
                                  reply,
                                  sizeof reply,
                                  &returned,
                                  nullptr);

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
