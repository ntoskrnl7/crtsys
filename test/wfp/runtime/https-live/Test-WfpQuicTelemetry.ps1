[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $ProxyLogPath,

  [string] $ResultPath = '',

  [switch] $RequireObservedBlock
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-TelemetryToken(
    [string] $Line,
    [string] $Name) {
  $match = [Regex]::Match(
      $Line,
      '(?:^|\s)' + [Regex]::Escape($Name) + '=([^\s]+)')
  if (-not $match.Success) {
    throw "Kernel QUIC telemetry is missing $Name."
  }
  return $match.Groups[1].Value
}

function Get-UnsignedTelemetry(
    [string] $Line,
    [string] $Name) {
  $value = 0L
  if (-not [long]::TryParse(
      (Get-TelemetryToken $Line $Name), [ref]$value) -or
      $value -lt 0) {
    throw "Kernel QUIC telemetry has an invalid $Name value."
  }
  return $value
}

$resolvedLogPath = [IO.Path]::GetFullPath($ProxyLogPath)
if (-not (Test-Path -LiteralPath $resolvedLogPath -PathType Leaf)) {
  throw "Browser proxy log was not found: $resolvedLogPath"
}
$output = [IO.File]::ReadAllText(
    $resolvedLogPath, [Text.Encoding]::UTF8)
$marker = 'NTL WFP QUIC telemetry: '
$line = @(
  $output -split "\r?\n" |
      Where-Object { $_.StartsWith($marker) }
) | Select-Object -Last 1
if ([string]::IsNullOrWhiteSpace($line)) {
  throw 'The browser runtime did not report kernel QUIC telemetry.'
}

$families = [Collections.Generic.List[object]]::new()
$totalHits = 0L
foreach ($family in @('ipv4', 'ipv6')) {
  $hits = Get-UnsignedTelemetry $line "$family-hits"
  $blocks = Get-UnsignedTelemetry $line "$family-blocks"
  $actionWrite =
      Get-UnsignedTelemetry $line "$family-action-write"
  $actionWriteMissing =
      Get-UnsignedTelemetry $line "$family-action-write-missing"
  $applicationMatches =
      (Get-TelemetryToken $line "$family-app-id-match") -eq 'yes'
  $protocol =
      Get-UnsignedTelemetry $line "$family-last-protocol"
  $port = Get-UnsignedTelemetry $line "$family-last-port"
  $valid = $blocks -eq $hits -and
      $actionWrite -eq $hits -and
      $actionWriteMissing -eq 0 -and
      ($hits -eq 0 -or
          ($applicationMatches -and
           $protocol -eq 17 -and $port -eq 443))
  $families.Add([pscustomobject][ordered]@{
    address_family = $family
    classify_hits = $hits
    block_decisions = $blocks
    action_write_available = $actionWrite
    action_write_missing = $actionWriteMissing
    application_id_matches = $applicationMatches
    last_protocol = $protocol
    last_remote_port = $port
    valid = $valid
  })
  $totalHits += $hits
}

$validTelemetry =
    @($families | Where-Object { -not $_.valid }).Count -eq 0
$observedBlock = $validTelemetry -and $totalHits -gt 0
$result = [pscustomobject][ordered]@{
  proxy_log_path = $resolvedLogPath
  total_classify_hits = $totalHits
  telemetry_valid = $validTelemetry
  observed_quic_block = $observedBlock
  families = @($families)
}

if (-not [string]::IsNullOrWhiteSpace($ResultPath)) {
  $resolvedResultPath = [IO.Path]::GetFullPath($ResultPath)
  $parent = Split-Path -Parent $resolvedResultPath
  if ([string]::IsNullOrWhiteSpace($parent)) {
    throw 'ResultPath must include a parent directory.'
  }
  New-Item -ItemType Directory -Path $parent -Force | Out-Null
  $result | ConvertTo-Json -Depth 6 |
      Set-Content -LiteralPath $resolvedResultPath -Encoding UTF8
}

Write-Output "QUIC_CLASSIFY_HITS=$totalHits"
Write-Output (
    'QUIC_TELEMETRY_VALID=' +
    $(if ($validTelemetry) { 'PASS' } else { 'FAIL' }))
Write-Output (
    'QUIC_BLOCK_OBSERVED=' +
    $(if ($observedBlock) { 'PASS' } else { 'FAIL' }))

if ($RequireObservedBlock -and -not $observedBlock) {
  if (-not $validTelemetry) {
    throw 'Kernel QUIC telemetry is inconsistent with a blocking callout.'
  }
  throw (
      'Edge did not challenge the fail-closed policy with a UDP/443 ' +
      'classify. The run cannot prove QUIC blocking.')
}
