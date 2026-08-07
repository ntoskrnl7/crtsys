# WFP ALE connect-block 런타임 acceptance

`Run-AleConnectBlockSuite.ps1`는 서명된 ALE connect-block callout을 load하고, 관찰 가능한 block/remove/restore 테스트를 반복한 뒤 service를 제거합니다. 호출자가 명시적인 `-AllowDisposableGuestMutation` acknowledgement를 제공하고 guest에 정확히 `CRTSYS_DISPOSABLE_TEST_GUEST`라는 내용의 sentinel file이 있을 때만 machine을 변경합니다.

`Run-AleConnectBlockVmAcceptance.ps1`는 operator가 이미 부팅한 VMware Windows guest에 package를 준비·staging하고 suite를 실행하며 Driver Verifier가 load를 관찰했는지 확인하고 crash event와 dump를 검사합니다. 실행 전후 `verifier /query`, `verifier /querysettings`를 읽고 설정이 동일하게 유지되어야 합니다.

runner는 VM을 시작·reset·revert·reboot하지 않으며 Driver Verifier 설정도 바꾸지 않습니다. operator는 `crtsys_wfp_ale_connect_block.sys`를 Verifier target으로 구성하고, 필요하면 **Disable driver signature enforcement**를 선택하는 것을 포함해 수동으로 부팅해야 합니다. 또한 guest에서 `C:\crtsys-disposable-test-guest.sentinel`을 만들고 그 내용으로 정확히 `CRTSYS_DISPOSABLE_TEST_GUEST`를 작성해야 합니다. runner는 이를 만들지 않습니다.

password는 `SecureString` 값으로 받고 보고되는 `vmrun` command failure에서는 redact합니다.

예:

```powershell
$vmxPath = Read-Host 'Path to the disposable test VMX'
$vmPassword = Read-Host 'VM encryption password' -AsSecureString
$guestPassword = Read-Host 'Guest password' -AsSecureString

.\test\wfp\runtime\ale-connect-block\Run-AleConnectBlockVmAcceptance.ps1 `
  -VmxPath $vmxPath `
  -VmPassword $vmPassword `
  -GuestPassword $guestPassword
```

build staging과 log는 기본적으로 현재 repository checkout의 `artifacts` 아래 sample 이름 directory를 사용하며, 호출자가 두 경로를 모두 override할 수 있습니다. build cache는 `artifacts/b/wfp-ale-connect-block-runtime` 아래에서 toolset과 SDK version별로 격리됩니다. `BuildRoot`로 이 위치를 override할 수 있고 SDK 간에 CMake cache를 공유하지 않습니다.

guest 안에서 같은 boot에 직접 실행하려면:

```powershell
.\Run-AleConnectBlockSuite.ps1 `
  -PackageRoot C:\wfp-ale-connect-block `
  -AllowDisposableGuestMutation `
  -DisposableGuestSentinelPath C:\crtsys-disposable-test-guest.sentinel
```
