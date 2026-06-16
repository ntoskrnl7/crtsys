param(
  [string] $ReleaseDirectory,

  [Parameter(Mandatory = $true)]
  [string] $Version,

  [ValidateSet('x64', 'ARM64')]
  [string] $Architecture = 'x64',

  [ValidateSet('Debug', 'Release')]
  [string] $Configuration = 'Release',

  [string] $WindowsSdkVersion = '10.0.22621.0',

  [string] $WorkDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

if ([string]::IsNullOrWhiteSpace($ReleaseDirectory)) {
  $ReleaseDirectory = Join-Path $repoRoot 'artifacts\release'
}

if ([string]::IsNullOrWhiteSpace($WorkDirectory)) {
  $WorkDirectory = Join-Path $repoRoot "artifacts\release-consumer-test\$Architecture\$Configuration"
}

$ReleaseDirectory = (Resolve-Path $ReleaseDirectory).Path
$prebuiltZipPath = Join-Path $ReleaseDirectory "crtsys-$Version-prebuilt.zip"
if (-not (Test-Path $prebuiltZipPath)) {
  throw "Prebuilt release zip was not found: $prebuiltZipPath"
}

$platformByArchitecture = @{
  x64 = 'x64'
  ARM64 = 'ARM64'
}
$platform = $platformByArchitecture[$Architecture]

Remove-Item -Recurse -Force -Path $WorkDirectory -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $WorkDirectory | Out-Null

$unpackDirectory = Join-Path $WorkDirectory 'prebuilt'
Expand-Archive -Path $prebuiltZipPath -DestinationPath $unpackDirectory -Force

$bundleRoot = Join-Path $unpackDirectory "crtsys-$Version"
if (-not (Test-Path (Join-Path $bundleRoot 'cmake\CrtSys.cmake'))) {
  if (Test-Path (Join-Path $unpackDirectory 'cmake\CrtSys.cmake')) {
    $bundleRoot = $unpackDirectory
  } else {
    $bundleRoot = Get-ChildItem -Path $unpackDirectory -Directory |
      Where-Object { Test-Path (Join-Path $_.FullName 'cmake\CrtSys.cmake') } |
      Select-Object -First 1 -ExpandProperty FullName
  }
}

if ([string]::IsNullOrWhiteSpace($bundleRoot) -or -not (Test-Path (Join-Path $bundleRoot 'cmake\CrtSys.cmake'))) {
  throw "Unpacked prebuilt release bundle does not contain cmake\CrtSys.cmake."
}

foreach ($requiredPath in @(
  "lib\native\$Architecture\$Configuration\crtsys.lib",
  "lib\native\$Architecture\$Configuration\Ldk.lib",
  'include\ntl\driver',
  'include\.internal\adjust_link_order'
)) {
  $fullPath = Join-Path $bundleRoot $requiredPath
  if (-not (Test-Path $fullPath)) {
    throw "Prebuilt release bundle is missing expected file: $fullPath"
  }
}

$consumerDirectory = Join-Path $WorkDirectory 'consumer'
New-Item -ItemType Directory -Force -Path $consumerDirectory | Out-Null

$cmakeBundleRoot = $bundleRoot.Replace('\', '/')
$cmakeLists = @"
cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_release_asset_smoke LANGUAGES C CXX)

set(CRTSYS_NTL_MAIN ON)
include("$cmakeBundleRoot/cmake/CrtSys.cmake")

crtsys_add_driver(crtsys_release_asset_smoke main.cpp)
"@
Set-Content -LiteralPath (Join-Path $consumerDirectory 'CMakeLists.txt') -Value $cmakeLists -Encoding UTF8

$mainCpp = @'
#include <string>
#include <vector>

#include <ntl/driver>

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;

  std::vector<int> values = {1, 2, 3};
  driver.on_unload([values]() {
    (void)values;
  });

  return ntl::status::ok();
}
'@
Set-Content -LiteralPath (Join-Path $consumerDirectory 'main.cpp') -Value $mainCpp -Encoding UTF8

$buildDirectory = Join-Path $WorkDirectory "build_$Architecture"
$configureArgs = @(
  '-S', $consumerDirectory,
  '-B', $buildDirectory,
  '-G', 'Visual Studio 17 2022',
  '-A', $platform,
  '-T', 'host=x64',
  "-DCMAKE_SYSTEM_VERSION=$WindowsSdkVersion",
  "-DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=$WindowsSdkVersion"
)

Write-Host "Configuring prebuilt release asset consumer for $Architecture $Configuration"
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
  throw "CMake configure failed with exit code $LASTEXITCODE."
}

Write-Host "Building prebuilt release asset consumer for $Architecture $Configuration"
& cmake --build $buildDirectory --config $Configuration --target crtsys_release_asset_smoke --parallel
if ($LASTEXITCODE -ne 0) {
  throw "CMake build failed with exit code $LASTEXITCODE."
}

$driverPath = Join-Path $buildDirectory "$Configuration\crtsys_release_asset_smoke.sys"
if (-not (Test-Path $driverPath)) {
  throw "Prebuilt release asset consumer driver was not produced: $driverPath"
}

Write-Host "Prebuilt release asset consumer test passed: $driverPath"
