[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $EventBaselinePath,

  [Parameter(Mandatory)]
  [string] $DumpBaselinePath,

  [string] $OutputPath = '',

  [switch] $CaptureBaseline
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-SystemEventsWithRetry {
  param(
    [Parameter(Mandatory)]
    [scriptblock] $Query
  )

  $failure = $null
  for ($attempt = 0; $attempt -ne 5; ++$attempt) {
    try {
      return @(& $Query)
    } catch {
      if ($_.FullyQualifiedErrorId -like 'NoMatchingEventsFound*') {
        return @()
      }
      $failure = $_
      if ($attempt -ne 4) {
        Start-Sleep -Milliseconds 200
      }
    }
  }
  throw $failure
}

function Get-CrashDumpFingerprints {
  $dumpFiles = @()
  if (Test-Path 'C:\Windows\Minidump') {
    $dumpFiles +=
        Get-ChildItem 'C:\Windows\Minidump' -Filter '*.dmp' -File
  }
  if (Test-Path 'C:\Windows\MEMORY.DMP') {
    $dumpFiles += Get-Item 'C:\Windows\MEMORY.DMP'
  }
  return @($dumpFiles | Sort-Object FullName | ForEach-Object {
    "$($_.FullName)|$($_.Length)|$($_.LastWriteTimeUtc.Ticks)"
  })
}

if ($CaptureBaseline) {
  $latest = @(Get-SystemEventsWithRetry {
    Get-WinEvent -LogName System -MaxEvents 1 -ErrorAction Stop
  })
  $recordId = if ($latest.Count -eq 0) {
    0
  } else {
    [long]$latest[0].RecordId
  }
  Set-Content -LiteralPath $EventBaselinePath -Value $recordId `
      -Encoding ASCII
  @(Get-CrashDumpFingerprints) |
      Set-Content -LiteralPath $DumpBaselinePath -Encoding UTF8
  return
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
  throw 'OutputPath is required unless CaptureBaseline is selected.'
}

$baselineRecordId = [long](
    (Get-Content -LiteralPath $EventBaselinePath -Raw).Trim())
$latest = @(Get-SystemEventsWithRetry {
  Get-WinEvent -LogName System -MaxEvents 1 -ErrorAction Stop
})
$currentRecordId = if ($latest.Count -eq 0) {
  0
} else {
  [long]$latest[0].RecordId
}
$eventLogReset = [int]($currentRecordId -lt $baselineRecordId)

$events = @(Get-SystemEventsWithRetry {
  Get-WinEvent -FilterHashtable @{
    LogName = 'System'
    Id = @(41, 1001, 6008)
  } -ErrorAction Stop
} | Where-Object {
  $_.RecordId -gt $baselineRecordId -and (
    ($_.Id -eq 41 -and
     $_.ProviderName -eq 'Microsoft-Windows-Kernel-Power') -or
    ($_.Id -eq 6008 -and $_.ProviderName -eq 'EventLog') -or
    ($_.Id -eq 1001 -and $_.ProviderName -in @(
      'Microsoft-Windows-WER-SystemErrorReporting', 'BugCheck'))
  )
})

$currentDumps = @(Get-CrashDumpFingerprints)
$baselineDumps = @(
  Get-Content -LiteralPath $DumpBaselinePath -ErrorAction SilentlyContinue)
$newDumps = @($currentDumps | Where-Object {
  $baselineDumps -notcontains $_
})

@(
  "EVENT_COUNT=$($events.Count)"
  "DUMP_COUNT=$($newDumps.Count)"
  "EVENT_LOG_RESET=$eventLogReset"
  "EVENT_BASELINE_RECORD_ID=$baselineRecordId"
  "EVENT_CURRENT_RECORD_ID=$currentRecordId"
  $events | ForEach-Object {
    "EVENT=$($_.RecordId)|$($_.ProviderName)|$($_.Id)"
  }
  $newDumps | ForEach-Object { "DUMP=$_" }
  'PASS'
) | Set-Content -LiteralPath $OutputPath -Encoding UTF8
