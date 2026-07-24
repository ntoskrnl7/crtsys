# NTL KMDF PnP sample

This sample is a root-enumerated KMDF device that demonstrates typed NTL
wrappers across the PnP and power lifecycle:

- prepare/release-hardware callbacks and resource-list iteration;
- D0 entry/exit state;
- device-interface registration and user-mode discovery through SetupAPI;
- idle-policy configuration;
- a PASSIVE-level sequential queue with typed input/output buffers; and
- context-owned counters verified by the companion application.

The included INF uses the development-only hardware ID
`Root\CrtSysKmdfNtlPnpSample`.

## Build

Open `crtsys_kmdf_pnp_ntl_sample_vs.sln` and build `Debug|x64` or
`Release|x64`, or use CMake from the repository root:

```powershell
cmake -S examples\kmdf\pnp `
      -B artifacts\examples\kmdf-pnp -A x64
cmake --build artifacts\examples\kmdf-pnp --config Debug
```

## Disposable VM smoke test

Install the built package from an elevated PowerShell session:

```powershell
.\install.ps1 -PackageDirectory .\x64\Debug
.\x64\Debug\crtsys_kmdf_pnp_ntl_sample_app.exe 40
.\remove.ps1
```

The app locates the device interface, issues the typed query IOCTL, and checks
that prepare-hardware and D0-entry callbacks ran at least once. Use only a
disposable test VM when installing development drivers.
