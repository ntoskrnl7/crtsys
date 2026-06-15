# crtsys NuGet Package

`crtsys` is a native NuGet package for Visual Studio WDK driver projects. It
adds the headers, MSBuild imports, and prebuilt native libraries needed to use
the default `ntl::main` flow without writing CMake package glue in the consumer
project.

Project documentation:

- Repository: <https://github.com/ntoskrnl7/crtsys>
- Design model: <https://github.com/ntoskrnl7/crtsys/blob/main/docs/design-rationale.md>
- NTL API reference: <https://github.com/ntoskrnl7/crtsys/blob/main/docs/ntl-api.md>

## Supported Package Target

The binary package currently targets:

- Visual Studio 2022
- Windows SDK/WDK `10.0.22621.0`
- `x64` and `ARM64`
- Release `crtsys.lib` and `Ldk.lib`
- `ntl::main` entry point flow

It does not currently provide Win32/x86 binaries.

## Install

Install the package into a Visual Studio WDK driver project:

```powershell
Install-Package crtsys
```

The package imports `build/native/crtsys.props` and
`build/native/crtsys.targets` automatically.

## What The Package Configures

For supported native driver projects, the package configures:

- `crtsys` include paths
- required internal compatibility include paths
- forced include setup
- preprocessor definitions
- native library paths
- `crtsys.lib` and `Ldk.lib` linker dependencies
- `CrtSysDriverEntry` for the default `ntl::main` flow
- `$(CrtSysRoot)` for advanced MSBuild customization

## Minimal Driver Entry

With the package installed, define `ntl::main`:

```cpp
#include <ntl/driver>

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  driver.on_unload([]() {});
  return ntl::status::ok();
}
```

## Execution Model

`crtsys` is designed primarily for driver control paths such as initialization,
unload, device setup, IOCTL/RPC handling, worker-thread coordination, ownership
cleanup, and error handling.

Unless an API explicitly documents a broader contract, assume `PASSIVE_LEVEL`.
Do not use runtime-backed C++/CRT/STL helpers in DPC, ISR, paging I/O,
spin-lock-held, or other elevated-IRQL paths unless that exact API is documented
and tested for that context.

## Notes

- Validate the final driver under the Windows, WDK, SDK, Visual Studio,
  architecture, Driver Verifier, and code integrity settings that you ship.
- The package includes the repository `docs` directory inside the `.nupkg`, but
  NuGet.org displays this package-specific README as the package page.
- For CMake-based projects, use the repository instructions instead of this
  native Visual Studio package flow.
