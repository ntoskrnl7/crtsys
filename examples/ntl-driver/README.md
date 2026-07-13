# NTL Sample Driver

This sample is a small WDK driver that uses the NTL helper layer shipped with
`crtsys`. It is meant to show a practical driver shape rather than a test
harness.

The driver demonstrates:

- `ntl::main` as the C++ entry point
- `ntl::driver_config` for optional `Parameters` registry settings
- `ntl::device_endpoint` for device + DOS symbolic-link ownership
- `ntl::ioctl` for typed `CTL_CODE` contracts
- `ntl::remove_lock` for dispatch/unload synchronization
- `ntl::passive_executor` for PASSIVE_LEVEL work handoff
- `ntl::pmr::pool_resource` for STL/PMR allocation from kernel pool

## Visual Studio / NuGet

Open [`crtsys_ntl_sample_vs.sln`](./crtsys_ntl_sample_vs.sln) in Visual
Studio with the WDK workload installed. The solution contains:

- `crtsys_ntl_sample`: the kernel driver
- `crtsys_ntl_sample_app`: the user-mode ping app

Restore NuGet packages, then build `Debug|x64` or `Release|x64`. The project
files use:

```xml
<PackageReference Include="crtsys" Version="$(CrtSysPackageVersion)" />
```

`CrtSysPackageVersion` defaults to `*`, so NuGet restore selects the latest
stable `crtsys` package from the configured package sources. For reproducible
builds, pin an exact package version from MSBuild:

```bat
msbuild crtsys_ntl_sample_vs.sln /restore /p:Configuration=Debug /p:Platform=x64 /p:CrtSysPackageVersion=0.1.32
```

## CMake Build

From the repository root:

```bat
cmake -S examples\ntl-driver -B examples\ntl-driver\build_x64 -A x64
cmake --build examples\ntl-driver\build_x64 --config Debug
```

The Debug output is:

```text
examples\ntl-driver\build_x64\Debug\crtsys_ntl_sample.sys
examples\ntl-driver\build_x64\Debug\crtsys_ntl_sample_app.exe
```

To suppress diagnostic breakpoints while experimenting:

```bat
cmake -S examples\ntl-driver -B examples\ntl-driver\build_x64 -A x64 -DCRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS=OFF
```

## IOCTL Contract

The shared contract lives in [`shared/ntl_sample_ioctl.hpp`](./shared/ntl_sample_ioctl.hpp).
It can be included by a driver after `<wdm.h>` or by a user-mode companion app
after `<winioctl.h>`.

The sample driver exposes `ntl_sample::ping_ioctl_code`. The request contains
one integer value. The reply returns that value incremented, a monotonically
increasing sequence number, the optional registry `Flags` setting, and a small
checksum computed by a PASSIVE_LEVEL work item.

## Loading

Use your normal isolated driver-test VM. For example:

```bat
sc create CrtSysNtlSample binpath= "C:\path\to\crtsys_ntl_sample.sys" type= kernel start= demand
sc start CrtSysNtlSample
sc stop CrtSysNtlSample
sc delete CrtSysNtlSample
```

## User-Mode Ping App

The sample includes a small companion app that opens `\\.\CrtSysNtlSample` and
sends the shared `ntl_sample::ping_ioctl_code` request:

```bat
examples\ntl-driver\build_x64\Debug\crtsys_ntl_sample_app.exe 41
```

Expected output:

```text
ping ok: request=41 reply=42 sequence=1 flags=0 checksum=42
```

The sequence number increases for each successful IOCTL. If a `Parameters\Flags`
DWORD exists below the service registry key, the driver includes that value in
the reply and checksum.
