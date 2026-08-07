[CmdletBinding()]
param(
  [ValidateSet('Debug', 'Release')]
  [string] $Configuration = 'Release',

  [ValidateSet('v143', 'v145')]
  [string] $PlatformToolset = 'v145',

  [ValidateSet('x64', 'ARM64')]
  [string] $Architecture = 'x64',

  [ValidatePattern('^(_[A-Za-z0-9]+)?$')]
  [string] $BuildDirectorySuffix = '',

  [string] $WindowsSdkVersion = '10.0.22621.0',

  [string] $OutputRoot = '',

  [switch] $SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$buildScript = Join-Path $repoRoot 'scripts\ci\Build-CrtSys.ps1'
$signScript = Join-Path $repoRoot 'scripts\ci\TestSign-Driver.ps1'
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
  $OutputRoot = Join-Path $repoRoot 'artifacts\wfp-https-live-staging'
}

$samples = @(
  [pscustomobject]@{
    Project = 'wfp-tls-inspection-proxy'
    Directory = 'tls-inspection-proxy'
    BaseName = 'crtsys_wfp_tls_inspection_proxy'
    ApplicationNames = @(
      'crtsys_wfp_tls_inspection_proxy_service.exe'
      'crtsys_wfp_tls_inspection_proxy_acceptance.exe'
      'crtsys_wfp_tls_inspection_proxy_live_acceptance.exe'
    )
  },
  [pscustomobject]@{
    Project = 'wfp-browser-https-inspection'
    Directory = 'browser-https-inspection'
    BaseName = 'crtsys_wfp_browser_https_inspection'
    ApplicationNames = @(
      'crtsys_wfp_browser_https_inspection_controller.exe'
      'crtsys_wfp_browser_https_inspection_http3_proxy_service.exe'
      'crtsys_wfp_browser_https_inspection_acceptance.exe'
      'crtsys_wfp_browser_https_inspection_managed_client_acceptance.exe'
    )
  }
)

