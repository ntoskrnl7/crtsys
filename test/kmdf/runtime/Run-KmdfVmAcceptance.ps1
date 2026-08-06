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

  [string] $DevConPath,

  [string] $GuestRoot = 'C:\crtsys-kmdf-acceptance',

  [string] $StagingRoot,

  [string] $LogRoot,

  [ValidateSet('Debug', 'Release')]
  [string] $DriverConfiguration = 'Debug',

  [ValidateSet('v143', 'v145')]
  [string] $PlatformToolset = 'v145',

  [string] $WindowsSdkVersion = '10.0.28000.0',

  [string[]] $RestoreDriverFileName = @(),

  [ValidateSet('Oneboot', 'Persistent', 'ResetOnBootFail')]
  [string] $RestoreBootMode = 'Persistent',

  [ValidateRange(1, 10000)]
  [int] $StressIterations = 64,

  [ValidateRange(1, 64)]
  [int] $StressWorkers = 4,

  [ValidateRange(1, 100)]
  [int] $StressLoadCycles = 3,

  [switch] $SkipBuild,

  [switch] $SkipWmi
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
if ([string]::IsNullOrWhiteSpace($StagingRoot)) {
  $StagingRoot = Join-Path $repoRoot 'artifacts\kmdf-runtime-staging'
}
if ([string]::IsNullOrWhiteSpace($LogRoot)) {
  $LogRoot = Join-Path $repoRoot 'artifacts\kmdf-acceptance'
}
$prepareScript = Join-Path $PSScriptRoot 'Prepare-KmdfRuntimeArtifacts.ps1'
$runtimeScript = Join-Path $PSScriptRoot 'Run-KmdfRuntimeSuite.ps1'
$verifierDrivers = @(
  'crtsys_kmdf_ntl_sample.sys',
  'crtsys_kmdf_pnp_ntl_sample.sys',
  'crtsys_kmdf_echo_ntl_sample.sys',
  'crtsys_kmdf_reference.sys',
  'crtsys_kmdf_bus_ntl_sample.sys',
  'crtsys_kmdf_bus_ntl_function.sys',
  'crtsys_kmdf_filter_stack_target.sys',
  'crtsys_kmdf_filter_stack_filter.sys',
  'crtsys_kmdf_wmi_ntl_sample.sys',
  'crtsys_kmdf_verifier_stress.sys'
)

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

