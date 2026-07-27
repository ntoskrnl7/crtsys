# KMDF software-only runtime suite

This fixture runs the public software-only KMDF examples as one observable VM
gate. It deliberately reuses the readable examples instead of maintaining
second copies of the same drivers.

The suite covers:

- non-PnP control-device load, I/O, cancellation, and unload;
- root PnP install, device-interface I/O, restart, and removal;
- delayed echo completion and `CancelIoEx`;
- versioned reference ABI, per-file sessions, delayed completion, and cancel;
- dynamic PDO plug, query, missing, re-plug, and eject;
- upper-filter forwarding and typed completion;
- MOF-backed WMI query, set, method, and event delivery; and
- optional x86 applications against the x64 drivers.

Each application validates returned state. Debug output is not a pass
condition.

## Artifact layout

Stage x64 packages under one directory:

```text
packages/
  basic/
  pnp/
  echo/
  reference/
  bus/
  filter-stack/
  wmi/
```

Each directory contains the signed/test-signed driver package (`.sys`, INF,
and catalog), the `.exe`, and any sample-specific MOF artifacts. The `bus`
directory must include both the bus and child-function INF packages. If
cross-bitness validation is desired, stage the x86 applications using the same
directory names under a second root.

The guest must already trust the certificate used to sign these packages (or
be configured with an equivalent production signing chain). The suite does not
modify certificate stores or enable test-signing mode.

## Disposable VM invocation

From an elevated PowerShell session:

```powershell
.\Run-KmdfRuntimeSuite.ps1 `
  -PackageRoot C:\crtsys-kmdf-runtime\x64 `
  -X86AppRoot C:\crtsys-kmdf-runtime\x86
```

Use `-SkipWmi` only for a guest image where the WMI service is intentionally
unavailable. The script rejects a non-elevated session, verifies every
required artifact before use, and removes each root-enumerated sample in a
`finally` block.

Driver Verifier concurrency and repeated-load stress remain in the separate
[`verifier-stress`](../verifier-stress/README.md) fixture. DMA, USB, PCI, and
device-class runtime validation require matching hardware and are not part of
this software-only gate.

## Host-side VM acceptance gate

`Run-KmdfVmAcceptance.ps1` is the complete host workflow. It:

1. builds and stages the x64 WDK packages and x86 applications;
2. selects every software-only sample plus the stress fixture in a `Oneboot`
   standard Driver Verifier configuration;
3. reboots and proves the selected binaries are active;
4. imports only the staged test certificates into the disposable guest;
5. runs the runtime suite with x64 and WOW64 clients;
6. runs the concurrent verifier-stress fixture through repeated load cycles;
7. checks active Verifier activity, bugcheck events, dumps, and device cleanup;
   and
8. restores the explicitly supplied prior Verifier targets and reboots again.

Passwords are `SecureString` parameters. The script converts them to plaintext
only while invoking `vmrun`, redacts credential arguments from errors, and
does not write them to generated scripts or logs.

```powershell
$vmPassword = Read-Host 'VM encryption password' -AsSecureString
$guestPassword = Read-Host 'Guest password' -AsSecureString

.\Run-KmdfVmAcceptance.ps1 `
  -VmxPath 'D:\VMs\Win11_25H2_KD\Windows 11 x64 25H2.vmx' `
  -VmPassword $vmPassword `
  -GuestUser test `
  -GuestPassword $guestPassword `
  -RestoreDriverFileName @(
    'previous_driver_one.sys',
    'previous_driver_two.sys'
  ) `
  -RestoreBootMode Persistent
```

Use `Prepare-KmdfRuntimeArtifacts.ps1 -SkipBuild` when validating an existing
set of local outputs. The acceptance wrapper also accepts `-SkipBuild` for the
same purpose. An empty `RestoreDriverFileName` resets Verifier instead of
inventing a prior configuration.

## Recorded validation

On 2026-07-26, the new `echo` and `filter-stack` paths were validated on
Windows 11 Pro for Workstations x64, build 26200:

- the x64 driver packages passed with both x64 and WOW64 applications;
- Echo observed two timer completions, one `CancelIoEx` cancellation, and
  PASSIVE_LEVEL execution;
- the filter stack reported both layer bits, target and completion callbacks,
  `PrepareHardware`, and D0 entry;
- both samples passed a device restart followed by a second application run;
  and
- every run removed the root device successfully.

On 2026-07-27, the same six cases were repeated in a controlled `Oneboot`
Driver Verifier session targeting:

- `crtsys_kmdf_echo_ntl_sample.sys`;
- `crtsys_kmdf_filter_stack_target.sys`; and
- `crtsys_kmdf_filter_stack_filter.sys`.

All six cases passed under the standard flags, including Special Pool, IRQL,
I/O, deadlock, DDI compliance, and WDF verification. The final active query
reported all three modules loaded and 675 of 675 successful pool allocations
served from Special Pool, with no failed or intentionally failed allocations.
No bugcheck or unexpected-reboot event and no new dump were recorded after the
Verifier boot. The root devices were absent after cleanup. The VM's original
Persistent Verifier configuration for its three minifilter binaries was then
restored and confirmed active after another reboot.

Later on 2026-07-27, the complete host acceptance gate passed on the same
Windows 11 x64 build 26200 VM:

- `basic`, `pnp`, `echo`, `reference`, `bus`, `filter-stack`, and `wmi`
  passed with both x64 and WOW64 applications;
- every applicable PnP case passed a device restart and second x64/WOW64 run;
- all ten participating driver binaries were listed as active Verifier
  targets and were observed loaded;
- the stress fixture passed 64 cancellation races with four workers across
  three independent driver load/unload cycles;
- Verifier reported 3,100 of 3,100 successful pool allocations served from
  Special Pool, with zero failed, intentionally failed, untagged, or
  untracked allocations;
- no test device remained present and no bugcheck, unexpected-reboot event,
  or dump was recorded after the Verifier boot; and
- the VM's original Persistent three-minifilter Verifier configuration was
  restored and confirmed after the final reboot.

The host-side logs are stored under the ignored `artifacts` directory using
the `kmdf-{echo,filter-stack}-win11-25h2-*.log` and
`kmdf-verifier-*-20260727.*` names. The complete gate above is recorded under
`artifacts/kmdf-acceptance-win11-25h2-20260727-v5`.
