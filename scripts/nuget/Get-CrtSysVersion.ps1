param(
  [string] $VersionHeader
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($VersionHeader)) {
  $VersionHeader = Join-Path $repoRoot 'include\.internal\version'
}

if (-not (Test-Path $VersionHeader)) {
  throw "Version header was not found: $VersionHeader"
}

$content = Get-Content -Raw $VersionHeader
$major = [regex]::Match($content, '#define\s+CRTSYS_VERSION_MAJOR\s+(\d+)')
$minor = [regex]::Match($content, '#define\s+CRTSYS_VERSION_MINOR\s+(\d+)')
$patch = [regex]::Match($content, '#define\s+CRTSYS_VERSION_PATCH\s+(\d+)')

if (-not ($major.Success -and $minor.Success -and $patch.Success)) {
  throw "Could not parse CRTSYS_VERSION_MAJOR/MINOR/PATCH from $VersionHeader"
}

"$($major.Groups[1].Value).$($minor.Groups[1].Value).$($patch.Groups[1].Value)"
