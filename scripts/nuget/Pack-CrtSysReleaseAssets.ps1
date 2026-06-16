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
  $OutputDirectory = Join-Path $repoRoot 'artifacts\release'
}

$nugetDirectory = Join-Path $repoRoot 'artifacts\nuget'
$stagingDirectory = Join-Path $repoRoot 'artifacts\nuget-staging'
$workDirectory = Join-Path $repoRoot 'artifacts\release-staging'
$bundleRoot = Join-Path $workDirectory "crtsys-$Version"
$nativeZipPath = Join-Path $OutputDirectory "crtsys-$Version-native.zip"
$checksumPath = Join-Path $OutputDirectory "crtsys-$Version-SHA256SUMS.txt"
$packagePath = Join-Path $nugetDirectory "crtsys.$Version.nupkg"
$releasePackagePath = Join-Path $OutputDirectory "crtsys.$Version.nupkg"

if (-not (Test-Path $packagePath)) {
  throw "NuGet package was not found: $packagePath. Run scripts\nuget\Pack-CrtSysNuGet.ps1 first."
}

foreach ($arch in @('x64', 'ARM64')) {
  foreach ($config in @('Debug', 'Release')) {
    foreach ($library in @('crtsys.lib', 'Ldk.lib')) {
      $requiredPath = Join-Path $stagingDirectory "lib\native\$arch\$config\$library"
      if (-not (Test-Path $requiredPath)) {
        throw "Required prebuilt release asset file is missing: $requiredPath."
      }
    }
  }
}

Remove-Item -Recurse -Force -Path $workDirectory -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $bundleRoot | Out-Null

Copy-Item -Path (Join-Path $repoRoot 'README.md') -Destination $bundleRoot -Force
Copy-Item -Path (Join-Path $repoRoot 'LICENSE') -Destination $bundleRoot -Force
Copy-Item -Path (Join-Path $repoRoot 'docs') -Destination (Join-Path $bundleRoot 'docs') -Recurse -Force
Copy-Item -Path (Join-Path $repoRoot 'include') -Destination (Join-Path $bundleRoot 'include') -Recurse -Force
Copy-Item -Path (Join-Path $repoRoot 'cmake') -Destination (Join-Path $bundleRoot 'cmake') -Recurse -Force
Copy-Item -Path (Join-Path $repoRoot 'nuget\build') -Destination (Join-Path $bundleRoot 'build') -Recurse -Force
Copy-Item -Path (Join-Path $stagingDirectory 'lib') -Destination (Join-Path $bundleRoot 'lib') -Recurse -Force

$bundleReadme = @"
# crtsys $Version Native Release Bundle

This archive is an offline-friendly native bundle built by the GitHub Actions
Package workflow.

Contents:

- include/: public and internal compatibility headers
- cmake/: CMake helpers; CrtSys.cmake links prebuilt libraries from this bundle
- build/native/: native MSBuild props and targets from the NuGet package
- lib/native/: prebuilt crtsys.lib and Ldk.lib for x64 and ARM64, Debug and Release
- docs/: repository documentation

The prebuilt driver libraries target Visual Studio 2022 and Windows SDK/WDK
10.0.22621.0. Validate the final driver with the Windows, WDK, SDK, Visual
Studio, architecture, Driver Verifier, and code integrity settings that you
ship.

For Visual Studio/MSBuild consumers, the .nupkg attached to the same GitHub
Release is usually the easiest offline install path.
"@
Set-Content -LiteralPath (Join-Path $bundleRoot 'README.release.md') -Value $bundleReadme -Encoding UTF8

Remove-Item -Force -Path $nativeZipPath -ErrorAction SilentlyContinue
Remove-Item -Force -Path $releasePackagePath -ErrorAction SilentlyContinue
Remove-Item -Force -Path $checksumPath -ErrorAction SilentlyContinue

Compress-Archive -Path $bundleRoot -DestinationPath $nativeZipPath -CompressionLevel Optimal
Copy-Item -Path $packagePath -Destination $releasePackagePath -Force

Get-FileHash -Algorithm SHA256 -Path $nativeZipPath, $releasePackagePath |
  ForEach-Object { "$($_.Hash.ToLowerInvariant())  $([System.IO.Path]::GetFileName($_.Path))" } |
  Set-Content -LiteralPath $checksumPath -Encoding ASCII

Write-Host "Created release assets:"
Get-ChildItem -Path $OutputDirectory -File | Sort-Object FullName | ForEach-Object {
  Write-Host "  $($_.FullName)"
}
