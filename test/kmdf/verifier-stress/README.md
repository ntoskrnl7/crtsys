# KMDF Verifier Stress Test

See the [KMDF test index](../README.md) for the distinction between repository
tests and the public examples.

This is a test fixture, not an onboarding example. It deliberately combines:

- concurrent open, close, and transform IOCTL operations;
- a release-versus-cancel race for a pending manual-queue request;
- exactly-once completion and counter checks;
- KMDF forward-progress callbacks; and
- repeated service load, app execution, unload, and deletion in the VM runner.

Every asynchronous operation has a bounded wait. The release/cancel race may
legally select either winner, but the request must complete exactly once and
only the matching driver counter may advance.

## Build

```powershell
.\scripts\ci\Build-CrtSys.ps1 `
  -Project kmdf-verifier-stress `
  -Architecture x64 `
  -Configuration Release
```

The app accepts optional iteration and worker counts:

```text
crtsys_kmdf_verifier_stress_app.exe [iterations] [workers]
```

The default is 64 race iterations and four parallel transform workers.

## Driver Verifier workflow

Run this fixture only in a disposable test VM with a kernel debugger attached.
Enable the standard Driver Verifier settings for
`crtsys_kmdf_verifier_stress.sys`, reboot the guest, and confirm the target is
listed by `verifier /query` before running the app. A representative run uses
64 iterations, four workers, and three independent driver load/unload cycles.

The test is successful only when every app invocation and service transition
passes without a verifier breakpoint or bugcheck. Record `verifier /query`
after the run so its load/unload and Special Pool counters demonstrate that
the target was actually verified. Reset persistent verifier settings and
reboot the guest after testing.

The companion VM repository wraps the driver/app runner with the expected
service name and app arguments:

```powershell
D:\projects\crtsys-vm-test\Run-CrtSysKmdfVerifierStressInVm.ps1 `
  -DriverPath .\test\kmdf\verifier-stress\build_x64\Release\crtsys_kmdf_verifier_stress.sys `
  -AppPath .\test\kmdf\verifier-stress\build_x64\Release\crtsys_kmdf_verifier_stress_app.exe `
  -Iterations 64 `
  -Workers 4 `
  -LoadCycles 3
```
