[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $PackageRoot,

  [ValidateSet('all', 'datagram-proxy', 'async-inspection',
               'flow-monitor', 'stream-edit', 'connect-redirect',
               'bind-redirect', 'tls-inspection-proxy',
               'udp-content-filter', 'tcp-content-filter',
               'specialized-observation')]
  [string] $SelectedSample = 'all',

  [ValidateRange(1, 10080)]
  [int] $DurationMinutes = 60,

  [ValidateRange(1, 100)]
  [int] $IterationsPerCycle = 3,

  [string[]] $RequiredProviderPattern = @(),

  [string] $EvidenceDirectory = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Administrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  if (-not $principal.IsInRole(
      [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'The WFP soak gate must run from an elevated PowerShell session.'
  }
}

function Save-WfpState([string] $Path) {
  $parent = Split-Path -Parent $Path
  New-Item -ItemType Directory -Path $parent -Force | Out-Null
  & netsh.exe wfp show state "file=$Path" *> $null
  if ($LASTEXITCODE -ne 0 -or
      -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "Capturing WFP state failed: $Path"
  }
}

function Assert-RequiredProviders([string] $Path) {
  if ($RequiredProviderPattern.Count -eq 0) {
    return
  }
  $text = Get-Content -LiteralPath $Path -Raw
  foreach ($pattern in $RequiredProviderPattern) {
    if ([string]::IsNullOrWhiteSpace($pattern)) {
      throw 'RequiredProviderPattern cannot contain an empty value.'
    }
    if ($text -notmatch $pattern) {
      throw "Required WFP provider pattern was absent: $pattern"
    }
  }
}

function Get-SystemMetrics {
  $os = Get-CimInstance Win32_OperatingSystem
  $memory = Get-CimInstance Win32_PerfFormattedData_PerfOS_Memory
  $system = Get-CimInstance Win32_PerfFormattedData_PerfOS_System
  [pscustomobject]@{
    timestampUtc = [DateTime]::UtcNow.ToString('o')
    availableMemoryMiB = [math]::Round(
        [double] $os.FreePhysicalMemory / 1024, 3)
    committedMemoryMiB = [math]::Round(
        ([double] $os.TotalVisibleMemorySize -
         [double] $os.FreePhysicalMemory) / 1024, 3)
    poolNonpagedBytes = [uint64] $memory.PoolNonpagedBytes
    poolPagedBytes = [uint64] $memory.PoolPagedBytes
    cacheBytes = [uint64] $memory.CacheBytes
    processes = [uint32] $system.Processes
    threads = [uint32] $system.Threads
    systemUpTimeSeconds = [uint64] $system.SystemUpTime
  }
}

function Get-Percentile([double[]] $Values, [double] $Percentile) {
  if ($Values.Count -eq 0) {
    return 0.0
  }
  $ordered = @($Values | Sort-Object)
  $index = [math]::Ceiling($Percentile * $ordered.Count) - 1
  return [double] $ordered[[math]::Max(0, $index)]
}

Assert-Administrator
$PackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
$suite = Join-Path $PSScriptRoot 'Run-WfpAdvancedSuite.ps1'
if (-not (Test-Path -LiteralPath $suite -PathType Leaf)) {
  throw "Advanced WFP suite was not found: $suite"
}
if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
  $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
  $EvidenceDirectory = Join-Path $PackageRoot "soak-evidence-$stamp"
}
$EvidenceDirectory = [IO.Path]::GetFullPath($EvidenceDirectory)
New-Item -ItemType Directory -Path $EvidenceDirectory -Force | Out-Null

$started = Get-Date
$deadline = $started.AddMinutes($DurationMinutes)
$crashIds = @(41, 1001)
$dumpRoot = Join-Path $env:SystemRoot 'Minidump'
$dumpsBefore = @(
  Get-ChildItem -LiteralPath $dumpRoot -File -ErrorAction SilentlyContinue |
      Select-Object -ExpandProperty FullName)
$eventsBefore = @(
  Get-WinEvent -FilterHashtable @{
    LogName = 'System'; Id = $crashIds; StartTime = $started.AddMinutes(-1)
  } -ErrorAction SilentlyContinue |
      Select-Object -ExpandProperty RecordId)

$beforeState = Join-Path $EvidenceDirectory 'wfp-before.xml'
Save-WfpState $beforeState
Assert-RequiredProviders $beforeState

$cycles = [Collections.Generic.List[object]]::new()
$metrics = [Collections.Generic.List[object]]::new()
$metrics.Add((Get-SystemMetrics))
$cycle = 0
try {
  do {
    ++$cycle
    $cycleStarted = Get-Date
    $log = Join-Path $EvidenceDirectory ("cycle-{0:D4}.log" -f $cycle)
    & $suite -PackageRoot $PackageRoot `
        -SelectedSample $SelectedSample `
        -Iterations $IterationsPerCycle *>&1 |
        Tee-Object -FilePath $log
    $cycleSeconds = ((Get-Date) - $cycleStarted).TotalSeconds
    $text = Get-Content -LiteralPath $log -Raw
    $latencies = @(
      [regex]::Matches(
          $text, 'ipv[46]-p95-us=(?<latency>[0-9]+)') |
          ForEach-Object { [double] $_.Groups['latency'].Value })
    $cycles.Add([pscustomobject]@{
      cycle = $cycle
      elapsedSeconds = [math]::Round($cycleSeconds, 3)
      maximumReportedFlowP95Microseconds =
          if ($latencies.Count -eq 0) { 0 } else {
            [double] ($latencies | Measure-Object -Maximum).Maximum
          }
      log = [IO.Path]::GetFileName($log)
    })
    $metrics.Add((Get-SystemMetrics))
  } while ((Get-Date) -lt $deadline)
} finally {
  $afterState = Join-Path $EvidenceDirectory 'wfp-after.xml'
  Save-WfpState $afterState
  Assert-RequiredProviders $afterState
}

$finished = Get-Date
$eventsAfter = @(
  Get-WinEvent -FilterHashtable @{
    LogName = 'System'; Id = $crashIds; StartTime = $started
  } -ErrorAction SilentlyContinue |
      Where-Object { $_.RecordId -notin $eventsBefore })
$dumpsAfter = @(
  Get-ChildItem -LiteralPath $dumpRoot -File -ErrorAction SilentlyContinue |
      Where-Object { $_.FullName -notin $dumpsBefore })
if ($eventsAfter.Count -ne 0 -or $dumpsAfter.Count -ne 0) {
  throw (
      "The soak gate detected $($eventsAfter.Count) new crash events and " +
      "$($dumpsAfter.Count) new dump files.")
}

$durations = [double[]] @($cycles | ForEach-Object elapsedSeconds)
$flowP95 = [double[]] @(
  $cycles | Where-Object maximumReportedFlowP95Microseconds -gt 0 |
      ForEach-Object maximumReportedFlowP95Microseconds)
$summary = [ordered]@{
  schema = 1
  startedUtc = $started.ToUniversalTime().ToString('o')
  finishedUtc = $finished.ToUniversalTime().ToString('o')
  requestedDurationMinutes = $DurationMinutes
  actualDurationSeconds = [math]::Round(($finished - $started).TotalSeconds, 3)
  selectedSample = $SelectedSample
  iterationsPerCycle = $IterationsPerCycle
  cycles = $cycles.Count
  cycleSeconds = [ordered]@{
    p50 = [math]::Round((Get-Percentile $durations 0.50), 3)
    p95 = [math]::Round((Get-Percentile $durations 0.95), 3)
    maximum = [math]::Round((Get-Percentile $durations 1.00), 3)
  }
  reportedFlowP95Microseconds = [ordered]@{
    p50 = [math]::Round((Get-Percentile $flowP95 0.50), 3)
    p95 = [math]::Round((Get-Percentile $flowP95 0.95), 3)
    maximum = [math]::Round((Get-Percentile $flowP95 1.00), 3)
  }
  requiredProviderPatterns = $RequiredProviderPattern
  crashEvents = 0
  newDumps = 0
  cycleResults = $cycles
  systemMetrics = $metrics
}
$summaryPath = Join-Path $EvidenceDirectory 'summary.json'
$summary | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $summaryPath -Encoding utf8
Write-Host (
    "Advanced WFP soak passed: cycles=$($cycles.Count), " +
    "seconds=$([math]::Round(($finished - $started).TotalSeconds, 3)), " +
    "evidence=$EvidenceDirectory")
