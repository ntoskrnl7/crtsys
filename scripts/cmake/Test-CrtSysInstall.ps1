param(
  [ValidateSet('x86', 'x64', 'ARM', 'ARM64')]
  [string] $Architecture = 'x64',

  [ValidateSet('Debug', 'Release')]
  [string] $Configuration = 'Release',

  [string] $WindowsSdkVersion = '10.0.22000.0',

  [string] $WorkDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

if ([string]::IsNullOrWhiteSpace($WorkDirectory)) {
  $WorkDirectory = Join-Path $repoRoot "build_install_$Architecture"
}

$WorkDirectory = [System.IO.Path]::GetFullPath($WorkDirectory)
$repoRootPrefix = $repoRoot.TrimEnd('\') + '\'
if (-not $WorkDirectory.StartsWith($repoRootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "WorkDirectory must be inside the repository: $repoRoot"
}

$platformByArchitecture = @{
  x86 = 'Win32'
  x64 = 'x64'
  ARM = 'ARM'
  ARM64 = 'ARM64'
}
$platform = $platformByArchitecture[$Architecture]
$generatorPlatform = "$platform,version=$WindowsSdkVersion"

$buildDirectory = Join-Path $WorkDirectory 'build'
$installDirectory = Join-Path $WorkDirectory 'prefix'
$consumerSourceDirectory = Join-Path $repoRoot 'test\cmake\install-consumer'
$installConsumerBuildDirectory = Join-Path $WorkDirectory 'install-consumer-build'

Remove-Item -Recurse -Force -Path $WorkDirectory -ErrorAction SilentlyContinue

function Invoke-CrtSysConsumerBuild {
  param(
    [Parameter(Mandatory = $true)]
    [string] $PackageRoot,

    [Parameter(Mandatory = $true)]
    [string] $BuildDirectory,

    [Parameter(Mandatory = $true)]
    [string] $Label
  )

  $cmakePrefix = $PackageRoot.Replace('\', '/')
  $consumerConfigureArgs = @(
    '-S', $consumerSourceDirectory,
    '-B', $BuildDirectory,
    '-G', 'Visual Studio 17 2022',
    '-A', $generatorPlatform,
    '-T', 'host=x64',
    "-DCRTSYS_PACKAGE_ROOT:PATH=$cmakePrefix",
    "-DCRTSYS_WDK_VERSION:STRING=$WindowsSdkVersion",
    "-DLDK_WDK_VERSION:STRING=$WindowsSdkVersion",
    "-DCMAKE_SYSTEM_VERSION:STRING=$WindowsSdkVersion",
    "-DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION:STRING=$WindowsSdkVersion"
  )

  Write-Host "Configuring $Label"
  & cmake @consumerConfigureArgs
  if ($LASTEXITCODE -ne 0) {
    throw "$Label configure failed with exit code $LASTEXITCODE."
  }

  Write-Host "Building $Label"
  & cmake --build $BuildDirectory --config $Configuration --target crtsys_install_consumer --parallel
  if ($LASTEXITCODE -ne 0) {
    throw "$Label build failed with exit code $LASTEXITCODE."
  }

  $driverPath = Join-Path $BuildDirectory "$Configuration\crtsys_install_consumer.sys"
  if (-not (Test-Path $driverPath)) {
    throw "$Label driver was not produced: $driverPath"
  }

  Write-Host "$Label passed: $driverPath"
}

$configureArgs = @(
  '-S', $repoRoot,
  '-B', $buildDirectory,
  '-G', 'Visual Studio 17 2022',
  '-A', $generatorPlatform,
  '-T', 'host=x64',
  "-DCMAKE_INSTALL_PREFIX=$installDirectory",
  "-DCRTSYS_WDK_VERSION=$WindowsSdkVersion",
  "-DLDK_WDK_VERSION=$WindowsSdkVersion",
  "-DCMAKE_SYSTEM_VERSION=$WindowsSdkVersion",
  "-DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=$WindowsSdkVersion"
)

Write-Host "Configuring crtsys install smoke test for $Architecture $Configuration"
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
  throw "CMake configure failed with exit code $LASTEXITCODE."
}

Write-Host "Building crtsys for $Architecture $Configuration"
& cmake --build $buildDirectory --config $Configuration --target crtsys --parallel
if ($LASTEXITCODE -ne 0) {
  throw "CMake build failed with exit code $LASTEXITCODE."
}

Write-Host "Installing crtsys to $installDirectory"
& cmake --install $buildDirectory --config $Configuration
if ($LASTEXITCODE -ne 0) {
  throw "CMake install failed with exit code $LASTEXITCODE."
}

foreach ($requiredPath in @(
  "include\ntl\driver",
  "include\.internal\adjust_link_order",
  "share\crtsys\cmake\crtsys-config.cmake",
  "share\crtsys\cmake\CrtSys.cmake",
  "lib\native\$Architecture\$Configuration\crtsys.lib",
  "lib\native\$Architecture\$Configuration\Ldk.lib"
)) {
  $fullPath = Join-Path $installDirectory $requiredPath
  if (-not (Test-Path $fullPath)) {
    throw "Installed crtsys tree is missing expected file: $fullPath"
  }
}

Invoke-CrtSysConsumerBuild `
  -PackageRoot $installDirectory `
  -BuildDirectory $installConsumerBuildDirectory `
  -Label 'installed crtsys consumer'

Write-Host "crtsys install smoke test passed."
