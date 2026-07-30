[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $NetLogPath,

  [Parameter(Mandatory)]
  [ValidateNotNullOrEmpty()]
  [string] $TargetHost,

  [string] $ResultPath = '',

  [switch] $RequireBlocked
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-PropertyValue(
    [object] $Value,
    [string] $Name,
    [object] $Default = $null) {
  if ($null -eq $Value) {
    return $Default
  }
  $property = $Value.PSObject.Properties[$Name]
  if ($null -eq $property) {
    return $Default
  }
  return $property.Value
}

function Get-RequiredConstant(
    [object] $Constants,
    [string] $Group,
    [string] $Name) {
  $values = Get-PropertyValue $Constants $Group
  $value = Get-PropertyValue $values $Name
  if ($null -eq $value) {
    throw "Edge NetLog is missing constants.$Group.$Name."
  }
  return [int] $value
}

function ConvertFrom-EdgeNetLog([string] $Path) {
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "Edge NetLog was not found: $Path"
  }
  $raw = [IO.File]::ReadAllText(
      [IO.Path]::GetFullPath($Path), [Text.Encoding]::UTF8)
  if ([string]::IsNullOrWhiteSpace($raw)) {
    throw 'Edge NetLog is empty.'
  }

  try {
    return $raw | ConvertFrom-Json
  } catch {
    # Edge is deliberately terminated at the end of the bounded browser
    # acceptance. NetLog can therefore contain complete event objects but
    # omit only the final array/object delimiters. Repair that exact shape in
    # memory; never alter the evidence file and never accept a partial event.
    $trimmed = $raw.TrimEnd()
    if ($trimmed.EndsWith(',')) {
      $trimmed = $trimmed.Substring(0, $trimmed.Length - 1).TrimEnd()
    }
    if ($trimmed -notmatch '"events"\s*:\s*\[') {
      throw 'Edge NetLog does not contain an events array.'
    }

    if ($trimmed.EndsWith(']')) {
      $candidate = $trimmed + '}'
    } elseif ($trimmed.EndsWith('}')) {
      $candidate = $trimmed + ']}'
    } else {
      throw (
          'Edge NetLog ended inside an event and cannot be repaired ' +
          'without changing evidence.')
    }
    try {
      return $candidate | ConvertFrom-Json
    } catch {
      throw "Edge NetLog JSON is invalid after bounded repair: $($_.Exception.Message)"
    }
  }
}

$resolvedNetLogPath = [IO.Path]::GetFullPath($NetLogPath)
$normalizedTarget = $TargetHost.Trim().TrimEnd('.').ToLowerInvariant()
if ([string]::IsNullOrWhiteSpace($normalizedTarget)) {
  throw 'TargetHost must contain a DNS host name.'
}

$document = ConvertFrom-EdgeNetLog $resolvedNetLogPath
$constants = Get-PropertyValue $document 'constants'
$events = @(Get-PropertyValue $document 'events' @())
if ($null -eq $constants -or $events.Count -eq 0) {
  throw 'Edge NetLog does not contain constants and events.'
}

$sourceQuicSession =
    Get-RequiredConstant $constants 'logSourceType' 'QUIC_SESSION'
$eventQuicSession =
    Get-RequiredConstant $constants 'logEventTypes' 'QUIC_SESSION'
$eventPacketSent =
    Get-RequiredConstant $constants 'logEventTypes' 'QUIC_SESSION_PACKET_SENT'
$eventPacketReceived =
    Get-RequiredConstant $constants 'logEventTypes' 'QUIC_SESSION_PACKET_RECEIVED'
$eventPacketAuthenticated =
    Get-RequiredConstant $constants 'logEventTypes' 'QUIC_SESSION_PACKET_AUTHENTICATED'
$eventCertificateVerified =
    Get-RequiredConstant $constants 'logEventTypes' 'QUIC_SESSION_CERTIFICATE_VERIFIED'
$phaseBegin =
    Get-RequiredConstant $constants 'logEventPhase' 'PHASE_BEGIN'

$responseEventTypes = [Collections.Generic.HashSet[int]]::new()
foreach ($name in @(
    'QUIC_CHROMIUM_CLIENT_STREAM_READ_RESPONSE_HEADERS',
    'HTTP_TRANSACTION_READ_RESPONSE_HEADERS')) {
  $value = Get-PropertyValue (
      Get-PropertyValue $constants 'logEventTypes') $name
  if ($null -ne $value) {
    [void] $responseEventTypes.Add([int] $value)
  }
}

$eventsBySource = @{}
foreach ($event in $events) {
  $source = Get-PropertyValue $event 'source'
  $sourceId = Get-PropertyValue $source 'id'
  if ($null -eq $sourceId) {
    continue
  }
  $key = [string] $sourceId
  if (-not $eventsBySource.ContainsKey($key)) {
    $eventsBySource[$key] = [Collections.Generic.List[object]]::new()
  }
  $eventsBySource[$key].Add($event)
}

