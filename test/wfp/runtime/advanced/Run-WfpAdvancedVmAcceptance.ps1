[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $VmxPath,

  [Parameter(Mandatory)]
  [Security.SecureString] $VmPassword,

  [string] $GuestUser = 'test',

  [Parameter(Mandatory)]
  [Security.SecureString] $GuestPassword,

  [ValidateSet('Manual', 'Automatic')]
  [string] $RestartMode = 'Manual',

  [ValidateRange(60, 3600)]
  [int] $ManualRestartTimeoutSeconds = 900,

  [string] $VmrunPath =
      'C:\Program Files\VMware\VMware Workstation\vmrun.exe',

  [string] $GuestRoot = 'C:\crtsys-wfp-advanced',

  [string] $StagingRoot = '',

  [string] $PrebuiltRoot = '',

  [string] $HttpsStagingRoot = '',

  [string] $LogRoot = '',

  [ValidateSet('v143', 'v145')]
  [string] $PlatformToolset = 'v145',

  [ValidateSet('x64', 'ARM64')]
  [string] $Architecture = 'x64',

  [string] $WindowsSdkVersion = '10.0.22621.0',

  [string[]] $RestoreDriverFileName = @(),

  [ValidateSet('Oneboot', 'Persistent', 'ResetOnBootFail')]
  [string] $RestoreBootMode = 'Persistent',

  [ValidatePattern('^(standard|0x[0-9A-Fa-f]+)$')]
  [string] $VerifierFlags = '0x209BB',

  [ValidatePattern('^(standard|0x[0-9A-Fa-f]+)$')]
  [string] $RestoreVerifierFlags = 'standard',

  [ValidateRange(1, 1000)]
  [int] $Iterations = 20,

  [ValidateSet('Systematic', 'Randomized')]
  [string] $LowResourceMode = 'Systematic',

  [ValidateRange(1, 10000)]
  [int] $LowResourceProbability = 10000,

  [ValidateRange(1, 10)]
  [int] $LowResourceRunsPerSample = 1,

  [ValidateRange(1, 64)]
  [int] $SystematicInjectionPassesPerSample = 4,

  [ValidateSet('all', 'datagram-proxy', 'async-inspection',
               'flow-monitor', 'stream-edit', 'connect-redirect',
               'bind-redirect', 'tls-inspection-proxy',
               'udp-content-filter', 'tcp-content-filter',
               'specialized-observation')]
  [string[]] $SelectedWfpSample = @('all'),

  [uri] $ManagedHttp3Url,

  [ValidateSet('Any', 'Client', 'Server')]
  [string] $ExpectedProductType = 'Any',

  [ValidateRange(0, 999999)]
  [int] $MinimumBuild = 0,

  [switch] $RequireHvci,

  [switch] $SkipLowResourcePass,

  [switch] $SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$prepareScript =
    Join-Path $PSScriptRoot 'Prepare-WfpAdvancedArtifacts.ps1'
$runtimeScript = Join-Path $PSScriptRoot 'Run-WfpAdvancedSuite.ps1'
$httpsPrepareScript = Join-Path $PSScriptRoot (
    '..\https-live\Prepare-WfpHttpsLiveArtifacts.ps1')
$wfpSamples = @(
  [pscustomobject]@{
    Name = 'datagram-proxy'
    Driver = 'crtsys_wfp_datagram_proxy.sys'
    Service = 'CrtSysWfpDatagramProxyAcceptance'
  },
  [pscustomobject]@{
    Name = 'async-inspection'
    Driver = 'crtsys_wfp_async_inspection.sys'
    Service = 'CrtSysWfpAsyncInspectionAcceptance'
  },
  [pscustomobject]@{
    Name = 'flow-monitor'
    Driver = 'crtsys_wfp_flow_monitor.sys'
    Service = 'CrtSysWfpFlowMonitorAcceptance'
  },
  [pscustomobject]@{
    Name = 'stream-edit'
    Driver = 'crtsys_wfp_stream_edit.sys'
    Service = 'CrtSysWfpStreamEditAcceptance'
  },
  [pscustomobject]@{
    Name = 'connect-redirect'
    Driver = 'crtsys_wfp_connect_redirect.sys'
    Service = 'CrtSysWfpConnectRedirectAcceptance'
  },
  [pscustomobject]@{
    Name = 'bind-redirect'
    Driver = 'crtsys_wfp_bind_redirect.sys'
    Service = 'CrtSysWfpBindRedirectAcceptance'
  },
  [pscustomobject]@{
    Name = 'tls-inspection-proxy'
    Driver = 'crtsys_wfp_tls_inspection_proxy.sys'
    Service = 'CrtSysWfpTlsInspectionProxyAcceptance'
  },
  [pscustomobject]@{
    Name = 'udp-content-filter'
    Driver = 'crtsys_wfp_udp_content_filter.sys'
    Service = 'CrtSysWfpUdpContentFilterAcceptance'
  },
  [pscustomobject]@{
    Name = 'tcp-content-filter'
    Driver = 'crtsys_wfp_tcp_content_filter.sys'
    Service = 'CrtSysWfpTcpContentFilterAcceptance'
  },
  [pscustomobject]@{
    Name = 'specialized-observation'
    Driver = 'crtsys_wfp_specialized_observation.sys'
    Service = 'CrtSysWfpSpecializedObservationAcceptance'
  }
)
$selectedWfpSamples = if ($SelectedWfpSample -contains 'all') {
  @($wfpSamples | ForEach-Object Name)
} else {
  @($wfpSamples |
      Where-Object { $SelectedWfpSample -contains $_.Name } |
      ForEach-Object Name)
}
$targetDrivers = @(
  $wfpSamples |
      Where-Object { $selectedWfpSamples -contains $_.Name } |
      ForEach-Object Driver
)
if ($null -ne $ManagedHttp3Url) {
  $targetDrivers += 'crtsys_wfp_browser_https_inspection.sys'
}
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
if ([string]::IsNullOrWhiteSpace($StagingRoot)) {
  $StagingRoot = Join-Path $repoRoot (
      "artifacts\wfp-advanced-staging-$($Architecture.ToLowerInvariant())")
}
if ([string]::IsNullOrWhiteSpace($HttpsStagingRoot)) {
  $HttpsStagingRoot = Join-Path $repoRoot 'artifacts\wfp-https-live-staging'
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

if (-not (Test-Path -LiteralPath $VmrunPath -PathType Leaf)) {
  throw "vmrun.exe was not found: $VmrunPath"
}
if (-not (Test-Path -LiteralPath $VmxPath -PathType Leaf)) {
  throw "VMX file was not found: $VmxPath"
}

$vmPasswordText = ConvertTo-PlainText $VmPassword
$guestPasswordText = ConvertTo-PlainText $GuestPassword
$verifierChanged = $false
$restoreCompleted = $false

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
      Write-Host "Guest operations ready after attempt $attempt."
      return
    }
    Start-Sleep -Seconds 2
  }
  throw 'Guest operations did not become ready within 180 seconds.'
}

