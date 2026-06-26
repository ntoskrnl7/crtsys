# MSBuild/NuGet Quick Start

[Back to README](../README.md)

Use this path for Visual Studio or Build Tools WDK driver projects that consume
`crtsys` as a native NuGet package. This is independent of the CMake/CPM source
path.

## Requirements

- Visual Studio or Build Tools 2017 or later
- Windows SDK and WDK for the selected toolset
- MSBuild with NuGet restore support
- Access to the NuGet source that contains `crtsys`

`nuget.exe` is optional for modern `PackageReference` projects. If MSBuild
restore is available, `msbuild /restore` is enough. Install `nuget.exe` only for
older `packages.config` flows or scripts that explicitly call `nuget restore`.

## Visual Studio

In Package Manager Console:

```powershell
Install-Package crtsys
```

Then build the WDK driver project normally for `x64` or `ARM64`.

## Build Tools Only

Add a `PackageReference` to the driver project:

```xml
<ItemGroup>
  <PackageReference Include="crtsys" Version="<version>" />
</ItemGroup>
```

Open a Developer PowerShell or Developer Command Prompt that has MSBuild, the
SDK, and the WDK on the environment, then build with restore:

```powershell
msbuild .\my_driver.vcxproj /restore /p:Configuration=Debug /p:Platform=x64
```

For `ARM64`:

```powershell
msbuild .\my_driver.vcxproj /restore /p:Configuration=Release /p:Platform=ARM64
```

## What The Package Imports

The native package supplies the MSBuild props/targets needed by a WDK consumer
project, including include paths, forced includes, runtime libraries, LDK
libraries, and the `CrtSysDriverEntry` entry point wiring used by the package
consumer tests.

The driver remains a normal WDK driver. Verifier, signing, target OS policy,
IRQL, paging, and unload safety are still owned by the driver project.

## CI Smoke Test Shape

The repository keeps NuGet consumer projects under [`test/nuget`](../test/nuget)
so package consumption is build-tested instead of only documented:

- `crtsys_nuget_app_test.vcxproj` verifies user-mode header/package
  consumption.
- `crtsys_nuget_test.vcxproj` builds the WDK driver test sources from the
  package for `x64` and `ARM64` `Debug`/`Release`.

A CI job can use the same shape:

```powershell
msbuild .\test\nuget\crtsys_nuget_test.vcxproj /restore /p:Configuration=Release /p:Platform=x64
```

Runtime driver loading is a separate concern from package consumption. The
repository documents that path in [CI Driver Load Tests](./ci-driver-load-tests.md).
