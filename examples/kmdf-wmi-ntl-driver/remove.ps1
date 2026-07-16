Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$command = Get-Command devcon.exe -ErrorAction SilentlyContinue
if ($command) {
  $devcon = $command.Source
} else {
  $tools = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Tools'
  $candidate = Get-ChildItem -Path $tools -Filter devcon.exe -Recurse |
    Where-Object { $_.FullName -match '\\x64\\devcon\.exe$' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
  if (-not $candidate) {
    throw 'devcon.exe was not found. Install the WDK Tools component.'
  }
  $devcon = $candidate.FullName
}

& $devcon remove 'Root\CrtSysKmdfNtlWmiSample'
if ($LASTEXITCODE -ne 0) {
  throw "devcon remove failed with exit code $LASTEXITCODE."
}
