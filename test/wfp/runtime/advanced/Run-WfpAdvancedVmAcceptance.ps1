[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $VmxPath,

  [Parameter(Mandatory)]
  [Security.SecureString] $VmPassword,

  [string] $GuestUser = 'test',

  [Parameter(Mandatory)]
  [Security.SecureString] $GuestPassword,

  [string] $VmrunPath =
      'C:\Program Files\VMware\VMware Workstation\vmrun.exe',

  [string] $GuestRoot = 'C:\crtsys-wfp-advanced',

  [string] $DisposableGuestSentinelPath =
      'C:\crtsys-disposable-test-guest.sentinel',

  [string] $StagingRoot = '',

  [string] $PrebuiltRoot = '',

  [string] $MsQuicDllPath = '',

  [string] $MsQuicPackageVersion = '2.5.9',

  [string] $NuGetExe = '',

  [string] $HttpsStagingRoot = '',

  [string] $LogRoot = '',

  [ValidateSet('Debug', 'Release')]
  [string] $Configuration = 'Release',

  [string] $BuildRoot = '',

  [ValidateSet('v143', 'v145')]
  [string] $PlatformToolset = 'v145',

  [ValidateSet('x64', 'ARM64')]
  [string] $Architecture = 'x64',

  [string] $WindowsSdkVersion = '10.0.22621.0',

  [ValidateRange(1, 1000)]
  [int] $Iterations = 20,

  [string] $SpecializedObservationTrafficTarget = '',

  [switch] $SpecializedObservationRequireMac,

  [switch] $SpecializedObservationRequireVSwitch,

  [ValidateRange(100, 300000)]
  [int] $SpecializedObservationTrafficDurationMs = 5000,

  [ValidateSet('all', 'datagram-proxy', 'async-inspection',
               'flow-monitor', 'stream-edit', 'connect-redirect',
               'bind-redirect', 'tls-inspection-proxy',
               'http3-inspection',
               'udp-content-filter', 'tcp-content-filter',
               'specialized-observation',
               'kernel-connect-redirect',
               'kernel-tls-inspection-proxy',
               'kernel-browser-https-inspection',
               'kernel-http3-inspection',
               'kernel-udp-content-filter',
               'kernel-tcp-content-filter')]
  [string[]] $SelectedWfpSample = @('all'),

  [uri] $ManagedHttp3Url,

  [ValidateSet('Any', 'Client', 'Server')]
  [string] $ExpectedProductType = 'Any',

  [ValidateRange(0, 999999)]
  [int] $MinimumBuild = 0,

  [switch] $RequireHvci,

  [switch] $RequireActiveIpsecSecurityAssociation,

  [switch] $RequireLowResourcesSimulation,

  [switch] $RuntimeOnly,

  [switch] $SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$prepareScript =
    Join-Path $PSScriptRoot 'Prepare-WfpAdvancedArtifacts.ps1'
$runtimeScript = Join-Path $PSScriptRoot 'Run-WfpAdvancedSuite.ps1'
$crashPostcheckScript =
    Join-Path $PSScriptRoot '..\..\..\common\Test-VmCrashPostcheck.ps1'
$guardScript =
    Join-Path $PSScriptRoot '..\common\DisposableGuestGuard.ps1'
$httpsPrepareScript = Join-Path $PSScriptRoot (
    '..\https-live\Prepare-WfpHttpsLiveArtifacts.ps1')
$managedHttp3Script = Join-Path $PSScriptRoot (
    '..\https-live\Run-WfpManagedHttp3Suite.ps1')

$wfpSamples = @(
  [pscustomobject]@{
    Name = 'datagram-proxy'
    Driver = 'crtsys_wfp_datagram_proxy.sys'
    AdditionalDrivers = @(
      'crtsys_wfp_datagram_proxy_fragmented_buffer_contract.sys'
    )
  },
  [pscustomobject]@{
    Name = 'async-inspection'
    Driver = 'crtsys_wfp_async_inspection.sys'
  },
  [pscustomobject]@{
    Name = 'flow-monitor'
    Driver = 'crtsys_wfp_flow_monitor.sys'
  },
  [pscustomobject]@{
    Name = 'stream-edit'
    Driver = 'crtsys_wfp_stream_edit.sys'
  },
  [pscustomobject]@{
    Name = 'connect-redirect'
    Driver = 'crtsys_wfp_connect_redirect.sys'
  },
  [pscustomobject]@{
    Name = 'bind-redirect'
    Driver = 'crtsys_wfp_bind_redirect.sys'
  },
  [pscustomobject]@{
    Name = 'tls-inspection-proxy'
    Driver = 'crtsys_wfp_tls_inspection_proxy.sys'
  },
  [pscustomobject]@{
    Name = 'http3-inspection'
    Driver = 'crtsys_wfp_http3_inspection_driver.sys'
  },
  [pscustomobject]@{
    Name = 'udp-content-filter'
    Driver = 'crtsys_wfp_udp_content_filter.sys'
  },
  [pscustomobject]@{
    Name = 'tcp-content-filter'
    Driver = 'crtsys_wfp_tcp_content_filter.sys'
  },
  [pscustomobject]@{
    Name = 'specialized-observation'
    Driver = 'crtsys_wfp_specialized_observation.sys'
  },
  [pscustomobject]@{
    Name = 'kernel-connect-redirect'
    Driver = 'crtsys_wfp_kernel_connect_redirect.sys'
  },
  [pscustomobject]@{
    Name = 'kernel-tls-inspection-proxy'
    Driver = 'crtsys_wfp_kernel_tls_inspection_proxy.sys'
  },
  [pscustomobject]@{
    Name = 'kernel-browser-https-inspection'
    Driver = 'crtsys_wfp_kernel_browser_https_inspection.sys'
  },
  [pscustomobject]@{
    Name = 'kernel-http3-inspection'
    Driver = 'crtsys_wfp_kernel_http3_inspection.sys'
  },
  [pscustomobject]@{
    Name = 'kernel-udp-content-filter'
    Driver = 'crtsys_wfp_kernel_udp_content_filter.sys'
  },
  [pscustomobject]@{
    Name = 'kernel-tcp-content-filter'
    Driver = 'crtsys_wfp_kernel_tcp_content_filter.sys'
  }
)

$selectedWfpSamples = @(
  if ($SelectedWfpSample -contains 'all') {
    $wfpSamples | ForEach-Object Name
  } else {
    $wfpSamples |
        Where-Object { $SelectedWfpSample -contains $_.Name } |
        ForEach-Object Name
  }
)
if ($selectedWfpSamples.Count -eq 0) {
  throw 'No WFP sample was selected.'
}
if ($RequireActiveIpsecSecurityAssociation) {
  if ($selectedWfpSamples -notcontains 'specialized-observation') {
    throw (
      'The active IPsec gate requires the specialized-observation sample.')
  }
  if ([string]::IsNullOrWhiteSpace(
      $SpecializedObservationTrafficTarget)) {
    throw (
      'The active IPsec gate requires ' +
      '-SpecializedObservationTrafficTarget set to the protected peer.')
  }
}
$targetDrivers = @(
  $wfpSamples |
      Where-Object { $selectedWfpSamples -contains $_.Name } |
      ForEach-Object {
        $_.Driver
        if ($_.PSObject.Properties['AdditionalDrivers']) {
          $_.AdditionalDrivers
        }
      } |
      Select-Object -Unique
)
if ($null -ne $ManagedHttp3Url) {
  $targetDrivers += 'crtsys_wfp_browser_https_inspection.sys'
  $targetDrivers = @($targetDrivers | Select-Object -Unique)
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
if ([string]::IsNullOrWhiteSpace($StagingRoot)) {
  $StagingRoot = Join-Path $repoRoot (
      "artifacts\wfp-advanced-staging-$($Architecture.ToLowerInvariant())")
}
if ([string]::IsNullOrWhiteSpace($HttpsStagingRoot)) {
  $HttpsStagingRoot = Join-Path $repoRoot (
      "artifacts\wfp-https-live-staging-$($Architecture.ToLowerInvariant())")
}
if ([string]::IsNullOrWhiteSpace($LogRoot)) {
  $LogRoot = Join-Path $repoRoot 'artifacts\wfp-advanced-acceptance'
}

function ConvertTo-PlainText([Security.SecureString] $Value) {
  $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Value)
  try {
    return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
  } finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
  }
}

