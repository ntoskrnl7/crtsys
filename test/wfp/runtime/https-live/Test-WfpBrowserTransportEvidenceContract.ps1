[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$analyzer =
    Join-Path $PSScriptRoot 'Test-WfpBrowserTransportEvidence.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) (
    'crtsys-wfp-browser-evidence-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null

$validMarkers = @(
  'NTL WFP native UDP/443 block: verified kind=native-enforcement layer=ALE_AUTH_CONNECT_V4 action=FWP_ACTION_BLOCK protocol=UDP remote_port=443 application_scoped=true filter_id=101'
  'NTL WFP native UDP/443 block: verified kind=native-enforcement layer=ALE_AUTH_CONNECT_V6 action=FWP_ACTION_BLOCK protocol=UDP remote_port=443 application_scoped=true filter_id=202'
  'NTL WFP native UDP/443 drop event: observed kind=classify-drop layer=ALE_AUTH_CONNECT_V4 filter_id=101 protocol=17 remote_port=443 application_scoped=true'
  'NTL WFP browser HTTPS inspection stopped: html-files=1'
)
$validInventory = @(
  'schema=ntl-wfp-policy-diagnostics-v2 application-id-size=32 inventory-bound=256'
  'filter layer=ale-auth-connect-v4 index=0 id=101 action=block conditions=3'
  'filter layer=ale-auth-connect-v6 index=0 id=202 action=block conditions=3'
)

function Invoke-RejectedCase(
    [string] $Name,
    [string[]] $Markers,
    [string[]] $Inventory,
    [switch] $WriteHtml) {
  $caseRoot = Join-Path $root $Name
  New-Item -ItemType Directory -Path $caseRoot | Out-Null
  $proxy = Join-Path $caseRoot 'proxy.log'
  $policy = Join-Path $caseRoot 'wfp-policy-diagnostics.log'
  $Markers | Set-Content -LiteralPath $proxy -Encoding UTF8
  $Inventory | Set-Content -LiteralPath $policy -Encoding UTF8
  if ($WriteHtml) {
    'html' | Set-Content -LiteralPath (
        Join-Path $caseRoot 'capture-target.example.invalid.html') `
        -Encoding UTF8
  }

  $rejected = $false
  try {
    & $analyzer -ProxyLogPath $proxy `
        -PolicyInventoryPath $policy -LogDirectory $caseRoot `
        -ExpectedHost 'target.example.invalid' `
        -RequireObservedUdpBlock | Out-Null
  } catch {
    $rejected = $true
  }
  if (-not $rejected) {
    throw "Invalid browser transport evidence was accepted: $Name"
  }
}

try {
  $validRoot = Join-Path $root 'valid'
  New-Item -ItemType Directory -Path $validRoot | Out-Null
  $proxy = Join-Path $validRoot 'proxy.log'
  $policy = Join-Path $validRoot 'wfp-policy-diagnostics.log'
  $result = Join-Path $validRoot 'result.json'
  $validMarkers | Set-Content -LiteralPath $proxy -Encoding UTF8
  $validInventory | Set-Content -LiteralPath $policy -Encoding UTF8
  'html' | Set-Content -LiteralPath (
      Join-Path $validRoot 'capture-target.example.invalid.html') `
      -Encoding UTF8
  & $analyzer -ProxyLogPath $proxy `
      -PolicyInventoryPath $policy -LogDirectory $validRoot `
      -ExpectedHost 'target.example.invalid' `
      -ResultPath $result -RequireObservedUdpBlock | Out-Null
  $document = Get-Content -LiteralPath $result -Raw | ConvertFrom-Json
  if ($document.schema -ne 'ntl-wfp-browser-transport-evidence-v1' -or
      -not $document.observed_udp443_drop -or
      $document.native_ipv4_filter_id -ne 101 -or
      $document.native_ipv6_filter_id -ne 202 -or
      $document.inspected_tcp_html_count -ne 1) {
    throw 'Valid browser transport evidence produced the wrong result.'
  }

  Invoke-RejectedCase -Name 'missing-ipv6-marker' `
      -Markers @($validMarkers | Where-Object {
        $_ -notmatch 'layer=ALE_AUTH_CONNECT_V6'
      }) -Inventory $validInventory -WriteHtml
  Invoke-RejectedCase -Name 'drop-filter-mismatch' `
      -Markers @($validMarkers -replace 'filter_id=101 protocol=17',
          'filter_id=999 protocol=17') `
      -Inventory $validInventory -WriteHtml
  Invoke-RejectedCase -Name 'filter-not-in-inventory' `
      -Markers $validMarkers `
      -Inventory @($validInventory -replace 'id=202', 'id=303') `
      -WriteHtml
  Invoke-RejectedCase -Name 'missing-html' `
      -Markers $validMarkers -Inventory $validInventory

  Write-Host 'WFP browser transport evidence contracts passed.'
} finally {
  if (Test-Path -LiteralPath $root -PathType Container) {
    Remove-Item -LiteralPath $root -Recurse -Force
  }
}
