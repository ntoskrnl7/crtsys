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
$vmxPath = Read-Host 'Path to the disposable test VMX'
$vmPassword = Read-Host 'VM encryption password' -AsSecureString
$guestUser = Read-Host 'Guest user'
$guestPassword = Read-Host 'Guest password' -AsSecureString
$existingVerifierTargets = @(
  # Copy exact .sys names from verifier /querysettings, if any.
)

.\Run-KmdfVmAcceptance.ps1 `
  -VmxPath $vmxPath `
  -VmPassword $vmPassword `
  -GuestUser $guestUser `
  -GuestPassword $guestPassword `
  -RestoreDriverFileName $existingVerifierTargets `
  -RestoreBootMode Persistent
```

Use `Prepare-KmdfRuntimeArtifacts.ps1 -SkipBuild` when validating an existing
set of local outputs. The acceptance wrapper also accepts `-SkipBuild` for the
same purpose. An empty `RestoreDriverFileName` resets Verifier instead of
inventing a prior configuration.

## Acceptance criteria

A complete host acceptance run must satisfy all of these conditions:

- every software-only sample passes with its x64 application and, when staged,
  its WOW64 application;
- every applicable PnP sample passes a device restart and a second application
  run;
- every temporary Verifier target is active and records driver load/unload;
- the stress fixture completes the configured cancellation races, workers, and
  load cycles;
- no test device, service, imported test certificate, crash event, or new dump
  remains after cleanup; and
- the exact caller-supplied Verifier target list and boot mode are restored.

Host logs are written to the configurable `LogRoot`; retain that directory as a
CI or release artifact.