function ConvertTo-PowerShellLiteral([string] $Value) {
  return "'" + ($Value -replace "'", "''") + "'"
}

foreach ($requiredPath in @(
    $VmrunPath, $VmxPath, $guardScript, $crashPostcheckScript)) {
  if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
    throw "Required acceptance file was not found: $requiredPath"
  }
}

$vmPasswordText = ConvertTo-PlainText $VmPassword
$guestPasswordText = ConvertTo-PlainText $GuestPassword

function Invoke-Vmrun {
  param(
    [Parameter(Mandatory)]
    [string[]] $Arguments,
    [switch] $Guest,
    [switch] $AllowFailure,
    [switch] $Quiet
  )

  $prefix = @('-T', 'ws')
  if ($vmPasswordText.Length -ne 0) {
    $prefix += @('-vp', $vmPasswordText)
  }
  if ($Guest) {
    $prefix += @('-gu', $GuestUser, '-gp', $guestPasswordText)
  }
  $allArguments = $prefix + $Arguments
  $output = @(& $VmrunPath @allArguments 2>&1)
  $exitCode = $LASTEXITCODE
  if (-not $Quiet) {
    $output | ForEach-Object { Write-Host $_ }
  }
  if ($exitCode -ne 0 -and -not $AllowFailure) {
    $displayArguments =
        for ($index = 0; $index -lt $allArguments.Count; ++$index) {
          if ($index -gt 0 -and
              $allArguments[$index - 1] -in @('-vp', '-gp')) {
            '<redacted>'
          } else {
            $allArguments[$index]
          }
        }
    throw "vmrun $($displayArguments -join ' ') failed with exit code $exitCode."
  }
  return [pscustomobject]@{ ExitCode = $exitCode; Output = $output }
}

