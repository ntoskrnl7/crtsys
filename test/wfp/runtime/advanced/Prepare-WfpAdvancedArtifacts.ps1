[CmdletBinding()]
param(
  [ValidateSet('Debug', 'Release')]
  [string] $Configuration = 'Release',

  [ValidateSet('v143', 'v145')]
  [string] $PlatformToolset = 'v145',

  [ValidateSet('x64', 'ARM64')]
  [string] $Architecture = 'x64',

  [string] $WindowsSdkVersion = '10.0.22621.0',

  [string] $BuildRoot = '',

  [string] $PrebuiltRoot = '',

  [string] $OutputRoot = '',

  [string] $MsQuicDllPath = '',

  [string] $MsQuicPackageVersion = '2.5.9',

  [string] $NuGetExe = '',

  [ValidateSet('all', 'datagram-proxy', 'async-inspection',
               'flow-monitor', 'stream-edit', 'connect-redirect',
               'bind-redirect', 'tls-inspection-proxy',
               'http3-inspection',
               'udp-content-filter', 'tcp-content-filter',
               'specialized-observation',
               'kernel-connect-redirect',
               'kernel-tls-inspection-proxy',
               'kernel-browser-https-inspection',
               'kernel-http3-inspection',
               'kernel-udp-content-filter',
               'kernel-tcp-content-filter')]
  [string[]] $SelectedWfpSample = @('all'),

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
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
  $sdkDirectory = $WindowsSdkVersion -replace '[^A-Za-z0-9]+', '_'
  $BuildRoot = Join-Path $repoRoot (
      "artifacts\b\wfp-advanced-runtime\$PlatformToolset\$Architecture\$sdkDirectory")
}

