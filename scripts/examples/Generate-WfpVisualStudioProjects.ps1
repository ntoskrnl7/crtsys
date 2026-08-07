[CmdletBinding()]
param(
  [switch] $Check
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$wfpRoot = Join-Path $repoRoot 'examples\wfp'
$generatorPath = 'scripts/examples/Generate-WfpVisualStudioProjects.ps1'
$msquicPackageVersion = '2.5.9'

function New-WfpProjectGuid([string] $Identity) {
  $sha256 = [System.Security.Cryptography.SHA256]::Create()
  try {
    $hash = $sha256.ComputeHash(
      [System.Text.Encoding]::UTF8.GetBytes("crtsys/wfp/$Identity"))
  } finally {
    $sha256.Dispose()
  }

  $bytes = [byte[]]::new(16)
  [Array]::Copy($hash, $bytes, $bytes.Length)
  return ([Guid]::new($bytes)).ToString('B').ToUpperInvariant()
}

function ConvertTo-XmlText([string] $Value) {
  return [System.Security.SecurityElement]::Escape($Value)
}

function ConvertTo-RootNamespace([string] $Target) {
  $parts = $Target -split '[^A-Za-z0-9]+' | Where-Object { $_ }
  return ($parts | ForEach-Object {
      $_.Substring(0, 1).ToUpperInvariant() + $_.Substring(1)
    }) -join ''
}

function New-WfpSample {
  param(
    [Parameter(Mandatory)] [string] $Path,
    [Parameter(Mandatory)] [string] $Driver,
    [Parameter(Mandatory)] [string[]] $DriverSources,
    [Parameter(Mandatory)] [string] $Application,
    [Parameter(Mandatory)] [string[]] $ApplicationSources,
    [bool] $DriverContentCodecs = $false,
    [bool] $DriverMsQuic = $false,
    [bool] $ApplicationContentCodecs = $false,
    [bool] $ApplicationMsQuic = $false,
    [string[]] $ApplicationDefinitions = @(),
    [object[]] $AdditionalApplications = @()
  )

  return [pscustomobject]@{
    Path = $Path
    Driver = $Driver
    DriverSources = $DriverSources
    Application = $Application
    ApplicationSources = $ApplicationSources
    DriverContentCodecs = $DriverContentCodecs
    DriverMsQuic = $DriverMsQuic
    ApplicationContentCodecs = $ApplicationContentCodecs
    ApplicationMsQuic = $ApplicationMsQuic
    ApplicationDefinitions = @($ApplicationDefinitions)
    AdditionalApplications = @($AdditionalApplications)
  }
}

function New-WfpApplication {
  param(
    [Parameter(Mandatory)] [string] $Target,
    [Parameter(Mandatory)] [string[]] $Sources,
    [bool] $ContentCodecs = $false,
    [bool] $MsQuic = $false,
    [string[]] $Definitions = @()
  )

  return [pscustomobject]@{
    Target = $Target
    Sources = $Sources
    ContentCodecs = $ContentCodecs
    MsQuic = $MsQuic
    Definitions = @($Definitions)
  }
}

$samples = @(
  New-WfpSample -Path 'user\browser-https-inspection' `
    -Driver 'crtsys_wfp_browser_https_inspection' `
    -DriverSources @('driver\main.cpp') `
    -Application 'crtsys_wfp_browser_https_inspection_controller' `
    -ApplicationSources @(
      'app\browser_log.cpp',
      'app\browser_policy.cpp',
      'app\browser_policy_diagnostics.cpp',
      'app\browser_proxy.cpp',
      'app\browser_runtime.cpp',
      'app\browser_tunnels.cpp',
      'app\main.cpp') `
    -ApplicationContentCodecs $true `
    -AdditionalApplications @(
      (New-WfpApplication `
        -Target 'crtsys_wfp_browser_https_inspection_http3_proxy_service' `
        -Sources @(
          'app\browser_log.cpp',
          'app\browser_policy.cpp',
          'app\http3_inspection.cpp',
          'app\http3_live_proxy.cpp',
          'app\http3_origin.cpp',
          'app\http3_proxy_service.cpp',
          'app\http3_service_main.cpp') `
        -ContentCodecs $true -MsQuic $true))

  New-WfpSample -Path 'user\connect-redirect' `
    -Driver 'crtsys_wfp_connect_redirect' `
    -DriverSources @('driver\main.cpp') `
    -Application 'crtsys_wfp_connect_redirect_proxy_service' `
    -ApplicationSources @('app\main.cpp')

  New-WfpSample -Path 'user\http3-inspection' `
    -Driver 'crtsys_wfp_http3_inspection_driver' `
    -DriverSources @('driver\main.cpp') `
    -Application 'crtsys_wfp_http3_inspection_service' `
    -ApplicationSources @(
      'app\main.cpp',
      'app\http3_service.cpp',
      'app\wfp_gate.cpp') `
    -ApplicationContentCodecs $true -ApplicationMsQuic $true

  New-WfpSample -Path 'user\tcp-content-filter' `
    -Driver 'crtsys_wfp_tcp_content_filter' `
    -DriverSources @('driver\main.cpp') `
    -Application 'crtsys_wfp_tcp_content_filter_policy_service' `
    -ApplicationSources @('app\main.cpp')

  New-WfpSample -Path 'user\tls-inspection-proxy' `
    -Driver 'crtsys_wfp_tls_inspection_proxy' `
    -DriverSources @('driver\main.cpp') `
    -Application 'crtsys_wfp_tls_inspection_proxy_service' `
    -ApplicationSources @(
      'app\inspection_policy.cpp',
      'app\main.cpp',
      'app\proxy_engine.cpp')

  New-WfpSample -Path 'user\udp-content-filter' `
    -Driver 'crtsys_wfp_udp_content_filter' `
    -DriverSources @('driver\main.cpp') `
    -Application 'crtsys_wfp_udp_content_filter_policy_service' `
    -ApplicationSources @('app\main.cpp')

  New-WfpSample -Path 'kernel\ale-connect-block' `
    -Driver 'crtsys_wfp_ale_connect_block' `
    -DriverSources @('driver\main.cpp') `
    -Application 'crtsys_wfp_ale_connect_block_controller' `
    -ApplicationSources @('app\main.cpp')

  New-WfpSample -Path 'kernel\async-inspection' `
    -Driver 'crtsys_wfp_async_inspection' `
    -DriverSources @('driver\main.cpp') `
    -Application 'crtsys_wfp_async_inspection_controller' `
    -ApplicationSources @('app\main.cpp')

  New-WfpSample -Path 'kernel\bind-redirect' `
    -Driver 'crtsys_wfp_bind_redirect' `
    -DriverSources @('driver\main.cpp') `
    -Application 'crtsys_wfp_bind_redirect_controller' `
    -ApplicationSources @('app\main.cpp')

  New-WfpSample -Path 'kernel\browser-https-inspection' `
    -Driver 'crtsys_wfp_kernel_browser_https_inspection' `
    -DriverSources @('driver\main.cpp') `
    -Application 'crtsys_wfp_kernel_browser_https_inspection_controller' `
    -ApplicationSources @(
      'app\main.cpp',
      'app\controller.cpp',
      'app\control_server.cpp',
      'app\browser_policy.cpp',
      'app\capture_log.cpp',
      'app\certificate_authority.cpp',
      'app\certificate_store.cpp',
      'app\identity_provisioner.cpp',
      'app\kernel_tls_service.cpp',
      'app\managed_policy.cpp') `
    -ApplicationDefinitions @('NOMINMAX', 'WIN32_LEAN_AND_MEAN') `
    -DriverContentCodecs $true -DriverMsQuic $true

  New-WfpSample -Path 'kernel\connect-redirect' `
    -Driver 'crtsys_wfp_kernel_connect_redirect' `
    -DriverSources @('driver\main.cpp') `
    -Application 'crtsys_wfp_kernel_connect_redirect_controller' `
    -ApplicationSources @('app\main.cpp')

  New-WfpSample -Path 'kernel\datagram-proxy' `
    -Driver 'crtsys_wfp_datagram_proxy' `
    -DriverSources @('driver\main.cpp') `
    -Application 'crtsys_wfp_datagram_proxy_controller' `
    -ApplicationSources @('app\main.cpp')

  New-WfpSample -Path 'kernel\flow-monitor' `
    -Driver 'crtsys_wfp_flow_monitor' `
    -DriverSources @('driver\main.cpp') `
    -Application 'crtsys_wfp_flow_monitor_controller' `
    -ApplicationSources @('app\main.cpp')

  New-WfpSample -Path 'kernel\http3-inspection' `
    -Driver 'crtsys_wfp_kernel_http3_inspection' `
    -DriverSources @('driver\main.cpp') `
    -Application 'crtsys_wfp_kernel_http3_inspection_controller' `
    -ApplicationSources @('app\controller.cpp', 'app\main.cpp') `
    -DriverContentCodecs $true -DriverMsQuic $true

  New-WfpSample -Path 'kernel\specialized-observation' `
    -Driver 'crtsys_wfp_specialized_observation' `
    -DriverSources @('driver\main.cpp') `
    -Application 'crtsys_wfp_specialized_observation_controller' `
    -ApplicationSources @('app\main.cpp')

  New-WfpSample -Path 'kernel\stream-edit' `
    -Driver 'crtsys_wfp_stream_edit' `
    -DriverSources @('driver\main.cpp') `
    -Application 'crtsys_wfp_stream_edit_controller' `
    -ApplicationSources @('app\main.cpp')

  New-WfpSample -Path 'kernel\tcp-content-filter' `
    -Driver 'crtsys_wfp_kernel_tcp_content_filter' `
    -DriverSources @('driver\main.cpp') `
    -Application 'crtsys_wfp_kernel_tcp_content_filter_controller' `
    -ApplicationSources @('app\main.cpp')

  New-WfpSample -Path 'kernel\tls-inspection-proxy' `
    -Driver 'crtsys_wfp_kernel_tls_inspection_proxy' `
    -DriverSources @(
      'driver\inspection_policy.cpp',
      'driver\main.cpp') `
    -Application 'crtsys_wfp_kernel_tls_inspection_proxy_controller' `
    -ApplicationSources @('app\main.cpp')

  New-WfpSample -Path 'kernel\udp-content-filter' `
    -Driver 'crtsys_wfp_kernel_udp_content_filter' `
    -DriverSources @('driver\main.cpp') `
    -Application 'crtsys_wfp_kernel_udp_content_filter_controller' `
    -ApplicationSources @('app\main.cpp')
)

function New-ProjectConfigurationsXml {
  return @'
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64">
      <Configuration>Debug</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64">
      <Configuration>Release</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
  </ItemGroup>
'@
}

function New-PackageReferenceXml([bool] $MsQuic) {
  $result = @'
  <ItemGroup Condition="'$(CrtSysUsePackageReference)' == 'true'">
    <PackageReference Include="crtsys" Version="$(CrtSysPackageVersion)" />
  </ItemGroup>
'@
  if ($MsQuic) {
    $result += @"
  <ItemGroup>
    <PackageReference Include="Microsoft.Native.Quic.MsQuic.Schannel" Version="$msquicPackageVersion" />
  </ItemGroup>
"@
  }
  return $result
}

function New-SourceItemsXml([string[]] $Sources, [bool] $Driver) {
  $items = $Sources | ForEach-Object {
    "    <ClCompile Include=`"$(ConvertTo-XmlText $_)`" />"
  }
  $headerRoots = if ($Driver) {
    @('driver\**\*.h', 'driver\**\*.hpp', 'shared\**\*.h', 'shared\**\*.hpp')
  } else {
    @('app\**\*.h', 'app\**\*.hpp', 'shared\**\*.h', 'shared\**\*.hpp')
  }
  $headers = $headerRoots | ForEach-Object {
    "    <ClInclude Include=`"$_`" />"
  }
  return @"
  <ItemGroup>
$($items -join "`r`n")
  </ItemGroup>
  <ItemGroup>
$($headers -join "`r`n")
  </ItemGroup>
"@
}

function New-DriverProjectXml($Sample) {
  $target = $Sample.Driver
  $guid = New-WfpProjectGuid "$($Sample.Path)/$target"
  $rootNamespace = ConvertTo-RootNamespace $target
  $contentCodecs = $Sample.DriverContentCodecs.ToString().ToLowerInvariant()
  $msquic = $Sample.DriverMsQuic.ToString().ToLowerInvariant()
  $ntTargetVersion = if ($Sample.DriverMsQuic) {
    '0xA000008'
  } else {
    '0xA000006'
  }
  $inf = "$target.inf"
  if (-not (Test-Path -LiteralPath (Join-Path $wfpRoot "$($Sample.Path)\$inf"))) {
    throw "The WFP Visual Studio manifest expects missing INF: $($Sample.Path)\$inf"
  }
  $additionalOptions = if ($target -match 'browser_https_inspection') {
    '/bigobj %(AdditionalOptions)'
  } else {
    '%(AdditionalOptions)'
  }
  $definitions = if ($Sample.DriverMsQuic) {
    'QUIC_API_ENABLE_PREVIEW_FEATURES;%(PreprocessorDefinitions)'
  } else {
    '%(PreprocessorDefinitions)'
  }
  $sourcesXml = New-SourceItemsXml $Sample.DriverSources $true
  $packagesXml = New-PackageReferenceXml $false
  $configurationsXml = New-ProjectConfigurationsXml

  return @"
<?xml version="1.0" encoding="utf-8"?>
<!-- Generated by $generatorPath. Edit the manifest, not this file. -->
<Project DefaultTargets="Build" ToolsVersion="17.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
$($configurationsXml.TrimEnd())
  <PropertyGroup Label="Globals">
    <BaseIntermediateOutputPath>`$(MSBuildThisFileDirectory)obj\`$(MSBuildProjectName)\</BaseIntermediateOutputPath>
    <MSBuildProjectExtensionsPath>`$(BaseIntermediateOutputPath)</MSBuildProjectExtensionsPath>
    <ProjectGuid>$guid</ProjectGuid>
    <RootNamespace>$rootNamespace</RootNamespace>
    <ProjectName>$target</ProjectName>
    <CrtSysGeneratedWfpProject>true</CrtSysGeneratedWfpProject>
    <CrtSysUseNtlKernelContentCodecs>$contentCodecs</CrtSysUseNtlKernelContentCodecs>
    <CrtSysUseNtlMsQuicHeaders>$msquic</CrtSysUseNtlMsQuicHeaders>
    <CrtSysUseNtlKernelMsQuic>$msquic</CrtSysUseNtlKernelMsQuic>
    <WindowsTargetPlatformVersion Condition="'`$(VisualStudioVersion)' == '18.0'">10.0.28000.0</WindowsTargetPlatformVersion>
    <WindowsTargetPlatformVersion Condition="'`$(WindowsTargetPlatformVersion)' == ''">10.0</WindowsTargetPlatformVersion>
    <_NT_TARGET_VERSION>$ntTargetVersion</_NT_TARGET_VERSION>
    <SkipPackageVerification Condition="'`$(VisualStudioVersion)' == '18.0'">true</SkipPackageVerification>
  </PropertyGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Label="Configuration" Condition="'`$(Configuration)'=='Debug'">
    <TargetVersion>Windows10</TargetVersion>
    <UseDebugLibraries>true</UseDebugLibraries>
    <DriverTargetPlatform>Desktop</DriverTargetPlatform>
    <DriverType>WDM</DriverType>
    <CrtSysWdmEntryPoint>NtlWfp</CrtSysWdmEntryPoint>
    <PlatformToolset>WindowsKernelModeDriver10.0</PlatformToolset>
    <ConfigurationType>Driver</ConfigurationType>
    <SignMode>Off</SignMode>
  </PropertyGroup>
  <PropertyGroup Label="Configuration" Condition="'`$(Configuration)'=='Release'">
    <TargetVersion>Windows10</TargetVersion>
    <UseDebugLibraries>false</UseDebugLibraries>
    <DriverTargetPlatform>Desktop</DriverTargetPlatform>
    <DriverType>WDM</DriverType>
    <CrtSysWdmEntryPoint>NtlWfp</CrtSysWdmEntryPoint>
    <PlatformToolset>WindowsKernelModeDriver10.0</PlatformToolset>
    <ConfigurationType>Driver</ConfigurationType>
    <SignMode>Off</SignMode>
  </PropertyGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.props" />
  <PropertyGroup>
    <TargetName>$target</TargetName>
  </PropertyGroup>
  <ImportGroup Label="PropertySheets">
    <Import Project="`$(UserRootDir)\Microsoft.Cpp.`$(Platform).user.props" Condition="Exists('`$(UserRootDir)\Microsoft.Cpp.`$(Platform).user.props')" />
  </ImportGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalIncludeDirectories>`$(MSBuildThisFileDirectory)driver;`$(MSBuildThisFileDirectory)shared;`$(MSBuildThisFileDirectory)..\common;`$(MSBuildThisFileDirectory)..\..\shared;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>$definitions</PreprocessorDefinitions>
      <AdditionalOptions>$additionalOptions</AdditionalOptions>
      <WarningLevel>Level4</WarningLevel>
      <TreatWarningAsError>true</TreatWarningAsError>
      <LanguageStandard>stdcpplatest</LanguageStandard>
    </ClCompile>
    <Link>
      <AdditionalDependencies>ndis.lib;ksecdd.lib;%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
  </ItemDefinitionGroup>
$($packagesXml.TrimEnd())
$($sourcesXml.TrimEnd())
  <ItemGroup>
    <Inf Include="$inf" />
    <FilesToPackage Include="`$(TargetPath)">
      <PackageRelativeDirectory></PackageRelativeDirectory>
    </FilesToPackage>
  </ItemGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
"@
}

function New-ApplicationProjectXml(
    [string] $SamplePath,
    [string] $Target,
    [string[]] $Sources,
    [bool] $ContentCodecs,
    [bool] $MsQuic,
    [string[]] $Definitions) {
  $guid = New-WfpProjectGuid "$SamplePath/$Target"
  $rootNamespace = ConvertTo-RootNamespace $Target
  $contentCodecsText = $ContentCodecs.ToString().ToLowerInvariant()
  $msquicText = $MsQuic.ToString().ToLowerInvariant()
  $allDefinitions = [System.Collections.Generic.List[string]]::new()
  if ($MsQuic) {
    $allDefinitions.Add('QUIC_API_ENABLE_PREVIEW_FEATURES')
  }
  foreach ($definition in $Definitions) {
    $allDefinitions.Add($definition)
  }
  $allDefinitions.Add('%(PreprocessorDefinitions)')
  $definitions = $allDefinitions -join ';'
  $sourcesXml = New-SourceItemsXml $Sources $false
  $packagesXml = New-PackageReferenceXml $MsQuic
  $configurationsXml = New-ProjectConfigurationsXml

  return @"
<?xml version="1.0" encoding="utf-8"?>
<!-- Generated by $generatorPath. Edit the manifest, not this file. -->
<Project DefaultTargets="Build" ToolsVersion="17.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
$($configurationsXml.TrimEnd())
  <PropertyGroup Label="Globals">
    <BaseIntermediateOutputPath>`$(MSBuildThisFileDirectory)obj\`$(MSBuildProjectName)\</BaseIntermediateOutputPath>
    <MSBuildProjectExtensionsPath>`$(BaseIntermediateOutputPath)</MSBuildProjectExtensionsPath>
    <ProjectGuid>$guid</ProjectGuid>
    <RootNamespace>$rootNamespace</RootNamespace>
    <ProjectName>$Target</ProjectName>
    <CrtSysGeneratedWfpProject>true</CrtSysGeneratedWfpProject>
    <CrtSysUseNtlContentCodecs>$contentCodecsText</CrtSysUseNtlContentCodecs>
    <CrtSysUseNtlMsQuicHeaders>$msquicText</CrtSysUseNtlMsQuicHeaders>
    <CrtSysAllowExternalPackageImports>$msquicText</CrtSysAllowExternalPackageImports>
    <WindowsTargetPlatformVersion Condition="'`$(VisualStudioVersion)' == '18.0'">10.0.28000.0</WindowsTargetPlatformVersion>
    <WindowsTargetPlatformVersion Condition="'`$(WindowsTargetPlatformVersion)' == ''">10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Label="Configuration" Condition="'`$(Configuration)'=='Debug'">
    <ConfigurationType>Application</ConfigurationType>
    <UseDebugLibraries>true</UseDebugLibraries>
    <PlatformToolset Condition="'`$(VisualStudioVersion)' == '18.0'">v145</PlatformToolset>
    <PlatformToolset Condition="'`$(PlatformToolset)' == ''">v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Label="Configuration" Condition="'`$(Configuration)'=='Release'">
    <ConfigurationType>Application</ConfigurationType>
    <UseDebugLibraries>false</UseDebugLibraries>
    <PlatformToolset Condition="'`$(VisualStudioVersion)' == '18.0'">v145</PlatformToolset>
    <PlatformToolset Condition="'`$(PlatformToolset)' == ''">v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.props" />
  <PropertyGroup>
    <TargetName>$Target</TargetName>
  </PropertyGroup>
  <ImportGroup Label="PropertySheets">
    <Import Project="`$(UserRootDir)\Microsoft.Cpp.`$(Platform).user.props" Condition="Exists('`$(UserRootDir)\Microsoft.Cpp.`$(Platform).user.props')" />
  </ImportGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalIncludeDirectories>`$(MSBuildThisFileDirectory)app;`$(MSBuildThisFileDirectory)shared;`$(MSBuildThisFileDirectory)..\common;`$(MSBuildThisFileDirectory)..\..\shared;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>$definitions</PreprocessorDefinitions>
      <ExceptionHandling>Sync</ExceptionHandling>
      <LanguageStandard>stdcpplatest</LanguageStandard>
      <RuntimeLibrary Condition="'`$(Configuration)'=='Debug'">MultiThreadedDebug</RuntimeLibrary>
      <RuntimeLibrary Condition="'`$(Configuration)'=='Release'">MultiThreaded</RuntimeLibrary>
      <WarningLevel>Level4</WarningLevel>
      <TreatWarningAsError>true</TreatWarningAsError>
    </ClCompile>
    <Link>
      <AdditionalDependencies>advapi32.lib;bcrypt.lib;crypt32.lib;fwpuclnt.lib;iphlpapi.lib;ncrypt.lib;secur32.lib;uuid.lib;ws2_32.lib;winhttp.lib;%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
  </ItemDefinitionGroup>
$($packagesXml.TrimEnd())
$($sourcesXml.TrimEnd())
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
"@
}

function New-SolutionText($Sample, [object[]] $Projects) {
  $projectLines = foreach ($project in $Projects) {
    $guid = New-WfpProjectGuid "$($Sample.Path)/$($project.Target)"
    @"
Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "$($project.Target)", "$($project.Target).vcxproj", "$guid"
EndProject
"@
  }

  $configurationLines = foreach ($project in $Projects) {
    $guid = New-WfpProjectGuid "$($Sample.Path)/$($project.Target)"
    @"
		$guid.Debug|x64.ActiveCfg = Debug|x64
		$guid.Debug|x64.Build.0 = Debug|x64
		$guid.Release|x64.ActiveCfg = Release|x64
		$guid.Release|x64.Build.0 = Release|x64
"@
  }

  return @"
Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 17
# Generated by $generatorPath. Edit the manifest, not this file.
VisualStudioVersion = 17.0.31903.59
MinimumVisualStudioVersion = 10.0.40219.1
$($projectLines -join "`r`n")
Global
	GlobalSection(SolutionConfigurationPlatforms) = preSolution
		Debug|x64 = Debug|x64
		Release|x64 = Release|x64
	EndGlobalSection
	GlobalSection(ProjectConfigurationPlatforms) = postSolution
$($configurationLines -join "`r`n")
	EndGlobalSection
	GlobalSection(SolutionProperties) = preSolution
		HideSolutionNode = FALSE
	EndGlobalSection
EndGlobal
"@
}

function ConvertTo-NormalizedContent([string] $Content) {
  return ($Content -replace "`r?`n", "`r`n").TrimEnd() + "`r`n"
}

$expectedFiles = [ordered]@{}
foreach ($sample in $samples) {
  $sampleRoot = Join-Path $wfpRoot $sample.Path
  if (-not (Test-Path -LiteralPath $sampleRoot -PathType Container)) {
    throw "The WFP Visual Studio manifest references missing sample: $($sample.Path)"
  }

  $driverPath = Join-Path $sampleRoot "$($sample.Driver).vcxproj"
  $expectedFiles[$driverPath] = ConvertTo-NormalizedContent (
    New-DriverProjectXml $sample)

  $projects = [System.Collections.Generic.List[object]]::new()
  $projects.Add([pscustomobject]@{
      Target = $sample.Driver
      Kind = 'Driver'
    })

  $appPath = Join-Path $sampleRoot "$($sample.Application).vcxproj"
  $expectedFiles[$appPath] = ConvertTo-NormalizedContent (
    New-ApplicationProjectXml $sample.Path $sample.Application `
      $sample.ApplicationSources $sample.ApplicationContentCodecs `
      $sample.ApplicationMsQuic $sample.ApplicationDefinitions)
  $projects.Add([pscustomobject]@{
      Target = $sample.Application
      Kind = 'Application'
    })

  foreach ($application in $sample.AdditionalApplications) {
    $additionalPath = Join-Path $sampleRoot "$($application.Target).vcxproj"
    $expectedFiles[$additionalPath] = ConvertTo-NormalizedContent (
      New-ApplicationProjectXml $sample.Path $application.Target `
        $application.Sources $application.ContentCodecs $application.MsQuic `
        $application.Definitions)
    $projects.Add([pscustomobject]@{
        Target = $application.Target
        Kind = 'Application'
      })
  }

  $solutionPath = Join-Path $sampleRoot "$($sample.Driver)_vs.sln"
  $expectedFiles[$solutionPath] = ConvertTo-NormalizedContent (
    New-SolutionText $sample $projects)
}

$errors = [System.Collections.Generic.List[string]]::new()
foreach ($entry in $expectedFiles.GetEnumerator()) {
  if ($Check) {
    if (-not (Test-Path -LiteralPath $entry.Key -PathType Leaf)) {
      $errors.Add("Missing generated WFP Visual Studio file: $($entry.Key)")
      continue
    }
    $actual = ConvertTo-NormalizedContent (
      Get-Content -LiteralPath $entry.Key -Raw)
    if ($actual -cne $entry.Value) {
      $errors.Add("Stale generated WFP Visual Studio file: $($entry.Key)")
    }
  } else {
    [System.IO.File]::WriteAllText(
      $entry.Key,
      $entry.Value,
      [System.Text.UTF8Encoding]::new($false))
  }
}

$generatedCandidates = @(
  foreach ($sample in $samples) {
    Get-ChildItem -LiteralPath (Join-Path $wfpRoot $sample.Path) -File |
      Where-Object { $_.Name -like '*.vcxproj' -or $_.Name -like '*_vs.sln' }
  }
)
foreach ($candidate in $generatedCandidates) {
  if ($expectedFiles.Contains($candidate.FullName)) {
    continue
  }
  $content = Get-Content -LiteralPath $candidate.FullName -Raw
  if (-not $content.Contains("Generated by $generatorPath")) {
    continue
  }
  if ($Check) {
    $errors.Add("Unexpected generated WFP Visual Studio file: $($candidate.FullName)")
  } else {
    Remove-Item -LiteralPath $candidate.FullName -Force
  }
}

if ($errors.Count -ne 0) {
  $errors | ForEach-Object { Write-Error $_ }
  throw "WFP Visual Studio project generation check failed with $($errors.Count) error(s)."
}

$verb = if ($Check) { 'Validated' } else { 'Generated' }
Write-Host "$verb $($expectedFiles.Count) WFP Visual Studio project and solution files for $($samples.Count) samples."
