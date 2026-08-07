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
  & cmake --build $BuildDirectory --config $Configuration --target `
      crtsys_install_consumer `
      crtsys_install_kernel_msquic_consumer `
      crtsys_http_transform_consumer `
      --parallel
  if ($LASTEXITCODE -ne 0) {
    throw "$Label build failed with exit code $LASTEXITCODE."
  }

  $driverPath = Join-Path $BuildDirectory "$Configuration\crtsys_install_consumer.sys"
  if (-not (Test-Path $driverPath)) {
    throw "$Label driver was not produced: $driverPath"
  }
  $kernelMsQuicDriverPath = Join-Path $BuildDirectory (
      "$Configuration\crtsys_install_kernel_msquic_consumer.sys")
  if (-not (Test-Path $kernelMsQuicDriverPath)) {
    throw "$Label kernel MsQuic WFP driver was not produced: $kernelMsQuicDriverPath"
  }
  $httpTransformPath = Join-Path $BuildDirectory (
      "$Configuration\crtsys_http_transform_consumer.exe")
  if (-not (Test-Path $httpTransformPath)) {
    throw "$Label HTTP transform consumer was not produced: $httpTransformPath"
  }
  & $httpTransformPath
  if ($LASTEXITCODE -ne 0) {
    throw "$Label HTTP transform consumer failed with exit code $LASTEXITCODE."
  }

  Write-Host "$Label passed: $driverPath; $kernelMsQuicDriverPath; $httpTransformPath"
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
  "include\ntl\net\borrowed_bounded_writer",
  "include\ntl\net\borrowed_memory_resource",
  "include\ntl\net\inspection\standard_content_decoders",
  "include\ntl\net\inspection\standard_content_encoders",
  "include\ntl\net\inspection\content_encoder",
  "include\ntl\net\inspection\content_encoder_brotli",
  "include\ntl\net\inspection\content_encoder_zlib",
  "include\ntl\net\inspection\content_stream",
  "include\ntl\net\websocket\permessage_deflate",
  "include\ntl\net\kernel\all",
  "include\ntl\net\kernel\executor",
  "include\ntl\net\kernel\started_task",
  "include\ntl\net\kernel\workspace_pool",
  "include\ntl\net\kernel\http1_proxy_session",
  "include\ntl\net\kernel\http2_proxy_session",
  "include\ntl\net\user\task",
  "include\ntl\net\user\redirected_tls_session",
  "include\ntl\net\user\redirected_tls_inspection",
  "include\ntl\net\http\http1_transform",
  "include\ntl\net\http\http1_stream_transform",
  "include\ntl\net\http\http1_proxy_connection",
  "include\ntl\net\http\authority",
  "include\ntl\net\http\http1_proxy_types",
  "include\ntl\net\http\inspection_context_view",
  "include\ntl\net\http\inspection_conditions",
  "include\ntl\net\http\decision_policy",
  "include\ntl\net\http\inspection_policy",
  "include\ntl\net\http\async_transform",
  "include\ntl\net\http\stream_transform",
  "include\ntl\net\http\transform",
  "include\ntl\net\grpc\framing",
  "include\ntl\net\grpc\transform",
  "include\ntl\net\http2\transform",
  "include\ntl\net\http2\stream_transform",
  "include\ntl\net\http2\flow_control",
  "include\ntl\net\http2\proxy_connection",
  "include\ntl\net\http2\proxy_session",
  "include\ntl\net\http2\websocket_tunnel",
  "include\ntl\net\http3\async_origin_pool",
  "include\ntl\net\http3\backend",
  "include\ntl\net\http3\inspection_proxy",
  "include\ntl\net\http3\msquic_backend",
  "include\ntl\net\http3\msquic_runtime",
  "include\ntl\net\http3\msquic_server",
  "include\ntl\net\http3\proxy_connection",
  "include\ntl\net\http3\qpack",
  "include\ntl\net\http3\qpack_core",
  "include\ntl\net\http3\standard_inspection_proxy",
  "include\ntl\net\http3\stream_transform",
  "include\ntl\net\http3\webtransport_transform",
  "include\ntl\net\http3\webtransport_session",
  "include\ntl\net\tls\product_backend",
  "include\ntl\net\tls\inspection_frontend",
  "include\ntl\net\tls\product_policy",
  "include\ntl\net\websocket\transform",
  "include\ntl\kmdf\pdo",
  "include\ntl\kmdf\query_interface",
  "include\ntl\kmdf\resource_requirements",
  "include\ntl\wfp\conditions",
  "include\ntl\wfp\telemetry",
  "include\.internal\adjust_link_order",
  "share\crtsys\cmake\crtsys-config.cmake",
  "share\crtsys\cmake\CrtSys.cmake",
  "share\crtsys\cmake\NtlContentCodecs.cmake",
  "share\crtsys\cmake\NtlMsQuic.cmake",
  "lib\native\v143\$Architecture\$Configuration\crtsys.lib",
  "lib\native\v143\$Architecture\$Configuration\Ldk.lib"
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
