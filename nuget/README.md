# crtsys NuGet Package

`crtsys` is a lightweight C/C++ runtime + helper set for Windows kernel drivers.
It brings managed access to selected MSVC/C++ runtime, CRT/STL style APIs, and
NTL abstractions so WDK driver code can be written in a more natural C++ flow.

This NuGet package is for **Visual Studio/MSBuild** consumers (`crtsys.<version>.nupkg`).
It is not a CMake package.

## Quick start (MSBuild)

```powershell
Install-Package crtsys
```

- App projects get compatibility headers/includes.
- Driver projects (WDK) get automatic WDK linkage for
  `crtsys.lib` / `Ldk.lib` (x64/ARM64).

Example (driver entry concept):

```cpp
#include <ntl/driver>

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;
  driver.on_unload([]() {});
  return ntl::status::ok();
}
```

This package does not install the WDK/SDK itself and does not convert a normal C++
project into a driver project.

## Contents

- `include/` headers
- native MSBuild props/targets (`nuget/build/native`)
- prebuilt libs:
  `lib/native/{x64,ARM64}/{Debug,Release}/(crtsys.lib|Ldk.lib)`

## Release artifacts (same version, not NuGet)

GitHub Release publishes these in addition to the `.nupkg`:

- `crtsys-<version>-prebuilt.zip`
- `crtsys-<version>-SHA256SUMS.txt`

These two are release-only artifacts and are **not part of the NuGet package**.

For full release workflow, CMake distribution, and API usage details, see:
- <https://github.com/ntoskrnl7/crtsys/blob/main/README.md>
