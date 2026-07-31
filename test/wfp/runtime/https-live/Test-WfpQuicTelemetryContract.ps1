[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$analyzer = Join-Path $PSScriptRoot 'Test-WfpQuicTelemetry.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) (
    'crtsys-wfp-quic-telemetry-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null

function Write-Telemetry(
    [string] $Path,
    [string] $Ipv4,
    [string] $Ipv6) {
  @(
    'NTL WFP browser HTTPS inspection ready:'
    "NTL WFP QUIC telemetry: quic-telemetry $Ipv4 $Ipv6"
  ) | Set-Content -LiteralPath $Path -Encoding UTF8
}

$zero =
    'ipv4-hits=0 ipv4-blocks=0 ipv4-action-write=0 ' +
    'ipv4-action-write-missing=0 ipv4-last-protocol=0 ' +
    'ipv4-last-port=0 ipv4-app-id-match=no'
$valid =
    'ipv6-hits=2 ipv6-blocks=2 ipv6-action-write=2 ' +
    'ipv6-action-write-missing=0 ipv6-last-protocol=17 ' +
    'ipv6-last-port=443 ipv6-app-id-match=yes'

try {
  $validPath = Join-Path $root 'valid.log'
  Write-Telemetry $validPath $zero $valid
  & $analyzer -ProxyLogPath $validPath -RequireObservedBlock |
      Out-Null

  foreach ($case in @(
      [pscustomobject]@{
        Name = 'zero'
        Ipv4 = $zero
        Ipv6 = $zero
      },
      [pscustomobject]@{
        Name = 'missing-action-right'
        Ipv4 = $zero
        Ipv6 = $valid.Replace(
            'ipv6-action-write=2',
            'ipv6-action-write=1')
      },
      [pscustomobject]@{
        Name = 'wrong-application'
        Ipv4 = $zero
        Ipv6 = $valid.Replace(
            'ipv6-app-id-match=yes',
            'ipv6-app-id-match=no')
      })) {
    $path = Join-Path $root ($case.Name + '.log')
    Write-Telemetry $path $case.Ipv4 $case.Ipv6
    $rejected = $false
    try {
      & $analyzer -ProxyLogPath $path -RequireObservedBlock |
          Out-Null
    } catch {
      $rejected = $true
    }
    if (-not $rejected) {
      throw "Invalid QUIC telemetry was accepted: $($case.Name)"
    }
  }
  Write-Host 'WFP QUIC telemetry contracts passed.'
} finally {
  if (Test-Path -LiteralPath $root -PathType Container) {
    Remove-Item -LiteralPath $root -Recurse -Force
  }
}