function Find-DevCon {
  if ($DevConPath) {
    return (Resolve-Path -LiteralPath $DevConPath).Path
  }
  $tools = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Tools'
  $candidate = Get-ChildItem -LiteralPath $tools -Filter devcon.exe -File `
      -Recurse | Where-Object FullName -match '\\x64\\devcon\.exe$' |
      Sort-Object FullName -Descending | Select-Object -First 1
  if (-not $candidate) {
    throw 'x64 devcon.exe was not found.'
  }
  return $candidate.FullName
}

if (-not (Test-Path -LiteralPath $VmrunPath -PathType Leaf)) {
  throw "vmrun.exe was not found: $VmrunPath"
}
if (-not (Test-Path -LiteralPath $VmxPath -PathType Leaf)) {
  throw "VMX file was not found: $VmxPath"
}
if (-not (Test-Path -LiteralPath $prepareScript -PathType Leaf) -or
    -not (Test-Path -LiteralPath $runtimeScript -PathType Leaf)) {
  throw 'The KMDF runtime helper scripts were not found.'
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
  return [pscustomobject]@{
    ExitCode = [int] $exitCode
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
  [string] $Source,
  [string] $Destination,
  [switch] $AllowFailure
) {
  return Invoke-Vmrun -Guest -Quiet -AllowFailure:$AllowFailure -Arguments @(
    'copyFileFromGuestToHost', $VmxPath, $Source, $Destination
  )
}

function Wait-GuestReady {
  $readyScript = 'exit 0'
  for ($attempt = 1; $attempt -le 90; ++$attempt) {
    $result =
        Invoke-GuestScript -Script $readyScript -AllowFailure -Quiet
    if ($result.ExitCode -eq 0) {
      Write-Host "Guest operations ready after attempt $attempt."
      return
    }
    Start-Sleep -Seconds 2
  }
  throw 'Guest operations did not become ready within 180 seconds.'
}

function Restart-Guest {
  Write-Host 'Restarting the guest.'
  Invoke-Vmrun -Arguments @('reset', $VmxPath, 'soft') | Out-Null
  Wait-GuestReady
}

function Invoke-VerifierConfiguration(
  [string[]] $Drivers,
  [string] $BootMode,
  [string] $GuestLogPath
) {
  $driverLiterals =
      $Drivers | ForEach-Object { ConvertTo-PowerShellLiteral $_ }
  $driversExpression = '@(' + ($driverLiterals -join ',') + ')'
  $logLiteral = ConvertTo-PowerShellLiteral $GuestLogPath
  $script = @"
`$ErrorActionPreference = 'Stop'
`$log = $logLiteral
`$drivers = $driversExpression
`$lines = [Collections.Generic.List[string]]::new()
function Run-Verifier([string] `$name, [string[]] `$arguments) {
  `$lines.Add("=== `$name ===")
  `$saved = `$ErrorActionPreference
  `$ErrorActionPreference = 'Continue'
  `$output = @(& verifier.exe @arguments 2>&1)
  `$code = `$LASTEXITCODE
  `$ErrorActionPreference = `$saved
  foreach (`$line in `$output) {
    `$lines.Add((`$line.ToString() -replace [char]0, ''))
  }
  `$lines.Add("`$name`_RC=`$code")
  if (`$code -notin @(0, 2)) {
    throw "verifier `$name failed with exit code `$code."
  }
}
Run-Verifier 'QUERY_SETTINGS_BEFORE' @('/querysettings')
Run-Verifier 'QUERY_ACTIVE_BEFORE' @('/query')
if (`$drivers.Count -eq 0) {
  Run-Verifier 'RESET' @('/reset')
} else {
  Run-Verifier 'ENABLE' (@('/standard', '/driver') + `$drivers)
  Run-Verifier 'BOOT_MODE' @('/bootmode', '$($BootMode.ToLowerInvariant())')
}
Run-Verifier 'QUERY_SETTINGS_AFTER' @('/querysettings')
Run-Verifier 'QUERY_ACTIVE_AFTER' @('/query')
`$lines.Add('PASS')
`$lines | Set-Content -LiteralPath `$log -Encoding UTF8
"@
  Invoke-GuestScript -Script $script | Out-Null
}

function Capture-Verifier(
  [string] $GuestLogPath,
  [string] $HostLogPath
) {
  $guestLiteral = ConvertTo-PowerShellLiteral $GuestLogPath
  $script = @"
`$ErrorActionPreference = 'Stop'
`$output = @(& verifier.exe /query 2>&1)
`$code = `$LASTEXITCODE
`$output | ForEach-Object { `$_.ToString() -replace [char]0, '' } |
    Set-Content -LiteralPath $guestLiteral -Encoding UTF8
if (`$code -ne 0) { exit `$code }
"@
  Invoke-GuestScript -Script $script | Out-Null
  Copy-FromGuest $GuestLogPath $HostLogPath | Out-Null
}

