[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$kitsInclude = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
$headers = @(
  Get-ChildItem -LiteralPath $kitsInclude -Filter ntstatus.h -File -Recurse |
    Where-Object { $_.DirectoryName -like '*\shared' } |
    Sort-Object { [version] $_.Directory.Parent.Name } -Descending
)

if ($headers.Count -eq 0) {
  throw "Could not find a Windows SDK ntstatus.h under '$kitsInclude'."
}

$statusNames = @{}
$definitionPattern =
    '^\s*#define\s+(STATUS_[A-Z0-9_]+)\s+\(\(NTSTATUS\)0x([0-9A-Fa-f]{8})L?\)'
foreach ($line in Get-Content -LiteralPath $headers[0].FullName) {
  if ($line -match $definitionPattern) {
    $value = [Convert]::ToUInt32($Matches[2], 16)
    if (-not $statusNames.ContainsKey($value)) {
      $statusNames[$value] = [System.Collections.Generic.List[string]]::new()
    }
    $statusNames[$value].Add($Matches[1])
  }
}

if ($statusNames.Count -eq 0) {
  throw "Could not parse STATUS_* definitions from '$($headers[0].FullName)'."
}

$tracked = @(& git -C $repoRoot ls-files -- '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx')
if ($LASTEXITCODE -ne 0) {
  throw 'Could not enumerate tracked C and C++ sources.'
}
$untracked = @(& git -C $repoRoot ls-files --others --exclude-standard -- '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx')
if ($LASTEXITCODE -ne 0) {
  throw 'Could not enumerate untracked C and C++ sources.'
}

$literalPattern = [regex]::new(
    '(?i)(?<![0-9a-f])0x([0-9a-f]{8})(?:ui64|ull|ul|u|l)?\b')
$errors = [System.Collections.Generic.List[string]]::new()

foreach ($relativePath in @($tracked + $untracked | Sort-Object -Unique)) {
  if ($relativePath -like 'include/ntl/deps/*') {
    continue
  }

  $path = Join-Path $repoRoot $relativePath
  $lineNumber = 0
  foreach ($line in Get-Content -LiteralPath $path) {
    ++$lineNumber
    foreach ($match in $literalPattern.Matches($line)) {
      $value = [Convert]::ToUInt32($match.Groups[1].Value, 16)
      # Small positive integers overlap ordinary flags, counts, and device
      # register values. Warning/error NTSTATUS values occupy the high half and
      # are the dangerous literals this audit can identify unambiguously.
      if ($value -ge 0x80000000L -and $statusNames.ContainsKey($value)) {
        $names = $statusNames[$value] -join ', '
        $errors.Add("${relativePath}:$lineNumber uses $($match.Value); use the WDK symbol $names.")
      }
    }
  }
}

if ($errors.Count -ne 0) {
  $errors | ForEach-Object { Write-Error $_ }
  throw "Found $($errors.Count) hard-coded NTSTATUS literal(s)."
}

Write-Host "Validated C/C++ sources against WDK warning/error STATUS_* values from $($headers[0].FullName)."
