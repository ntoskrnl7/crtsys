param(
  [Parameter(Mandatory = $true)]
  [ValidateSet('app', 'driver', 'kmdf-compile', 'kmdf-verifier-stress',
               'kmdf-example-basic', 'kmdf-example-pnp',
               'kmdf-example-echo', 'kmdf-example-bus',
               'kmdf-example-filter-stack', 'kmdf-example-reference',
               'kmdf-example-dma',
               'kmdf-example-usb', 'kmdf-example-wmi',
               'rpc-lifecycle-stress', 'rpc-async', 'rpc-notifications',
               'rpc-security', 'rpc-streaming', 'flt-runtime',
               'flt-cross-bitness-app', 'flt-verifier-stress',
               'wfp-compile', 'wfp-ale-connect-block',
               'wfp-datagram-proxy', 'wfp-async-inspection',
               'wfp-flow-monitor', 'wfp-stream-edit',
               'wfp-connect-redirect', 'wfp-bind-redirect',
               'wfp-tls-inspection-proxy',
               'wfp-browser-https-inspection',
               'wfp-http3-inspection',
               'wfp-udp-content-filter',
               'wfp-tcp-content-filter',
               'wfp-kernel-connect-redirect',
               'wfp-kernel-tls-inspection-proxy',
               'wfp-kernel-browser-https-inspection',
               'wfp-kernel-http3-inspection',
               'wfp-kernel-udp-content-filter',
               'wfp-kernel-tcp-content-filter',
               'wfp-specialized-observation',
               'ntl-net-kernel-contracts')]
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

  [string] $BuildDirectory = '',

  [string] $PrebuiltRoot = $env:CRTSYS_CI_PREBUILT_ROOT,

  [switch] $NoBreakpoint
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sourceDir = if ($Project -like 'kmdf-example-*') {
  $sample = $Project.Substring(('kmdf-example-').Length)
  Join-Path $repoRoot "examples\kmdf\$sample"
} elseif ($Project -eq 'kmdf-verifier-stress') {
  Join-Path $repoRoot 'test\kmdf\verifier-stress'
} elseif ($Project -eq 'kmdf-compile') {
  Join-Path $repoRoot 'test\kmdf\compile'
} elseif ($Project -eq 'flt-runtime') {
  Join-Path $repoRoot 'test\flt\runtime'
} elseif ($Project -eq 'flt-cross-bitness-app') {
  Join-Path $repoRoot 'test\flt\cross-bitness'
} elseif ($Project -eq 'flt-verifier-stress') {
  Join-Path $repoRoot 'test\flt\verifier-stress'
} elseif ($Project -eq 'wfp-compile') {
  Join-Path $repoRoot 'test\wfp\compile'
} elseif ($Project -eq 'ntl-net-kernel-contracts') {
  Join-Path $repoRoot 'test\net\kernel-contracts'
} elseif ($Project -in @(
    'wfp-ale-connect-block',
    'wfp-datagram-proxy',
    'wfp-async-inspection',
    'wfp-flow-monitor',
    'wfp-stream-edit',
    'wfp-bind-redirect',
    'wfp-specialized-observation')) {
  $sample = $Project.Substring(('wfp-').Length)
  Join-Path $repoRoot "examples\wfp\kernel\$sample"
} elseif ($Project -in @(
    'wfp-connect-redirect',
    'wfp-tls-inspection-proxy',
    'wfp-browser-https-inspection',
    'wfp-http3-inspection',
    'wfp-udp-content-filter',
    'wfp-tcp-content-filter')) {
  $sample = $Project.Substring(('wfp-').Length)
  Join-Path $repoRoot "examples\wfp\user\$sample"
} elseif ($Project -like 'wfp-kernel-*') {
  $sample = $Project.Substring(('wfp-kernel-').Length)
  Join-Path $repoRoot "examples\wfp\kernel\$sample"
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

$buildDir = if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
  Join-Path $sourceDir "build_$buildDirSuffix"
} else {
  [IO.Path]::GetFullPath($BuildDirectory)
}

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

if ([string]::IsNullOrWhiteSpace($PrebuiltRoot)) {
  # Make repeated local invocations deterministic when a build directory was
  # previously configured by a CI-style prebuilt build.
  $configureArgs += '-DCRTSYS_USE_PREBUILT=OFF'
} else {
  if ($Project -eq 'driver') {
    throw 'The main driver test requires its source-built crtsys test-hook variant.'
  }
  $PrebuiltRoot = [IO.Path]::GetFullPath($PrebuiltRoot)
  $nativeLibraryRoot = Join-Path $PrebuiltRoot 'lib\native'
  if (-not (Test-Path -LiteralPath $nativeLibraryRoot -PathType Container)) {
    throw "The crtsys prebuilt native library root was not found: $nativeLibraryRoot"
  }
  $configureArgs += @(
    '-DCRTSYS_USE_PREBUILT=ON',
    "-DCRTSYS_ROOT:PATH=$($PrebuiltRoot.Replace('\', '/'))"
  )
}

if ($Project -eq 'driver' -and $NoBreakpoint) {
  $configureArgs += @(
    '-DCRTSYS_TEST_BREAKPOINT=OFF',
    '-DCRTSYS_ENABLE_DIAGNOSTIC_BREAKPOINTS=OFF'
  )
}

if ($PlatformToolset) {
  $prebuiltDescription = if ([string]::IsNullOrWhiteSpace($PrebuiltRoot)) { 'source crtsys' } else { "prebuilt crtsys from $PrebuiltRoot" }
  Write-Host "Configuring $Project $Architecture $Configuration with Windows SDK $WindowsSdkVersion, WDK $WdkVersion, $PlatformToolset, and $prebuiltDescription"
} else {
  $prebuiltDescription = if ([string]::IsNullOrWhiteSpace($PrebuiltRoot)) { 'source crtsys' } else { "prebuilt crtsys from $PrebuiltRoot" }
  Write-Host "Configuring $Project $Architecture $Configuration with Windows SDK $WindowsSdkVersion, WDK $WdkVersion, and $prebuiltDescription"
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
