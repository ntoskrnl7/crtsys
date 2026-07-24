param(
  [string] $PackageDirectory = (Join-Path $PSScriptRoot 'x64\Debug')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Find-DevCon {
  $command = Get-Command devcon.exe -ErrorAction SilentlyContinue
  if ($command) {
    return $command.Source
  }

  $tools = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Tools'
  $candidate = Get-ChildItem -Path $tools -Filter devcon.exe -Recurse |
    Where-Object { $_.FullName -match '\\x64\\devcon\.exe$' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
  if (-not $candidate) {
    throw 'devcon.exe was not found. Install the WDK Tools component.'
  }
  return $candidate.FullName
}

$inf = Join-Path $PackageDirectory 'crtsys_kmdf_pnp_ntl_sample.inf'
if (-not (Test-Path $inf)) {
  throw "The built sample INF was not found: $inf"
}

$devcon = Find-DevCon
& $devcon install (Resolve-Path $inf).Path 'Root\CrtSysKmdfNtlPnpSample'
if ($LASTEXITCODE -ne 0) {
  throw "devcon install failed with exit code $LASTEXITCODE."
}
