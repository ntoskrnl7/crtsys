# crtsys NuGet Package

`crtsys` is a native NuGet package for Visual Studio/MSBuild projects.
User-mode applications can consume the public NTL headers from the package.
WDK driver projects can also consume the prebuilt `crtsys.lib` and `Ldk.lib`
driver libraries.

Installing this package into a normal C++ application project keeps it in
header-only app mode. Installing it into an existing WDK kernel-mode driver
project enables driver support automatically. It does not convert a normal C++
project, console application, static library, or CMake project into a WDK driver
project, and it does not provide the WDK toolset itself.

For CMake usage, design notes, full API documentation, and tested feature
coverage, use the repository documentation:

- Repository: <https://github.com/ntoskrnl7/crtsys>
- CMake quick start: <https://github.com/ntoskrnl7/crtsys#quick-start>
- Design model: <https://github.com/ntoskrnl7/crtsys/blob/main/docs/design-rationale.md>
- NTL API reference: <https://github.com/ntoskrnl7/crtsys/blob/main/docs/ntl-api.md>
- NTL usage examples: <https://github.com/ntoskrnl7/crtsys/blob/main/docs/usage-examples.md>

## Install

Create or open a Visual Studio project, then install the package into that
project:

```powershell
Install-Package crtsys
```

For application projects, the package adds the `crtsys` include path and C++
compatibility options only. It does not add driver libraries or a driver entry
point.

For WDK driver projects, the project is still responsible for the kernel-mode
platform toolset, target SDK/WDK, driver type, WDK include paths, WDK libraries,
signing, INF, and packaging settings. The `crtsys` package imports native
MSBuild props/targets automatically and adds the `crtsys` include paths, forced
include setup, preprocessor definitions, library path, `crtsys.lib`, `Ldk.lib`,
`libcntpr.lib`, and the `CrtSysDriverEntry` entry point for the default
`ntl::main` flow.

The package also mirrors the repository CMake driver setup for the parts that
matter to `crtsys`: it disables the WDK `/kernel` compiler switch and puts the
Visual C++ / Windows SDK include paths before inherited WDK `km\crt` include
paths. This is required for the C++ runtime and STL headers that `crtsys`
supports.

Driver support is enabled automatically when MSBuild sees a driver project
(`ConfigurationType=Driver` or `DriverType` is set). If you need to override
that detection, set `CrtSysUseDriverSupport=true` or `false` in your project
before importing the package props.

## Supported Binary Package Target

- Visual Studio 2022
- Windows SDK/WDK `10.0.22621.0`
- app header checks on `x86`, `x64`, and `ARM64`
- driver libraries on `x64` and `ARM64`
- Release `crtsys.lib` and `Ldk.lib`
- `ntl::main` entry point flow

Win32/x86 binaries are not included.

## Minimal Driver Entry

```cpp
#include <ntl/driver>

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;
  driver.on_unload([]() {});
  return ntl::status::ok();
}
```

## Minimal App Usage

```cpp
#include <ntl/rpc/client>

int main() {
  ntl::rpc::client client(L"test_rpc");
  return 0;
}
```

For an app/driver RPC skeleton and a raw `DeviceIoControl` skeleton, see the
repository usage examples.

## Notes

- `crtsys` is primarily designed for `PASSIVE_LEVEL` driver control paths.
- Validate your final driver with the Windows, WDK, SDK, Visual Studio,
  architecture, Driver Verifier, and code integrity settings that you ship.
- The `.nupkg` also contains the repository `docs` directory for local
  reference after installation.
