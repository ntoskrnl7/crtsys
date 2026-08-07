[CmdletBinding()]
param(
  [string] $RepositoryRoot = '',
  [string] $InfVerifPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
  $RepositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
}
$root = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar)

function Find-InfVerif {
  if (-not [string]::IsNullOrWhiteSpace($InfVerifPath)) {
    $explicit = [IO.Path]::GetFullPath($InfVerifPath)
    if (-not (Test-Path -LiteralPath $explicit -PathType Leaf)) {
      throw "InfVerif was not found at the explicit path: $explicit"
    }
    return $explicit
  }

  $kitsRoot = if (-not [string]::IsNullOrWhiteSpace($env:WindowsSdkDir)) {
    [IO.Path]::GetFullPath($env:WindowsSdkDir)
  } else {
    Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
  }
  $toolsRoot = Join-Path $kitsRoot 'Tools'
  $candidates = [Collections.Generic.List[string]]::new()
  if (-not [string]::IsNullOrWhiteSpace($env:WindowsSDKVersion)) {
    $sdkVersion = $env:WindowsSDKVersion.Trim().TrimEnd('\', '/')
    $candidates.Add((Join-Path $toolsRoot "$sdkVersion\x64\InfVerif.exe"))
  }
  if (Test-Path -LiteralPath $toolsRoot -PathType Container) {
    $versionDirectories = @(
      Get-ChildItem -LiteralPath $toolsRoot -Directory | Where-Object {
        $parsed = [version]::new()
        [version]::TryParse($_.Name, [ref] $parsed)
      } | Sort-Object { [version] $_.Name } -Descending)
    foreach ($directory in $versionDirectories) {
      $candidates.Add((Join-Path $directory.FullName 'x64\InfVerif.exe'))
    }
  }
  foreach ($candidate in $candidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
      return [IO.Path]::GetFullPath($candidate)
    }
  }
  throw (
    "InfVerif.exe was not found under '$toolsRoot'. " +
    'Install the Windows Driver Kit Tools component.')
}

$exampleRoot = Join-Path $root 'examples\wfp'
if (-not (Test-Path -LiteralPath $exampleRoot -PathType Container)) {
  throw "WFP example root does not exist: $exampleRoot"
}
$infRoots = @(
  Join-Path $exampleRoot 'user'
  Join-Path $exampleRoot 'kernel'
  Join-Path $root 'test\wfp\runtime\contracts'
) | Where-Object { Test-Path -LiteralPath $_ -PathType Container }
$infFiles = @(
  Get-ChildItem -LiteralPath $infRoots -File -Recurse -Filter '*.inf' |
      Where-Object {
        $_.FullName -notmatch '[\\/]build(?:[\\/_.-]|$)' -and
        $_.FullName -notmatch '[\\/]CMakeFiles[\\/]'
      } | Sort-Object FullName)
if ($infFiles.Count -eq 0) {
  throw 'No source WFP INF packages were found.'
}

$tool = Find-InfVerif
$paths = @($infFiles | ForEach-Object { $_.FullName })
& $tool /u @paths
if ($LASTEXITCODE -ne 0) {
  throw "InfVerif /u rejected one or more source WFP INF packages (exit $LASTEXITCODE)."
}
Write-Host (
  "WFP INF package verification passed: $($infFiles.Count) source INF files, " +
  "tool='$tool', mode=/u.")