$sessions = [Collections.Generic.List[object]]::new()
foreach ($event in $events) {
  $source = Get-PropertyValue $event 'source'
  if ([int](Get-PropertyValue $source 'type' -1) -ne
      $sourceQuicSession -or
      [int](Get-PropertyValue $event 'type' -1) -ne
      $eventQuicSession -or
      [int](Get-PropertyValue $event 'phase' -1) -ne $phaseBegin) {
    continue
  }

  $parameters = Get-PropertyValue $event 'params'
  $sessionHost = [string](Get-PropertyValue $parameters 'host' '')
  $normalizedHost =
      $sessionHost.Trim().TrimEnd('.').ToLowerInvariant()
  $port = [int](Get-PropertyValue $parameters 'port' 0)
  $proxyChain = [string](Get-PropertyValue $parameters 'proxy_chain' '')
  if ($normalizedHost -ne $normalizedTarget -or $port -ne 443 -or
      $proxyChain -notmatch '(?i)direct://') {
    continue
  }

  $sourceId = [int64](Get-PropertyValue $source 'id' 0)
  $sessionEvents = @($eventsBySource[[string]$sourceId])
  $sent = 0
  $received = 0
  $authenticated = 0
  $certificateVerified = 0
  $responseHeaders = 0
  foreach ($sessionEvent in $sessionEvents) {
    $type = [int](Get-PropertyValue $sessionEvent 'type' -1)
    switch ($type) {
      $eventPacketSent { ++$sent; break }
      $eventPacketReceived { ++$received; break }
      $eventPacketAuthenticated { ++$authenticated; break }
      $eventCertificateVerified { ++$certificateVerified; break }
      default {
        if ($responseEventTypes.Contains($type)) {
          ++$responseHeaders
        }
      }
    }
  }

  # An ALE_AUTH_CONNECT fail-closed filter can allow a local connection
  # attempt to be logged, but it must not let the browser authenticate
  # packets received from the public endpoint. Certificate or HTTP response
  # evidence makes the stronger application-level success explicit.
  $networkReached =
      $received -gt 0 -and $authenticated -gt 0
  $applicationSucceeded =
      $certificateVerified -gt 0 -or $responseHeaders -gt 0
  $sessions.Add([pscustomobject][ordered]@{
    source_id = $sourceId
    host = $sessionHost
    port = $port
    proxy_chain = $proxyChain
    sent_packets = $sent
    received_packets = $received
    authenticated_packets = $authenticated
    certificate_verified = $certificateVerified
    response_headers = $responseHeaders
    network_reached = $networkReached
    application_succeeded = $applicationSucceeded
  })
}

$reachable = @($sessions | Where-Object network_reached)
$successful = @($sessions | Where-Object application_succeeded)
$blocked = $reachable.Count -eq 0 -and $successful.Count -eq 0
$result = [pscustomobject][ordered]@{
  target_host = $normalizedTarget
  netlog_path = $resolvedNetLogPath
  direct_quic_sessions = $sessions.Count
  reachable_direct_quic_sessions = $reachable.Count
  successful_direct_quic_sessions = $successful.Count
  quic_fail_closed = $blocked
  sessions = @($sessions)
}

if (-not [string]::IsNullOrWhiteSpace($ResultPath)) {
  $resolvedResultPath = [IO.Path]::GetFullPath($ResultPath)
  $resultParent = Split-Path -Parent $resolvedResultPath
  if ([string]::IsNullOrWhiteSpace($resultParent)) {
    throw 'ResultPath must include a parent directory.'
  }
  New-Item -ItemType Directory -Path $resultParent -Force | Out-Null
  $result | ConvertTo-Json -Depth 8 |
      Set-Content -LiteralPath $resolvedResultPath -Encoding UTF8
}

Write-Output "TARGET_HOST=$normalizedTarget"
Write-Output "DIRECT_QUIC_SESSIONS=$($sessions.Count)"
Write-Output "REACHABLE_DIRECT_QUIC_SESSIONS=$($reachable.Count)"
Write-Output "SUCCESSFUL_DIRECT_QUIC_SESSIONS=$($successful.Count)"
Write-Output (
    'QUIC_FAIL_CLOSED=' + $(if ($blocked) { 'PASS' } else { 'FAIL' }))
foreach ($session in $sessions) {
  Write-Output (
      "QUIC_SESSION=$($session.source_id) " +
      "sent=$($session.sent_packets) received=$($session.received_packets) " +
      "authenticated=$($session.authenticated_packets) " +
      "certificate_verified=$($session.certificate_verified) " +
      "response_headers=$($session.response_headers)")
}

if ($RequireBlocked -and -not $blocked) {
  throw (
      "Direct QUIC reached $normalizedTarget while fail-closed blocking " +
      "was required: reachable=$($reachable.Count), " +
      "successful=$($successful.Count).")
}
