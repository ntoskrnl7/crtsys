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
the service before returning, then leaves the restarted service ready for the
next cycle. Defaults are 128
handle cycles per worker, eight workers, and a 1500 ms slow callback.

## Build

```powershell
cmake -S test\rpc\lifecycle-stress `
  -B test\rpc\lifecycle-stress\build_x64 -A x64
cmake --build test\rpc\lifecycle-stress\build_x64 --config Debug
```
