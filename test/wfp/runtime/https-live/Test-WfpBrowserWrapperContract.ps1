[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$wrapper =
    Join-Path $PSScriptRoot 'Start-WfpBrowserHttpsInspection.ps1'
$packager =
    Join-Path $PSScriptRoot 'Prepare-WfpHttpsLiveArtifacts.ps1'
$vmRunner =
    Join-Path $PSScriptRoot 'Run-WfpHttpsVmAcceptance.ps1'
foreach ($path in @($wrapper, $packager, $vmRunner)) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Browser runtime contract input is missing: $path"
  }
}

$wrapperText = Get-Content -LiteralPath $wrapper -Raw
$allRuntimeText = @(
  Get-Content -LiteralPath $wrapper, $packager, $vmRunner -Raw
) -join "`n"
$forbidden = @(
  '--user-data-dir'
  '--no-first-run'
  '--no-default-browser-check'
  '--new-window'
  '--log-net-log'
  '--ignore-certificate-errors'
  '--ignore-certificate-errors-spki-list'
  '--origin-to-force-quic-on'
  '--disable-quic'
  '--enable-quic'
  '--disable-features'
  '--enable-features'
  '--host-resolver-rules'
  'NetLogPath'
  'CaptureBrowserNetLog'
  'Test-EdgeNetLogQuicPolicy'
  'Test-WfpQuicTelemetry'
  'Stop-TestBrowser'
  'taskkill'
)
foreach ($token in $forbidden) {
  if ($allRuntimeText.IndexOf(
      $token, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
    throw "Browser runtime retained a forbidden browser mutation: $token"
  }
}

if ($wrapperText -match
    '(?is)Start-Process\s+-FilePath\s+\$BrowserPath') {
  throw 'The browser wrapper still launches the selected browser.'
}
if ($wrapperText -match
    '(?is)Stop-Process[^\r\n]*(Browser|msedge|chrome)') {
  throw 'The browser wrapper still terminates a browser process.'
}
foreach ($required in @(
    'Wait-ObservedBrowserProcess'
    'Test-WfpBrowserTransportEvidence.ps1'
    'wfp-policy-diagnostics.log'
    'browser-transport-evidence.json')) {
  if ($wrapperText.IndexOf(
      $required, [StringComparison]::Ordinal) -lt 0) {
    throw "Browser wrapper contract is missing: $required"
  }
}

Write-Host 'WFP browser wrapper no-mutation contract passed.'
