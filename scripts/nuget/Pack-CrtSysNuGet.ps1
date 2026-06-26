param(
  [string] $Version,
  [string] $OutputDirectory,
  [string] $NuGetExe
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

foreach ($arch in @('x86', 'x64', 'ARM64')) {
  foreach ($config in @('Debug', 'Release')) {
    foreach ($library in @('crtsys.lib', 'Ldk.lib')) {
      $requiredPath = "lib\native\$arch\$config\$library"
      $fullPath = Join-Path $stagingDirectory $requiredPath
      if (-not (Test-Path $fullPath)) {
        throw "Required prebuilt library is missing: $fullPath. Run scripts\nuget\Build-CrtSysNuGetLibs.ps1 first."
      }
    }
  }
}

Write-Host "Packing crtsys $Version to $OutputDirectory"
if ([string]::IsNullOrWhiteSpace($NuGetExe)) {
  $nuget = Get-Command nuget -ErrorAction SilentlyContinue
  if (-not $nuget) {
    throw "nuget command was not found. Pass -NuGetExe or add nuget.exe to PATH."
  }

  $NuGetExe = $nuget.Source
} else {
  $NuGetExe = (Resolve-Path $NuGetExe).Path
}

& $NuGetExe pack $manifest `
  -OutputDirectory $OutputDirectory `
  -Version $Version `
  -NonInteractive

if ($LASTEXITCODE -ne 0) {
  throw "nuget pack failed with exit code $LASTEXITCODE."
}
