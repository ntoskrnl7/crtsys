param(
  [Parameter(Mandatory = $true)]
  [string] $DriverPath,

  [Parameter(Mandatory = $true)]
  [string] $AppPath,

  [ValidateRange(1, 1000)]
  [int] $Cycles = 20,

  [ValidateRange(1, 1000)]
  [int] $IterationsPerCycle = 10,

  [string] $ServiceName = 'CrtSysNtlNetKernelContracts',

  [string] $EvidencePath,

  [string] $CrashPostcheckPath = '',

  [switch] $AllowDisposableGuestMutation,

  [string] $DisposableGuestSentinelPath =
      'C:\crtsys-disposable-test-guest.sentinel',

  [switch] $RequireVerifierTarget,

  [switch] $CaptureVerifierSettings,

  [ValidateRange(1, 3600)]
  [int] $OperationTimeoutSeconds = 30
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$guardCandidates = @(
  (Join-Path $PSScriptRoot 'DisposableGuestGuard.ps1')
  (Join-Path $PSScriptRoot '..\..\wfp\runtime\common\DisposableGuestGuard.ps1')
)
$guardPath = $guardCandidates | Where-Object {
  Test-Path -LiteralPath $_ -PathType Leaf
} | Select-Object -First 1
if (-not $guardPath) {
  throw 'DisposableGuestGuard.ps1 was not found. Copy it beside this runner.'
}
. $guardPath
Assert-CrtSysDisposableGuest `
    -AllowDisposableGuestMutation:$AllowDisposableGuestMutation `
    -SentinelPath $DisposableGuestSentinelPath

$DriverPath = (Resolve-Path -LiteralPath $DriverPath).Path
$AppPath = (Resolve-Path -LiteralPath $AppPath).Path
$crashPostcheckCandidates = if (
    [string]::IsNullOrWhiteSpace($CrashPostcheckPath)) {
  @(
    (Join-Path $PSScriptRoot 'Test-VmCrashPostcheck.ps1')
    (Join-Path $PSScriptRoot '..\..\common\Test-VmCrashPostcheck.ps1')
  )
} else {
  @($CrashPostcheckPath)
}
$resolvedCrashPostcheckPath = $null
foreach ($candidate in $crashPostcheckCandidates) {
  if (Test-Path -LiteralPath $candidate -PathType Leaf) {
    $resolvedCrashPostcheckPath = (Resolve-Path -LiteralPath $candidate).Path
    break
  }
}
if (-not $resolvedCrashPostcheckPath) {
  throw 'Test-VmCrashPostcheck.ps1 was not found. Copy it beside this ' +
      'runner or pass -CrashPostcheckPath explicitly.'
}
$started = Get-Date
$completedRuns = 0
$resolvedEvidencePath = $null
$resolvedProgressPath = $null
$verifierSettings = 'Not queried. Use -CaptureVerifierSettings to capture it.'
$verifierActive = 'Not queried. Use -RequireVerifierTarget to capture it.'
if ($EvidencePath) {
  $resolvedEvidencePath = [System.IO.Path]::GetFullPath($EvidencePath)
  $resolvedProgressPath = "$resolvedEvidencePath.progress.json"
  $evidenceDirectory = Split-Path -Parent $resolvedEvidencePath
  if ($evidenceDirectory) {
    New-Item -ItemType Directory -Path $evidenceDirectory -Force *> $null
  }
}
$crashEvidenceBase = if ($resolvedEvidencePath) {
  $resolvedEvidencePath
} else {
  Join-Path ([IO.Path]::GetTempPath()) (
      "crtsys-net-kernel-contracts-$PID-$([guid]::NewGuid().ToString('N'))")
}
$eventBaselinePath = "$crashEvidenceBase.crash-event-baseline.txt"
$dumpBaselinePath = "$crashEvidenceBase.crash-dump-baseline.txt"
$crashPostcheckOutputPath = "$crashEvidenceBase.crash-postcheck.txt"
$crashResult = $null

function ConvertTo-ProcessArgument([string] $Value) {
  if ($null -eq $Value -or $Value.Length -eq 0) {
    return '""'
  }
  if ($Value -notmatch '[\s"]') {
    return $Value
  }
  return '"' + $Value.Replace('"', '\"') + '"'
}

function Invoke-BoundedProcess(
  [Parameter(Mandatory = $true)]
  [string] $FilePath,

  [string[]] $ArgumentList = @(),

  [int] $TimeoutSeconds = $OperationTimeoutSeconds
) {
  $startInfo = New-Object System.Diagnostics.ProcessStartInfo
  $startInfo.FileName = $FilePath
  $startInfo.Arguments = (($ArgumentList | ForEach-Object {
    ConvertTo-ProcessArgument $_
  }) -join ' ')
  $startInfo.UseShellExecute = $false
  $startInfo.CreateNoWindow = $true
  $startInfo.RedirectStandardOutput = $true
  $startInfo.RedirectStandardError = $true

  $process = New-Object System.Diagnostics.Process
  $process.StartInfo = $startInfo
  if (-not $process.Start()) {
    throw "Could not start $FilePath."
  }

  $stdoutTask = $process.StandardOutput.ReadToEndAsync()
  $stderrTask = $process.StandardError.ReadToEndAsync()
  if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    try {
      $process.Kill()
    } catch {
      throw "Could not terminate timed-out process ${FilePath}: $($_.Exception.Message)"
    }
    if (-not $process.WaitForExit(5000)) {
      throw "Timed-out process $FilePath did not terminate within 5 seconds."
    }
    throw "$FilePath exceeded the $TimeoutSeconds second operation timeout."
  }
  $process.WaitForExit()

  [pscustomobject]@{
    ExitCode = $process.ExitCode
    StandardOutput = $stdoutTask.GetAwaiter().GetResult()
    StandardError = $stderrTask.GetAwaiter().GetResult()
  }
}

