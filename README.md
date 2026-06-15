# crtsys

**C**/C++ Run**t**ime support for Windows kernel **sys** drivers.

[![CMake](https://github.com/ntoskrnl7/crtsys/actions/workflows/cmake.yml/badge.svg)](https://github.com/ntoskrnl7/crtsys/actions/workflows/cmake.yml)
![GitHub](https://img.shields.io/github/license/ntoskrnl7/crtsys)
![GitHub release](https://img.shields.io/github/v/release/ntoskrnl7/crtsys)
![Windows 7+](https://img.shields.io/badge/Windows-7%2B-blue?logo=windows&logoColor=white)
![Visual Studio 2017+](https://img.shields.io/badge/Visual%20Studio-2017%2B-682270?logo=visualstudio&logoColor=white)
![CMake 3.14+](https://img.shields.io/badge/CMake-3.14%2B-064f8c?logo=cmake&logoColor=white)
![C++14+](https://img.shields.io/badge/C%2B%2B-14%2B-00599c?logo=cplusplus&logoColor=white)

[Korean documentation](./docs/ko-kr.md)

`crtsys` is an experimental runtime layer that helps Windows kernel drivers use
C++ language features, selected Microsoft STL facilities, and small
kernel-friendly helper abstractions. It is intended for driver projects that
want a more familiar C++ development model without leaving the WDK/CMake build
flow.

The project combines three pieces:

- CRT/STL compatibility glue for kernel-mode builds.
- [`Ldk`](https://github.com/ntoskrnl7/ldk), which provides a subset of
  Win32/NTDLL-like APIs used by the runtime and STL.
- NTL, a small C++ helper layer for common driver objects and synchronization.

## Status

This project is not a full user-mode CRT, not a complete Windows compatibility
layer, and not a replacement for careful driver design. It enables useful C++
and STL scenarios in kernel mode, but every driver should still be tested under
the exact Windows, WDK, SDK, Visual Studio, architecture, and verifier settings
that it will ship with.

Known constraints:

- Thread-local storage and thread-safe local static initialization are not
  supported yet.
- Kernel stacks are small; use `ntl::expand_stack` for paths that need more
  stack, especially exception-heavy or STL-heavy paths.
- SDK and WDK version mismatches can cause build failures. Prefer matching SDK
  and WDK versions.
- New Windows 11 24H2 WDK releases no longer support x86 kernel-mode driver
  development. Use WDK 23H2 or older when x86 driver targets are required.

## Features

The README keeps this section short. The detailed, test-linked checklist lives
in the documentation directory:

- [Detailed feature coverage](./docs/feature-coverage.md)
- [NTL API reference](./docs/ntl-api.md)

High-level coverage:

- C++ runtime support
  - non-local static initialization
  - dynamic initialization
  - exceptions
- Microsoft STL support
  - time utilities
  - threading primitives
  - synchronization primitives
  - async primitives
  - stream objects
- C runtime support
  - CRT glue needed by the supported STL paths
  - math helpers
  - floating-point classification helpers
- NTL support
  - C++ driver entry point wrapper
  - driver and device helpers
  - RPC helpers
  - IRQL and synchronization helpers

## Requirements

- Windows 7 or later
- Visual Studio or Build Tools 2017 or later
- Windows SDK and WDK compatible with the selected Visual Studio toolset
- CMake 3.14 or later
- Git

Tested toolchains include Visual Studio 2017, 2019, and 2022 with WDK/SDK
versions such as `10.0.17763.0`, `10.0.18362.0`, `10.0.22000.0`, and
`10.0.22621.0`.

Visual Studio 2017 has missing CRT source/header pieces for some paths, so
`crtsys` uses selected UCXXRT compatibility code for that toolset.

## Quick Start

Create a driver project, place CPM at `cmake/CPM.cmake`, and add `crtsys`:

```cmake
cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(my_driver LANGUAGES C CXX)

include(cmake/CPM.cmake)

set(CRTSYS_NTL_MAIN ON)
CPMAddPackage("gh:ntoskrnl7/crtsys@0.1.10")
include(${crtsys_SOURCE_DIR}/cmake/CrtSys.cmake)

crtsys_add_driver(my_driver src/main.cpp)
```

`CRTSYS_NTL_MAIN` enables the C++ entry point wrapper. With it enabled, define
`ntl::main` instead of writing `DriverEntry` directly:

```cpp
#include <iostream>
#include <string>
#include <ntl/driver>

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  std::wcout << L"load: " << registry_path << L"\n";

  driver.on_unload([registry_path]() {
    std::wcout << L"unload: " << registry_path << L"\n";
  });

  return ntl::status::ok();
}
```

If `CRTSYS_NTL_MAIN` is disabled, keep the normal WDK `DriverEntry` entry
point and initialize your driver manually.

Build the project with a Visual Studio generator:

```bat
cmake -S . -B build_x64 -A x64
cmake --build build_x64 --config Debug
```

## NuGet Package

`crtsys` can be distributed as a native NuGet package for Visual Studio driver
projects. The package includes public NTL headers, internal compatibility
headers, native MSBuild imports, and prebuilt `crtsys.lib`/`Ldk.lib` binaries.

After installing the package in a Visual Studio WDK driver project, NuGet
imports `build/native/crtsys.props` and `build/native/crtsys.targets`
automatically. Those files add the include paths, forced include file,
preprocessor definitions, library path, linker dependencies, and
`CrtSysDriverEntry` entry point needed for the default `ntl::main` flow.
The binary NuGet package currently supports the `ntl::main` entry point flow
only.

The current binary package targets:

- Visual Studio 2022
- Windows SDK/WDK `10.0.22621.0`
- `x64` and `ARM64`
- Release `crtsys.lib` and `Ldk.lib`

Install it from Visual Studio's **Manage NuGet Packages** UI. In the Package
Manager Console, select your driver project as the default project and run:

```powershell
Install-Package crtsys -Version 0.1.10
```

Then define `ntl::main` in your driver:

```cpp
#include <ntl/driver>

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  driver.on_unload([]() {});
  return ntl::status::ok();
}
```

For native MSBuild consumers, the package also exposes `$(CrtSysRoot)`.

Pack locally:

```powershell
.\scripts\nuget\Build-CrtSysNuGetLibs.ps1
.\scripts\nuget\Pack-CrtSysNuGet.ps1
```

Publish with an API key in the environment:

```powershell
$env:NUGET_API_KEY = '<nuget-api-key>'
.\scripts\nuget\Push-CrtSysNuGet.ps1 -SkipDuplicate
```

GitHub Actions builds the prebuilt libraries and package on pull requests and
pushes. A tag such as `v0.1.10` publishes to nuget.org through NuGet Trusted
Publishing when the tag version matches `include/.internal/version`.

For GitHub Actions publishing, create a nuget.org Trusted Publishing policy
with the package owner shown by nuget.org, repository owner `ntoskrnl7`,
repository `crtsys`, workflow file `nuget.yml`, and no environment
restriction. Set the GitHub Actions repository variable
`NUGET_TRUSTED_PUBLISHING_USER` to the nuget.org user that created the policy.

## Building This Repository

Clone the repository and build the test app and driver for the host
architecture:

```bat
git clone https://github.com/ntoskrnl7/crtsys
cd crtsys
test\build.bat
```

Build a specific target manually:

```bat
build.bat test\app x64 Debug
build.bat test\driver x64 Debug
build.bat test\app x64 Release
build.bat test\driver x64 Release
```

Build all supported architecture/configuration combinations:

```bat
build_all.bat test\app
build_all.bat test\driver
```

Typical Debug outputs:

```text
test\driver\build_x64\Debug\crtsys_test.sys
test\app\build_x64\Debug\crtsys_test_app.exe
```

## Running Tests

`crtsys_test.sys` is a kernel driver. Build validation can happen in CI, but
loading and executing the test driver must happen in a Windows driver test
environment.

The CI build workflow and optional self-hosted driver load test path are
documented in [CI driver load tests](./docs/ci-driver-load-tests.md).

```bat
sc create CrtSysTest binpath= "C:\path\to\crtsys_test.sys" displayname= "crtsys test" start= demand type= kernel
sc start CrtSysTest

C:\path\to\crtsys_test_app.exe

sc stop CrtSysTest
sc delete CrtSysTest
```

The test driver uses Google Test internally. Inspect output with DebugView,
WinDbg, or your normal kernel debugging setup.

## Repository Layout

```text
cmake/             CMake helpers, including CrtSys.cmake
include/ntl/       NTL C++ helper headers
include/.internal/ Internal version and toolchain compatibility headers
src/               crtsys runtime and CRT/STL compatibility code
test/app/          User-mode test companion application
test/driver/       Kernel-mode test driver
docs/              Additional documentation
```

## Background

`crtsys` was created after experimenting with other kernel C++ runtime
projects, especially UCXXRT and KTL. The design goal is to keep the CMake/WDK
workflow practical while supporting a broad enough subset of Microsoft CRT/STL
behavior for real driver experiments.

The project avoids treating the Microsoft CRT/STL source as a vendored library.
Instead, it relies on the locally installed Visual Studio/Build Tools layout
and layers kernel-mode compatibility code around it. For older toolsets where
the Microsoft-provided source/header layout is incomplete, small compatibility
pieces are used.

Several standalone implementations are also referenced where they are a better
fit for kernel-mode support:

- [RetrievAL](https://github.com/SpoilerScriptsGroup/RetrievAL)
- [musl](https://github.com/bminor/musl)
- [zpp serializer](https://github.com/eyalz800/serializer)

## Roadmap

- Add CMake install/package handling.
- Expand unsupported C++ and STL feature coverage.
- Reduce Visual Studio 2017 compatibility gaps.
- Add CI coverage for actual driver load/run tests where the environment
  supports it.