$samples = @(
  [pscustomobject]@{
    Project = 'wfp-datagram-proxy'
    Runtime = 'kernel'
    Directory = 'datagram-proxy'
    BaseName = 'crtsys_wfp_datagram_proxy'
    ControllerBaseName = 'crtsys_wfp_datagram_proxy_controller'
    AcceptanceBaseName = 'crtsys_wfp_datagram_proxy_acceptance'
    AdditionalDrivers = @(
      [pscustomobject]@{
        BaseName =
            'crtsys_wfp_datagram_proxy_fragmented_buffer_contract'
        Service =
            'CrtSysWfpDatagramFragmentedBufferContractAcceptance'
      }
    )
  },
  [pscustomobject]@{
    Project = 'wfp-async-inspection'
    Runtime = 'kernel'
    Directory = 'async-inspection'
    BaseName = 'crtsys_wfp_async_inspection'
    ControllerBaseName = 'crtsys_wfp_async_inspection_controller'
    AcceptanceBaseName = 'crtsys_wfp_async_inspection_acceptance'
  },
  [pscustomobject]@{
    Project = 'wfp-flow-monitor'
    Runtime = 'kernel'
    Directory = 'flow-monitor'
    BaseName = 'crtsys_wfp_flow_monitor'
    ControllerBaseName = 'crtsys_wfp_flow_monitor_controller'
    AcceptanceBaseName = 'crtsys_wfp_flow_monitor_acceptance'
  },
  [pscustomobject]@{
    Project = 'wfp-stream-edit'
    Runtime = 'kernel'
    Directory = 'stream-edit'
    BaseName = 'crtsys_wfp_stream_edit'
    ControllerBaseName = 'crtsys_wfp_stream_edit_controller'
    AcceptanceBaseName = 'crtsys_wfp_stream_edit_acceptance'
  },
  [pscustomobject]@{
    Project = 'wfp-connect-redirect'
    Runtime = 'user'
    Directory = 'connect-redirect'
    BaseName = 'crtsys_wfp_connect_redirect'
    ControllerBaseName = 'crtsys_wfp_connect_redirect_proxy_service'
    AcceptanceBaseName = 'crtsys_wfp_connect_redirect_acceptance'
  },
  [pscustomobject]@{
    Project = 'wfp-bind-redirect'
    Runtime = 'kernel'
    Directory = 'bind-redirect'
    BaseName = 'crtsys_wfp_bind_redirect'
    ControllerBaseName = 'crtsys_wfp_bind_redirect_controller'
    AcceptanceBaseName = 'crtsys_wfp_bind_redirect_acceptance'
  },
  [pscustomobject]@{
    Project = 'wfp-tls-inspection-proxy'
    Runtime = 'user'
    Directory = 'tls-inspection-proxy'
    BaseName = 'crtsys_wfp_tls_inspection_proxy'
    ControllerBaseName = 'crtsys_wfp_tls_inspection_proxy_service'
    AcceptanceBaseName = 'crtsys_wfp_tls_inspection_proxy_acceptance'
  },
  [pscustomobject]@{
    Project = 'wfp-http3-inspection'
    Runtime = 'user'
    Directory = 'http3-inspection'
    BaseName = 'crtsys_wfp_http3_inspection'
    DriverBaseName = 'crtsys_wfp_http3_inspection_driver'
    ControllerBaseName = 'crtsys_wfp_http3_inspection_service'
    AcceptanceBaseName = 'crtsys_wfp_http3_inspection_acceptance'
  },
  [pscustomobject]@{
    Project = 'wfp-udp-content-filter'
    Runtime = 'user'
    Directory = 'udp-content-filter'
    BaseName = 'crtsys_wfp_udp_content_filter'
    ControllerBaseName = 'crtsys_wfp_udp_content_filter_policy_service'
    AcceptanceBaseName = 'crtsys_wfp_udp_content_filter_acceptance'
  },
  [pscustomobject]@{
    Project = 'wfp-tcp-content-filter'
    Runtime = 'user'
    Directory = 'tcp-content-filter'
    BaseName = 'crtsys_wfp_tcp_content_filter'
    ControllerBaseName = 'crtsys_wfp_tcp_content_filter_policy_service'
    AcceptanceBaseName = 'crtsys_wfp_tcp_content_filter_acceptance'
  },
  [pscustomobject]@{
    Project = 'wfp-specialized-observation'
    Runtime = 'kernel'
    Directory = 'specialized-observation'
    BaseName = 'crtsys_wfp_specialized_observation'
    ControllerBaseName = 'crtsys_wfp_specialized_observation_controller'
    AcceptanceBaseName = 'crtsys_wfp_specialized_observation_acceptance'
  },
  [pscustomobject]@{
    Project = 'wfp-kernel-connect-redirect'
    Runtime = 'kernel'
    Directory = 'kernel-connect-redirect'
    SourceDirectory = 'connect-redirect'
    BaseName = 'crtsys_wfp_kernel_connect_redirect'
    ControllerBaseName = 'crtsys_wfp_kernel_connect_redirect_controller'
    AcceptanceBaseName = 'crtsys_wfp_kernel_connect_redirect_acceptance'
  },
  [pscustomobject]@{
    Project = 'wfp-kernel-tls-inspection-proxy'
    Runtime = 'kernel'
    Directory = 'kernel-tls-inspection-proxy'
    SourceDirectory = 'tls-inspection-proxy'
    BaseName = 'crtsys_wfp_kernel_tls_inspection_proxy'
    ControllerBaseName = 'crtsys_wfp_kernel_tls_inspection_proxy_controller'
    AcceptanceBaseName = 'crtsys_wfp_kernel_tls_inspection_proxy_acceptance'
  },
  [pscustomobject]@{
    Project = 'wfp-kernel-browser-https-inspection'
    Runtime = 'kernel'
    Directory = 'kernel-browser-https-inspection'
    SourceDirectory = 'browser-https-inspection'
    BaseName = 'crtsys_wfp_kernel_browser_https_inspection'
    ControllerBaseName =
        'crtsys_wfp_kernel_browser_https_inspection_controller'
    AcceptanceBaseName =
        'crtsys_wfp_kernel_browser_https_inspection_acceptance'
  },
  [pscustomobject]@{
    Project = 'wfp-kernel-http3-inspection'
    Runtime = 'kernel'
    Directory = 'kernel-http3-inspection'
    SourceDirectory = 'http3-inspection'
    BaseName = 'crtsys_wfp_kernel_http3_inspection'
    ControllerBaseName = 'crtsys_wfp_kernel_http3_inspection_controller'
    AcceptanceBaseName = 'crtsys_wfp_kernel_http3_inspection_acceptance'
  },
  [pscustomobject]@{
    Project = 'wfp-kernel-udp-content-filter'
    Runtime = 'kernel'
    Directory = 'kernel-udp-content-filter'
    SourceDirectory = 'udp-content-filter'
    BaseName = 'crtsys_wfp_kernel_udp_content_filter'
    ControllerBaseName = 'crtsys_wfp_kernel_udp_content_filter_controller'
    AcceptanceBaseName = 'crtsys_wfp_kernel_udp_content_filter_acceptance'
  },
  [pscustomobject]@{
    Project = 'wfp-kernel-tcp-content-filter'
    Runtime = 'kernel'
    Directory = 'kernel-tcp-content-filter'
    SourceDirectory = 'tcp-content-filter'
    BaseName = 'crtsys_wfp_kernel_tcp_content_filter'
    ControllerBaseName = 'crtsys_wfp_kernel_tcp_content_filter_controller'
    AcceptanceBaseName = 'crtsys_wfp_kernel_tcp_content_filter_acceptance'
  }
)
if ($SelectedWfpSample -notcontains 'all') {
  $samples = @(
    $samples |
        Where-Object { $SelectedWfpSample -contains $_.Directory }
  )
}
if (-not [string]::IsNullOrWhiteSpace($PrebuiltRoot)) {
  if (-not $SkipBuild) {
    throw 'PrebuiltRoot requires SkipBuild.'
  }
  $PrebuiltRoot = (Resolve-Path -LiteralPath $PrebuiltRoot).Path
}
if ($samples.Count -eq 0) {
  throw 'No WFP sample was selected for packaging.'
}

