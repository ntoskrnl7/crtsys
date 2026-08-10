# crtsys vcpkg port

This directory contains the first-party overlay port for the prebuilt crtsys
release bundle. The port installs one target architecture, keeps the supported
MSVC toolset variants, and exposes both supported consumption paths:

- CMake through `find_package(crtsys CONFIG REQUIRED)` and
  `crtsys_add_driver(...)`.
- Visual Studio/MSBuild through the existing crtsys entry-point property page.

The package is Windows/WDK-only and uses static libraries with the static MSVC
runtime.

## Git registry

Published versions can be consumed without a source checkout. Add this
`vcpkg-configuration.json` next to the consumer manifest:

```json
{
  "default-registry": null,
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/ntoskrnl7/crtsys",
      "reference": "vcpkg-registry",
      "baseline": "6e1c3ad29a817831bcbf1eff9cfbfdaf487d35c7",
      "packages": ["crtsys"]
    }
  ]
}
```

This minimal example disables the default registry. If the project has other
vcpkg dependencies, keep its existing pinned default registry instead. Add the
dependency to `vcpkg.json`:

```json
{
  "name": "my-driver",
  "version-string": "0",
  "dependencies": ["crtsys"]
}
```

Then select a matching static-CRT triplet:

```powershell
vcpkg install --triplet=x64-windows-static
```

## Overlay port

For local port development or a source checkout, select the overlay explicitly:

```powershell
vcpkg install --triplet=x64-windows-static `
  --overlay-ports=D:\path\to\crtsys\vcpkg\ports
```

Standalone vcpkg installations can also use classic mode with
`vcpkg install crtsys:x64-windows-static --overlay-ports=...`.

## CMake

Configure the consumer with the vcpkg toolchain, then use the installed package
normally:

```cmake
find_package(crtsys CONFIG REQUIRED)

set(CRTSYS_NTL_MAIN ON)
crtsys_add_driver(my_driver src/main.cpp)
```

The existing model-specific forms remain available:

```cmake
crtsys_add_driver(my_kmdf KMDF 1.15 NTL src/main.cpp)
crtsys_add_driver(my_filter MINIFILTER NTL src/main.cpp)
crtsys_add_driver(my_wfp WFP NTL src/main.cpp)
```

## Visual Studio/MSBuild UI

vcpkg's MSBuild integration adds installed include and library directories, but
does not automatically import a port's package-specific property pages. After
the first `vcpkg install`, import the crtsys bridge from `Directory.Build.targets`
or the consuming `.vcxproj`:

```xml
<Project>
  <Import
    Project="$([MSBuild]::NormalizePath('$(VcpkgManifestRoot)', 'vcpkg_installed', '$(VcpkgTriplet)', 'share', 'crtsys', 'msbuild', 'crtsys-vcpkg.targets'))"
    Condition="Exists('$([MSBuild]::NormalizePath('$(VcpkgManifestRoot)', 'vcpkg_installed', '$(VcpkgTriplet)', 'share', 'crtsys', 'msbuild', 'crtsys-vcpkg.targets'))')" />
</Project>
```

Set `VcpkgTriplet` to a static-CRT triplet such as `x64-windows-static`. Reload
the Visual Studio solution after the first install so MSBuild reevaluates the
new import. The existing **No NTL entry point**, **NTL WDM**, **NTL KMDF**,
**NTL Minifilter**, and **NTL WFP** selections then behave exactly as they do
for the NuGet package.

NuGet remains the zero-import Visual Studio installation path. The vcpkg bridge
exists for repositories that standardize dependency restoration on a vcpkg
manifest while still requiring the crtsys WDK property UI.

## Validation

The contract-only check is fast and does not download a release:

```powershell
./scripts/vcpkg/Test-CrtSysVcpkgPort.ps1 -ContractOnly
```

The full check installs the overlay port and evaluates the MSBuild UI contract:

```powershell
./scripts/vcpkg/Test-CrtSysVcpkgPort.ps1 `
  -Triplet x64-windows-static
```

The overlay manifest version and archive SHA-512 must be updated for every new
crtsys release before publishing the port to a Git registry or the curated
vcpkg registry.
