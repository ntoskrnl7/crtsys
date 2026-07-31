# WFP ALE connect-block runtime acceptance

`Run-AleConnectBlockSuite.ps1` loads the signed ALE connect-block callout,
repeats the observable block/remove/restore test, and removes the service.

`Run-AleConnectBlockVmAcceptance.ps1` prepares and stages the package into a
VMware Windows guest, enables standard Driver Verifier checks for one boot,
runs the suite, verifies that Verifier observed a driver load, checks for
crash events and dumps, and restores the exact driver list and boot mode
supplied through `RestoreDriverFileName` and `RestoreBootMode`.

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
  -GuestPassword $guestPassword `
  -RestoreDriverFileName $existingVerifierTargets `
  -RestoreBootMode Persistent
```

`$existingVerifierTargets` is the caller's previously captured list. Pass an
empty array only when the disposable guest intentionally had no Verifier
targets. Build staging and logs default to sample-named directories under
`artifacts` in the current repository checkout; callers may override both
paths. The build cache is isolated by toolset and SDK version under
`artifacts/b/wfp-ale-connect-block-runtime`; `BuildRoot` may override that
location without sharing a CMake cache across SDKs.