function Assert-SafeOutputRoot([string] $Path) {
  $artifactsRoot =
      [IO.Path]::GetFullPath((Join-Path $repoRoot 'artifacts')).
          TrimEnd('\') + '\'
  $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\') + '\'
  if (-not $fullPath.StartsWith(
      $artifactsRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must stay under $artifactsRoot"
  }
}

if (-not $SkipBuild) {
  foreach ($sample in $samples) {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $buildScript `
        -Project $sample.Project -Architecture $Architecture `
        -Configuration $Configuration `
        -WindowsSdkVersion $WindowsSdkVersion `
        -WdkVersion $WindowsSdkVersion `
        -PlatformToolset $PlatformToolset
    if ($LASTEXITCODE -ne 0) {
      throw "The $($sample.Project) build failed."
    }
  }
}

Assert-SafeOutputRoot $OutputRoot
$resolvedOutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $resolvedOutputRoot) {
  Remove-Item -LiteralPath $resolvedOutputRoot -Recurse -Force
}
$packageRoot = New-Item -ItemType Directory -Force -Path $resolvedOutputRoot

Copy-Item -LiteralPath (
    Join-Path $PSScriptRoot '..\common\DisposableGuestGuard.ps1') `
    -Destination $packageRoot.FullName
Copy-Item -LiteralPath (
    Join-Path $PSScriptRoot 'Run-WfpHttpsLiveTest.ps1') `
    -Destination $packageRoot.FullName
Copy-Item -LiteralPath (
    Join-Path $PSScriptRoot 'Start-WfpBrowserHttpsInspection.ps1') `
    -Destination $packageRoot.FullName
Copy-Item -LiteralPath (
    Join-Path $PSScriptRoot 'Test-WfpBrowserTransportEvidence.ps1') `
    -Destination $packageRoot.FullName
Copy-Item -LiteralPath (
    Join-Path $PSScriptRoot 'Start-ManagedHttp3Inspection.ps1') `
    -Destination $packageRoot.FullName
Copy-Item -LiteralPath (
    Join-Path $PSScriptRoot 'Start-WfpManagedHttp3Inspection.ps1') `
    -Destination $packageRoot.FullName
Copy-Item -LiteralPath (
    Join-Path $PSScriptRoot 'Run-WfpManagedHttp3Suite.ps1') `
    -Destination $packageRoot.FullName
Copy-Item -LiteralPath (
    Join-Path $PSScriptRoot 'Start-ControlledHttp3EndToEnd.ps1') `
    -Destination $packageRoot.FullName
Copy-Item -LiteralPath (
    Join-Path $PSScriptRoot 'CONTROLLED-HTTP3-README.ko-KR.md') `
    -Destination $packageRoot.FullName
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') `
    -Destination $packageRoot.FullName

$drivers = @()
$applications = @()
$certificates = @()
foreach ($sample in $samples) {
  $buildRoot = Join-Path $repoRoot (
      "examples\wfp\user\$($sample.Directory)\" +
      "build_${Architecture}_$PlatformToolset$BuildDirectorySuffix\$Configuration")
  $driverSource = Join-Path $buildRoot "$($sample.BaseName).sys"
  $applicationSources = @(
    $sample.ApplicationNames |
        ForEach-Object { Join-Path $buildRoot $_ }
  )
  $infSource = Join-Path $repoRoot (
      "examples\wfp\user\$($sample.Directory)\$($sample.BaseName).inf")
  foreach ($path in @($driverSource) + $applicationSources + @($infSource)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
      throw "Required live HTTPS artifact was not found: $path"
    }
  }

  $driver =
      Join-Path $packageRoot.FullName "$($sample.BaseName).sys"
  $certificate =
      Join-Path $packageRoot.FullName "$($sample.BaseName).cer"
  Copy-Item -LiteralPath $driverSource -Destination $driver
  foreach ($applicationSource in $applicationSources) {
    $application = Join-Path $packageRoot.FullName (
        Split-Path -Leaf $applicationSource)
    Copy-Item -LiteralPath $applicationSource -Destination $application
    $applications += $application
  }
  Copy-Item -LiteralPath $infSource -Destination $packageRoot.FullName

  $signingRoot =
      Join-Path $resolvedOutputRoot "signing-$($sample.Directory)"
  try {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $signScript `
        -DriverPath $driver -WorkDir $signingRoot
    if ($LASTEXITCODE -ne 0) {
      throw "Signing $($sample.BaseName).sys failed."
    }
    Copy-Item -LiteralPath (
        Join-Path $signingRoot 'crtsys-test-signing.cer') `
        -Destination $certificate
  } finally {
    if (Test-Path -LiteralPath $signingRoot) {
      Remove-Item -LiteralPath $signingRoot -Recurse -Force
    }
  }
  $drivers += $driver
  $certificates += $certificate
}

$browserBuildRoot = Join-Path $repoRoot (
    "examples\wfp\user\browser-https-inspection\" +
    "build_${Architecture}_$PlatformToolset$BuildDirectorySuffix\$Configuration")
$http3RuntimeLibraries = @(
  (Join-Path $browserBuildRoot 'msh3.dll'),
  (Join-Path $browserBuildRoot 'msquic.dll')
)
foreach ($library in $http3RuntimeLibraries) {
  if (-not (Test-Path -LiteralPath $library -PathType Leaf)) {
    throw "Required HTTP/3 runtime library was not found: $library"
  }
  Copy-Item -LiteralPath $library -Destination $packageRoot.FullName
}

$browserCmakeCache =
    Join-Path (Split-Path -Parent $browserBuildRoot) 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $browserCmakeCache -PathType Leaf)) {
  throw "Browser CMake cache was not found: $browserCmakeCache"
}
$msh3SourceMatch =
    Select-String -LiteralPath $browserCmakeCache `
        -Pattern '^msh3_SOURCE_DIR:STATIC=(.+)$' |
        Select-Object -First 1
if (-not $msh3SourceMatch) {
  throw 'The configured msh3 source directory was not found in CMakeCache.'
}
$msh3SourceRoot =
    [IO.Path]::GetFullPath($msh3SourceMatch.Matches[0].Groups[1].Value)
$http3Notices = @(
  [pscustomobject]@{
    Source = Join-Path $msh3SourceRoot 'LICENSE'
    Destination = 'LICENSE.msh3.txt'
  },
  [pscustomobject]@{
    Source = Join-Path $msh3SourceRoot 'msquic\LICENSE'
    Destination = 'LICENSE.msquic.txt'
  },
  [pscustomobject]@{
    Source = Join-Path $msh3SourceRoot 'msquic\THIRD-PARTY-NOTICES'
    Destination = 'THIRD-PARTY-NOTICES.msquic.txt'
  }
)
foreach ($notice in $http3Notices) {
  if (-not (Test-Path -LiteralPath $notice.Source -PathType Leaf)) {
    throw "Required HTTP/3 third-party notice was not found: $($notice.Source)"
  }
  Copy-Item -LiteralPath $notice.Source -Destination (
      Join-Path $packageRoot.FullName $notice.Destination)
}

Write-Host "Prepared live HTTPS runtime package: $($packageRoot.FullName)"
[pscustomobject]@{
  Root = $packageRoot.FullName
  Architecture = $Architecture
  Drivers = $drivers
  Applications = $applications
  DriverCertificates = $certificates
  ControlledTest =
      (Join-Path $packageRoot.FullName 'Run-WfpHttpsLiveTest.ps1')
  BrowserTest =
      (Join-Path $packageRoot.FullName `
          'Start-WfpBrowserHttpsInspection.ps1')
  ManagedHttp3Test =
      (Join-Path $packageRoot.FullName `
          'Start-ManagedHttp3Inspection.ps1')
  WfpManagedHttp3Test =
      (Join-Path $packageRoot.FullName `
          'Run-WfpManagedHttp3Suite.ps1')
  ControlledHttp3EndToEnd =
      (Join-Path $packageRoot.FullName `
          'Start-ControlledHttp3EndToEnd.ps1')
}