function Invoke-GuestScript {
  param(
    [Parameter(Mandatory)]
    [string] $Script,
    [switch] $AllowFailure,
    [switch] $Quiet
  )
  $encoded = [Convert]::ToBase64String(
      [Text.Encoding]::Unicode.GetBytes($Script))
  return Invoke-Vmrun -Guest -AllowFailure:$AllowFailure -Quiet:$Quiet `
      -Arguments @(
        'runProgramInGuest', $VmxPath,
        'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
        '-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-EncodedCommand', $encoded
      )
}

function Copy-ToGuest([string] $Source, [string] $Destination) {
  Invoke-Vmrun -Guest -Quiet -Arguments @(
    'copyFileFromHostToGuest', $VmxPath, $Source, $Destination
  ) | Out-Null
}

function Copy-FromGuest(
  [string] $Source, [string] $Destination, [switch] $AllowFailure
) {
  Invoke-Vmrun -Guest -Quiet -AllowFailure:$AllowFailure -Arguments @(
    'copyFileFromGuestToHost', $VmxPath, $Source, $Destination
  ) | Out-Null
}

function Wait-GuestReady {
  for ($attempt = 1; $attempt -le 90; ++$attempt) {
    $result = Invoke-GuestScript -Script 'exit 0' -AllowFailure -Quiet
    if ($result.ExitCode -eq 0) {
      return
    }
    Start-Sleep -Seconds 2
  }
  throw 'Guest operations did not become ready within 180 seconds.'
}

function Capture-Verifier(
  [string] $GuestQuery, [string] $HostQuery,
  [string] $GuestSettings, [string] $HostSettings
) {
  $queryLiteral = ConvertTo-PowerShellLiteral $GuestQuery
  $settingsLiteral = ConvertTo-PowerShellLiteral $GuestSettings
  Invoke-GuestScript -Script @"
`$ErrorActionPreference = 'Stop'
`$query = @(& verifier.exe /query 2>&1)
`$queryCode = `$LASTEXITCODE
`$query | ForEach-Object { `$_.ToString() -replace [char]0, '' } |
    Set-Content -LiteralPath $queryLiteral -Encoding UTF8
`$settings = @(& verifier.exe /querysettings 2>&1)
`$settingsCode = `$LASTEXITCODE
`$settings | ForEach-Object { `$_.ToString() -replace [char]0, '' } |
    Set-Content -LiteralPath $settingsLiteral -Encoding UTF8
if (`$queryCode -notin @(0, 2) -or `$settingsCode -notin @(0, 2)) {
  throw "Driver Verifier query failed: query=`$queryCode settings=`$settingsCode"
}
"@ | Out-Null
  Copy-FromGuest $GuestQuery $HostQuery
  Copy-FromGuest $GuestSettings $HostSettings
}

function Get-DriverVerifierIntentionalFailureCount([string] $Path) {
  $lines = @(Get-Content -LiteralPath $Path)
  $specialPoolIndex = -1
  for ($index = 0; $index -lt $lines.Count; ++$index) {
    if ($lines[$index].Contains('SpecialPool')) {
      $specialPoolIndex = $index
      break
    }
  }
  if ($specialPoolIndex -lt 0) {
    throw 'Driver Verifier pool statistics did not contain SpecialPool.'
  }

  $followingCounters = @()
  for ($index = $specialPoolIndex + 1;
       $index -lt $lines.Count -and $followingCounters.Count -lt 4;
       ++$index) {
    $match = [regex]::Match($lines[$index], ':\s*([0-9][0-9., ]*)$')
    if ($match.Success) {
      $digits = $match.Groups[1].Value -replace '[^0-9]', ''
      if ($digits.Length -ne 0) {
        $followingCounters += [UInt64]::Parse(
            $digits, [Globalization.CultureInfo]::InvariantCulture)
      }
    }
  }
  if ($followingCounters.Count -ne 4) {
    throw (
      'Driver Verifier pool statistics did not expose the expected four ' +
      'post-SpecialPool counters.')
  }
  return [UInt64] $followingCounters[3]
}