function Stage-Tree([string] $HostRoot, [string] $GuestDestinationRoot) {
  $directories = @(
    Get-ChildItem -LiteralPath $HostRoot -Recurse -Directory |
      Sort-Object FullName
  )
  $guestDirectories = @($GuestDestinationRoot)
  foreach ($directory in $directories) {
    $relative = $directory.FullName.Substring($HostRoot.Length).
        TrimStart('\')
    $guestDirectories +=
        $GuestDestinationRoot + '\' + $relative
  }
  $directoryLiterals =
      $guestDirectories |
      ForEach-Object { ConvertTo-PowerShellLiteral $_ }
  $script = @"
`$ErrorActionPreference = 'Stop'
foreach (`$directory in @($($directoryLiterals -join ','))) {
  New-Item -ItemType Directory -Force -Path `$directory | Out-Null
}
"@
  Invoke-GuestScript -Script $script | Out-Null

  foreach ($file in Get-ChildItem -LiteralPath $HostRoot -Recurse -File) {
    $relative = $file.FullName.Substring($HostRoot.Length).TrimStart('\')
    Copy-ToGuest $file.FullName ($GuestDestinationRoot + '\' + $relative)
  }
}

function Restore-Verifier {
  $guestRestoreLog = Join-Path $GuestRoot 'verifier-restore-stage.txt'
  Invoke-VerifierConfiguration $RestoreDriverFileName $RestoreBootMode `
      $guestRestoreLog
  Copy-FromGuest $guestRestoreLog `
      (Join-Path $LogRoot 'verifier-restore-stage.txt') | Out-Null
  Restart-Guest

  $guestActive = Join-Path $GuestRoot 'verifier-restored-active.txt'
  $hostActive = Join-Path $LogRoot 'verifier-restored-active.txt'
  Capture-Verifier $guestActive $hostActive
  $activeText = Get-Content -LiteralPath $hostActive -Raw
  foreach ($driver in $RestoreDriverFileName) {
    if (-not $activeText.Contains($driver)) {
      throw "Restored verifier target was not active: $driver"
    }
  }
  $script:restoreCompleted = $true
}

try {
  New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null

  $prepareArguments = @(
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-File', $prepareScript,
    '-DriverConfiguration', $DriverConfiguration,
    '-PlatformToolset', $PlatformToolset,
    '-WindowsSdkVersion', $WindowsSdkVersion,
    '-OutputRoot', $StagingRoot
  )
  if ($SkipBuild) {
    $prepareArguments += '-SkipBuild'
  }
  & powershell.exe @prepareArguments
  if ($LASTEXITCODE -ne 0) {
    throw 'Preparing KMDF runtime artifacts failed.'
  }

  $x64Root = Join-Path $StagingRoot 'x64'
  $x86Root = Join-Path $StagingRoot 'x86'
  $stressRoot = Join-Path $StagingRoot 'stress'
  $devcon = Find-DevCon

  $guestRootLiteral = ConvertTo-PowerShellLiteral $GuestRoot
  Invoke-GuestScript -Script @"
`$ErrorActionPreference = 'Stop'
`$root = $guestRootLiteral
if (`$root -ne 'C:\' -and `$root.Length -gt 3 -and
    (Test-Path -LiteralPath `$root)) {
  Remove-Item -LiteralPath `$root -Recurse -Force
}
New-Item -ItemType Directory -Force -Path `$root | Out-Null
"@ | Out-Null

  $preflightGuest = Join-Path $GuestRoot 'verifier-preflight.txt'
  $preflightHost = Join-Path $LogRoot 'verifier-preflight.txt'
  Capture-Verifier $preflightGuest $preflightHost

  $enableGuest = Join-Path $GuestRoot 'verifier-enable.txt'
  Invoke-VerifierConfiguration $verifierDrivers Oneboot $enableGuest
  $verifierChanged = $true
  Copy-FromGuest $enableGuest (Join-Path $LogRoot 'verifier-enable.txt') |
      Out-Null
  Restart-Guest

  $activeGuest = Join-Path $GuestRoot 'verifier-active.txt'
  $activeHost = Join-Path $LogRoot 'verifier-active.txt'
  Capture-Verifier $activeGuest $activeHost
  $activeText = Get-Content -LiteralPath $activeHost -Raw
  foreach ($driver in $verifierDrivers) {
    if (-not $activeText.Contains($driver)) {
      throw "Verifier target was not active after reboot: $driver"
    }
  }

  Stage-Tree $x64Root (Join-Path $GuestRoot 'x64')
  Stage-Tree $x86Root (Join-Path $GuestRoot 'x86')
  Stage-Tree $stressRoot (Join-Path $GuestRoot 'stress')
  Copy-ToGuest $runtimeScript (Join-Path $GuestRoot 'Run-KmdfRuntimeSuite.ps1')
  Copy-ToGuest $devcon (Join-Path $GuestRoot 'devcon.exe')

  $guestSuiteLog = Join-Path $GuestRoot 'runtime-suite.log'
  $guestStressLog = Join-Path $GuestRoot 'verifier-stress.log'
  $rootLiteral = ConvertTo-PowerShellLiteral $GuestRoot
  $skipWmiValue = if ($SkipWmi) { 1 } else { 0 }
  $guestRunScript = @"
`$ErrorActionPreference = 'Stop'
`$root = $rootLiteral
`$env:Path = `$root + ';' + `$env:Path
`$suiteLog = Join-Path `$root 'runtime-suite.log'
`$stressLog = Join-Path `$root 'verifier-stress.log'
`$certificates = @(
  Get-ChildItem -LiteralPath `$root -Filter '*.cer' -Recurse -File |
    Sort-Object FullName -Unique
)
foreach (`$certificate in `$certificates) {
  & certutil.exe -f -addstore Root `$certificate.FullName *> `$null
  if (`$LASTEXITCODE -ne 0) {
    throw "Root certificate import failed for `$(`$certificate.FullName)."
  }
  & certutil.exe -f -addstore TrustedPublisher `$certificate.FullName *> `$null
  if (`$LASTEXITCODE -ne 0) {
    throw "Publisher certificate import failed for `$(`$certificate.FullName)."
  }
}

try {
  `$suiteParameters = @{
    PackageRoot = Join-Path `$root 'x64'
    X86AppRoot = Join-Path `$root 'x86'
  }
  if ($skipWmiValue -ne 0) { `$suiteParameters.SkipWmi = `$true }
  & (Join-Path `$root 'Run-KmdfRuntimeSuite.ps1') @suiteParameters *>&1 |
      Tee-Object -FilePath `$suiteLog
  Add-Content -LiteralPath `$suiteLog -Encoding UTF8 -Value 'PASS'
} catch {
  `$_ | Out-String | Add-Content -LiteralPath `$suiteLog -Encoding UTF8
  Add-Content -LiteralPath `$suiteLog -Encoding UTF8 -Value 'FAIL'
  throw
}

`$stressDriver =
    Join-Path `$root 'stress\crtsys_kmdf_verifier_stress.sys'
`$stressApp =
    Join-Path `$root 'stress\crtsys_kmdf_verifier_stress_app.exe'
Set-Content -LiteralPath `$stressLog -Encoding UTF8 -Value (
    "BEGIN " + (Get-Date).ToString('s'))
try {
  for (`$cycle = 1; `$cycle -le $StressLoadCycles; ++`$cycle) {
    `$service = "CrtSysKmdfAcceptanceStress`$cycle"
    & sc.exe create `$service 'binPath=' `$stressDriver 'type=' 'kernel' \
        'start=' 'demand' | Add-Content `$stressLog
    if (`$LASTEXITCODE -ne 0) {
      throw "sc create failed in stress cycle `$cycle."
    }
    try {
      & sc.exe start `$service | Add-Content `$stressLog
      if (`$LASTEXITCODE -ne 0) {
        throw "sc start failed in stress cycle `$cycle."
      }
      & `$stressApp '$StressIterations' '$StressWorkers' 2>&1 |
          Add-Content `$stressLog
      if (`$LASTEXITCODE -ne 0) {
        throw "Stress app failed in cycle `$cycle."
      }
    } finally {
      & sc.exe stop `$service | Add-Content `$stressLog
      & sc.exe delete `$service | Add-Content `$stressLog
    }
  }
  Add-Content -LiteralPath `$stressLog -Encoding UTF8 -Value 'PASS'
} catch {
  `$_ | Out-String | Add-Content -LiteralPath `$stressLog -Encoding UTF8
  Add-Content -LiteralPath `$stressLog -Encoding UTF8 -Value 'FAIL'
  throw
}
"@
  $guestRunScript = $guestRunScript -replace ' \\\r?\n', ' '
  $runResult =
      Invoke-GuestScript -Script $guestRunScript -AllowFailure
  Copy-FromGuest $guestSuiteLog (Join-Path $LogRoot 'runtime-suite.log') `
      -AllowFailure | Out-Null
  Copy-FromGuest $guestStressLog (Join-Path $LogRoot 'verifier-stress.log') `
      -AllowFailure | Out-Null
  if ($runResult.ExitCode -ne 0) {
    throw "Guest KMDF acceptance workload failed with exit code $($runResult.ExitCode)."
  }

  $afterGuest = Join-Path $GuestRoot 'verifier-after.txt'
  $afterHost = Join-Path $LogRoot 'verifier-after.txt'
  Capture-Verifier $afterGuest $afterHost
  $afterText = Get-Content -LiteralPath $afterHost -Raw
  foreach ($driver in $verifierDrivers) {
    if (-not $afterText.Contains($driver)) {
      throw "Verifier activity did not list target: $driver"
    }
  }
  if ($afterText -notmatch 'SpecialPool:\s+[1-9][0-9]*') {
    throw 'Verifier did not report any successful Special Pool allocations.'
  }

  $postcheckGuest = Join-Path $GuestRoot 'postcheck.txt'
  $postcheckHost = Join-Path $LogRoot 'postcheck.txt'
  $postcheckLiteral = ConvertTo-PowerShellLiteral $postcheckGuest
  $postcheckScript = @"
`$ErrorActionPreference = 'Stop'
`$os = Get-CimInstance Win32_OperatingSystem
`$boot = `$os.LastBootUpTime
`$lines = [Collections.Generic.List[string]]::new()
`$lines.Add("TIME=" + (Get-Date).ToString('s'))
`$lines.Add("LAST_BOOT=" + `$boot.ToString('s'))
`$lines.Add('=== PRESENT_KMDF_TEST_DEVICES ===')
`$devices = @(
  Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object FriendlyName -like 'crtsys NTL KMDF*'
)
if (`$devices.Count -eq 0) {
  `$lines.Add('NONE')
} else {
  foreach (`$device in `$devices) {
    `$lines.Add("DEVICE `$(`$device.InstanceId) `$(`$device.FriendlyName)")
  }
}
`$lines.Add('=== BUGCHECK_OR_UNEXPECTED_REBOOT_EVENTS ===')
`$events = @(
  Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=`$boot} \
      -ErrorAction SilentlyContinue |
    Where-Object Id -in @(41, 1001, 6008)
)
if (`$events.Count -eq 0) {
  `$lines.Add('NONE')
} else {
  foreach (`$event in `$events) {
    `$lines.Add("EVENT `$(`$event.TimeCreated.ToString('s')) \
        ID=`$(`$event.Id) PROVIDER=`$(`$event.ProviderName)")
  }
}
`$lines.Add('=== DUMPS_WRITTEN_SINCE_BOOT ===')
`$dumps = @()
if (Test-Path 'C:\Windows\Minidump') {
  `$dumps += Get-ChildItem 'C:\Windows\Minidump' -Filter '*.dmp' -File |
      Where-Object LastWriteTime -ge `$boot
}
if (Test-Path 'C:\Windows\MEMORY.DMP') {
  `$memoryDump = Get-Item 'C:\Windows\MEMORY.DMP'
  if (`$memoryDump.LastWriteTime -ge `$boot) { `$dumps += `$memoryDump }
}
if (`$dumps.Count -eq 0) {
  `$lines.Add('NONE')
} else {
  foreach (`$dump in `$dumps) {
    `$lines.Add("DUMP `$(`$dump.FullName) `$(`$dump.Length)")
  }
}
`$lines.Add('PASS')
`$lines | Set-Content -LiteralPath $postcheckLiteral -Encoding UTF8
"@
  $postcheckScript = $postcheckScript -replace ' \\\r?\n', ' '
  Invoke-GuestScript -Script $postcheckScript | Out-Null
  Copy-FromGuest $postcheckGuest $postcheckHost | Out-Null
  $postcheckText = Get-Content -LiteralPath $postcheckHost -Raw
  if ($postcheckText -match '(?m)^(DEVICE|EVENT|DUMP) ') {
    throw 'Postcheck found a present test device, crash event, or new dump.'
  }

  Restore-Verifier
  Write-Host 'KMDF VM acceptance gate passed.'
} finally {
  if ($verifierChanged -and -not $restoreCompleted) {
    Write-Warning 'Attempting verifier restoration after an incomplete run.'
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
