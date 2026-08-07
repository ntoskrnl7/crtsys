[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $ProxyLogPath,

  [Parameter(Mandatory)]
  [string] $PolicyInventoryPath,

  [Parameter(Mandatory)]
  [string] $LogDirectory,

  [string[]] $ExpectedHost = @(),

  [DateTime] $SessionStartedUtc = [DateTime]::MinValue,

  [string] $ResultPath = '',

  [switch] $RequireObservedUdpBlock
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function ConvertFrom-MarkerTokens(
    [string] $Line,
    [string] $Marker) {
  if (-not $Line.StartsWith(
      $Marker, [StringComparison]::Ordinal)) {
    throw "Evidence line does not start with '$Marker'."
  }

  $tokens = @{}
  $remainder = $Line.Substring($Marker.Length)
  foreach ($match in [Regex]::Matches(
      $remainder,
      '(?<name>[A-Za-z][A-Za-z0-9_-]*)=(?<value>[^\s]+)')) {
    $tokens[$match.Groups['name'].Value.ToLowerInvariant()] =
        $match.Groups['value'].Value
  }
  return $tokens
}

function Get-RequiredToken(
    [hashtable] $Tokens,
    [string] $Name,
    [string] $EvidenceName) {
  $key = $Name.ToLowerInvariant()
  if (-not $Tokens.ContainsKey($key) -or
      [string]::IsNullOrWhiteSpace($Tokens[$key])) {
    throw "$EvidenceName is missing $Name."
  }
  return [string]$Tokens[$key]
}

function Test-TokenValue(
    [string] $Value,
    [string[]] $Allowed) {
  foreach ($candidate in $Allowed) {
    if ($Value.Equals(
        $candidate, [StringComparison]::OrdinalIgnoreCase)) {
      return $true
    }
  }
  return $false
}

function ConvertTo-FilterId(
    [string] $Value,
    [string] $EvidenceName) {
  [UInt64] $filterId = 0
  if (-not [UInt64]::TryParse($Value, [ref]$filterId) -or
      $filterId -eq 0) {
    throw "$EvidenceName has an invalid filter_id."
  }
  return $filterId
}

$resolvedProxyLog = [IO.Path]::GetFullPath($ProxyLogPath)
$resolvedInventory = [IO.Path]::GetFullPath($PolicyInventoryPath)
$resolvedLogDirectory = [IO.Path]::GetFullPath($LogDirectory)
foreach ($requiredFile in @($resolvedProxyLog, $resolvedInventory)) {
  if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
    throw "Browser evidence file was not found: $requiredFile"
  }
}
if (-not (Test-Path -LiteralPath $resolvedLogDirectory -PathType Container)) {
  throw "Browser log directory was not found: $resolvedLogDirectory"
}

$sessionUtc = $SessionStartedUtc.ToUniversalTime()
foreach ($requiredFile in @($resolvedProxyLog, $resolvedInventory)) {
  if ((Get-Item -LiteralPath $requiredFile).LastWriteTimeUtc -lt $sessionUtc) {
    throw "Browser evidence is stale and not from this run: $requiredFile"
  }
}

$proxyText = [IO.File]::ReadAllText(
    $resolvedProxyLog, [Text.Encoding]::UTF8)
$inventoryText = [IO.File]::ReadAllText(
    $resolvedInventory, [Text.Encoding]::UTF8)
if ($inventoryText -notmatch '(?m)^schema=ntl-wfp-policy-diagnostics-v[0-9]+\s') {
  throw 'The native WFP inventory has no supported schema marker.'
}

