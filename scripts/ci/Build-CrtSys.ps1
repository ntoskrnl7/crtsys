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

  [switch] $NoBreakpoint
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sourceDir = Join-Path $repoRoot "test\$Project"

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
$buildDir = Join-Path $sourceDir "build_$Architecture"

$configureArgs = @(
  '-S', $sourceDir,
  '-B', $buildDir,
  '-G', 'Visual Studio 17 2022',
  '-A', $platform,
  '-T', 'host=x64',
  "-DCMAKE_SYSTEM_VERSION=$WindowsSdkVersion",
  "-DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=$WindowsSdkVersion",
  '-DCMAKE_CXX_FLAGS=/MP'
)

if ($Project -eq 'driver' -and $NoBreakpoint) {
  $configureArgs += '-DCRTSYS_TEST_BREAKPOINT=OFF'
}

Write-Host "Configuring $Project $Architecture $Configuration with Windows SDK $WindowsSdkVersion"
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
  throw "CMake configure failed with exit code $LASTEXITCODE."
}

Write-Host "Building $Project $Architecture $Configuration"
& cmake --build $buildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) {
  throw "CMake build failed with exit code $LASTEXITCODE."
}
