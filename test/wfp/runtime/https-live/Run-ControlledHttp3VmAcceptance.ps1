[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $VmxPath,

  [string] $GuestUser = 'test',

  [Parameter(Mandatory)]
  [Security.SecureString] $GuestPassword,

  [Security.SecureString] $VmPassword =
      (New-Object Security.SecureString),

  [string] $VmrunPath =
      'C:\Program Files\VMware\VMware Workstation\vmrun.exe',

  [string] $GuestRoot = 'C:\crtsys-controlled-http3',

  [string] $StagingRoot = '',

  [string] $LogRoot = '',

  [ValidateRange(2, 32)]
  [int] $Concurrency = 8
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot =
    (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$crashPostcheckScript = Join-Path $PSScriptRoot (
    '..\..\..\common\Test-VmCrashPostcheck.ps1')
if ([string]::IsNullOrWhiteSpace($StagingRoot)) {
  $StagingRoot =
      Join-Path $repoRoot 'artifacts\controlled-http3-staging'
}
if ([string]::IsNullOrWhiteSpace($LogRoot)) {
  $LogRoot =
      Join-Path $repoRoot 'artifacts\controlled-http3-vm-acceptance'
}

function ConvertTo-PlainText(
    [Security.SecureString] $Value) {
  $pointer =
      [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Value)
  try {
    return [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
        $pointer)
  } finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
  }
}

function ConvertTo-PowerShellLiteral([string] $Value) {
  return "'" + ($Value -replace "'", "''") + "'"
}

foreach ($required in @(
    $VmrunPath, $VmxPath, $StagingRoot, $crashPostcheckScript)) {
  if (-not (Test-Path -LiteralPath $required)) {
    throw "Required VM acceptance path was not found: $required"
  }
}
$driverArtifacts = @(
  Get-ChildItem -LiteralPath $StagingRoot -File |
      Where-Object Extension -in @('.sys', '.inf', '.cat'))
if ($driverArtifacts.Count -ne 0) {
  throw 'The controlled VM package must not contain driver artifacts.'
}

$artifactsRoot =
    [IO.Path]::GetFullPath(
        (Join-Path $repoRoot 'artifacts')).TrimEnd('\') + '\'
$resolvedLogRoot = [IO.Path]::GetFullPath($LogRoot)
if (-not ($resolvedLogRoot.TrimEnd('\') + '\').StartsWith(
    $artifactsRoot, [StringComparison]::OrdinalIgnoreCase)) {
  throw "LogRoot must stay under $artifactsRoot"
}
if (Test-Path -LiteralPath $resolvedLogRoot) {
  Remove-Item -LiteralPath $resolvedLogRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $resolvedLogRoot -Force |
    Out-Null

if (-not $GuestRoot.StartsWith(
        'C:\', [StringComparison]::OrdinalIgnoreCase) -or
    $GuestRoot.Length -le 3) {
  throw 'GuestRoot must be a specific directory below C:\.'
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
  if (-not [string]::IsNullOrEmpty($vmPasswordText)) {
    $prefix += @('-vp', $vmPasswordText)
  }
  if ($Guest) {
    $prefix += @(
      '-gu', $GuestUser, '-gp', $guestPasswordText)
  }
  $allArguments = $prefix + $Arguments
  $output = @(& $VmrunPath @allArguments 2>&1)
  $exitCode = $LASTEXITCODE
  if (-not $Quiet) {
    $output | ForEach-Object { Write-Host $_ }
  }
  if ($exitCode -ne 0 -and -not $AllowFailure) {
    $display =
        for ($index = 0;
             $index -lt $allArguments.Count; ++$index) {
          if ($index -gt 0 -and
              $allArguments[$index - 1] -in @('-vp', '-gp')) {
            '<redacted>'
          } else {
            $allArguments[$index]
          }
        }
    throw (
        "vmrun $($display -join ' ') failed with " +
        "exit code $exitCode.")
  }
  return [pscustomobject]@{
    ExitCode = $exitCode
    Output = $output
  }
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
  return Invoke-Vmrun -Guest -AllowFailure:$AllowFailure `
      -Quiet:$Quiet -Arguments @(
        'runProgramInGuest', $VmxPath,
        'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
        '-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-EncodedCommand', $encoded)
}

function Copy-ToGuest([string] $Source, [string] $Destination) {
  Invoke-Vmrun -Guest -Quiet -Arguments @(
    'copyFileFromHostToGuest', $VmxPath, $Source, $Destination
  ) | Out-Null
}

function Copy-FromGuest([string] $Source, [string] $Destination) {
  Invoke-Vmrun -Guest -Quiet -Arguments @(
    'copyFileFromGuestToHost', $VmxPath, $Source, $Destination
  ) | Out-Null
}

for ($attempt = 1; $attempt -le 90; ++$attempt) {
  $ready =
      Invoke-GuestScript -Script 'exit 0' -AllowFailure -Quiet
  if ($ready.ExitCode -eq 0) {
    Write-Host "Guest operations ready after attempt $attempt."
    break
  }
  if ($attempt -eq 90) {
    throw 'Guest operations did not become ready within 180 seconds.'
  }
  Start-Sleep -Seconds 2
}

$guestRootLiteral =
    ConvertTo-PowerShellLiteral $GuestRoot
Invoke-GuestScript -Script @"
`$root = $guestRootLiteral
if (`$root.Length -le 3 -or
    -not `$root.StartsWith(
        'C:\', [StringComparison]::OrdinalIgnoreCase)) {
  throw 'Unsafe controlled HTTP/3 guest root.'
}
if (Test-Path -LiteralPath `$root) {
  Remove-Item -LiteralPath `$root -Recurse -Force
}
New-Item -ItemType Directory -Path `$root -Force | Out-Null
"@ | Out-Null

foreach ($file in Get-ChildItem -LiteralPath $StagingRoot -File) {
  Copy-ToGuest $file.FullName (
      Join-Path $GuestRoot $file.Name)
}
Copy-ToGuest $crashPostcheckScript (
    Join-Path $GuestRoot 'Test-VmCrashPostcheck.ps1')

$guestResult =
    Join-Path $GuestRoot 'vm-acceptance.result.txt'
$guestEvidence =
    Join-Path $GuestRoot 'vm-acceptance-evidence.zip'
$guestResultLiteral =
    ConvertTo-PowerShellLiteral $guestResult
$guestEvidenceLiteral =
    ConvertTo-PowerShellLiteral $guestEvidence
$guestProgram = @"
Set-StrictMode -Version Latest
`$ErrorActionPreference = 'Stop'
`$root = $guestRootLiteral
`$result = $guestResultLiteral
`$evidenceZip = $guestEvidenceLiteral
`$evidence = Join-Path `$root 'evidence'
`$crashPostcheck = Join-Path `$root 'Test-VmCrashPostcheck.ps1'
`$eventBaseline = Join-Path `$root 'crash-event-baseline.txt'
`$dumpBaseline = Join-Path `$root 'crash-dump-baseline.txt'
`$postcheck = Join-Path `$root 'postcheck.txt'
function Get-VerifierSnapshot {
  return (@(
    & verifier.exe /querysettings 2>&1 |
        ForEach-Object { "`$_".TrimEnd() }
  ) -join "``n").Trim()
}
function Get-RootSnapshot {
  return (@(
    Get-ChildItem Cert:\CurrentUser\Root, Cert:\LocalMachine\Root |
        ForEach-Object {
          "`$(`$_.PSParentPath)|`$(`$_.Thumbprint)"
        } |
        Sort-Object -Unique
  ) -join "``n")
}
function Get-KeySnapshot {
  `$entries = @(
    foreach (`$scope in @('user', 'machine')) {
      `$arguments =
          if (`$scope -eq 'user') {
            @('-user', '-key')
          } else {
            @('-key')
          }
      & certutil.exe @arguments 2>&1 |
          Where-Object {
            "`$_" -match 'crtsys-ntl-(controlled|wfp-tls-)'
          } |
          ForEach-Object {
            `$line = "`$_".Trim()
            "`$scope|`$line"
          }
    }
  )
  return (@(`$entries | Sort-Object -Unique) -join "``n")
}
function Get-ServiceSnapshot {
  return (@(
    Get-Service -Name @(
        'CrtSysWfpHttpsLiveTest',
        'CrtSysWfpBrowserHttpsInspection') -ErrorAction SilentlyContinue |
        ForEach-Object {
          "`$(`$_.Name)|`$(`$_.Status)|`$(`$_.StartType)"
        } |
        Sort-Object
  ) -join "``n")
}
try {
  New-Item -ItemType Directory -Path `$evidence -Force |
      Out-Null
  & `$crashPostcheck -EventBaselinePath `$eventBaseline `
      -DumpBaselinePath `$dumpBaseline -CaptureBaseline
  `$verifierBefore = Get-VerifierSnapshot
  `$rootsBefore = Get-RootSnapshot
  `$keysBefore = Get-KeySnapshot
  `$servicesBefore = Get-ServiceSnapshot
  `$bootBefore =
      (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
  if (@(
      Get-ChildItem -LiteralPath `$root -File |
          Where-Object Extension -in @('.sys', '.inf', '.cat')
      ).Count -ne 0) {
    throw 'The guest package unexpectedly contains a driver.'
  }
  `$controlledArguments = @{
    PackageRoot = `$root
    LogDirectory = Join-Path `$root 'logs'
    Concurrency = $Concurrency
  }
  & (Join-Path `$root 'Start-ControlledHttp3EndToEnd.ps1') @controlledArguments *>&1 |
      Tee-Object -FilePath (
          Join-Path `$evidence 'controlled-http3-run.log')

  `$run = Get-ChildItem -LiteralPath (
      Join-Path `$root 'logs') -Directory |
      Sort-Object LastWriteTime -Descending |
      Select-Object -First 1
  if (-not `$run) {
    throw 'The controlled HTTP/3 run directory is missing.'
  }
  `$acceptance =
      Join-Path `$run.FullName 'acceptance.result.txt'
  if (-not (Test-Path -LiteralPath `$acceptance) -or
      -not ([IO.File]::ReadAllText(`$acceptance)).Contains(
          'CONTROLLED_HTTP3=PASS')) {
    throw 'The controlled HTTP/3 contract result is missing.'
  }
  Copy-Item -LiteralPath `$run.FullName -Destination (
      Join-Path `$evidence 'run') -Recurse -Force

  `$verifierAfter = Get-VerifierSnapshot
  `$rootsAfter = Get-RootSnapshot
  `$keysAfter = Get-KeySnapshot
  `$servicesAfter = Get-ServiceSnapshot
  `$bootAfter =
      (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
  if (`$verifierAfter -cne `$verifierBefore) {
    throw 'Driver Verifier settings changed.'
  }
  if (`$rootsAfter -cne `$rootsBefore) {
    throw 'A Windows root certificate store changed.'
  }
  if (`$keysAfter -cne `$keysBefore) {
    throw 'A controlled HTTP/3 private key remained.'
  }
  if (`$servicesAfter -cne `$servicesBefore) {
    throw 'A WFP service changed.'
  }
  if (`$bootAfter -ne `$bootBefore) {
    throw 'The VM restarted during the acceptance.'
  }
  `$processes = @(
    Get-Process -Name @(
        'crtsys_wfp_*', 'crtsys_ntl_*') -ErrorAction SilentlyContinue)
  if (`$processes.Count -ne 0) {
    throw 'A controlled HTTP/3 process remained.'
  }
  & `$crashPostcheck -EventBaselinePath `$eventBaseline `
      -DumpBaselinePath `$dumpBaseline -OutputPath `$postcheck
  `$postcheckText = Get-Content -LiteralPath `$postcheck -Raw
  if (`$postcheckText -notmatch 'EVENT_COUNT=0' -or
      `$postcheckText -notmatch 'DUMP_COUNT=0' -or
      `$postcheckText -notmatch 'EVENT_LOG_RESET=0') {
    throw "Crash postcheck failed: `$postcheckText"
  }
  Copy-Item -LiteralPath `$postcheck -Destination `$evidence -Force

  @(
    'CONTROLLED_HTTP3_VM=PASS'
    'CLIENT_TO_PROXY_H3=PASS'
    'PROXY_TO_ORIGIN_H3=PASS'
    'DRIVER_PACKAGE_PRESENT=NO'
    'WFP_DRIVER_USED=NO'
    'ROOT_STORE_CHANGED=NO'
    'PERSISTENT_KEYS=NO'
    'DRIVER_VERIFIER_CHANGED=NO'
    'RESTART_REQUESTED=NO'
    'VM_RESTARTED=NO'
    'NEW_CRASH_EVENTS=0'
    'NEW_DUMPS=0'
    'REMAINING_PROCESSES=0'
    'PASS'
  ) | Set-Content -LiteralPath `$result -Encoding ASCII
} catch {
  `$_ | Out-String |
      Set-Content -LiteralPath `$result -Encoding UTF8
  Add-Content -LiteralPath `$result -Value 'FAIL'
  throw
} finally {
  if (Test-Path -LiteralPath `$evidenceZip) {
    Remove-Item -LiteralPath `$evidenceZip -Force
  }
  if (Test-Path -LiteralPath `$evidence) {
    Compress-Archive -Path (
        Join-Path `$evidence '*') -DestinationPath `$evidenceZip -Force
  }
}
"@

$guestProgramPath =
    Join-Path $resolvedLogRoot 'Run-ControlledHttp3VmGuest.ps1'
[IO.File]::WriteAllText(
    $guestProgramPath, $guestProgram,
    [Text.UTF8Encoding]::new($false))
$parseErrors = $null
[void][Management.Automation.Language.Parser]::ParseFile(
    $guestProgramPath, [ref]$null, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) {
  throw "Generated guest script is invalid: $($parseErrors -join '; ')"
}
$guestProgramDestination =
    Join-Path $GuestRoot 'Run-ControlledHttp3VmGuest.ps1'
Copy-ToGuest $guestProgramPath $guestProgramDestination

$run = Invoke-Vmrun -Guest -AllowFailure -Arguments @(
  'runProgramInGuest', $VmxPath,
  'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
  '-NoProfile', '-ExecutionPolicy', 'Bypass',
  '-File', $guestProgramDestination)

Copy-FromGuest $guestResult (
    Join-Path $resolvedLogRoot 'vm-acceptance.result.txt')
$evidenceCopy = Invoke-Vmrun -Guest -AllowFailure -Quiet -Arguments @(
  'copyFileFromGuestToHost', $VmxPath, $guestEvidence,
  (Join-Path $resolvedLogRoot 'vm-acceptance-evidence.zip'))

$resultText = [IO.File]::ReadAllText(
    (Join-Path $resolvedLogRoot 'vm-acceptance.result.txt'))
if ($run.ExitCode -ne 0 -or
    -not $resultText.Contains('CONTROLLED_HTTP3_VM=PASS') -or
    -not $resultText.TrimEnd().EndsWith('PASS')) {
  throw (
      "Controlled HTTP/3 VM acceptance failed. " +
      "See $resolvedLogRoot")
}
if ($evidenceCopy.ExitCode -ne 0) {
  throw 'The passing VM run did not produce its evidence archive.'
}

$cleanup = Invoke-GuestScript -AllowFailure -Quiet -Script @"
`$root = $guestRootLiteral
if (`$root.Length -gt 3 -and
    `$root.StartsWith(
        'C:\', [StringComparison]::OrdinalIgnoreCase) -and
    (Test-Path -LiteralPath `$root)) {
  Remove-Item -LiteralPath `$root -Recurse -Force
}
"@
if ($cleanup.ExitCode -ne 0) {
  throw 'The controlled HTTP/3 guest directory was not removed.'
}

Write-Host $resultText.Trim()
Write-Host (
    "Controlled HTTP/3 VM acceptance passed without a " +
    "driver or reboot: $resolvedLogRoot")
