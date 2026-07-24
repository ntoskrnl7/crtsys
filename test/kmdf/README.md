# KMDF tests

The directories below contain automated or stress-oriented KMDF fixtures. They
are kept separate from the onboarding examples under
[`examples/kmdf`](../../examples/kmdf/README.md).

| Test | Purpose |
| --- | --- |
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
