# crtsys NuGet Package

`crtsys` is a native NuGet package for Visual Studio WDK driver projects. It is
intended for users who want to consume prebuilt `crtsys.lib` and `Ldk.lib`
directly from Visual Studio/MSBuild.

This package must be installed into an existing WDK kernel-mode driver project.
It does not convert a normal C++ project, console application, static library,
or CMake project into a WDK driver project, and it does not provide the WDK
toolset itself.

For CMake usage, design notes, full API documentation, and tested feature
coverage, use the repository documentation:

- Repository: <https://github.com/ntoskrnl7/crtsys>
- CMake quick start: <https://github.com/ntoskrnl7/crtsys#quick-start>
- Design model: <https://github.com/ntoskrnl7/crtsys/blob/main/docs/design-rationale.md>
- NTL API reference: <https://github.com/ntoskrnl7/crtsys/blob/main/docs/ntl-api.md>

## Install

Create or open a Visual Studio WDK kernel-mode driver project first, then
install the package into that driver project:

```powershell
Install-Package crtsys
```

The WDK project is still responsible for the kernel-mode platform toolset,
target SDK/WDK, driver type, WDK include paths, WDK libraries, signing, INF, and
packaging settings. The `crtsys` package imports native MSBuild props/targets
automatically and adds the `crtsys` include paths, forced include setup,
preprocessor definitions, library path, `crtsys.lib`, `Ldk.lib`, and the
`CrtSysDriverEntry` entry point for the default `ntl::main` flow.

## Supported Binary Package Target

- Visual Studio 2022
- Windows SDK/WDK `10.0.22621.0`
- `x64` and `ARM64`
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

## Notes

- `crtsys` is primarily designed for `PASSIVE_LEVEL` driver control paths.
- Validate your final driver with the Windows, WDK, SDK, Visual Studio,
  architecture, Driver Verifier, and code integrity settings that you ship.
- The `.nupkg` also contains the repository `docs` directory for local
  reference after installation.
