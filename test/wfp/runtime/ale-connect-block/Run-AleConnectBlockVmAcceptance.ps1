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

  [string] $DisposableGuestSentinelPath =
      'C:\crtsys-disposable-test-guest.sentinel',

  [string] $StagingRoot = '',

  [string] $LogRoot = '',

  [ValidateSet('v143', 'v145')]
  [string] $PlatformToolset = 'v145',

  [string] $WindowsSdkVersion = '10.0.28000.0',

  [ValidateRange(1, 1000)]
  [int] $Iterations = 20,

  [switch] $SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$prepareScript =
    Join-Path $PSScriptRoot 'Prepare-AleConnectBlockArtifacts.ps1'
$runtimeScript = Join-Path $PSScriptRoot 'Run-AleConnectBlockSuite.ps1'
$guardScript =
    Join-Path $PSScriptRoot '..\common\DisposableGuestGuard.ps1'
$crashPostcheckScript =
    Join-Path $PSScriptRoot '..\..\..\common\Test-VmCrashPostcheck.ps1'
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
foreach ($requiredScript in @($guardScript, $crashPostcheckScript)) {
  if (-not (Test-Path -LiteralPath $requiredScript -PathType Leaf)) {
    throw "Required guest script was not found: $requiredScript"
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
  $eventBaselineGuest = Join-Path $GuestRoot 'crash-event-baseline.txt'
  $dumpBaselineGuest = Join-Path $GuestRoot 'crash-dump-baseline.txt'
  Invoke-Vmrun -Guest -Quiet -Arguments @(
    'runProgramInGuest', $VmxPath,
    'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
    $guestCrashPostcheck,
    '-EventBaselinePath', $eventBaselineGuest,
    '-DumpBaselinePath', $dumpBaselineGuest,
    '-CaptureBaseline'
  ) | Out-Null
  Copy-FromGuest $eventBaselineGuest (
      Join-Path $LogRoot 'crash-event-baseline.txt')
  Copy-FromGuest $dumpBaselineGuest (
      Join-Path $LogRoot 'crash-dump-baseline.txt')

  $beforeQueryGuest = Join-Path $GuestRoot 'verifier-before.txt'
  $beforeQueryHost = Join-Path $LogRoot 'verifier-before.txt'
  $beforeSettingsGuest = Join-Path $GuestRoot 'verifier-settings-before.txt'
  $beforeSettingsHost = Join-Path $LogRoot 'verifier-settings-before.txt'
  Capture-Verifier $beforeQueryGuest $beforeQueryHost `
      $beforeSettingsGuest $beforeSettingsHost
  $beforeText = Get-Content -LiteralPath $beforeQueryHost -Raw
  if (-not $beforeText.Contains($targetDriver)) {
    throw (
      "Driver Verifier is not preconfigured for $targetDriver. " +
      'Configure Verifier and boot the guest manually before running this gate.')
  }

  foreach ($file in Get-ChildItem -LiteralPath $StagingRoot -File) {
    Copy-ToGuest $file.FullName (Join-Path $GuestRoot $file.Name)
  }
  Copy-ToGuest $runtimeScript (
      Join-Path $GuestRoot ([IO.Path]::GetFileName($runtimeScript)))
  Copy-ToGuest $guardScript (
      Join-Path $GuestRoot 'DisposableGuestGuard.ps1')

  $guestLog = Join-Path $GuestRoot 'runtime-suite.log'
  $guestLogLiteral = ConvertTo-PowerShellLiteral $guestLog
  $sentinelLiteral =
      ConvertTo-PowerShellLiteral $DisposableGuestSentinelPath
  $runResult = Invoke-GuestScript -AllowFailure -Script @"
`$ErrorActionPreference = 'Stop'
try {
  & (Join-Path $rootLiteral 'Run-AleConnectBlockSuite.ps1') `
      -PackageRoot $rootLiteral -Iterations $Iterations `
      -AllowDisposableGuestMutation `
      -DisposableGuestSentinelPath $sentinelLiteral *>&1 |
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

  $afterQueryGuest = Join-Path $GuestRoot 'verifier-after.txt'
  $afterQueryHost = Join-Path $LogRoot 'verifier-after.txt'
  $afterSettingsGuest = Join-Path $GuestRoot 'verifier-settings-after.txt'
  $afterSettingsHost = Join-Path $LogRoot 'verifier-settings-after.txt'
  Capture-Verifier $afterQueryGuest $afterQueryHost `
      $afterSettingsGuest $afterSettingsHost
  $afterText = Get-Content -LiteralPath $afterQueryHost -Raw
  $targetLine = @(
    $afterText -split '\r?\n' |
        Where-Object { $_.Contains($targetDriver) }
  ) | Select-Object -First 1
  if (-not $targetLine) {
    throw 'Verifier no longer listed the preconfigured WFP target.'
  }
  if ($targetLine -notmatch '\([^0-9]*[1-9][0-9]*\s*/') {
    throw 'Verifier did not report a WFP driver load during the suite.'
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

  Write-Host (
    'WFP ALE connect-block VM acceptance passed without changing ' +
    'guest boot or Driver Verifier state.')
} finally {
  $vmPasswordText = $null
  $guestPasswordText = $null
}
