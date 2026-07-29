# NDIS runtime acceptance

`Run-NdisLwfSuite.ps1` installs the LWF as a network component, verifies
attach/restart and real outbound NBL observation, repeats the controller, and
uninstalls the component so pause/detach run.

`Prepare-NdisLwfArtifacts.ps1` builds x64 Release, signs the SYS, creates a
catalog from that exact signed binary, signs the catalog, and packages both
test certificates. Run the suite only in a disposable VM with test-signing
enabled. The host machine is never an installation target.

NDIS acceptance is intentionally independent from the WFP VM gate. Package
with `Prepare-NdisLwfArtifacts.ps1`, copy the resulting directory and
`Run-NdisLwfSuite.ps1` into a disposable test VM, then run the suite while
Driver Verifier targets `crtsys_ndis_lwf_monitor.sys` with the NDIS/WIFI
verification class enabled. Restore the VM's prior Verifier configuration
after the test.

## Deferred NDIS work

The current LWF smoke gate has exercised component install/uninstall,
attach/restart/pause/detach, and real send/complete observation. The broader
NDIS phase is deliberately tracked separately from WFP and HTTP/3 work. It
still needs dedicated receive-path traffic, offload metadata mutation and
restoration, OID handling, TCP reassembly under loss/reordering, pause/restart
stress, and longer Driver Verifier runs before the NDIS surface is called
complete.