function Format-ProcessOutput([object] $ProcessResult) {
  (@($ProcessResult.StandardOutput, $ProcessResult.StandardError) |
    Where-Object { $_ } | Out-String).Trim()
}

function Write-Progress(
  [string] $Phase,
  [int] $Cycle = 0,
  [int] $Iteration = 0,
  [string] $Message = ''
) {
  $progress = [pscustomobject]@{
    Phase = $Phase
    Cycle = $Cycle
    Iteration = $Iteration
    CompletedRuns = $completedRuns
    TimestampUtc = (Get-Date).ToUniversalTime().ToString('o')
    Message = $Message
  }
  if ($resolvedProgressPath) {
    # Progress is observational evidence, not part of the driver contract.
    # A VM monitor may have the previous snapshot open while this process is
    # publishing the next one, so never fail the acceptance run on that read
    # race. Publish through a per-process temporary file and keep the previous
    # complete snapshot if replacement is momentarily unavailable.
    $temporaryProgressPath = "$resolvedProgressPath.$PID.tmp"
    try {
      $progress | ConvertTo-Json -Depth 3 |
        Set-Content -LiteralPath $temporaryProgressPath -Encoding utf8
      Move-Item -LiteralPath $temporaryProgressPath `
        -Destination $resolvedProgressPath -Force
    } catch [System.IO.IOException] {
      Remove-Item -LiteralPath $temporaryProgressPath -Force `
        -ErrorAction SilentlyContinue
    }
  }
}

function Write-Evidence([object] $Value) {
  $json = $Value | ConvertTo-Json -Depth 4
  if ($resolvedEvidencePath) {
    Set-Content -LiteralPath $resolvedEvidencePath -Value $json -Encoding utf8
  }
  $json
}