function Get-DriverVerifierLoadCounts([string] $Path, [string] $Driver) {
  $line = @(Get-Content -LiteralPath $Path | Where-Object {
    $_.IndexOf($Driver, [StringComparison]::OrdinalIgnoreCase) -ge 0
  }) | Select-Object -First 1
  if (-not $line) {
    throw "Driver Verifier did not list target $Driver."
  }

  # Both localized and English verifier output end the module line with two
  # counters in parentheses: load-count / unload-count.  Match from the final
  # parenthesized pair so digits in names such as http3 cannot be mistaken for
  # evidence.
  $match = [regex]::Match(
      $line, '\([^0-9]*([0-9]+)\s*/[^0-9]*([0-9]+)\s*\)\s*$')
  if (-not $match.Success) {
    throw "Driver Verifier load/unload counters could not be read for $Driver."
  }

  return [pscustomobject]@{
    Load = [UInt64]::Parse(
        $match.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
    Unload = [UInt64]::Parse(
        $match.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture)
  }
}

function Capture-ActiveIpsecSecurityAssociation(
  [string] $GuestPath, [string] $HostPath, [string] $ExpectedPeer
) {
  $guestPathLiteral = ConvertTo-PowerShellLiteral $GuestPath
  $peerLiteral = ConvertTo-PowerShellLiteral $ExpectedPeer
  $result = Invoke-GuestScript -AllowFailure -Script @"
`$ErrorActionPreference = 'Stop'
`$peer = $peerLiteral
`$associations = @(
  Get-NetIPsecQuickModeSA | Where-Object {
    [string] `$_.LocalEndpoint -eq `$peer -or
        [string] `$_.RemoteEndpoint -eq `$peer
  }
)
if (`$associations.Count -eq 0) {
  throw "No active IPsec Quick Mode SA matched peer `$peer."
}
@{
  Peer = `$peer
  Count = `$associations.Count
  CapturedAtUtc = [DateTime]::UtcNow.ToString('o')
  Associations = @(`$associations | Select-Object `
      InstanceID, LocalEndpoint, RemoteEndpoint, `
      TransportLayerFilterName, EncapsulationMode, Direction, `
      EncryptionAlgorithm, IntegrityAlgorithm)
} | ConvertTo-Json -Depth 5 | `
    Set-Content -LiteralPath $guestPathLiteral -Encoding UTF8
"@
  if ($result.ExitCode -ne 0) {
    return $false
  }
  Copy-FromGuest $GuestPath $HostPath
  return $true
}

try {
  New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null

  $prepareParameters = @{
    Configuration = $Configuration
    PlatformToolset = $PlatformToolset
    Architecture = $Architecture
    WindowsSdkVersion = $WindowsSdkVersion
    OutputRoot = $StagingRoot
    SelectedWfpSample = $selectedWfpSamples
    SkipBuild = [bool] $SkipBuild
    MsQuicPackageVersion = $MsQuicPackageVersion
  }
  if (-not [string]::IsNullOrWhiteSpace($BuildRoot)) {
    $prepareParameters.BuildRoot = $BuildRoot
  }
  if (-not [string]::IsNullOrWhiteSpace($PrebuiltRoot)) {
    $prepareParameters.PrebuiltRoot = $PrebuiltRoot
  }
  if (-not [string]::IsNullOrWhiteSpace($MsQuicDllPath)) {
    $prepareParameters.MsQuicDllPath = $MsQuicDllPath
  }
  if (-not [string]::IsNullOrWhiteSpace($NuGetExe)) {
    $prepareParameters.NuGetExe = $NuGetExe
  }
  & $prepareScript @prepareParameters
  if ($LASTEXITCODE -ne 0) {
    throw 'Preparing advanced WFP runtime artifacts failed.'
  }

  if ($null -ne $ManagedHttp3Url) {
    $httpsPrepareArguments = @(
      '-NoProfile', '-ExecutionPolicy', 'Bypass',
       '-File', $httpsPrepareScript,
       '-PlatformToolset', $PlatformToolset,
       '-Architecture', $Architecture,
       '-WindowsSdkVersion', $WindowsSdkVersion,
      '-OutputRoot', $HttpsStagingRoot
    )
    if ($SkipBuild) { $httpsPrepareArguments += '-SkipBuild' }
    & powershell.exe @httpsPrepareArguments
    if ($LASTEXITCODE -ne 0) {
      throw 'Preparing the WFP managed HTTP/3 artifacts failed.'
    }
  }

  Wait-GuestReady
  $rootLiteral = ConvertTo-PowerShellLiteral $GuestRoot
  Invoke-GuestScript -Script @"
`$root = $rootLiteral
if (`$root -ne 'C:\' -and `$root.Length -gt 3 -and
    (Test-Path -LiteralPath `$root)) {
  Remove-Item -LiteralPath `$root -Recurse -Force
}
New-Item -ItemType Directory -Force -Path `$root | Out-Null
"@ | Out-Null
  $guestCrashPostcheck =
      Join-Path $GuestRoot 'Test-VmCrashPostcheck.ps1'
  Copy-ToGuest $crashPostcheckScript $guestCrashPostcheck

  $platformGuest = Join-Path $GuestRoot 'platform-evidence.json'
  $platformHost = Join-Path $LogRoot 'platform-evidence.json'
  $platformLiteral = ConvertTo-PowerShellLiteral $platformGuest
  $requireHvciLiteral = if ($RequireHvci) { '$true' } else { '$false' }
  Invoke-GuestScript -Script @"
`$ErrorActionPreference = 'Stop'
`$os = Get-CimInstance Win32_OperatingSystem
`$deviceGuard = `$null
if ($requireHvciLiteral) {
  `$deviceGuard = Get-CimInstance -Namespace 'root\Microsoft\Windows\DeviceGuard' -ClassName Win32_DeviceGuard -OperationTimeoutSec 10 -ErrorAction Stop
}
`$securityServices = @(if (`$deviceGuard) {
  `$deviceGuard.SecurityServicesRunning
})
[pscustomobject]@{
  ComputerName = `$env:COMPUTERNAME
  Architecture = `$env:PROCESSOR_ARCHITECTURE
  Caption = `$os.Caption
  Version = `$os.Version
  BuildNumber = [int] `$os.BuildNumber
  ProductType = [int] `$os.ProductType
  HvciRunning = [bool] (`$securityServices -contains 2)
  HvciEvidenceAvailable = [bool] (`$null -ne `$deviceGuard)
  CapturedAtUtc = [DateTime]::UtcNow.ToString('o')
} | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $platformLiteral -Encoding UTF8
"@ | Out-Null
  Copy-FromGuest $platformGuest $platformHost
  $platform = Get-Content -LiteralPath $platformHost -Raw |
      ConvertFrom-Json
  $expectedGuestArchitecture =
      if ($Architecture -eq 'ARM64') { 'ARM64' } else { 'AMD64' }
  if ($platform.Architecture -ne $expectedGuestArchitecture) {
    throw (
      "Guest architecture '$($platform.Architecture)' did not match " +
      "'$expectedGuestArchitecture'.")
  }
  if ($MinimumBuild -ne 0 -and
      [int] $platform.BuildNumber -lt $MinimumBuild) {
    throw "Guest build $($platform.BuildNumber) is older than $MinimumBuild."
  }
  if ($ExpectedProductType -eq 'Client' -and
      [int] $platform.ProductType -ne 1) {
    throw 'The matrix row requires a Windows client guest.'
  }
  if ($ExpectedProductType -eq 'Server' -and
      [int] $platform.ProductType -eq 1) {
    throw 'The matrix row requires a Windows Server guest.'
  }
  if ($RequireHvci -and -not [bool] $platform.HvciRunning) {
    throw 'The matrix row requires running HVCI/Memory Integrity.'
  }

  $beforeQueryGuest = Join-Path $GuestRoot 'verifier-before.txt'
  $beforeQueryHost = Join-Path $LogRoot 'verifier-before.txt'
  $beforeSettingsGuest = Join-Path $GuestRoot 'verifier-settings-before.txt'
  $beforeSettingsHost = Join-Path $LogRoot 'verifier-settings-before.txt'
  Capture-Verifier $beforeQueryGuest $beforeQueryHost `
      $beforeSettingsGuest $beforeSettingsHost
  $beforeIntentionalAllocationFailures = [UInt64] 0
  $afterIntentionalAllocationFailures = [UInt64] 0
  $intentionalAllocationFailureDelta = [UInt64] 0
  if ($RequireLowResourcesSimulation) {
    $settingsText = Get-Content -LiteralPath $beforeSettingsHost -Raw
    $flagMatch = [regex]::Match($settingsText, '0x([0-9a-fA-F]{8})')
    if (-not $flagMatch.Success) {
      throw 'Driver Verifier flags could not be read from /querysettings.'
    }
    $verifierFlags = [Convert]::ToUInt32($flagMatch.Groups[1].Value, 16)
    $lowResourcesFlags = 0x00000004 -bor 0x00040000
    if (($verifierFlags -band $lowResourcesFlags) -eq 0) {
      throw (
          'Low Resources Simulation is required but neither Random nor ' +
          'Systematic Low Resources Simulation is active in this boot.')
    }
    $beforeIntentionalAllocationFailures =
        Get-DriverVerifierIntentionalFailureCount $beforeQueryHost
  }
  if ($RequireActiveIpsecSecurityAssociation) {
    $ipsecBeforeGuest = Join-Path $GuestRoot 'ipsec-quick-mode-sa-before.json'
    $ipsecBeforeHost = Join-Path $LogRoot 'ipsec-quick-mode-sa-before.json'
    if (-not (Capture-ActiveIpsecSecurityAssociation `
        $ipsecBeforeGuest $ipsecBeforeHost `
        $SpecializedObservationTrafficTarget)) {
      throw (
        'No active IPsec Quick Mode security association matched the ' +
        'configured protected peer before driver load.')
    }
  }

  # Record guest-native event-log and dump baselines. Comparing RecordId and
  # file fingerprints avoids host/guest time-zone and clock-skew ambiguity.
  $eventBaselineGuest = Join-Path $GuestRoot 'crash-event-baseline.txt'
  $eventBaselineHost = Join-Path $LogRoot 'crash-event-baseline.txt'
  $dumpBaselineGuest = Join-Path $GuestRoot 'crash-dump-baseline.txt'
  $dumpBaselineHost = Join-Path $LogRoot 'crash-dump-baseline.txt'
  Invoke-Vmrun -Guest -Quiet -Arguments @(
    'runProgramInGuest', $VmxPath,
    'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
    $guestCrashPostcheck,
    '-EventBaselinePath', $eventBaselineGuest,
    '-DumpBaselinePath', $dumpBaselineGuest,
    '-CaptureBaseline'
  ) | Out-Null
  Copy-FromGuest $eventBaselineGuest $eventBaselineHost
  Copy-FromGuest $dumpBaselineGuest $dumpBaselineHost
  $beforeText = Get-Content -LiteralPath $beforeQueryHost -Raw
  $beforeVerifierCounts = @{}
  if (-not $RuntimeOnly) {
    foreach ($driver in $targetDrivers) {
      if (-not $beforeText.Contains($driver)) {
        throw (
          "Driver Verifier is not preconfigured for $driver. " +
          'Configure all selected targets and boot the guest manually first.')
      }
      $beforeVerifierCounts[$driver] =
          Get-DriverVerifierLoadCounts $beforeQueryHost $driver
    }
  }

  foreach ($file in Get-ChildItem -LiteralPath $StagingRoot -File) {
    Copy-ToGuest $file.FullName (Join-Path $GuestRoot $file.Name)
  }
  if ($null -ne $ManagedHttp3Url) {
    foreach ($file in Get-ChildItem -LiteralPath $HttpsStagingRoot -File) {
      Copy-ToGuest $file.FullName (Join-Path $GuestRoot $file.Name)
    }
    Copy-ToGuest $managedHttp3Script (
        Join-Path $GuestRoot 'Run-WfpManagedHttp3Suite.ps1')
  }
  Copy-ToGuest $runtimeScript (
      Join-Path $GuestRoot ([IO.Path]::GetFileName($runtimeScript)))
  Copy-ToGuest $guardScript (
      Join-Path $GuestRoot 'DisposableGuestGuard.ps1')
  $guestLog = Join-Path $GuestRoot 'runtime-suite.log'
  $guestLogLiteral = ConvertTo-PowerShellLiteral $guestLog
  $selectedWfpLiterals = $selectedWfpSamples |
      ForEach-Object { ConvertTo-PowerShellLiteral $_ }
  $selectedWfpExpression =
      '@(' + ($selectedWfpLiterals -join ',') + ')'
  $sentinelLiteral =
      ConvertTo-PowerShellLiteral $DisposableGuestSentinelPath
  $specializedTargetLiteral =
      ConvertTo-PowerShellLiteral $SpecializedObservationTrafficTarget
  $specializedRequireMacExpression = if (
      $SpecializedObservationRequireMac) { '$true' } else { '$false' }
  $specializedRequireVSwitchExpression = if (
      $SpecializedObservationRequireVSwitch) { '$true' } else { '$false' }
  $runResult = Invoke-GuestScript -AllowFailure -Script @"
`$ErrorActionPreference = 'Stop'
try {
  `$selectedSamples = $selectedWfpExpression
  Remove-Item -LiteralPath $guestLogLiteral -Force -ErrorAction SilentlyContinue
  'GUEST_WRAPPER_START' | Out-File -LiteralPath $guestLogLiteral -Encoding Unicode
  foreach (`$selectedSample in `$selectedSamples) {
    "GUEST_SUITE_START=`$selectedSample" | Out-File -LiteralPath $guestLogLiteral -Encoding Unicode -Append
    `$suiteParameters = @{
      PackageRoot = $rootLiteral
      SelectedSample = `$selectedSample
      Iterations = $Iterations
      AllowDisposableGuestMutation = `$true
      DisposableGuestSentinelPath = $sentinelLiteral
      SpecializedObservationTrafficTarget = $specializedTargetLiteral
      SpecializedObservationRequireMac = $specializedRequireMacExpression
      SpecializedObservationRequireVSwitch = $specializedRequireVSwitchExpression
      SpecializedObservationTrafficDurationMs = $SpecializedObservationTrafficDurationMs
    }
    & (Join-Path $rootLiteral 'Run-WfpAdvancedSuite.ps1') @suiteParameters *>&1 | Tee-Object -FilePath $guestLogLiteral -Append | Out-Null
  }
  'PASS' | Out-File -LiteralPath $guestLogLiteral -Encoding Unicode -Append
} catch {
  `$_ | Out-String | Out-File -LiteralPath $guestLogLiteral -Encoding Unicode -Append
  'FAIL' | Out-File -LiteralPath $guestLogLiteral -Encoding Unicode -Append
  throw
}
"@
  Copy-FromGuest $guestLog (Join-Path $LogRoot 'runtime-suite.log') `
      -AllowFailure
  $suiteExitCode = $runResult.ExitCode
  $suiteFailureMessage = $null
  $guestFailureMessage = $null
  if ($suiteExitCode -ne 0) {
    $suiteFailureMessage =
        "The guest WFP suite failed with $suiteExitCode."
    $guestFailureMessage = $suiteFailureMessage
  }

  # The suite removes driver services in finally blocks, including when fault
  # injection makes an acceptance operation fail.  Verify that cleanup from
  # outside the suite before a Low Resources gate can treat that failure as an
  # expected fail-closed outcome.
  $cleanupGuest = Join-Path $GuestRoot 'runtime-cleanup-evidence.json'
  $cleanupHost = Join-Path $LogRoot 'runtime-cleanup-evidence.json'
  $cleanupGuestLiteral = ConvertTo-PowerShellLiteral $cleanupGuest
  $cleanupEvidenceResult = Invoke-GuestScript -AllowFailure -Script @"
`$rootPrefix = $rootLiteral
`$remainingDrivers = @(
  Get-CimInstance Win32_SystemDriver | Where-Object {
    `$path = [string] `$_.PathName
    -not [string]::IsNullOrWhiteSpace(`$path) -and
        `$path.IndexOf(`$rootPrefix, [StringComparison]::OrdinalIgnoreCase) -ge 0
  } | Select-Object Name, State, PathName
)
`$remainingProcesses = @(
  Get-CimInstance Win32_Process | Where-Object {
    `$path = [string] `$_.ExecutablePath
    -not [string]::IsNullOrWhiteSpace(`$path) -and
        `$path.IndexOf(`$rootPrefix, [StringComparison]::OrdinalIgnoreCase) -ge 0
  } | Select-Object ProcessId, Name, ExecutablePath
)
[pscustomobject]@{
  RemainingDriverCount = `$remainingDrivers.Count
  RemainingProcessCount = `$remainingProcesses.Count
  RemainingDrivers = `$remainingDrivers
  RemainingProcesses = `$remainingProcesses
  CapturedAtUtc = [DateTime]::UtcNow.ToString('o')
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $cleanupGuestLiteral -Encoding UTF8
"@
  Copy-FromGuest $cleanupGuest $cleanupHost -AllowFailure
  $cleanupFailureMessage = $null
  if ($cleanupEvidenceResult.ExitCode -ne 0 -or
      -not (Test-Path -LiteralPath $cleanupHost)) {
    $cleanupFailureMessage = 'The guest cleanup evidence could not be captured.'
  } else {
    $cleanupEvidence = Get-Content -LiteralPath $cleanupHost -Raw |
        ConvertFrom-Json
    if ([int] $cleanupEvidence.RemainingDriverCount -ne 0 -or
        [int] $cleanupEvidence.RemainingProcessCount -ne 0) {
      $cleanupFailureMessage =
          'A staged WFP driver service or process survived suite cleanup.'
    }
  }
  if ($cleanupFailureMessage) {
    $guestFailureMessage = $cleanupFailureMessage
  }

  if (-not $guestFailureMessage -and $null -ne $ManagedHttp3Url) {
    $guestHttp3Log = Join-Path $GuestRoot 'wfp-managed-http3-suite.log'
    $guestHttp3LogLiteral =
        ConvertTo-PowerShellLiteral $guestHttp3Log
    $managedHttp3UrlLiteral =
        ConvertTo-PowerShellLiteral $ManagedHttp3Url.AbsoluteUri
    $http3RunResult = Invoke-GuestScript -AllowFailure -Script @"
`$ErrorActionPreference = 'Stop'
try {
  `$http3Parameters = @{
    PackageRoot = $rootLiteral
    Url = $managedHttp3UrlLiteral
    AllowDisposableGuestMutation = `$true
    DisposableGuestSentinelPath = $sentinelLiteral
  }

  & (Join-Path $rootLiteral 'Run-WfpManagedHttp3Suite.ps1') @http3Parameters *>&1 | Tee-Object -FilePath $guestHttp3LogLiteral | Out-Null
  'PASS' | Out-File -LiteralPath $guestHttp3LogLiteral -Encoding Unicode -Append
} catch {
  `$_ | Out-String | Out-File -LiteralPath $guestHttp3LogLiteral -Encoding Unicode -Append
  'FAIL' | Out-File -LiteralPath $guestHttp3LogLiteral -Encoding Unicode -Append
  throw
}
"@
    Copy-FromGuest $guestHttp3Log (
        Join-Path $LogRoot 'wfp-managed-http3-suite.log') -AllowFailure
    if ($http3RunResult.ExitCode -ne 0) {
      $guestFailureMessage =
          "The guest managed HTTP/3 suite failed: $($http3RunResult.ExitCode)."
    }
  }

  if (-not $guestFailureMessage -and
      $RequireActiveIpsecSecurityAssociation) {
    $ipsecAfterGuest = Join-Path $GuestRoot 'ipsec-quick-mode-sa-after.json'
    $ipsecAfterHost = Join-Path $LogRoot 'ipsec-quick-mode-sa-after.json'
    if (-not (Capture-ActiveIpsecSecurityAssociation `
        $ipsecAfterGuest $ipsecAfterHost `
        $SpecializedObservationTrafficTarget)) {
      $guestFailureMessage =
          'The protected peer IPsec Quick Mode SA did not survive the test.'
    }
  }

  $afterQueryGuest = Join-Path $GuestRoot 'verifier-after.txt'
  $afterQueryHost = Join-Path $LogRoot 'verifier-after.txt'
  $afterSettingsGuest = Join-Path $GuestRoot 'verifier-settings-after.txt'
  $afterSettingsHost = Join-Path $LogRoot 'verifier-settings-after.txt'
  Capture-Verifier $afterQueryGuest $afterQueryHost `
      $afterSettingsGuest $afterSettingsHost
  if ($RequireLowResourcesSimulation) {
    $afterIntentionalAllocationFailures =
        Get-DriverVerifierIntentionalFailureCount $afterQueryHost
    $intentionalAllocationFailureDelta =
        $afterIntentionalAllocationFailures -
        $beforeIntentionalAllocationFailures
    if ($intentionalAllocationFailureDelta -le 0 -and
        -not $guestFailureMessage) {
      $guestFailureMessage =
          'Low Resources Simulation produced no intentional allocation failure.'
    }
  }
  $afterText = Get-Content -LiteralPath $afterQueryHost -Raw
  if (-not $RuntimeOnly) {
    $verifierLoadEvidence = @()
    foreach ($driver in $targetDrivers) {
      $beforeCounts = $beforeVerifierCounts[$driver]
      $afterCounts = Get-DriverVerifierLoadCounts $afterQueryHost $driver
      if ($afterCounts.Load -le $beforeCounts.Load -or
          $afterCounts.Unload -le $beforeCounts.Unload) {
        throw (
          "Verifier did not report a new load/unload cycle for $driver " +
          "(before=$($beforeCounts.Load)/$($beforeCounts.Unload), " +
          "after=$($afterCounts.Load)/$($afterCounts.Unload)).")
      }
      $verifierLoadEvidence += [pscustomobject]@{
        Driver = $driver
        BeforeLoad = $beforeCounts.Load
        BeforeUnload = $beforeCounts.Unload
        AfterLoad = $afterCounts.Load
        AfterUnload = $afterCounts.Unload
        LoadDelta = $afterCounts.Load - $beforeCounts.Load
        UnloadDelta = $afterCounts.Unload - $beforeCounts.Unload
      }
    }
    $verifierLoadEvidence | ConvertTo-Json | Set-Content -LiteralPath (
        Join-Path $LogRoot 'verifier-load-unload-evidence.json') -Encoding UTF8
  }
  $beforeSettings =
      (Get-Content -LiteralPath $beforeSettingsHost -Raw) -replace "`r`n", "`n"
  $afterSettings =
      (Get-Content -LiteralPath $afterSettingsHost -Raw) -replace "`r`n", "`n"
  if ($beforeSettings -cne $afterSettings) {
    throw 'Driver Verifier settings changed during the read-only gate.'
  }

  $postGuest = Join-Path $GuestRoot 'postcheck.txt'
  $postHost = Join-Path $LogRoot 'postcheck.txt'
  Invoke-Vmrun -Guest -Quiet -Arguments @(
    'runProgramInGuest', $VmxPath,
    'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
    $guestCrashPostcheck,
    '-EventBaselinePath', $eventBaselineGuest,
    '-DumpBaselinePath', $dumpBaselineGuest,
    '-OutputPath', $postGuest
  ) | Out-Null
  Copy-FromGuest $postGuest $postHost
  $postText = Get-Content -LiteralPath $postHost -Raw
  if ($postText -notmatch 'EVENT_COUNT=0' -or
      $postText -notmatch 'DUMP_COUNT=0' -or
      $postText -notmatch 'EVENT_LOG_RESET=0') {
    throw 'The VM postcheck found a new crash event or dump.'
  }

  $lowResourcesGracefulFailure = $false
  if ($RequireLowResourcesSimulation -and $suiteFailureMessage -and
      $intentionalAllocationFailureDelta -gt 0 -and
      -not $cleanupFailureMessage -and
      $guestFailureMessage -eq $suiteFailureMessage) {
    $guestFailureMessage = $null
    $lowResourcesGracefulFailure = $true
  }

  if ($RequireLowResourcesSimulation) {
    [pscustomobject]@{
      Before = $beforeIntentionalAllocationFailures
      After = $afterIntentionalAllocationFailures
      Delta = $intentionalAllocationFailureDelta
      SuiteExitCode = $suiteExitCode
      GracefulFailClosedObserved = $lowResourcesGracefulFailure
      CleanupVerified = (-not $cleanupFailureMessage)
      CrashAndDumpPostcheckPassed = $true
      VerifierSettingsUnchanged = $true
      CapturedAtUtc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json | Set-Content -LiteralPath (
        Join-Path $LogRoot 'low-resources-evidence.json') -Encoding UTF8
  }

  if ($guestFailureMessage) {
    throw $guestFailureMessage
  }

  if ($RuntimeOnly) {
    Write-Host (
      'Advanced WFP VM runtime acceptance passed with crash/dump checks ' +
      'and unchanged Driver Verifier state; selected drivers were not ' +
      'claimed as Verifier targets.')
  } else {
    Write-Host (
      'Advanced WFP VM acceptance passed without changing guest boot or ' +
      'Driver Verifier state.')
  }
} finally {
  $vmPasswordText = $null
  $guestPasswordText = $null
}
