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

  [string] $GuestRoot = 'C:\crtsys-wfp-ale-connect-block',

  [string] $StagingRoot = '',

  [string] $LogRoot = '',

  [ValidateSet('v143', 'v145')]
  [string] $PlatformToolset = 'v145',

  [string] $WindowsSdkVersion = '10.0.28000.0',

  [string[]] $RestoreDriverFileName = @(),

  [ValidateSet('Oneboot', 'Persistent', 'ResetOnBootFail')]
  [string] $RestoreBootMode = 'Persistent',

  [ValidateRange(1, 1000)]
  [int] $Iterations = 20,

  [switch] $SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$prepareScript =
    Join-Path $PSScriptRoot 'Prepare-AleConnectBlockArtifacts.ps1'
$runtimeScript = Join-Path $PSScriptRoot 'Run-AleConnectBlockSuite.ps1'
$targetDriver = 'crtsys_wfp_ale_connect_block.sys'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
if ([string]::IsNullOrWhiteSpace($StagingRoot)) {
  $StagingRoot =
      Join-Path $repoRoot 'artifacts\wfp-ale-connect-block-staging'
}
if ([string]::IsNullOrWhiteSpace($LogRoot)) {
  $LogRoot =
      Join-Path $repoRoot 'artifacts\wfp-ale-connect-block-acceptance'
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

function Restart-Guest {
  Invoke-Vmrun -Arguments @('reset', $VmxPath, 'soft') | Out-Null
  Wait-GuestReady
}

function Set-Verifier(
  [string[]] $Drivers, [string] $BootMode, [string] $GuestLog
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
if (`$drivers.Count -eq 0) {
  Run 'RESET' @('/reset')
} else {
  Run 'ENABLE' (@('/standard', '/driver') + `$drivers)
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
  Set-Verifier $RestoreDriverFileName $RestoreBootMode $guestStage
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
    throw 'Preparing WFP runtime artifacts failed.'
  }

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
  Set-Verifier @($targetDriver) Oneboot $enableGuest
  $verifierChanged = $true
  Copy-FromGuest $enableGuest (Join-Path $LogRoot 'verifier-enable.txt')
  Restart-Guest

  $activeGuest = Join-Path $GuestRoot 'verifier-active.txt'
  $activeHost = Join-Path $LogRoot 'verifier-active.txt'
  Capture-Verifier $activeGuest $activeHost
  if (-not (Get-Content -LiteralPath $activeHost -Raw).
      Contains($targetDriver)) {
    throw 'The WFP driver was not active under Driver Verifier.'
  }

  foreach ($file in Get-ChildItem -LiteralPath $StagingRoot -File) {
    Copy-ToGuest $file.FullName (Join-Path $GuestRoot $file.Name)
  }
  Copy-ToGuest $runtimeScript (
      Join-Path $GuestRoot ([IO.Path]::GetFileName($runtimeScript)))

  $guestLog = Join-Path $GuestRoot 'runtime-suite.log'
  $guestLogLiteral = ConvertTo-PowerShellLiteral $guestLog
  $runResult = Invoke-GuestScript -AllowFailure -Script @"
`$ErrorActionPreference = 'Stop'
try {
  & (Join-Path $rootLiteral 'Run-AleConnectBlockSuite.ps1') -PackageRoot $rootLiteral -Iterations $Iterations *>&1 |
      Tee-Object -FilePath $guestLogLiteral
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

  $afterGuest = Join-Path $GuestRoot 'verifier-after.txt'
  $afterHost = Join-Path $LogRoot 'verifier-after.txt'
  Capture-Verifier $afterGuest $afterHost
  $afterText = Get-Content -LiteralPath $afterHost -Raw
  $targetLine = @(
    $afterText -split '\r?\n' |
      Where-Object { $_.Contains($targetDriver) }
  ) | Select-Object -First 1
  if (-not $targetLine) {
    throw 'Verifier no longer listed the WFP target after the runtime suite.'
  }
  # The labels are localized, but verifier prints the load/unload counters in
  # a stable "(label: N/label: N)" shape.
  if ($targetLine -notmatch '\([^0-9]*[1-9][0-9]*\s*/') {
    throw 'Verifier did not report a WFP driver load after the runtime suite.'
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
  Write-Host 'WFP ALE connect-block VM acceptance gate passed.'
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
