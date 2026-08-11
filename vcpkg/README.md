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
      "baseline": "bd49aa5fffc3a5f625e9264f8c4510d06ecf625a",
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
the first `vcpkg install`, run the initializer once from the manifest root:
If user-wide vcpkg MSBuild integration is not enabled yet, first run
`vcpkg integrate install` once.

```powershell
.\vcpkg_installed\x64-windows-static\tools\crtsys\crtsys-vs-init.cmd
```

If this command is absent from the installation, the selected registry baseline
predates the initializer. Update to a baseline that publishes the tool, or use
the manual setup below.

The command enables the manifest, selects a static-CRT triplet, and adds the
crtsys bridge while preserving other content in `Directory.Build.props` and
`Directory.Build.targets`. Repeated runs are idempotent. A different default
triplet can be selected, and the generated integration can be removed:

```powershell
.\vcpkg_installed\x64-windows-static\tools\crtsys\crtsys-vs-init.cmd `
  -Triplet arm64-windows-static

.\vcpkg_installed\x64-windows-static\tools\crtsys\crtsys-vs-init.cmd -Remove
```

Reload the Visual Studio solution after the first install and initialization so
MSBuild reevaluates the new import. The existing **No NTL entry point**,
**NTL WDM**, **NTL KMDF**,
**NTL Minifilter**, and **NTL WFP** selections then behave exactly as they do
for the NuGet package.

For manual setup, the installed
`share/crtsys/msbuild/crtsys-vcpkg.targets` bridge can still be imported from
`Directory.Build.targets` directly.

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

Tagged release workflows update the overlay manifest version during release
preparation. After the validated GitHub Release asset is uploaded, the Package
workflow computes its SHA-512, publishes the port and versions database to the
`vcpkg-registry` branch, and synchronizes the source overlay and documented
baseline on `main`.

The underlying maintenance commands are also available for local recovery or
validation:

```powershell
./scripts/vcpkg/Update-CrtSysVcpkgPort.ps1 `
  -Version <version> -ArchivePath <prebuilt-zip>

./scripts/vcpkg/Publish-CrtSysVcpkgRegistry.ps1 `
  -Version <version> `
  -ArchivePath <prebuilt-zip> `
  -SourcePortDirectory ./vcpkg/ports/crtsys `
  -RegistryDirectory <registry-worktree>
```

Publishing rejects attempts to rewrite an existing version or move the
registry baseline backwards.

## Updating the official microsoft/vcpkg port

The separate **Update official vcpkg** workflow is intentionally manual. Run it
only after the matching stable GitHub Release and
`crtsys-<version>-prebuilt.zip` asset have been published:

1. Open **Actions** and select **Update official vcpkg**.
2. Enter the numeric release version without the `v` prefix.
3. Choose `validate` to prepare the official port, regenerate its versions
   database, install it without a binary cache, and test both CMake and Visual
   Studio consumers without pushing anything.
4. For later releases, choose `submit` to perform the same checks, update the
   `ntoskrnl7/vcpkg` fork branch, and create or update the microsoft/vcpkg pull
   request.

`submit` requires the repository Actions secret `VCPKG_UPSTREAM_TOKEN`. Use a
GitHub token owned by a maintainer that can write to the `ntoskrnl7/vcpkg` fork
and create a public pull request. The workflow never merges the upstream pull
request; microsoft/vcpkg review and CI approval remain required.

Until the initial crtsys port pull request is merged, use `validate` only. Once
the port exists on microsoft/vcpkg `master`, future released versions can use
`submit` directly. Re-running `submit` for the same pending version safely
updates the same fork branch and pull request instead of creating duplicates.