function Invoke-CrashPostcheck {
  & $resolvedCrashPostcheckPath `
      -EventBaselinePath $eventBaselinePath `
      -DumpBaselinePath $dumpBaselinePath `
      -OutputPath $crashPostcheckOutputPath

  $lines = @(Get-Content -LiteralPath $crashPostcheckOutputPath)
  if ($lines.Count -eq 0 -or $lines[-1] -ne 'PASS') {
    throw 'The VM crash postcheck did not produce a complete result.'
  }
  $values = @{}
  foreach ($line in $lines) {
    $separator = $line.IndexOf('=')
    if ($separator -gt 0) {
      $values[$line.Substring(0, $separator)] =
          $line.Substring($separator + 1)
    }
  }
  foreach ($required in @(
      'EVENT_COUNT', 'DUMP_COUNT', 'EVENT_LOG_RESET',
      'EVENT_BASELINE_RECORD_ID', 'EVENT_CURRENT_RECORD_ID')) {
    if (-not $values.ContainsKey($required)) {
      throw "The VM crash postcheck omitted $required."
    }
  }
  return [pscustomobject]@{
    EventCount = [int]$values.EVENT_COUNT
    DumpCount = [int]$values.DUMP_COUNT
    EventLogReset = [int]$values.EVENT_LOG_RESET
    EventBaselineRecordId = [long]$values.EVENT_BASELINE_RECORD_ID
    EventCurrentRecordId = [long]$values.EVENT_CURRENT_RECORD_ID
    Details = @($lines | Where-Object {
      $_ -like 'EVENT=*' -or $_ -like 'DUMP=*'
    })
  }
}

$driverFileName = [System.IO.Path]::GetFileName($DriverPath)

function Remove-SampleService {
  $query = Invoke-BoundedProcess sc.exe @('query', $ServiceName)
  if ($query.ExitCode -eq 0) {
    $null = Invoke-BoundedProcess sc.exe @('stop', $ServiceName)
    $delete = Invoke-BoundedProcess sc.exe @('delete', $ServiceName)
    if ($delete.ExitCode -ne 0 -and $delete.ExitCode -ne 1072) {
      throw "sc delete failed: $(Format-ProcessOutput $delete)"
    }
    for ($attempt = 0; $attempt -ne 50; ++$attempt) {
      $query = Invoke-BoundedProcess sc.exe @('query', $ServiceName)
      if ($query.ExitCode -ne 0) {
        break
      }
      Start-Sleep -Milliseconds 100
    }
    $query = Invoke-BoundedProcess sc.exe @('query', $ServiceName)
    if ($query.ExitCode -eq 0) {
      throw "Service deletion did not complete: $ServiceName"
    }
  }
}

try {
  Write-Progress 'initializing'
  Write-Progress 'capturing-crash-baseline'
  & $resolvedCrashPostcheckPath `
      -EventBaselinePath $eventBaselinePath `
      -DumpBaselinePath $dumpBaselinePath -CaptureBaseline
  if ($CaptureVerifierSettings) {
    Write-Progress 'querying-verifier-settings'
    $settings = Invoke-BoundedProcess verifier.exe @('/querysettings')
    if ($settings.ExitCode -ne 0) {
      throw "verifier /querysettings failed with exit code $($settings.ExitCode): $(Format-ProcessOutput $settings)"
    }
    $verifierSettings = (Format-ProcessOutput $settings)
  }
  if ($RequireVerifierTarget) {
    Write-Progress 'querying-active-verifier'
    $active = Invoke-BoundedProcess verifier.exe @('/query')
    if ($active.ExitCode -ne 0) {
      throw "verifier /query failed with exit code $($active.ExitCode): $(Format-ProcessOutput $active)"
    }
    $verifierActive = (Format-ProcessOutput $active)
    if ($verifierActive -notmatch [regex]::Escape($driverFileName)) {
      throw "Driver Verifier is not active for $driverFileName in the current boot. Configure it in the VM and reboot manually before using -RequireVerifierTarget."
    }
  }

  Write-Progress 'removing-stale-service'
  Remove-SampleService
  for ($cycle = 1; $cycle -le $Cycles; ++$cycle) {
    Write-Progress 'creating-service' $cycle
    $create = Invoke-BoundedProcess sc.exe @(
      'create', $ServiceName, 'type=', 'kernel', 'start=', 'demand',
      'binPath=', $DriverPath
    )
    Format-ProcessOutput $create | Out-Host
    if ($create.ExitCode -ne 0) {
      throw "sc create failed in cycle ${cycle}: $(Format-ProcessOutput $create)"
    }
    try {
      Write-Progress 'starting-driver' $cycle
      $start = Invoke-BoundedProcess sc.exe @('start', $ServiceName)
      Format-ProcessOutput $start | Out-Host
      if ($start.ExitCode -ne 0) {
        throw "sc start failed in cycle ${cycle}: $(Format-ProcessOutput $start)"
      }
      for ($iteration = 1; $iteration -le $IterationsPerCycle; ++$iteration) {
        Write-Progress 'running-app' $cycle $iteration
        $app = Invoke-BoundedProcess $AppPath
        $appText = Format-ProcessOutput $app
        $appText | Out-Host
        if ($app.ExitCode -ne 0) {
          throw "The network-core app failed in cycle $cycle iteration $iteration with exit code $($app.ExitCode). Output: $appText"
        }
        ++$completedRuns
        Write-Progress 'app-complete' $cycle $iteration
      }
    } finally {
      Write-Progress 'removing-service' $cycle
      Remove-SampleService
      Write-Progress 'service-removed' $cycle
    }
  }

  Write-Progress 'verifying-cleanup'
  Remove-SampleService
  Write-Progress 'checking-crash-events'
  $crashResult = Invoke-CrashPostcheck
  if ($crashResult.EventCount -ne 0 -or
      $crashResult.DumpCount -ne 0 -or
      $crashResult.EventLogReset -ne 0) {
    throw 'The VM crash postcheck found a new crash event, crash dump, ' +
        'or System-log reset.'
  }

  Write-Progress 'complete'
  Write-Evidence ([pscustomobject]@{
    Result = 'PASS'
    Cycles = $Cycles
    IterationsPerCycle = $IterationsPerCycle
    CompletedRuns = $completedRuns
    Driver = $DriverPath
    StartedUtc = $started.ToUniversalTime().ToString('o')
    FinishedUtc = (Get-Date).ToUniversalTime().ToString('o')
    NewCrashEvents = $crashResult.EventCount
    NewCrashDumps = $crashResult.DumpCount
    EventLogReset = $crashResult.EventLogReset
    EventBaselineRecordId = $crashResult.EventBaselineRecordId
    EventCurrentRecordId = $crashResult.EventCurrentRecordId
    CrashDetails = $crashResult.Details
    VerifierActive = $verifierActive
    VerifierSettings = $verifierSettings
  })
} catch {
  Write-Progress 'failed' 0 0 $_.Exception.Message
  Write-Evidence ([pscustomobject]@{
    Result = 'FAIL'
    Cycles = $Cycles
    IterationsPerCycle = $IterationsPerCycle
    CompletedRuns = $completedRuns
    Driver = $DriverPath
    StartedUtc = $started.ToUniversalTime().ToString('o')
    FinishedUtc = (Get-Date).ToUniversalTime().ToString('o')
    Error = $_.Exception.Message
    CrashPostcheck = $crashResult
    VerifierActive = $verifierActive
    VerifierSettings = $verifierSettings
  }) | Out-Host
  throw
} finally {
  Write-Progress 'cleanup'
  Remove-SampleService
  Write-Progress 'cleanup-complete'
}
