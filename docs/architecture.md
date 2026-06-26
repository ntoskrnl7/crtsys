# Architecture

[Back to README](../README.md)

`crtsys` is built as a kernel-mode runtime substrate for Windows drivers. The
main idea is to let driver projects use familiar MSVC C++ runtime, CRT, and STL
entry points while the runtime dependencies are mapped onto driver-safe kernel
facilities and an explicit compatibility substrate.

## Responsibilities

`crtsys` owns the MSVC runtime/STL integration inside the driver binary. It
selects the Microsoft runtime source paths that are needed by the tested driver
surface, supplies kernel-mode runtime adapters where the hosted runtime would
normally depend on user-mode facilities, and keeps the resulting behavior tied
to driver tests.

`LDK` provides the Windows/NTDLL-compatible API surface and ICU ABI substrate
used by the runtime and STL paths. This includes the lower-level primitives that
MSVC runtime code expects to call when it is built outside a normal user-mode
process.

`NTL` is the C++ helper layer exposed to driver code. It provides the C++ entry
point wrapper, driver/device helpers, synchronization helpers, RPC-style control
paths, IRQL helpers, and stack expansion tools.

## Layer Responsibilities

| Layer | Responsibility |
| --- | --- |
| MSVC CRT/STL/VCRT/UCRT source paths | Preserve the familiar MSVC C++/CRT/STL entry points that driver code uses. |
| crtsys compatibility layer | Provide kernel-mode runtime adapters, ABI helpers, selected CRT/STL integration, and the driver-tested coverage contract. |
| LDK substrate | Provide Windows/NTDLL-compatible APIs and ICU ABI entry points required by runtime/STL paths. |
| NTL | Provide optional driver-shaped C++ helpers without changing the default MSVC STL surface. |
| WDK / NT kernel | Provide the actual kernel primitives, object model, IRQL rules, pool allocation, and verifier environment. |

## Public C++ Surface

The intended default surface is ordinary MSVC C++: include the MSVC standard
headers, use the standard CRT/STL types, and link the runtime substrate into the
driver. Kernel-specific helpers such as NTL are available for driver-shaped
tasks, while the default STL path remains the familiar MSVC STL path.

This is why the coverage matrix is tied to driver tests instead of a separate
compatibility vocabulary. It records the standard C++/CRT/STL paths that have
actually been exercised under the kernel driver harness.

## Consumer Paths

Visual Studio/MSBuild driver projects normally consume the NuGet package through
`PackageReference`, `Install-Package crtsys`, and `msbuild /restore`; see the
[MSBuild/NuGet quick start](./msbuild-nuget-quickstart.md). CMake projects can
either consume the GitHub Release prebuilt bundle through
`find_package(crtsys CONFIG REQUIRED)` or consume `crtsys` directly from GitHub
with CPM.cmake and `CPMAddPackage("gh:ntoskrnl7/crtsys@<version>")`.

All three paths target the same model: `crtsys` is linked into the driver and
the driver remains a normal WDK driver.

## Tested Surface

The feature coverage matrix is intentionally evidence based. Listed C++/CRT/STL
paths are built into a kernel driver test target and run under that harness.
Unlisted headers or code paths are not automatically unsupported; they are
simply not part of the explicit driver-tested matrix yet.

Driver-tested areas include C++ initialization, C++ exception handling, SEH
handling, RTTI, core STL containers and utilities, filesystem, format/print,
regex, locale, random, chrono/timezone, synchronization, threading, async,
atomics, streams, PMR, and selected CRT/math paths.

## Boundaries

`crtsys` does not make arbitrary user-mode assumptions safe in kernel mode. The
driver still owns normal WDK concerns: IRQL, pageable code, pool allocation,
stack depth, unload safety, verifier behavior, HVCI, and target OS validation.

Unless a feature explicitly documents a broader contract, treat runtime-backed
C++/CRT/STL paths as `PASSIVE_LEVEL` control-path facilities.
