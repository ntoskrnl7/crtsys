param(
  [string] $Version,
  [string] $OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

if ([string]::IsNullOrWhiteSpace($Version)) {
  $Version = & (Join-Path $PSScriptRoot 'Get-CrtSysVersion.ps1')
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = Join-Path $repoRoot 'artifacts\nuget'
}

$manifest = Join-Path $repoRoot 'nuget\crtsys.nuspec'
$stagingDirectory = Join-Path $repoRoot 'artifacts\nuget-staging'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

foreach ($requiredPath in @(
  'lib\native\x64\Release\crtsys.lib',
  'lib\native\x64\Release\Ldk.lib',
  'lib\native\ARM64\Release\crtsys.lib',
  'lib\native\ARM64\Release\Ldk.lib'
)) {
  $fullPath = Join-Path $stagingDirectory $requiredPath
  if (-not (Test-Path $fullPath)) {
    throw "Required prebuilt library is missing: $fullPath. Run scripts\nuget\Build-CrtSysNuGetLibs.ps1 first."
  }
}

Write-Host "Packing crtsys $Version to $OutputDirectory"
& dotnet pack $manifest `
  --output $OutputDirectory `
  --version $Version

if ($LASTEXITCODE -ne 0) {
  throw "dotnet pack failed with exit code $LASTEXITCODE."
}
