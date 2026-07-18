# NTL RPC lifecycle stress test

This is a test fixture, not an onboarding example. It exercises lifetime
boundaries that a single RPC call cannot cover:

- concurrent open, contract query, call, and close cycles;
- serialized container allocation on every short-lived handle;
- a service stop while a long RPC callback owns server rundown protection;
- completion of that in-flight call before driver unload;
- service restart followed by a new contract query and RPC call; and
- repeated outer load, run, stop, unload, and delete cycles.

The app arguments are:

```text
crtsys_rpc_lifecycle_stress_app.exe [iterations] [workers] [service-name] [slow-ms]
```

When `service-name` is present, the app performs the in-flight stop and restarts
the service before returning. The outer VM runner can therefore stop and delete
the restarted service normally and repeat the complete cycle. Defaults are 128
handle cycles per worker, eight workers, and a 1500 ms slow callback.

## Build

```powershell
cmake -S test\rpc\lifecycle-stress `
  -B test\rpc\lifecycle-stress\build_x64 -A x64
cmake --build test\rpc\lifecycle-stress\build_x64 --config Debug
```

## VM and Driver Verifier

Run only in a disposable kernel-debugging VM. A representative run uses three
outer load cycles and passes the service name to the app:

```powershell
& D:\projects\crtsys-vm-test\Run-CrtSysDriverAppInVm.ps1 `
  -DriverPath .\test\rpc\lifecycle-stress\build_x64\Debug\crtsys_rpc_lifecycle_stress_driver.sys `
  -AppPath .\test\rpc\lifecycle-stress\build_x64\Debug\crtsys_rpc_lifecycle_stress_app.exe `
  -GuestDriverFileName crtsys_rpc_lifecycle_stress_driver.sys `
  -ServiceName CrtSysRpcLifecycleStress `
  -GuestDirectory C:\crtsys-rpc-lifecycle-stress `
  -AppArguments 128,8,CrtSysRpcLifecycleStress,1500 `
  -LoadCycles 3
```

Enable Driver Verifier for `crtsys_rpc_lifecycle_stress_driver.sys`, reboot the
guest, and confirm the target with `verifier /query` before a long run. Success
requires every app and service transition to pass, no verifier breakpoint or
bugcheck, and a successful post-restart contract query on every cycle.

The fixture has been validated with a v143 x64 Debug driver and a v143 x86
Release client for three outer load cycles. Each cycle completed 1,024 client
open/close operations, 2,048 RPC calls, an in-flight service stop, a service
restart, and a fresh post-restart RPC call. With standard Driver Verifier
enabled, the same run completed six verified driver loads and six unloads
because every outer cycle includes the in-flight restart.
