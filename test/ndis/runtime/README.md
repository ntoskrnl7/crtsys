# NDIS runtime acceptance

`Run-NdisLwfSuite.ps1` installs the LWF as a network component, verifies
attach/restart, real send/receive NBLs, regular OID pass-through, status/NetPnP
propagation, and metadata preservation/restoration. It restarts a selected
adapter to force pause/restart plus status/NetPnP callbacks, repeats the
controller, and uninstalls the component so detach runs.

`Prepare-NdisLwfArtifacts.ps1` builds x64 Release, signs the SYS, creates a
catalog from that exact signed binary, signs the catalog, and packages both
test certificates. Run the suite only in a disposable VM with test-signing
enabled. The host machine is never an installation target.

NDIS acceptance is intentionally independent from the WFP VM gate. Package
with `Prepare-NdisLwfArtifacts.ps1`, copy the resulting directory and
`Run-NdisLwfSuite.ps1` into a disposable test VM, then run the suite while
Driver Verifier actively targets `crtsys_ndis_lwf_monitor.sys` with the
NDIS/WIFI verification class enabled. Restore the VM's prior Verifier
configuration after the test. Pass `-RequireVerifier` to query the active—not
merely next-boot—settings and reject a mismatched VM. Use `-SoakMinutes 30` (or
longer) for a sustained run. `-AdapterName` selects the adapter to restart;
otherwise the first Up,
non-loopback adapter is used. Adapter restart can temporarily disconnect the
VM, so run the suite from its console rather than a remote-only session.

Example:

```powershell
.\Run-NdisLwfSuite.ps1 `
  -PackageRoot C:\ndis-lwf-package `
  -AdapterName Ethernet `
  -RestartIterations 5 `
  -SoakMinutes 30 `
  -RequireVerifier
```

The suite is independent from the WFP and HTTP/3 VM gates. The load-time
contract also covers bounded TCP reassembly across reordering,
loss/retransmission, overlap rejection, FIN, and sequence wraparound. Forced
load-time edge contracts cover immediate and pending lower-edge OID results,
cancel observe-before-forward ordering, and resource-constrained immediate
receive return even when the physical adapter does not naturally emit those
cases.
