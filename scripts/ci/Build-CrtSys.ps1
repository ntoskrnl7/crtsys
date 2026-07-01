param(
  [Parameter(Mandatory = $true)]
  [ValidateSet('app', 'driver')]
  [string] $Project,

  [Parameter(Mandatory = $true)]
  [ValidateSet('x86', 'x64', 'ARM', 'ARM64')]
  [string] $Architecture,

  [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
  [string] $Configuration = 'Debug',

  [string] $WindowsSdkVersion = '10.0.22621.0',

  [ValidateSet('', 'v142', 'v143')]
  [string] $PlatformToolset = '',

  [switch] $NoBreakpoint
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sourceDir = Join-Path $repoRoot "test\cmake\$Project"

if (-not (Test-Path (Join-Path $sourceDir 'CMakeLists.txt'))) {
  throw "CMakeLists.txt was not found under '$sourceDir'."
}

$platformByArchitecture = @{
  x86 = 'Win32'
  x64 = 'x64'
  ARM = 'ARM'
  ARM64 = 'ARM64'
}

$platform = $platformByArchitecture[$Architecture]
$generatorPlatform = $platform
if ($Architecture -eq 'ARM') {
  $generatorPlatform = "$platform,version=$WindowsSdkVersion"
}
$buildDirSuffix = $Architecture
if ($PlatformToolset) {
  $buildDirSuffix = "${Architecture}_${PlatformToolset}"
}

$buildDir = Join-Path $sourceDir "build_$buildDirSuffix"

$generatorToolset = 'host=x64'
if ($PlatformToolset) {
  $generatorToolset = "$PlatformToolset,host=x64"
}

$configureArgs = @(
  '-S', $sourceDir,
  '-B', $buildDir,
  '-G', 'Visual Studio 17 2022',
  '-A', $generatorPlatform,
  '-T', $generatorToolset,
  "-DCRTSYS_WDK_VERSION=$WindowsSdkVersion",
  "-DLDK_WDK_VERSION=$WindowsSdkVersion",
  "-DCMAKE_SYSTEM_VERSION=$WindowsSdkVersion",
  "-DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=$WindowsSdkVersion",
  '-DCMAKE_CXX_FLAGS=/MP'
)

if ($Project -eq 'driver' -and $NoBreakpoint) {
  $configureArgs += @(
    '-DCRTSYS_TEST_BREAKPOINT=OFF',
    '-DCRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS=OFF'
  )
}

if ($PlatformToolset) {
  Write-Host "Configuring $Project $Architecture $Configuration with Windows SDK $WindowsSdkVersion and $PlatformToolset"
} else {
  Write-Host "Configuring $Project $Architecture $Configuration with Windows SDK $WindowsSdkVersion"
}
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
  throw "CMake configure failed with exit code $LASTEXITCODE."
}

Write-Host "Building $Project $Architecture $Configuration"
& cmake --build $buildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) {
  throw "CMake build failed with exit code $LASTEXITCODE."
}