function Get-GuestBootStamp {
  $guestStamp = Join-Path $GuestRoot '.boot-stamp.txt'
  $hostStamp = Join-Path $LogRoot '.boot-stamp.txt'
  $guestLiteral = ConvertTo-PowerShellLiteral $guestStamp
  $result = Invoke-GuestScript -AllowFailure -Quiet -Script @"
`$value = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
`$value.ToFileTimeUtc() |
    Set-Content -LiteralPath $guestLiteral -Encoding ASCII
"@
  if ($result.ExitCode -ne 0) {
    return $null
  }
  if (Test-Path -LiteralPath $hostStamp) {
    Remove-Item -LiteralPath $hostStamp -Force
  }
  Copy-FromGuest $guestStamp $hostStamp -AllowFailure
  if (-not (Test-Path -LiteralPath $hostStamp -PathType Leaf)) {
    return $null
  }
  $text = (Get-Content -LiteralPath $hostStamp -Raw).Trim()
  $match = [regex]::Match($text, '\b[0-9]{10,}\b')
  if (-not $match.Success) {
    return $null
  }
  return $match.Value
}

function Restart-Guest {
  $before = Get-GuestBootStamp
  if (-not $before) {
    throw 'Could not read the guest boot timestamp before restart.'
  }

  if ($RestartMode -eq 'Automatic') {
    Write-Host 'Requesting one guest operating-system restart.'
    Invoke-GuestScript -AllowFailure -Quiet -Script @'
Start-Process -FilePath "$env:SystemRoot\System32\shutdown.exe" `
    -ArgumentList '/r','/t','0','/f' -WindowStyle Hidden
'@ | Out-Null
    $maximumAttempts = 120
  } else {
    Write-Host (
        'Manual guest restart required. Restart the VM with the required ' +
        'driver-signing boot option; the runner is waiting for a new boot.')
    $maximumAttempts =
        [Math]::Ceiling($ManualRestartTimeoutSeconds / 2.0)
  }

  for ($attempt = 1; $attempt -le $maximumAttempts; ++$attempt) {
    Start-Sleep -Seconds 2
    $after = Get-GuestBootStamp
    if ($after -and $after -ne $before) {
      Write-Host "Guest restart completed after attempt $attempt."
      return
    }
  }
  $timeoutSeconds =
      if ($RestartMode -eq 'Automatic') {
        240
      } else {
        $ManualRestartTimeoutSeconds
      }
  throw (
      'The guest did not complete its operating-system restart in ' +
      "$timeoutSeconds seconds.")
}

function Set-Verifier(
  [string[]] $Drivers, [string] $BootMode, [string] $GuestLog,
  [string] $Flags
) {
  $driverLiterals =
      $Drivers | ForEach-Object { ConvertTo-PowerShellLiteral $_ }
  $driversExpression = '@(' + ($driverLiterals -join ',') + ')'
  $logLiteral = ConvertTo-PowerShellLiteral $GuestLog
  $script = @"
`$ErrorActionPreference = 'Stop'
`$drivers = $driversExpression
`$lines = [Collections.Generic.List[string]]::new()
function Run([string] `$name, [string[]] `$arguments) {
  `$lines.Add("=== `$name ===")
  `$output = @(& verifier.exe @arguments 2>&1)
  `$code = `$LASTEXITCODE
  foreach (`$line in `$output) {
    `$lines.Add((`$line.ToString() -replace [char]0, ''))
  }
  `$lines.Add("`$name`_RC=`$code")
  if (`$code -notin @(0, 2)) { throw "verifier `$name failed: `$code" }
}
Run 'SETTINGS_BEFORE' @('/querysettings')
Run 'ACTIVE_BEFORE' @('/query')
Run 'RESET' @('/reset')
if (`$drivers.Count -ne 0) {
  `$enableArguments = if ('$Flags' -eq 'standard') {
    @('/standard', '/driver') + `$drivers
  } else {
    @('/flags', '$Flags', '/driver') + `$drivers
  }
  Run 'ENABLE' `$enableArguments
  Run 'BOOT_MODE' @('/bootmode', '$($BootMode.ToLowerInvariant())')
}
Run 'SETTINGS_AFTER' @('/querysettings')
Run 'ACTIVE_AFTER' @('/query')
`$lines.Add('PASS')
`$lines | Set-Content -LiteralPath $logLiteral -Encoding UTF8
"@
  Invoke-GuestScript -Script $script | Out-Null
}

function Capture-Verifier([string] $GuestLog, [string] $HostLog) {
  $literal = ConvertTo-PowerShellLiteral $GuestLog
  Invoke-GuestScript -Script @"
`$output = @(& verifier.exe /query 2>&1)
`$code = `$LASTEXITCODE
`$output | ForEach-Object { `$_.ToString() -replace [char]0, '' } |
    Set-Content -LiteralPath $literal -Encoding UTF8
if (`$code -ne 0) { exit `$code }
"@ | Out-Null
  Copy-FromGuest $GuestLog $HostLog
}

function Set-LowResourceVerifier(
  [string[]] $Drivers,
  [string] $BootMode,
  [string] $GuestLog, [string] $HostLog
) {
  $driverLiterals =
      $Drivers | ForEach-Object { ConvertTo-PowerShellLiteral $_ }
  $driversExpression = '@(' + ($driverLiterals -join ',') + ')'
  $literal = ConvertTo-PowerShellLiteral $GuestLog
  Invoke-GuestScript -Script @"
`$ErrorActionPreference = 'Stop'
`$drivers = $driversExpression
`$lines = [Collections.Generic.List[string]]::new()
function Run([string] `$name, [string[]] `$arguments) {
  `$lines.Add("=== `$name ===")
  `$output = @(& verifier.exe @arguments 2>&1)
  `$code = `$LASTEXITCODE
  foreach (`$line in `$output) {
    `$lines.Add((`$line.ToString() -replace [char]0, ''))
  }
  `$lines.Add("`$name`_RC=`$code")
  if (`$code -notin @(0, 2)) {
    throw "verifier `$name failed: `$code"
  }
}
Run 'SETTINGS_BEFORE' @('/querysettings')
Run 'ACTIVE_BEFORE' @('/query')
Run 'RESET' @('/reset')
if ('$LowResourceMode' -eq 'Systematic') {
  Run 'ENABLE_SYSTEMATIC_LOW_RESOURCES' (
      @('/rc', '19', '36', '/driver') + `$drivers)
} else {
  Run 'ENABLE_RANDOMIZED_LOW_RESOURCES' (
      @('/faults', '$LowResourceProbability', '""', '""', '0', '/driver') +
      `$drivers)
}
Run 'BOOT_MODE' @('/bootmode', '$($BootMode.ToLowerInvariant())')
Run 'SETTINGS_AFTER' @('/querysettings')
Run 'ACTIVE_AFTER' @('/query')
`$lines.Add('PASS')
`$lines | Set-Content -LiteralPath $literal -Encoding UTF8
"@ | Out-Null
  Copy-FromGuest $GuestLog $HostLog
}

function Invoke-VerifierOperation(
  [string] $Name,
  [string[]] $Arguments,
  [string] $GuestLog,
  [string] $HostLog
) {
  $argumentLiterals =
      $Arguments |
          ForEach-Object { ConvertTo-PowerShellLiteral $_ }
  $argumentsExpression =
      '@(' + ($argumentLiterals -join ',') + ')'
  $nameLiteral = ConvertTo-PowerShellLiteral $Name
  $guestLogLiteral = ConvertTo-PowerShellLiteral $GuestLog
  Invoke-GuestScript -Script @"
`$ErrorActionPreference = 'Stop'
`$name = $nameLiteral
`$arguments = $argumentsExpression
`$output = @(& verifier.exe @arguments 2>&1)
`$code = `$LASTEXITCODE
@(
  "=== `$name ==="
  `$output
  "`$name`_RC=`$code"
) | ForEach-Object {
  `$_.ToString() -replace [char]0, ''
} | Set-Content -LiteralPath $guestLogLiteral -Encoding UTF8
if (`$code -notin @(0, 2)) {
  throw "verifier `$name failed: `$code"
}
"@ | Out-Null
  Copy-FromGuest $GuestLog $HostLog
  return Get-Content -LiteralPath $HostLog -Raw
}

function Restore-Verifier {
  if (-not $SkipLowResourcePass -and
      $LowResourceMode -eq 'Systematic') {
    Invoke-GuestScript -AllowFailure -Quiet -Script @'
& verifier.exe /faultssystematic resetruntime 2>&1 | Out-Null
& verifier.exe /faultssystematic resetboottime 2>&1 | Out-Null
'@ | Out-Null
  }
  $guestStage = Join-Path $GuestRoot 'verifier-restore-stage.txt'
  Set-Verifier $RestoreDriverFileName $RestoreBootMode $guestStage `
      $RestoreVerifierFlags
  Copy-FromGuest $guestStage (
      Join-Path $LogRoot 'verifier-restore-stage.txt')
  Restart-Guest
  $guestActive = Join-Path $GuestRoot 'verifier-restored-active.txt'
  $hostActive = Join-Path $LogRoot 'verifier-restored-active.txt'
  Capture-Verifier $guestActive $hostActive
  $text = Get-Content -LiteralPath $hostActive -Raw
  foreach ($driver in $RestoreDriverFileName) {
    if (-not $text.Contains($driver)) {
      throw "Restored verifier target was not active: $driver"
    }
  }
  foreach ($driver in $targetDrivers) {
    if ($RestoreDriverFileName -notcontains $driver -and
        $text.Contains($driver)) {
      throw "Temporary network verifier target remained active: $driver"
    }
  }
  $script:restoreCompleted = $true
}

try {
  New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null

  $prepareParameters = @{
    PlatformToolset = $PlatformToolset
    Architecture = $Architecture
    WindowsSdkVersion = $WindowsSdkVersion
    OutputRoot = $StagingRoot
    SelectedWfpSample = $selectedWfpSamples
    SkipBuild = [bool] $SkipBuild
  }
  if (-not [string]::IsNullOrWhiteSpace($PrebuiltRoot)) {
    $prepareParameters.PrebuiltRoot = $PrebuiltRoot
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

  $platformGuest = Join-Path $GuestRoot 'platform-evidence.json'
  $platformHost = Join-Path $LogRoot 'platform-evidence.json'
  $platformLiteral = ConvertTo-PowerShellLiteral $platformGuest
  $requireHvciLiteral = if ($RequireHvci) { '$true' } else { '$false' }
  Invoke-GuestScript -Script @"
`$ErrorActionPreference = 'Stop'
`$os = Get-CimInstance Win32_OperatingSystem
`$deviceGuard = `$null
if ($requireHvciLiteral) {
  `$deviceGuard = Get-CimInstance `
      -Namespace 'root\Microsoft\Windows\DeviceGuard' `
      -ClassName Win32_DeviceGuard -OperationTimeoutSec 10 `
      -ErrorAction Stop
}
`$securityServices = @(
  if (`$deviceGuard) { `$deviceGuard.SecurityServicesRunning }
)
[pscustomobject]@{
  ComputerName = `$env:COMPUTERNAME
  Architecture = `$env:PROCESSOR_ARCHITECTURE
  Caption = `$os.Caption
  Version = `$os.Version
  BuildNumber = [int] `$os.BuildNumber
  ProductType = [int] `$os.ProductType
  HvciRunning = [bool] (`$securityServices -contains 2)
  HvciEvidenceAvailable = [bool] (`$null -ne `$deviceGuard)
  SecurityServicesRunning = @(`$securityServices)
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
    throw (
        "Guest build $($platform.BuildNumber) is older than " +
        "the required build $MinimumBuild.")
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

  $preflightGuest = Join-Path $GuestRoot 'verifier-preflight.txt'
  Capture-Verifier $preflightGuest (
      Join-Path $LogRoot 'verifier-preflight.txt')

  $temporaryBootMode =
      if ($RestartMode -eq 'Manual') {
        'Persistent'
      } else {
        'Oneboot'
      }
  $enableGuest = Join-Path $GuestRoot 'verifier-enable.txt'
  Set-Verifier $targetDrivers $temporaryBootMode $enableGuest $VerifierFlags
  $verifierChanged = $true
  Copy-FromGuest $enableGuest (Join-Path $LogRoot 'verifier-enable.txt')
  Restart-Guest

  $activeGuest = Join-Path $GuestRoot 'verifier-active.txt'
  $activeHost = Join-Path $LogRoot 'verifier-active.txt'
  Capture-Verifier $activeGuest $activeHost
  $activeText = Get-Content -LiteralPath $activeHost -Raw
  foreach ($driver in $targetDrivers) {
    if (-not $activeText.Contains($driver)) {
      throw "The network driver was not active under Driver Verifier: $driver"
    }
  }
  foreach ($file in Get-ChildItem -LiteralPath $StagingRoot -File) {
    Copy-ToGuest $file.FullName (Join-Path $GuestRoot $file.Name)
  }
  if ($null -ne $ManagedHttp3Url) {
    foreach ($file in Get-ChildItem -LiteralPath $HttpsStagingRoot -File) {
      Copy-ToGuest $file.FullName (Join-Path $GuestRoot $file.Name)
    }
  }
  Copy-ToGuest $runtimeScript (
      Join-Path $GuestRoot ([IO.Path]::GetFileName($runtimeScript)))
  $guestLog = Join-Path $GuestRoot 'runtime-suite.log'
  $guestLogLiteral = ConvertTo-PowerShellLiteral $guestLog
  $selectedWfpLiterals =
      $selectedWfpSamples |
          ForEach-Object { ConvertTo-PowerShellLiteral $_ }
  $selectedWfpExpression =
      '@(' + ($selectedWfpLiterals -join ',') + ')'
  $runResult = Invoke-GuestScript -AllowFailure -Script @"
`$ErrorActionPreference = 'Stop'
try {
  `$selectedSamples = $selectedWfpExpression
  Remove-Item -LiteralPath $guestLogLiteral -Force -ErrorAction SilentlyContinue
  foreach (`$selectedSample in `$selectedSamples) {
    & (Join-Path $rootLiteral 'Run-WfpAdvancedSuite.ps1') -PackageRoot $rootLiteral -SelectedSample `$selectedSample -Iterations $Iterations *>&1 |
        Tee-Object -FilePath $guestLogLiteral -Append | Out-Null
  }
  Add-Content -LiteralPath $guestLogLiteral -Value 'PASS'
} catch {
  `$_ | Out-String | Add-Content -LiteralPath $guestLogLiteral
  Add-Content -LiteralPath $guestLogLiteral -Value 'FAIL'
  throw
}
"@
  Copy-FromGuest $guestLog (Join-Path $LogRoot 'runtime-suite.log') `
      -AllowFailure
  if ($runResult.ExitCode -ne 0) {
    throw "The guest WFP suite failed with $($runResult.ExitCode)."
  }

  if (-not $SkipLowResourcePass) {
    $lowResourceGuest =
        Join-Path $GuestRoot 'verifier-low-resource-enable.txt'
    $lowResourceHost =
        Join-Path $LogRoot 'verifier-low-resource-enable.txt'
    Set-LowResourceVerifier $targetDrivers $temporaryBootMode `
        $lowResourceGuest $lowResourceHost
    Restart-Guest

    $lowResourceActiveGuest =
        Join-Path $GuestRoot 'verifier-low-resource-active.txt'
    $lowResourceActiveHost =
        Join-Path $LogRoot 'verifier-low-resource-active.txt'
    Capture-Verifier $lowResourceActiveGuest $lowResourceActiveHost
    $lowResourceActiveText =
        Get-Content -LiteralPath $lowResourceActiveHost -Raw
    $lowResourceActiveFlagMatch = [regex]::Match(
        $lowResourceActiveText, '(?i)0x[0-9a-f]{8}')
    if (-not $lowResourceActiveFlagMatch.Success) {
      throw 'Driver Verifier did not report its active option flags.'
    }
    $lowResourceActiveFlags = [Convert]::ToUInt32(
        $lowResourceActiveFlagMatch.Value.Substring(2), 16)
    if ($LowResourceMode -eq 'Systematic' -and
        ($lowResourceActiveFlags -band 0x00040000) -eq 0) {
      throw (
          'Systematic Low Resources was not active after the dedicated ' +
          'guest restart.')
    }
    if ($LowResourceMode -eq 'Randomized' -and
        $lowResourceActiveFlags -ne 0x00000004) {
      throw (
          'Randomized Low Resources was not the only active option after ' +
          'the dedicated guest restart.')
    }
    foreach ($driver in $targetDrivers) {
      if (-not $lowResourceActiveText.Contains($driver)) {
        throw (
            'The network driver was not active during the separate ' +
            "low-resource boot: $driver")
      }
    }

    $lowResourceFailures = 0
    $systematicInjectedFaults = 0
    foreach ($selectedSample in $selectedWfpSamples) {
      if ($LowResourceMode -eq 'Systematic') {
        foreach ($operation in @(
          [pscustomobject]@{
            Name = 'RESET_RUNTIME'
            Arguments = @('/faultssystematic', 'resetruntime')
          },
          [pscustomobject]@{
            Name = 'ENABLE_RUNTIME'
            Arguments = @('/faultssystematic', 'enableruntime')
          },
          [pscustomobject]@{
            Name = 'STATISTICS_BEFORE'
            Arguments = @('/faultssystematic', 'querystatistics')
          }
        )) {
          $operationFileName =
              "systematic-$selectedSample-" +
              "$($operation.Name.ToLowerInvariant()).txt"
          Invoke-VerifierOperation $operation.Name `
              $operation.Arguments `
              (Join-Path $GuestRoot $operationFileName) `
              (Join-Path $LogRoot $operationFileName) |
              Out-Null
        }
      }

      $maximumRuns =
          if ($LowResourceMode -eq 'Systematic') {
            $SystematicInjectionPassesPerSample
          } else {
            $LowResourceRunsPerSample
          }
      for ($run = 1;
           $run -le $maximumRuns;
           ++$run) {
        $attemptLog = Join-Path $GuestRoot (
            "low-resource-$selectedSample-$run.log")
        $attemptLogLiteral =
            ConvertTo-PowerShellLiteral $attemptLog
        $sampleLiteral =
            ConvertTo-PowerShellLiteral $selectedSample
        $lowRun = Invoke-GuestScript -AllowFailure -Quiet -Script @"
`$ErrorActionPreference = 'Stop'
try {
  Remove-Item -LiteralPath $attemptLogLiteral -Force -ErrorAction SilentlyContinue
  & (Join-Path $rootLiteral 'Run-WfpAdvancedSuite.ps1') -PackageRoot $rootLiteral -SelectedSample $sampleLiteral -Iterations 1 *>&1 |
      Tee-Object -FilePath $attemptLogLiteral | Out-Null
  Add-Content -LiteralPath $attemptLogLiteral -Value 'PASS'
} catch {
  `$_ | Out-String | Add-Content -LiteralPath $attemptLogLiteral
  Add-Content -LiteralPath $attemptLogLiteral -Value 'RESOURCE_PATH_FAILURE'
  throw
}
"@
        $attemptHost = Join-Path $LogRoot (
            "low-resource-$selectedSample-$run.log")
        Copy-FromGuest $attemptLog $attemptHost -AllowFailure
        $resultMarker =
            if ($lowRun.ExitCode -eq 0) { 'PASS' } else { 'RESOURCE_FAILURE' }
        if ($lowRun.ExitCode -ne 0) {
          ++$lowResourceFailures
        }
        Write-Host (
            "Low-resource run $selectedSample/${run}: $resultMarker")
        if ($LowResourceMode -eq 'Systematic') {
          $counterFileName =
              "systematic-$selectedSample-counter-$run.txt"
          Invoke-VerifierOperation 'INCREMENT_COUNTER' `
              @('/faultssystematic', 'incrementcounter') `
              (Join-Path $GuestRoot $counterFileName) `
              (Join-Path $LogRoot $counterFileName) |
              Out-Null
        }
      }

      if ($LowResourceMode -eq 'Systematic') {
        $statisticsFileName =
            "systematic-$selectedSample-statistics-after.txt"
        $statisticsText = Invoke-VerifierOperation `
            'STATISTICS_AFTER_SAMPLE' `
            @('/faultssystematic', 'querystatistics') `
            (Join-Path $GuestRoot $statisticsFileName) `
            (Join-Path $LogRoot $statisticsFileName)
        $injectionMatch = [regex]::Match(
            $statisticsText,
            '(?im)^\s*InjectionCount:\s*(?<count>[0-9]+)\s*$')
        if (-not $injectionMatch.Success) {
          throw (
              'Systematic statistics were not parseable for ' +
              "$selectedSample.")
        }
        $sampleInjectedFaults =
            [int] $injectionMatch.Groups['count'].Value
        if ($sampleInjectedFaults -eq 0) {
          throw (
              'Systematic Verifier injected no fault for ' +
              "$selectedSample.")
        }
        $systematicInjectedFaults += $sampleInjectedFaults
        $disableFileName =
            "systematic-$selectedSample-disable-runtime.txt"
        Invoke-VerifierOperation 'DISABLE_RUNTIME' `
            @('/faultssystematic', 'disableruntime') `
            (Join-Path $GuestRoot $disableFileName) `
            (Join-Path $LogRoot $disableFileName) |
            Out-Null

        $recoveryGuestLog = Join-Path $GuestRoot (
            "systematic-$selectedSample-recovery.log")
        $recoveryHostLog = Join-Path $LogRoot (
            "systematic-$selectedSample-recovery.log")
        $recoveryGuestLogLiteral =
            ConvertTo-PowerShellLiteral $recoveryGuestLog
        $recoveryResult =
            Invoke-GuestScript -AllowFailure -Quiet -Script @"
`$ErrorActionPreference = 'Stop'
try {
  Remove-Item -LiteralPath $recoveryGuestLogLiteral -Force -ErrorAction SilentlyContinue
  & (Join-Path $rootLiteral 'Run-WfpAdvancedSuite.ps1') -PackageRoot $rootLiteral -SelectedSample $sampleLiteral -Iterations 1 *>&1 |
      Tee-Object -FilePath $recoveryGuestLogLiteral | Out-Null
  Add-Content -LiteralPath $recoveryGuestLogLiteral -Value 'PASS'
} catch {
  `$_ | Out-String | Add-Content -LiteralPath $recoveryGuestLogLiteral
  Add-Content -LiteralPath $recoveryGuestLogLiteral -Value 'RECOVERY_FAILURE'
  throw
}
"@
        Copy-FromGuest $recoveryGuestLog $recoveryHostLog `
            -AllowFailure
        if ($recoveryResult.ExitCode -ne 0) {
          throw (
              "$selectedSample did not recover after Systematic " +
              'runtime injection was disabled.')
        }
      }
    }

    $selectedServices = @(
      $wfpSamples |
          Where-Object { $selectedWfpSamples -contains $_.Name } |
          ForEach-Object Service
    )
    $selectedBaseNames = @(
      $wfpSamples |
          Where-Object { $selectedWfpSamples -contains $_.Name } |
          ForEach-Object { [IO.Path]::GetFileNameWithoutExtension(
              $_.Driver) }
    )
    $serviceLiterals = @(
        $selectedServices |
            ForEach-Object { ConvertTo-PowerShellLiteral $_ }
    )
    $processLiterals = @(
        $selectedBaseNames |
            ForEach-Object {
              ConvertTo-PowerShellLiteral "$($_)_app"
            }
    )
    $serviceExpression =
        '@(' + ($serviceLiterals -join ',') + ')'
    $processExpression =
        '@(' + ($processLiterals -join ',') + ')'
    $cleanupGuest =
        Join-Path $GuestRoot 'low-resource-cleanup-check.txt'
    $cleanupHost =
        Join-Path $LogRoot 'low-resource-cleanup-check.txt'
    $cleanupLiteral = ConvertTo-PowerShellLiteral $cleanupGuest
    Invoke-GuestScript -Script @"
`$ErrorActionPreference = 'Stop'
`$services = $serviceExpression
`$processes = $processExpression
`$deadline = (Get-Date).AddSeconds(30)
do {
  `$remainingServices = @(
    Get-CimInstance Win32_SystemDriver -ErrorAction SilentlyContinue |
        Where-Object { `$services -contains `$_.Name })
  `$remainingProcesses = @(
    Get-Process -ErrorAction SilentlyContinue |
        Where-Object { `$processes -contains `$_.ProcessName })
  if (`$remainingServices.Count -eq 0 -and
      `$remainingProcesses.Count -eq 0) {
    break
  }
  Start-Sleep -Milliseconds 100
} while ((Get-Date) -lt `$deadline)
if (`$remainingServices.Count -ne 0 -or
    `$remainingProcesses.Count -ne 0) {
  throw 'Low-resource run left a service or controller process behind.'
}
`$certificateFiles = @(
  Get-ChildItem -LiteralPath $rootLiteral -Filter '*.cer' -File)
foreach (`$certificateFile in `$certificateFiles) {
  `$certificate =
      [Security.Cryptography.X509Certificates.X509Certificate2]::new(
          `$certificateFile.FullName)
  try {
    foreach (`$store in @('Root', 'TrustedPublisher')) {
      if (Test-Path -LiteralPath (
          "Cert:\LocalMachine\`$store\`$(`$certificate.Thumbprint)")) {
        throw "Low-resource run left a test certificate in `$store."
      }
    }
  } finally {
    `$certificate.Dispose()
  }
}
@(
  'SERVICE_COUNT=0'
  'PROCESS_COUNT=0'
  'CERTIFICATE_COUNT=0'
  'PASS'
) | Set-Content -LiteralPath $cleanupLiteral -Encoding UTF8
"@ | Out-Null
    Copy-FromGuest $cleanupGuest $cleanupHost

    if ($LowResourceMode -eq 'Systematic') {
      $systematicAfterGuest =
          Join-Path $GuestRoot 'verifier-systematic-after.txt'
      $systematicAfterHost =
          Join-Path $LogRoot 'verifier-systematic-after.txt'
      $systematicAfterLiteral =
          ConvertTo-PowerShellLiteral $systematicAfterGuest
      Invoke-GuestScript -Script @"
`$ErrorActionPreference = 'Stop'
`$lines = [Collections.Generic.List[string]]::new()
function Run([string] `$name, [string[]] `$arguments) {
  `$lines.Add("=== `$name ===")
  `$output = @(& verifier.exe @arguments 2>&1)
  `$code = `$LASTEXITCODE
  foreach (`$line in `$output) {
    `$lines.Add((`$line.ToString() -replace [char]0, ''))
  }
  `$lines.Add("`$name`_RC=`$code")
  if (`$code -notin @(0, 2)) {
    throw "verifier `$name failed: `$code"
  }
}
Run 'STATISTICS_AFTER' @('/faultssystematic', 'querystatistics')
Run 'DISABLE_RUNTIME' @('/faultssystematic', 'disableruntime')
`$lines.Add('PASS')
`$lines | Set-Content -LiteralPath $systematicAfterLiteral -Encoding UTF8
"@ | Out-Null
      Copy-FromGuest $systematicAfterGuest $systematicAfterHost
    }
  }

  if ($null -ne $ManagedHttp3Url) {
    $guestHttp3Log = Join-Path $GuestRoot 'wfp-managed-http3-suite.log'
    $guestHttp3LogLiteral =
        ConvertTo-PowerShellLiteral $guestHttp3Log
    $managedHttp3UrlLiteral =
        ConvertTo-PowerShellLiteral $ManagedHttp3Url.AbsoluteUri
    $http3RunResult = Invoke-GuestScript -AllowFailure -Script @"
`$ErrorActionPreference = 'Stop'
try {
  & (Join-Path $rootLiteral 'Run-WfpManagedHttp3Suite.ps1') -PackageRoot $rootLiteral -Url $managedHttp3UrlLiteral *>&1 |
      Tee-Object -FilePath $guestHttp3LogLiteral | Out-Null
  Add-Content -LiteralPath $guestHttp3LogLiteral -Value 'PASS'
} catch {
  `$_ | Out-String | Add-Content -LiteralPath $guestHttp3LogLiteral
  Add-Content -LiteralPath $guestHttp3LogLiteral -Value 'FAIL'
  throw
}
"@
    Copy-FromGuest $guestHttp3Log (
        Join-Path $LogRoot 'wfp-managed-http3-suite.log') -AllowFailure
    if ($http3RunResult.ExitCode -ne 0) {
      throw (
          'The guest WFP managed HTTP/3 suite failed with ' +
          "$($http3RunResult.ExitCode).")
    }
  }

  $afterGuest = Join-Path $GuestRoot 'verifier-after.txt'
  $afterHost = Join-Path $LogRoot 'verifier-after.txt'
  Capture-Verifier $afterGuest $afterHost
  $afterText = Get-Content -LiteralPath $afterHost -Raw
  foreach ($driver in $targetDrivers) {
    $targetLine = @(
      $afterText -split '\r?\n' |
        Where-Object { $_.Contains($driver) }
    ) | Select-Object -First 1
    if (-not $targetLine) {
      throw "Verifier no longer listed the network target: $driver"
    }
    if ($targetLine -notmatch
        '\([^0-9]*[1-9][0-9]*\s*/[^0-9]*[1-9][0-9]*\)') {
      throw "Verifier did not report load/unload for $driver"
    }
  }
  if (-not $SkipLowResourcePass -and
      $LowResourceMode -eq 'Randomized') {
    $counterMatches = [regex]::Matches(
        $afterText,
        '(?m)^\s*[^:\r\n]+:\s*' +
        '(?<value>0x[0-9a-fA-F]+|[0-9]+)\s*$')
    if ($counterMatches.Count -eq 0) {
      throw (
          'Verifier did not expose numeric statistics after the ' +
          'low-resource pass.')
    }
    # /query ends its language-localized global-statistics block with the
    # deliberate allocation-failure counter. Driver rows that follow contain
    # load/unload pairs rather than a bare numeric value, so selecting the last
    # exact numeric row is independent of the guest display language.
    $faultText =
        $counterMatches[$counterMatches.Count - 1].
            Groups['value'].Value
    $faultCount = if ($faultText.StartsWith(
        '0x', [StringComparison]::OrdinalIgnoreCase)) {
      [Convert]::ToUInt64($faultText.Substring(2), 16)
    } else {
      [Convert]::ToUInt64($faultText, 10)
    }
    if ($faultCount -eq 0) {
      throw (
          'The low-resource pass completed without one deliberate ' +
          'allocation failure.')
    }
  } elseif (-not $SkipLowResourcePass -and
            $systematicInjectedFaults -eq 0) {
    throw (
        'Systematic Low Resources completed without one injected fault.')
  }

  $postGuest = Join-Path $GuestRoot 'postcheck.txt'
  $postHost = Join-Path $LogRoot 'postcheck.txt'
  $postLiteral = ConvertTo-PowerShellLiteral $postGuest
  Invoke-GuestScript -Script @"
`$boot = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
`$lines = [Collections.Generic.List[string]]::new()
`$events = @(Get-WinEvent -FilterHashtable @{
  LogName='System'; StartTime=`$boot
} -ErrorAction SilentlyContinue | Where-Object Id -in @(41,1001,6008))
`$dumps = @()
if (Test-Path 'C:\Windows\Minidump') {
  `$dumps += Get-ChildItem 'C:\Windows\Minidump' -Filter '*.dmp' -File |
      Where-Object LastWriteTime -ge `$boot
}
if (Test-Path 'C:\Windows\MEMORY.DMP') {
  `$dump = Get-Item 'C:\Windows\MEMORY.DMP'
  if (`$dump.LastWriteTime -ge `$boot) { `$dumps += `$dump }
}
`$lines.Add("EVENT_COUNT=`$(`$events.Count)")
`$lines.Add("DUMP_COUNT=`$(`$dumps.Count)")
`$lines.Add('PASS')
`$lines | Set-Content -LiteralPath $postLiteral -Encoding UTF8
"@ | Out-Null
  Copy-FromGuest $postGuest $postHost
  $postText = Get-Content -LiteralPath $postHost -Raw
  if ($postText -notmatch 'EVENT_COUNT=0' -or
      $postText -notmatch 'DUMP_COUNT=0') {
    throw 'The VM postcheck found a crash event or dump.'
  }

  Restore-Verifier

  $postRestoreGuest =
      Join-Path $GuestRoot 'post-restore-postcheck.txt'
  $postRestoreHost =
      Join-Path $LogRoot 'post-restore-postcheck.txt'
  $postRestoreLiteral =
      ConvertTo-PowerShellLiteral $postRestoreGuest
  Invoke-GuestScript -Script @"
`$boot = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
`$lines = [Collections.Generic.List[string]]::new()
`$events = @(Get-WinEvent -FilterHashtable @{
  LogName='System'; StartTime=`$boot
} -ErrorAction SilentlyContinue | Where-Object Id -in @(41,1001,6008))
`$dumps = @()
if (Test-Path 'C:\Windows\Minidump') {
  `$dumps += Get-ChildItem 'C:\Windows\Minidump' -Filter '*.dmp' -File |
      Where-Object LastWriteTime -ge `$boot
}
if (Test-Path 'C:\Windows\MEMORY.DMP') {
  `$dump = Get-Item 'C:\Windows\MEMORY.DMP'
  if (`$dump.LastWriteTime -ge `$boot) { `$dumps += `$dump }
}
`$lines.Add("EVENT_COUNT=`$(`$events.Count)")
`$lines.Add("DUMP_COUNT=`$(`$dumps.Count)")
`$lines.Add('PASS')
`$lines | Set-Content -LiteralPath $postRestoreLiteral -Encoding UTF8
"@ | Out-Null
  Copy-FromGuest $postRestoreGuest $postRestoreHost
  $postRestoreText =
      Get-Content -LiteralPath $postRestoreHost -Raw
  if ($postRestoreText -notmatch 'EVENT_COUNT=0' -or
      $postRestoreText -notmatch 'DUMP_COUNT=0') {
    throw 'The restored VM boot produced a crash event or dump.'
  }

  Write-Host 'Advanced WFP VM acceptance gate passed.'
} finally {
  if ($verifierChanged -and -not $restoreCompleted) {
    Write-Warning 'Attempting Verifier restoration after an incomplete run.'
    try {
      Wait-GuestReady
      Restore-Verifier
    } catch {
      Write-Warning "Verifier restoration failed: $($_.Exception.Message)"
    }
  }
  $vmPasswordText = $null
  $guestPasswordText = $null
}
