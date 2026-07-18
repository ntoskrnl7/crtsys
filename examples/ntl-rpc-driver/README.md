# NTL RPC Sample Driver

This sample shows the NTL RPC helper layer. It is intentionally separate from
the typed IOCTL sample in `examples/ntl-driver`: the IOCTL sample shows manual
device-control contracts, while this sample generates both sides from one
shared callback declaration.

The sample demonstrates:

- `ntl::main` as the C++ driver entry point
- `ntl::rpc::server` lifetime owned by the driver unload callback
- `ntl::rpc::client` from a user-mode companion app
- shared-schema RPC callback IDs and deduced return types
- direct generated wrappers plus a reusable typed client
- bounded variable-size responses checked before server callback execution
- a secure control device restricted to Local System and Administrators by
  default
- per-method request and decode-allocation limits
- an immutable dispatch table with rundown-protected shutdown
- startup contract discovery for version, capability, and method compatibility
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
[`shared/ntl_rpc_sample.hpp`](./shared/ntl_rpc_sample.hpp). The macro bodies
become kernel callbacks when included after `<ntl/rpc/server>` and typed
user-mode wrappers when included after `<ntl/rpc/client>`.

The schema exposes:

- `crtsys_ntl_rpc_sample::add`
- `crtsys_ntl_rpc_sample::describe`
- `crtsys_ntl_rpc_sample::series`

The schema uses `NTL_RPC_BEGIN_CONTRACT` to publish application contract
version `1`, one sample capability bit, and the registered method IDs. The app
calls `client.require_contract()` once before its first generated wrapper, so a
mismatched driver fails with a contract diagnostic instead of an unrelated
method error.

The driver-side `describe` callback intentionally calls the kernel-only WDK API
`KeGetCurrentIrql()` and returns that value as `server_irql`. This makes the
execution boundary visible: the app only receives the serialized reply.
The callbacks also emit one-line `DbgPrint` messages so a kernel debugger can
see that `add`, `describe`, and `series` ran in the driver.

The `series` callback uses `NTL_RPC_BOUNDED_RESPONSE` to declare a 64 KiB
maximum serialized response. The client uses that value as its receive
capacity, and the server verifies the capacity before executing the callback.
Use `NTL_RPC_METHOD_LIMITS` instead when a method also needs request and
decode-allocation limits smaller than the secure defaults documented in
[`docs/ntl/rpc.md`](../../docs/ntl/rpc.md).

The macro-generated server freezes its dispatch table after registering the
shared schema. Keep the returned server owner alive until driver unload. NTL
rejects new RPC calls during shutdown and waits for callbacks already in
progress before the owner releases the device.

## Loading

Use your normal isolated driver-test VM:

```bat
sc create CrtSysNtlRpcSample binpath= "C:\path\to\crtsys_ntl_rpc_sample.sys" type= kernel start= demand
sc start CrtSysNtlRpcSample
sc stop CrtSysNtlRpcSample
sc delete CrtSysNtlRpcSample
```

## User-Mode RPC App

The sample app uses the generated wrappers for the occasional `add` and
`describe` calls, then reuses one `ntl::rpc::client` for the `series` call:

```bat
examples\ntl-rpc-driver\build_x64\Debug\crtsys_ntl_rpc_sample_app.exe 21 7
```

Expected output:

```text
rpc ok: add=42 value=21 doubled=42 biased=28 server_irql=0 series=4
```
