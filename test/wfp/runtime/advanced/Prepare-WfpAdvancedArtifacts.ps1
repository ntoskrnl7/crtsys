[CmdletBinding()]
param(
  [ValidateSet('Debug', 'Release')]
  [string] $Configuration = 'Release',

  [ValidateSet('v143', 'v145')]
  [string] $PlatformToolset = 'v145',

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
  $OutputRoot = Join-Path $repoRoot 'artifacts\wfp-advanced-staging'
}

$samples = @(
  [pscustomobject]@{
    Project = 'wfp-datagram-proxy'
    Directory = 'datagram-proxy'
    BaseName = 'crtsys_wfp_datagram_proxy'
  },
  [pscustomobject]@{
    Project = 'wfp-async-inspection'
    Directory = 'async-inspection'
    BaseName = 'crtsys_wfp_async_inspection'
  },
  [pscustomobject]@{
    Project = 'wfp-flow-monitor'
    Directory = 'flow-monitor'
    BaseName = 'crtsys_wfp_flow_monitor'
  },
  [pscustomobject]@{
    Project = 'wfp-stream-edit'
    Directory = 'stream-edit'
    BaseName = 'crtsys_wfp_stream_edit'
  },
  [pscustomobject]@{
    Project = 'wfp-connect-redirect'
    Directory = 'connect-redirect'
    BaseName = 'crtsys_wfp_connect_redirect'
  },
  [pscustomobject]@{
    Project = 'wfp-bind-redirect'
    Directory = 'bind-redirect'
    BaseName = 'crtsys_wfp_bind_redirect'
  },
  [pscustomobject]@{
    Project = 'wfp-tls-inspection-proxy'
    Directory = 'tls-inspection-proxy'
    BaseName = 'crtsys_wfp_tls_inspection_proxy'
  },
  [pscustomobject]@{
    Project = 'wfp-udp-content-filter'
    Directory = 'udp-content-filter'
    BaseName = 'crtsys_wfp_udp_content_filter'
  },
  [pscustomobject]@{
    Project = 'wfp-tcp-content-filter'
    Directory = 'tcp-content-filter'
    BaseName = 'crtsys_wfp_tcp_content_filter'
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
        -Project $sample.Project -Architecture x64 `
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

foreach ($sample in $samples) {
  $buildRoot = Join-Path $repoRoot (
      "examples\wfp\$($sample.Directory)\build_x64_$PlatformToolset\" +
      $Configuration)
  $driverSource = Join-Path $buildRoot "$($sample.BaseName).sys"
  $appSource = Join-Path $buildRoot "$($sample.BaseName)_app.exe"
  $infSource = Join-Path $repoRoot (
      "examples\wfp\$($sample.Directory)\$($sample.BaseName).inf")
  foreach ($path in @($driverSource, $appSource, $infSource)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
      throw "Required WFP runtime artifact was not found: $path"
    }
  }

  $driver = Join-Path $packageRoot.FullName "$($sample.BaseName).sys"
  Copy-Item -LiteralPath $driverSource -Destination $driver
  Copy-Item -LiteralPath $appSource -Destination $packageRoot.FullName
  Copy-Item -LiteralPath $infSource -Destination $packageRoot.FullName

  $signingRoot =
      Join-Path $resolvedOutputRoot "signing\$($sample.Directory)"
  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $signScript `
      -DriverPath $driver -WorkDir $signingRoot
  if ($LASTEXITCODE -ne 0) {
    throw "Signing $($sample.BaseName).sys failed."
  }
  Copy-Item -LiteralPath (
      Join-Path $signingRoot 'crtsys-test-signing.cer') `
      -Destination (
        Join-Path $packageRoot.FullName "$($sample.BaseName).cer")
}

Write-Host "Prepared advanced WFP runtime package: $($packageRoot.FullName)"
[pscustomobject]@{
  Root = $packageRoot.FullName
  Drivers = @($samples | ForEach-Object {
      Join-Path $packageRoot.FullName "$($_.BaseName).sys"
    })
  Applications = @($samples | ForEach-Object {
      Join-Path $packageRoot.FullName "$($_.BaseName)_app.exe"
    })
}
