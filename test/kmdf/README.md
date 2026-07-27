# KMDF tests

The directories below contain compile, runtime, and stress-oriented KMDF
fixtures. They are kept separate from the onboarding examples under
[`examples/kmdf`](../../examples/kmdf/README.md).

The repository-wide mapping from Microsoft samples to typed NTL mechanisms is
maintained in the
[WDK KMDF sample coverage matrix](WDK-SAMPLE-COVERAGE.md).

| Test | Purpose |
| --- | --- |
| [`compile`](compile) | Compile-time callback, object, request, context, PnP, filter, bus, DMA, USB, and WMI contracts. |
| [`runtime`](runtime) | Software-only build/staging and VM orchestration for control, PnP, echo/cancel, reference ABI, bus, filter, WMI, restart, unload, cross-bitness, Driver Verifier, crash checks, and verifier restoration. |
| [`verifier-stress`](verifier-stress/README.md) | Repeated queue, request, timer, work-item, object-lifetime, and unload exercise intended for Driver Verifier runs. |

Build the verifier fixture through the same CI entry point used by the
repository:

```powershell
.\scripts\ci\Build-CrtSys.ps1 `
  -Project kmdf-verifier-stress `
  -Architecture x64 `
  -Configuration Release
```

The VM runner and verifier configuration live in the companion
`crtsys-vm-test` repository. See the fixture README for the current invocation
and pass/fail artifacts.
