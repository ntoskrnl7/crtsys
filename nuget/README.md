# crtsys NuGet Package

`crtsys` is a lightweight C/C++ runtime + helper set for Windows kernel drivers.
It brings managed access to selected MSVC/C++ runtime, CRT/STL style APIs, and
NTL abstractions so WDK driver code can be written in a more natural C++ flow.

This NuGet package is for **Visual Studio/MSBuild** consumers (`crtsys.<version>.nupkg`).

## Quick start

For modern MSBuild projects, add a `PackageReference`:

```xml
<ItemGroup>
  <PackageReference Include="crtsys" Version="<version>" />
</ItemGroup>
```

Then restore and build with MSBuild:

```powershell
msbuild .\my_driver.vcxproj /restore /p:Configuration=Debug /p:Platform=x64
```

For x86 driver projects, use the MSBuild `Win32` platform name:

```powershell
msbuild .\my_driver.vcxproj /restore /p:Configuration=Debug /p:Platform=Win32
```

For Visual Studio Package Manager Console:

```powershell
Install-Package crtsys
```

- App projects get compatibility headers/includes.
- Driver projects (WDK) get automatic WDK linkage for
  `crtsys.lib` / `Ldk.lib` (x86/x64/ARM64).

What this NuGet package is for:

- Modern C++ ownership for control-plane code (`ntl::driver`, `ntl::device`,
  unload callback)
- Small, readable status/error flow with `ntl::status`
- Shared user/kernel contracts from a single header source (`shared/*.hpp`)
- Reliable RAII-style lifecycle for driver resources and cleanup

Example (minimal driver entry):

```cpp
#include <ntl/driver>

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;
  driver.on_unload([]() {});
  return ntl::status::ok();
}
```

### IOCTL sample (kernel + app pair)

Shared header (`shared/demo_ioctl.hpp`):

```cpp
// shared/demo_ioctl.hpp
#pragma once

#define DEMO_DEVICE_NAME L"demo_device"
#define DEMO_IOCTL_ECHO \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
```

Kernel side:

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

App side:

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

### RPC sample (kernel + app pair)

Shared header (`shared/demo_rpc.hpp`):

```cpp
// shared/demo_rpc.hpp
#pragma once

NTL_RPC_BEGIN(demo_rpc)

NTL_ADD_CALLBACK_2(demo_rpc, int, add, int, a, int, b, {
  return a + b;
})

NTL_ADD_CALLBACK_1(demo_rpc, int, negate, int, value, {
  return -value;
})

NTL_RPC_END(demo_rpc)
```

Kernel side:

```cpp
#include <memory>
#include <ntl/driver>
#include <ntl/rpc/server>
#include "shared/demo_rpc.hpp" // generated RPC contract header

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;

  auto rpc_server = demo_rpc::init(driver); // creates \\Device\\demo_rpc endpoint

  driver.on_unload([rpc_server]() mutable {
    rpc_server.reset(); // remove endpoint before driver unload completes
  });

  return ntl::status::ok();
}
```

App side:

```cpp
#include <exception>
#include <iostream>
#include <ntl/rpc/client>
#include "shared/demo_rpc.hpp" // same shared contract header

int wmain() {
  try {
    std::wcout << L"40 + 2 = " << demo_rpc::add(40, 2) << L"\n";
    ntl::rpc::client client(L"demo_rpc");
    auto value = client.invoke<int>(demo_rpc::negate_1_index, 7);
    std::wcout << L"negate(7) = " << value << L"\n";
  } catch (const std::exception& e) {
    std::cerr << "RPC call failed: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
```

This package does not install the WDK/SDK itself and does not convert a normal C++
project into a driver project.

## Contents

- `include/` headers
- native MSBuild props/targets (`nuget/build/native`)
- prebuilt libs:
  `lib/native/{x86,x64,ARM64}/{Debug,Release}/(crtsys.lib|Ldk.lib)`

## Release artifacts

- `crtsys-<version>-prebuilt.zip`  
  A prebuilt bundle containing headers, libraries, documentation, and CMake helpers.
  It includes `CrtSys.cmake` and `find_package(crtsys CONFIG)` support for CMake-based consumers, and also carries the same native MSBuild build support files.
- `crtsys-<version>-SHA256SUMS.txt`  
  Checksum file for offline/manual verification.

This package README is intentionally self-contained for nuget.org. The package
metadata carries the project URL, repository URL, license, and release asset
links separately, so this document avoids repository-relative documentation
links that do not resolve on the package page.