function Get-SampleApplicationFileNames([object] $Sample) {
  $hasController =
      $null -ne $Sample.PSObject.Properties['ControllerBaseName']
  $hasAcceptance =
      $null -ne $Sample.PSObject.Properties['AcceptanceBaseName']
  if ($hasController -or $hasAcceptance) {
    if (-not ($hasController -and $hasAcceptance)) {
      throw (
        "$($Sample.Directory) must declare both ControllerBaseName and " +
        'AcceptanceBaseName.')
    }
    return @(
      "$($Sample.ControllerBaseName).exe"
      "$($Sample.AcceptanceBaseName).exe"
    )
  }
  $baseName = if ($Sample.PSObject.Properties['AppBaseName']) {
    $Sample.AppBaseName
  } else {
    $Sample.BaseName
  }
  return @("$baseName`_app.exe")
}

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

Assert-SafeOutputRoot $BuildRoot
Assert-SafeOutputRoot $OutputRoot
$resolvedBuildRoot = [IO.Path]::GetFullPath($BuildRoot)

function Resolve-MsQuicRuntimeDll {
  if (-not [string]::IsNullOrWhiteSpace($MsQuicDllPath)) {
    return (Resolve-Path -LiteralPath $MsQuicDllPath).Path
  }

  $runtimeArchitecture = $Architecture.ToLowerInvariant()
  $dependencyRoot = Join-Path $repoRoot 'artifacts\deps'
  $relativeDll = "build\native\bin\$runtimeArchitecture\msquic.dll"
  $candidates = @(
    Join-Path (Join-Path $dependencyRoot "msquic-$MsQuicPackageVersion") `
        $relativeDll
    Join-Path (Join-Path $dependencyRoot (
        "Microsoft.Native.Quic.MsQuic.Schannel.$MsQuicPackageVersion")) `
        $relativeDll
  )
  foreach ($candidate in $candidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
      return (Resolve-Path -LiteralPath $candidate).Path
    }
  }

  if ([string]::IsNullOrWhiteSpace($NuGetExe)) {
    $repoNuGet = Join-Path $repoRoot 'artifacts\tools\nuget.exe'
    $command = Get-Command nuget.exe -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $repoNuGet -PathType Leaf) {
      $script:NuGetExe = $repoNuGet
    } elseif ($command) {
      $script:NuGetExe = $command.Source
    } else {
      throw (
        'The selected MsQuic-backed WFP sample needs the official ' +
        'MsQuic runtime. ' +
        'Pass -MsQuicDllPath or -NuGetExe.')
    }
  }

  New-Item -ItemType Directory -Force -Path $dependencyRoot | Out-Null
  & $NuGetExe install Microsoft.Native.Quic.MsQuic.Schannel `
      -Version $MsQuicPackageVersion `
      -OutputDirectory $dependencyRoot -NonInteractive
  if ($LASTEXITCODE -ne 0) {
    throw 'Downloading the pinned official MsQuic runtime package failed.'
  }
  foreach ($candidate in $candidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
      return (Resolve-Path -LiteralPath $candidate).Path
    }
  }
  throw (
      "The MsQuic $MsQuicPackageVersion package did not contain " +
      "$relativeDll")
}

