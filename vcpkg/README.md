# crtsys vcpkg port

This directory contains the first-party crtsys overlay port. The official port
builds pinned source revisions with the selected Windows static-CRT triplet and
installs its archives in vcpkg's `lib/manual-link` layout. It supports:

- CMake through `find_package(crtsys CONFIG REQUIRED)` and
  `crtsys_add_driver(...)`.
- Visual Studio/MSBuild through the crtsys driver-model property pages.

Visual Studio with the C++ workload and a compatible Windows Driver Kit are
required. The port never downloads dependencies while a consumer is being
configured.

## Git registry

Published versions can be consumed without a crtsys source checkout. Add this
next to the consumer's `vcpkg.json`:

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

Keep an existing default registry if the project uses other vcpkg packages.
The manifest dependency is:

```json
{
  "name": "my-driver",
  "version-string": "0",
  "dependencies": ["crtsys"]
}
```

Install with a static-CRT triplet:

```powershell
vcpkg install --triplet=x64-windows-static
```

For local port development, add
`--overlay-ports=D:\path\to\crtsys\vcpkg\ports`.

## Optional features

User-mode zlib and Brotli helpers are explicit vcpkg dependencies:

```json
"dependencies": [
  { "name": "crtsys", "features": ["content-codecs"] }
]
```

The pinned optional MsQuic public headers use the `msquic-headers` feature.
Kernel-mode content codecs are not built from vcpkg's user-mode codec
libraries; build crtsys from source when separately audited kernel codecs are
required.

## CMake

```cmake
find_package(crtsys CONFIG REQUIRED)

set(CRTSYS_NTL_MAIN ON)
crtsys_add_driver(my_driver src/main.cpp)
```

The `KMDF`, `MINIFILTER`, `WFP`, and `NTL` options remain available.

## Visual Studio/MSBuild UI

After the first manifest install, run the initializer once from the manifest
root and reload the solution:

```powershell
vcpkg env --tools --triplet=x64-windows-static "crtsys-vs-init.cmd"
```

The initializer preserves existing `Directory.Build.props` and
`Directory.Build.targets`, is idempotent, and can be removed with
`crtsys-vs-init.cmd -Remove`. The **No NTL entry point**, **NTL WDM**,
**NTL KMDF**, **NTL Minifilter**, and **NTL WFP** property-page choices are
installed with the port. NuGet remains the zero-initializer Visual Studio path.

## Validation and publishing

The fast contract check is:

```powershell
./scripts/vcpkg/Test-CrtSysVcpkgPort.ps1 -ContractOnly
```

The full check builds the source port and validates the MSBuild UI:

```powershell
./scripts/vcpkg/Test-CrtSysVcpkgPort.ps1 -Triplet x64-windows-static
```

After publishing a stable tag, update the source hash with the tag archive:

```powershell
./scripts/vcpkg/Update-CrtSysVcpkgPort.ps1 `
  -Version <version> -SourceArchivePath <source-tarball>
```

The manual **Update official vcpkg** workflow validates the stable tag,
regenerates the versions database, builds the port from source without a binary
cache, and tests CMake and Visual Studio consumers. `validate` has no external
side effects; `submit` pushes the fork branch and creates or updates a PR.
Official microsoft/vcpkg review and CI approval are still required.
