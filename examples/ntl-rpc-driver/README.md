# NTL RPC Sample Driver

This sample shows the NTL RPC helper layer. It is intentionally separate from
the typed IOCTL sample in `examples/ntl-driver`: the IOCTL sample shows manual
device-control contracts, while this sample shows a shared schema that
generates both the kernel server and the user-mode client wrappers.

The sample demonstrates:

- `ntl::main` as the C++ driver entry point
- `ntl::rpc::server` lifetime owned by the driver unload callback
- `ntl::rpc::client` from a user-mode companion app
- stable explicit RPC callback IDs with `NTL_ADD_CALLBACK_ID_*`
- serialization of simple scalar values, a custom request/reply pair, and a
  `std::vector`

## Visual Studio / NuGet

Open [`crtsys_ntl_rpc_sample_vs.sln`](./crtsys_ntl_rpc_sample_vs.sln) in Visual
Studio with the WDK workload installed. The solution contains:

- `crtsys_ntl_rpc_sample`: the kernel RPC server driver
- `crtsys_ntl_rpc_sample_app`: the user-mode RPC client app

Restore NuGet packages, then build `Debug|x64` or `Release|x64`. The project
files use:

```xml
<PackageReference Include="crtsys" Version="$(CrtSysPackageVersion)" />
```

`CrtSysPackageVersion` defaults to `*`, so NuGet restore selects the latest
stable `crtsys` package from the configured package sources. For reproducible
builds, pin an exact package version from MSBuild:

```bat
msbuild crtsys_ntl_rpc_sample_vs.sln /restore /p:Configuration=Debug /p:Platform=x64 /p:CrtSysPackageVersion=0.1.32
```

## CMake Build

From the repository root:

```bat
cmake -S examples\ntl-rpc-driver -B examples\ntl-rpc-driver\build_x64 -A x64
cmake --build examples\ntl-rpc-driver\build_x64 --config Debug
```

The Debug output is:

```text
examples\ntl-rpc-driver\build_x64\Debug\crtsys_ntl_rpc_sample.sys
examples\ntl-rpc-driver\build_x64\Debug\crtsys_ntl_rpc_sample_app.exe
```

To suppress diagnostic breakpoints while experimenting:

```bat
cmake -S examples\ntl-rpc-driver -B examples\ntl-rpc-driver\build_x64 -A x64 -DCRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS=OFF
```

## Shared Schema

The shared contract lives in
[`shared/ntl_rpc_sample.hpp`](./shared/ntl_rpc_sample.hpp). The driver includes
it after `<ntl/rpc/server>`, and the app includes it after `<ntl/rpc/client>`.
The callback bodies in that shared header are the server implementation: they
run in the kernel driver when the app sends an RPC request. The user-mode app
gets matching wrapper declarations from the same schema, not local execution of
those callback bodies.

The schema exposes:

- `crtsys_ntl_rpc_sample::add(a, b)`
- `crtsys_ntl_rpc_sample::describe(request)`
- `crtsys_ntl_rpc_sample::series(count)`

The `describe` callback intentionally calls the kernel-only WDK API
`KeGetCurrentIrql()` and returns that value as `server_irql`. This makes the
execution boundary visible: the callback body is driver-side code, while the
app only receives the serialized reply.
The callbacks also emit one-line `DbgPrint` messages so a kernel debugger can
see that `add`, `describe`, and `series` ran in the driver.

Use explicit callback IDs for externally visible RPC contracts. The
`NTL_ADD_CALLBACK_*` macros that use `__LINE__` are convenient for tests, but
line numbers are not a stable ABI.

## Loading

Use your normal isolated driver-test VM:

```bat
sc create CrtSysNtlRpcSample binpath= "C:\path\to\crtsys_ntl_rpc_sample.sys" type= kernel start= demand
sc start CrtSysNtlRpcSample
sc stop CrtSysNtlRpcSample
sc delete CrtSysNtlRpcSample
```

## User-Mode RPC App

The sample app calls both generated wrappers and an explicit reusable
`ntl::rpc::client`:

```bat
examples\ntl-rpc-driver\build_x64\Debug\crtsys_ntl_rpc_sample_app.exe 21 7
```

Expected output:

```text
rpc ok: add=42 value=21 doubled=42 biased=28 server_irql=0 series=4
```
