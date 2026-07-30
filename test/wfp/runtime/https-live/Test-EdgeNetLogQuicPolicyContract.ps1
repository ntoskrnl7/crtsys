[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$analyzer = Join-Path $PSScriptRoot 'Test-EdgeNetLogQuicPolicy.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) (
    'crtsys-edge-netlog-contract-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null

function New-NetLog([object[]] $Events) {
  return [ordered]@{
    constants = [ordered]@{
      logSourceType = @{QUIC_SESSION = 13}
      logEventTypes = [ordered]@{
        QUIC_SESSION = 100
        QUIC_SESSION_PACKET_SENT = 101
        QUIC_SESSION_PACKET_RECEIVED = 102
        QUIC_SESSION_PACKET_AUTHENTICATED = 103
        QUIC_SESSION_CERTIFICATE_VERIFIED = 104
        QUIC_CHROMIUM_CLIENT_STREAM_READ_RESPONSE_HEADERS = 105
      }
      logEventPhase = @{PHASE_BEGIN = 1}
    }
    events = $Events
  }
}

function New-Event(
    [int] $SourceId,
    [int] $Type,
    [int] $Phase = 0,
    [hashtable] $Parameters = @{}) {
  return [ordered]@{
    phase = $Phase
    source = @{id = $SourceId; type = 13}
    type = $Type
    params = $Parameters
  }
}

try {
  $blockedPath = Join-Path $root 'blocked.json'
  New-NetLog @(
    (New-Event 1 100 1 @{
      host='www.google.com'; port=443; proxy_chain='[direct://]'
    }),
    (New-Event 1 101)
  ) | ConvertTo-Json -Depth 8 |
      Set-Content -LiteralPath $blockedPath -Encoding UTF8
  & $analyzer -NetLogPath $blockedPath -TargetHost 'www.google.com' `
      -RequireBlocked | Out-Null

  $bypassPath = Join-Path $root 'bypass.json'
  New-NetLog @(
    (New-Event 2 100 1 @{
      host='www.google.com'; port=443; proxy_chain='[direct://]'
    }),
    (New-Event 2 101),
    (New-Event 2 102),
    (New-Event 2 103),
    (New-Event 2 104)
  ) | ConvertTo-Json -Depth 8 |
      Set-Content -LiteralPath $bypassPath -Encoding UTF8
  $bypassRejected = $false
  try {
    & $analyzer -NetLogPath $bypassPath -TargetHost 'www.google.com' `
        -RequireBlocked | Out-Null
  } catch {
    $bypassRejected = $true
  }
  if (-not $bypassRejected) {
    throw 'A successful direct QUIC session was not rejected.'
  }

  $truncatedPath = Join-Path $root 'truncated.json'
  $truncated = [IO.File]::ReadAllText($bypassPath).TrimEnd()
  if (-not $truncated.EndsWith('}')) {
    throw 'The synthetic NetLog did not have the expected JSON shape.'
  }
  $eventsEnd = $truncated.LastIndexOf(']')
  if ($eventsEnd -lt 0) {
    throw 'The synthetic NetLog did not contain an events terminator.'
  }
  [IO.File]::WriteAllText(
      $truncatedPath,
      $truncated.Substring(0, $eventsEnd).TrimEnd() + ",`r`n",
      [Text.UTF8Encoding]::new($false))
  $truncatedRejected = $false
  try {
    & $analyzer -NetLogPath $truncatedPath `
        -TargetHost 'www.google.com' -RequireBlocked | Out-Null
  } catch {
    $truncatedRejected = $true
  }
  if (-not $truncatedRejected) {
    throw 'A repaired truncated NetLog did not preserve the bypass verdict.'
  }

  & $analyzer -NetLogPath $bypassPath -TargetHost 'example.com' `
      -RequireBlocked | Out-Null
  Write-Host 'Edge NetLog QUIC policy contracts passed.'
} finally {
  if (Test-Path -LiteralPath $root -PathType Container) {
    Remove-Item -LiteralPath $root -Recurse -Force
  }
}
