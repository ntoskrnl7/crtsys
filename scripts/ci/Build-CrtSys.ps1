param(
  [Parameter(Mandatory = $true)]
  [ValidateSet('app', 'driver', 'kmdf-verifier-stress',
               'rpc-lifecycle-stress', 'rpc-async', 'rpc-notifications',
               'rpc-security', 'rpc-streaming', 'flt-runtime',
               'flt-cross-bitness-app', 'flt-verifier-stress')]
  [string] $Project,

  [Parameter(Mandatory = $true)]
  [ValidateSet('x86', 'x64', 'ARM', 'ARM64')]
  [string] $Architecture,

  [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
  [string] $Configuration = 'Debug',

  [string] $WindowsSdkVersion = '10.0.22621.0',

  [string] $WdkVersion = '',

  [ValidateSet('', 'v142', 'v143', 'v145')]
  [string] $PlatformToolset = '',

  [switch] $NoBreakpoint
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sourceDir = if ($Project -eq 'kmdf-verifier-stress') {
  Join-Path $repoRoot 'test\kmdf\verifier-stress'
} elseif ($Project -eq 'flt-runtime') {
  Join-Path $repoRoot 'test\flt\runtime'
} elseif ($Project -eq 'flt-cross-bitness-app') {
  Join-Path $repoRoot 'test\flt\cross-bitness'
} elseif ($Project -eq 'flt-verifier-stress') {
  Join-Path $repoRoot 'test\flt\verifier-stress'
} elseif ($Project -eq 'rpc-lifecycle-stress') {
  Join-Path $repoRoot 'test\rpc\lifecycle-stress'
} elseif ($Project -eq 'rpc-async') {
  Join-Path $repoRoot 'test\rpc\async'
} elseif ($Project -eq 'rpc-notifications') {
  Join-Path $repoRoot 'test\rpc\notifications'
} elseif ($Project -eq 'rpc-security') {
  Join-Path $repoRoot 'test\rpc\security'
} elseif ($Project -eq 'rpc-streaming') {
  Join-Path $repoRoot 'test\rpc\streaming'
} else {
  Join-Path $repoRoot "test\cmake\$Project"
}

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
$generatorPlatform = "$platform,version=$WindowsSdkVersion"
if ([string]::IsNullOrWhiteSpace($WdkVersion)) {
  $WdkVersion = $WindowsSdkVersion
}

$buildDirSuffix = $Architecture
if ($PlatformToolset) {
  $buildDirSuffix = "${Architecture}_${PlatformToolset}"
}

$buildDir = Join-Path $sourceDir "build_$buildDirSuffix"

$generatorToolset = 'host=x64'
$generator = 'Visual Studio 17 2022'
if ($PlatformToolset) {
  $generatorToolset = "$PlatformToolset,host=x64"
  if ($PlatformToolset -eq 'v145') {
    $generator = 'Visual Studio 18 2026'
    $generatorToolset = 'host=x64'
  }
}

$configureArgs = @(
  '-S', $sourceDir,
  '-B', $buildDir,
  '-G', $generator,
  '-A', $generatorPlatform,
  '-T', $generatorToolset,
  "-DCRTSYS_WDK_VERSION=$WdkVersion",
  "-DLDK_WDK_VERSION=$WdkVersion",
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
  Write-Host "Configuring $Project $Architecture $Configuration with Windows SDK $WindowsSdkVersion, WDK $WdkVersion, and $PlatformToolset"
} else {
  Write-Host "Configuring $Project $Architecture $Configuration with Windows SDK $WindowsSdkVersion and WDK $WdkVersion"
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
