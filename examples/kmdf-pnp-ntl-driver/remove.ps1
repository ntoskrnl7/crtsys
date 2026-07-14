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

$devcon = Find-DevCon
& $devcon remove 'Root\CrtSysKmdfNtlPnpSample'
if ($LASTEXITCODE -ne 0) {
  throw "devcon remove failed with exit code $LASTEXITCODE."
}