$msQuicRuntimeDll = $null
if (($samples.Directory -contains 'http3-inspection') -or
    ($samples.Directory -contains 'kernel-http3-inspection') -or
    ($samples.Directory -contains 'kernel-browser-https-inspection')) {
  $msQuicRuntimeDll = Resolve-MsQuicRuntimeDll
}

if (-not $SkipBuild) {
  foreach ($sample in $samples) {
    $sampleBuildRoot =
        Join-Path $resolvedBuildRoot $sample.Directory
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $buildScript `
        -Project $sample.Project -Architecture $Architecture `
        -Configuration $Configuration `
        -WindowsSdkVersion $WindowsSdkVersion `
        -WdkVersion $WindowsSdkVersion `
        -PlatformToolset $PlatformToolset `
        -BuildDirectory $sampleBuildRoot
    if ($LASTEXITCODE -ne 0) {
      throw "The $($sample.Project) build failed."
    }
  }
}

$resolvedOutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $resolvedOutputRoot) {
  Remove-Item -LiteralPath $resolvedOutputRoot -Recurse -Force
}
$packageRoot = New-Item -ItemType Directory -Force -Path $resolvedOutputRoot
$signedDriverBaseNames = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)

foreach ($sample in $samples) {
  $buildRoot = if ([string]::IsNullOrWhiteSpace($PrebuiltRoot)) {
    Join-Path (
        Join-Path $resolvedBuildRoot $sample.Directory) $Configuration
  } else {
    Join-Path (
        Join-Path $PrebuiltRoot $sample.Project) $Configuration
  }
  $driverBaseName = if ($sample.PSObject.Properties['DriverBaseName']) {
    $sample.DriverBaseName
  } else {
    $sample.BaseName
  }
  $driverSource = Join-Path $buildRoot "$driverBaseName.sys"
  $applicationSources = @(
    Get-SampleApplicationFileNames $sample |
        ForEach-Object { Join-Path $buildRoot $_ }
  )
  $sourceDirectory = if (
      $sample.PSObject.Properties['InfSourceDirectory']) {
    $sample.InfSourceDirectory
  } elseif ($sample.PSObject.Properties['SourceDirectory']) {
    $sample.SourceDirectory
  } else {
    $sample.Directory
  }
  $infSource = Join-Path $repoRoot (
      "examples\wfp\$($sample.Runtime)\$sourceDirectory\" +
      "$driverBaseName.inf")
  foreach ($path in @($driverSource) + $applicationSources + @($infSource)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
      throw "Required WFP runtime artifact was not found: $path"
    }
  }

  $driver = Join-Path $packageRoot.FullName "$driverBaseName.sys"
  foreach ($applicationSource in $applicationSources) {
    Copy-Item -LiteralPath $applicationSource `
        -Destination $packageRoot.FullName
  }
  Copy-Item -LiteralPath $infSource -Destination $packageRoot.FullName

  if ($signedDriverBaseNames.Add($driverBaseName)) {
    Copy-Item -LiteralPath $driverSource -Destination $driver
    $signingRoot =
        Join-Path $resolvedOutputRoot "signing\$($sample.Directory)"
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $signScript `
        -DriverPath $driver -WorkDir $signingRoot
    if ($LASTEXITCODE -ne 0) {
      throw "Signing $driverBaseName.sys failed."
    }
    Copy-Item -LiteralPath (
        Join-Path $signingRoot 'crtsys-test-signing.cer') `
        -Destination (
          Join-Path $packageRoot.FullName "$driverBaseName.cer")
  }

  if ($sample.PSObject.Properties['AdditionalDrivers']) {
    foreach ($additional in $sample.AdditionalDrivers) {
      $additionalBaseName = $additional.BaseName
      $additionalDriverSource =
          Join-Path $buildRoot "$additionalBaseName.sys"
      $additionalInfSource =
          Join-Path $buildRoot "$additionalBaseName.inf"
      foreach ($path in @($additionalDriverSource, $additionalInfSource)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
          throw "Required WFP runtime artifact was not found: $path"
        }
      }
      Copy-Item -LiteralPath $additionalInfSource `
          -Destination $packageRoot.FullName
      if ($signedDriverBaseNames.Add($additionalBaseName)) {
        $additionalDriver =
            Join-Path $packageRoot.FullName "$additionalBaseName.sys"
        Copy-Item -LiteralPath $additionalDriverSource `
            -Destination $additionalDriver
        $additionalSigningRoot = Join-Path $resolvedOutputRoot (
            "signing\$($sample.Directory)-$additionalBaseName")
        & powershell.exe -NoProfile -ExecutionPolicy Bypass `
            -File $signScript -DriverPath $additionalDriver `
            -WorkDir $additionalSigningRoot
        if ($LASTEXITCODE -ne 0) {
          throw "Signing $additionalBaseName.sys failed."
        }
        Copy-Item -LiteralPath (
            Join-Path $additionalSigningRoot 'crtsys-test-signing.cer') `
            -Destination (
              Join-Path $packageRoot.FullName "$additionalBaseName.cer")
      }
    }
  }
}

if ($msQuicRuntimeDll) {
  Copy-Item -LiteralPath $msQuicRuntimeDll -Destination (
      Join-Path $packageRoot.FullName 'msquic.dll')
}

Write-Host "Prepared advanced WFP runtime package: $($packageRoot.FullName)"
[pscustomobject]@{
  Root = $packageRoot.FullName
  Drivers = @($samples | ForEach-Object {
      $baseName = if ($_.PSObject.Properties['DriverBaseName']) {
        $_.DriverBaseName
      } else {
        $_.BaseName
      }
      Join-Path $packageRoot.FullName "$baseName.sys"
      if ($_.PSObject.Properties['AdditionalDrivers']) {
        foreach ($additional in $_.AdditionalDrivers) {
          Join-Path $packageRoot.FullName "$($additional.BaseName).sys"
        }
      }
    } | Select-Object -Unique)
  Applications = @($samples | ForEach-Object {
      foreach ($fileName in Get-SampleApplicationFileNames $_) {
        Join-Path $packageRoot.FullName $fileName
      }
    })
  MsQuicRuntime = if ($msQuicRuntimeDll) {
    Join-Path $packageRoot.FullName 'msquic.dll'
  } else {
    $null
  }
}
