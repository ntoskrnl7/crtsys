# WFP ALE connect-block runtime acceptance

`Run-AleConnectBlockSuite.ps1` loads the signed ALE connect-block callout,
repeats the observable block/remove/restore test, and removes the service.
It refuses to mutate a machine unless the caller supplies the explicit
`-AllowDisposableGuestMutation` acknowledgement and the guest contains a
sentinel file with the exact text `CRTSYS_DISPOSABLE_TEST_GUEST`.

`Run-AleConnectBlockVmAcceptance.ps1` prepares and stages the package into a
VMware Windows guest that the operator has already booted, runs the suite,
verifies that Driver Verifier observed a load, and checks for crash events and
dumps. It reads `verifier /query` and `verifier /querysettings` before and
after the run and requires the settings to remain identical.

The runner never starts, resets, reverts, or reboots the VM and never changes
Driver Verifier. The operator must configure
`crtsys_wfp_ale_connect_block.sys` as a Verifier target and boot manually,
including choosing **Disable driver signature enforcement** when required.
The operator must also create
`C:\crtsys-disposable-test-guest.sentinel` in the guest with the exact
contents `CRTSYS_DISPOSABLE_TEST_GUEST`; the runner never creates it.

Passwords are accepted as `SecureString` values and redacted from reported
`vmrun` command failures.

Example:

```powershell
$vmxPath = Read-Host 'Path to the disposable test VMX'
$vmPassword = Read-Host 'VM encryption password' -AsSecureString
$guestPassword = Read-Host 'Guest password' -AsSecureString

.\test\wfp\runtime\ale-connect-block\Run-AleConnectBlockVmAcceptance.ps1 `
  -VmxPath $vmxPath `
  -VmPassword $vmPassword `
  -GuestPassword $guestPassword
```

Build staging and logs default to sample-named directories under `artifacts`
in the current repository checkout; callers may override both paths. The
build cache is isolated by toolset and SDK version under
`artifacts/b/wfp-ale-connect-block-runtime`; `BuildRoot` may override that
location without sharing a CMake cache across SDKs.

For a same-boot direct run inside the guest:

```powershell
.\Run-AleConnectBlockSuite.ps1 `
  -PackageRoot C:\wfp-ale-connect-block `
  -AllowDisposableGuestMutation `
  -DisposableGuestSentinelPath C:\crtsys-disposable-test-guest.sentinel
```