$nativeMarker = 'NTL WFP native UDP/443 block: verified '
$nativeLines = @(
  $proxyText -split "\r?\n" |
      Where-Object {
        $_.StartsWith($nativeMarker, [StringComparison]::Ordinal)
      }
)
$expectedLayers = @(
  'ALE_AUTH_CONNECT_V4'
  'ALE_AUTH_CONNECT_V6'
)
$verifiedFilters = @{}
foreach ($line in $nativeLines) {
  $tokens = ConvertFrom-MarkerTokens $line $nativeMarker
  $kind = Get-RequiredToken $tokens 'kind' 'Native block marker'
  $layer = Get-RequiredToken $tokens 'layer' 'Native block marker'
  $action = Get-RequiredToken $tokens 'action' 'Native block marker'
  $protocol = Get-RequiredToken $tokens 'protocol' 'Native block marker'
  $remotePort =
      Get-RequiredToken $tokens 'remote_port' 'Native block marker'
  $applicationScoped =
      Get-RequiredToken $tokens 'application_scoped' 'Native block marker'
  $filterId = ConvertTo-FilterId (
      Get-RequiredToken $tokens 'filter_id' 'Native block marker') (
      'Native block marker')

  if (-not $kind.Equals(
      'native-enforcement', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The UDP/443 policy marker is not native enforcement.'
  }
  if ($layer -notin $expectedLayers) {
    throw "The native block marker has an unexpected layer: $layer"
  }
  if (-not (Test-TokenValue $action @('block', 'FWP_ACTION_BLOCK')) -or
      -not (Test-TokenValue $protocol @('17', 'UDP')) -or
      $remotePort -ne '443' -or
      -not (Test-TokenValue $applicationScoped @('true', 'yes'))) {
    throw "The native block marker has unsafe conditions for $layer."
  }
  if ($verifiedFilters.ContainsKey($layer)) {
    throw "The native block marker is duplicated for $layer."
  }

  $inventoryLayer =
      $layer.Replace('ALE_AUTH_CONNECT_', 'ale-auth-connect-').ToLowerInvariant()
  $matchingInventoryLines = @(
    $inventoryText -split "\r?\n" |
        Where-Object {
          $_ -match '(?i)^filter\s' -and
          $_ -match ('(?i)(?:^|\s)layer=' +
              [Regex]::Escape($inventoryLayer) + '(?:\s|$)') -and
          $_ -match ('(?i)(?:^|\s)id=' +
              [Regex]::Escape([string]$filterId) + '(?:\s|$)')
        }
  )
  if ($matchingInventoryLines.Count -ne 1) {
    throw (
        "Native filter $filterId for $layer was not uniquely present " +
        'in the same-run WFP inventory.')
  }
  $verifiedFilters[$layer] = $filterId
}

foreach ($layer in $expectedLayers) {
  if (-not $verifiedFilters.ContainsKey($layer)) {
    throw "The native UDP/443 block was not verified for $layer."
  }
}

$dropMarker = 'NTL WFP native UDP/443 drop event: observed '
$dropEvents = [Collections.Generic.List[object]]::new()
foreach ($line in @(
    $proxyText -split "\r?\n" |
        Where-Object {
          $_.StartsWith($dropMarker, [StringComparison]::Ordinal)
        })) {
  $tokens = ConvertFrom-MarkerTokens $line $dropMarker
  $kind = Get-RequiredToken $tokens 'kind' 'UDP drop event'
  $layer = Get-RequiredToken $tokens 'layer' 'UDP drop event'
  $protocol = Get-RequiredToken $tokens 'protocol' 'UDP drop event'
  $remotePort = Get-RequiredToken $tokens 'remote_port' 'UDP drop event'
  $applicationScoped =
      Get-RequiredToken $tokens 'application_scoped' 'UDP drop event'
  $filterId = ConvertTo-FilterId (
      Get-RequiredToken $tokens 'filter_id' 'UDP drop event') (
      'UDP drop event')

  if (-not $kind.Equals(
      'classify-drop', [StringComparison]::OrdinalIgnoreCase) -or
      $layer -notin $expectedLayers -or
      -not (Test-TokenValue $protocol @('17', 'UDP')) -or
      $remotePort -ne '443' -or
      -not (Test-TokenValue $applicationScoped @('true', 'yes')) -or
      -not $verifiedFilters.ContainsKey($layer) -or
      [UInt64]$verifiedFilters[$layer] -ne $filterId) {
    throw 'A UDP drop event does not match the verified native filter.'
  }
  $dropEvents.Add([pscustomobject][ordered]@{
    layer = $layer
    filter_id = $filterId
  })
}

$freshHtml = @(
  Get-ChildItem -LiteralPath $resolvedLogDirectory -Filter '*.html' -File `
      -Recurse |
      Where-Object { $_.LastWriteTimeUtc -ge $sessionUtc }
)
if ($freshHtml.Count -eq 0) {
  throw 'No fresh inspected TCP HTML capture was produced in this run.'
}

$normalizedExpectedHosts = @(
  $ExpectedHost |
      Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
      ForEach-Object { $_.Trim().TrimEnd('.').ToLowerInvariant() } |
      Sort-Object -Unique
)
foreach ($hostName in $normalizedExpectedHosts) {
  $matchingHtml = @(
    $freshHtml | Where-Object {
      if ($_.BaseName.EndsWith(
          '-' + $hostName, [StringComparison]::OrdinalIgnoreCase)) {
        return $true
      }
      $metadataPath = Join-Path $_.DirectoryName 'metadata.txt'
      if (-not (Test-Path -LiteralPath $metadataPath -PathType Leaf) -or
          (Get-Item -LiteralPath $metadataPath).LastWriteTimeUtc -lt
              $sessionUtc) {
        return $false
      }
      $metadata = Get-Content -LiteralPath $metadataPath -Raw
      return $metadata -match (
          '(?im)^server-name=' + [Regex]::Escape($hostName) + '\s*$')
    }
  )
  if ($matchingHtml.Count -eq 0) {
    throw "No fresh inspected TCP HTML was captured for $hostName."
  }
}

$observedUdpBlock = $dropEvents.Count -gt 0
$result = [pscustomobject][ordered]@{
  schema = 'ntl-wfp-browser-transport-evidence-v1'
  proxy_log_path = $resolvedProxyLog
  policy_inventory_path = $resolvedInventory
  native_ipv4_filter_id = [UInt64]$verifiedFilters['ALE_AUTH_CONNECT_V4']
  native_ipv6_filter_id = [UInt64]$verifiedFilters['ALE_AUTH_CONNECT_V6']
  observed_udp443_drop = $observedUdpBlock
  drop_events = @($dropEvents)
  inspected_tcp_html_count = $freshHtml.Count
  expected_hosts = $normalizedExpectedHosts
  inspected_tcp_html = @($freshHtml | ForEach-Object { $_.FullName })
}

if (-not [string]::IsNullOrWhiteSpace($ResultPath)) {
  $resolvedResultPath = [IO.Path]::GetFullPath($ResultPath)
  $parent = Split-Path -Parent $resolvedResultPath
  if ([string]::IsNullOrWhiteSpace($parent)) {
    throw 'ResultPath must include a parent directory.'
  }
  New-Item -ItemType Directory -Path $parent -Force | Out-Null
  $result | ConvertTo-Json -Depth 8 |
      Set-Content -LiteralPath $resolvedResultPath -Encoding UTF8
}

Write-Output 'NATIVE_UDP443_FILTERS=PASS'
Write-Output (
    'UDP443_DROP_EVENT=' +
    $(if ($observedUdpBlock) { 'PASS' } else { 'NOT_OBSERVED' }))
Write-Output "INSPECTED_TCP_HTML=$($freshHtml.Count)"

if ($RequireObservedUdpBlock -and -not $observedUdpBlock) {
  throw (
      'No browser UDP/443 classify-drop event matched the native block ' +
      'filter. The run is inconclusive; it cannot claim QUIC fallback.')
}
