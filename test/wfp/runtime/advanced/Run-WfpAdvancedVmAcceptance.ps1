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

  [string] $StagingRoot = '',

  [string] $HttpsStagingRoot = '',

  [string] $LogRoot = '',

  [ValidateSet('v143', 'v145')]
  [string] $PlatformToolset = 'v145',

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

  [ValidateSet('all', 'datagram-proxy', 'async-inspection',
               'flow-monitor', 'stream-edit', 'connect-redirect',
               'bind-redirect', 'tls-inspection-proxy',
               'udp-content-filter', 'tcp-content-filter')]
  [string[]] $SelectedWfpSample = @('all'),

  [uri] $ManagedHttp3Url,

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
    Name = 'udp-content-filter'
    Driver = 'crtsys_wfp_udp_content_filter.sys'
  },
  [pscustomobject]@{
    Name = 'tcp-content-filter'
    Driver = 'crtsys_wfp_tcp_content_filter.sys'
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
  $StagingRoot = Join-Path $repoRoot 'artifacts\wfp-advanced-staging'
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

  Write-Host 'Requesting one guest operating-system restart.'
  Invoke-GuestScript -AllowFailure -Quiet -Script @'
Start-Process -FilePath "$env:SystemRoot\System32\shutdown.exe" `
    -ArgumentList '/r','/t','0','/f' -WindowStyle Hidden
'@ | Out-Null

  for ($attempt = 1; $attempt -le 120; ++$attempt) {
    Start-Sleep -Seconds 2
    $after = Get-GuestBootStamp
    if ($after -and $after -ne $before) {
      Write-Host "Guest restart completed after attempt $attempt."
      return
    }
  }
  throw 'The guest did not complete its operating-system restart in 240 seconds.'
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

function Restore-Verifier {
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

  $prepareArguments = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', $prepareScript,
    '-PlatformToolset', $PlatformToolset,
    '-WindowsSdkVersion', $WindowsSdkVersion,
    '-OutputRoot', $StagingRoot
  )
  if ($SkipBuild) { $prepareArguments += '-SkipBuild' }
  & powershell.exe @prepareArguments
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

  $preflightGuest = Join-Path $GuestRoot 'verifier-preflight.txt'
  Capture-Verifier $preflightGuest (
      Join-Path $LogRoot 'verifier-preflight.txt')

  $enableGuest = Join-Path $GuestRoot 'verifier-enable.txt'
  Set-Verifier $targetDrivers Oneboot $enableGuest $VerifierFlags
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
