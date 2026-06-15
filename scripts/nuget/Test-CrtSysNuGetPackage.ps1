param(
  [string] $PackageDirectory,

  [Parameter(Mandatory = $true)]
  [string] $Version,

  [ValidateSet('x64', 'ARM64')]
  [string] $Architecture = 'x64',

  [ValidateSet('Release')]
  [string] $Configuration = 'Release',

  [string] $WindowsSdkVersion = '10.0.22621.0',

  [string] $WorkDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

if ([string]::IsNullOrWhiteSpace($PackageDirectory)) {
  $PackageDirectory = Join-Path $repoRoot 'artifacts\nuget'
}

if ([string]::IsNullOrWhiteSpace($WorkDirectory)) {
  $WorkDirectory = Join-Path $repoRoot "artifacts\nuget-consumer-test\$Architecture"
}

$PackageDirectory = (Resolve-Path $PackageDirectory).Path

$packagePath = Join-Path $PackageDirectory "crtsys.$Version.nupkg"
if (-not (Test-Path $packagePath)) {
  throw "NuGet package was not found: $packagePath"
}

$nuget = Get-Command nuget -ErrorAction SilentlyContinue
if (-not $nuget) {
  throw "nuget command was not found. Add NuGet/setup-nuget before running this script."
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
  throw "vswhere.exe was not found: $vswhere"
}

$visualStudioPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if ([string]::IsNullOrWhiteSpace($visualStudioPath)) {
  throw "Visual Studio with MSBuild was not found."
}

$msbuild = Join-Path $visualStudioPath 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path $msbuild)) {
  throw "MSBuild.exe was not found: $msbuild"
}

$windowsKitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$wdkHeader = Join-Path $windowsKitsRoot "Include\$WindowsSdkVersion\km\wdm.h"
if (-not (Test-Path $wdkHeader)) {
  throw "WDK header was not found: $wdkHeader"
}

$wdkPlatform = $Architecture
$wdkKernelLibDirectory = Join-Path $windowsKitsRoot "Lib\$WindowsSdkVersion\km\$wdkPlatform"
if (-not (Test-Path (Join-Path $wdkKernelLibDirectory 'ntoskrnl.lib'))) {
  throw "WDK kernel library directory is missing ntoskrnl.lib: $wdkKernelLibDirectory"
}

$wdkPreprocessorDefinitions = switch ($Architecture) {
  'x64' { '_WIN64;_AMD64_;AMD64;WINNT=1;_WIN32_WINNT=0x0601' }
  'ARM64' { '_ARM64;_ARM64_;ARM64;STD_CALL;WINNT=1;_WIN32_WINNT=0x0601' }
}

$wdkAdditionalDependencies = switch ($Architecture) {
  'x64' { 'ntoskrnl.lib;hal.lib;wmilib.lib;bufferoverflowk.lib' }
  'ARM64' { 'ntoskrnl.lib;hal.lib;wmilib.lib;bufferoverflowfastfailk.lib;arm64rt.lib' }
}

Remove-Item -Recurse -Force -Path $WorkDirectory -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $WorkDirectory | Out-Null

$packagesDirectory = Join-Path $WorkDirectory 'packages'
& $nuget.Source install crtsys `
  -Version $Version `
  -Source $PackageDirectory `
  -OutputDirectory $packagesDirectory `
  -ExcludeVersion `
  -NonInteractive `
  -Verbosity detailed

if ($LASTEXITCODE -ne 0) {
  throw "nuget install failed with exit code $LASTEXITCODE."
}

$packageRoot = Join-Path $packagesDirectory 'crtsys'
foreach ($requiredPath in @(
  'README.md',
  'build\native\crtsys.props',
  'build\native\crtsys.targets',
  "lib\native\$Architecture\Release\crtsys.lib",
  "lib\native\$Architecture\Release\Ldk.lib",
  'include\ntl\driver',
  'docs\ntl-api.md'
)) {
  $fullPath = Join-Path $packageRoot $requiredPath
  if (-not (Test-Path $fullPath)) {
    throw "Installed package is missing expected file: $fullPath"
  }
}

$packageReadme = Get-Content -LiteralPath (Join-Path $packageRoot 'README.md') -Raw -Encoding UTF8
if ($packageReadme -notmatch 'crtsys NuGet Package') {
  throw "Installed package README.md does not look like the NuGet package README."
}

$sourceFile = Join-Path $WorkDirectory 'main.cpp'
@'
#include <string>
#include <ntl/driver>

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;
  driver.on_unload([]() {});
  return ntl::status::ok();
}
'@ | Set-Content -LiteralPath $sourceFile -Encoding UTF8

$projectGuid = [guid]::NewGuid().ToString('B').ToUpperInvariant()
$projectFile = Join-Path $WorkDirectory 'crtsys_nuget_smoke.vcxproj'
$projectXml = @'
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" ToolsVersion="12.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="__CONFIGURATION__|__PLATFORM__">
      <Configuration>__CONFIGURATION__</Configuration>
      <Platform>__PLATFORM__</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <ProjectGuid>__PROJECT_GUID__</ProjectGuid>
    <Keyword>Win32Proj</Keyword>
    <RootNamespace>CrtSysNuGetSmoke</RootNamespace>
    <Configuration Condition="'$(Configuration)' == ''">__CONFIGURATION__</Configuration>
    <Platform Condition="'$(Platform)' == ''">__PLATFORM__</Platform>
    <WindowsTargetPlatformVersion>__WINDOWS_SDK_VERSION__</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Label="Configuration" Condition="'$(Configuration)|$(Platform)'=='__CONFIGURATION__|__PLATFORM__'">
    <TargetVersion>Windows10</TargetVersion>
    <ConfigurationType>Driver</ConfigurationType>
    <UseDebugLibraries>false</UseDebugLibraries>
    <DriverTargetPlatform>Windows Driver</DriverTargetPlatform>
    <PlatformToolset>WindowsKernelModeDriver10.0</PlatformToolset>
    <DriverType>WDM</DriverType>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <PropertyGroup>
    <CrtSysPackageRoot>$([MSBuild]::NormalizeDirectory('$(MSBuildThisFileDirectory)', 'packages', 'crtsys'))</CrtSysPackageRoot>
    <TargetName>crtsys_nuget_smoke</TargetName>
  </PropertyGroup>
  <ImportGroup Label="PropertySheets">
    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="Exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" />
    <Import Project="$(CrtSysPackageRoot)build\native\crtsys.props" Condition="Exists('$(CrtSysPackageRoot)build\native\crtsys.props')" />
  </ImportGroup>
  <ItemGroup Label="WrappedTaskItems" />
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='__CONFIGURATION__|__PLATFORM__'">
    <ClCompile>
      <WarningLevel>Level4</WarningLevel>
      <TreatWarningAsError>true</TreatWarningAsError>
      <LanguageStandard>stdcpp17</LanguageStandard>
      <PreprocessorDefinitions>__WDK_PREPROCESSOR_DEFINITIONS__;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <AdditionalIncludeDirectories>$(WindowsSdkDir)Include\$(WindowsTargetPlatformVersion)\shared;$(WindowsSdkDir)Include\$(WindowsTargetPlatformVersion)\km;$(WindowsSdkDir)Include\$(WindowsTargetPlatformVersion)\km\crt;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <AdditionalOptions>/Zp8 /GF /GR- /EHsc %(AdditionalOptions)</AdditionalOptions>
    </ClCompile>
    <Link>
      <AdditionalLibraryDirectories>$(WindowsSdkDir)Lib\$(WindowsTargetPlatformVersion)\km\__WDK_PLATFORM__;$(WindowsSdkDir)Lib\$(WindowsTargetPlatformVersion)\um\__WDK_PLATFORM__;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <AdditionalDependencies>__WDK_ADDITIONAL_DEPENDENCIES__;%(AdditionalDependencies)</AdditionalDependencies>
      <GenerateManifest>false</GenerateManifest>
      <IgnoreAllDefaultLibraries>true</IgnoreAllDefaultLibraries>
      <SubSystem>Native</SubSystem>
      <AdditionalOptions>/DRIVER /OPT:REF /OPT:ICF /MERGE:_TEXT=.text /MERGE:_PAGE=PAGE /SECTION:INIT,d /VERSION:10.0 %(AdditionalOptions)</AdditionalOptions>
    </Link>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClCompile Include="main.cpp" />
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
  <ImportGroup Label="ExtensionTargets">
    <Import Project="$(CrtSysPackageRoot)build\native\crtsys.targets" Condition="Exists('$(CrtSysPackageRoot)build\native\crtsys.targets')" />
  </ImportGroup>
</Project>
'@

$projectXml = $projectXml.Replace('__CONFIGURATION__', $Configuration)
$projectXml = $projectXml.Replace('__PLATFORM__', $Architecture)
$projectXml = $projectXml.Replace('__PROJECT_GUID__', $projectGuid)
$projectXml = $projectXml.Replace('__WINDOWS_SDK_VERSION__', $WindowsSdkVersion)
$projectXml = $projectXml.Replace('__WDK_PLATFORM__', $wdkPlatform)
$projectXml = $projectXml.Replace('__WDK_PREPROCESSOR_DEFINITIONS__', $wdkPreprocessorDefinitions)
$projectXml = $projectXml.Replace('__WDK_ADDITIONAL_DEPENDENCIES__', $wdkAdditionalDependencies)
$projectXml | Set-Content -LiteralPath $projectFile -Encoding UTF8

Write-Host "Building NuGet consumer smoke project for $Architecture"
& $msbuild $projectFile `
  /m `
  /p:Configuration=$Configuration `
  /p:Platform=$Architecture `
  /p:WindowsTargetPlatformVersion=$WindowsSdkVersion `
  /v:minimal

if ($LASTEXITCODE -ne 0) {
  throw "MSBuild package consumer test failed with exit code $LASTEXITCODE."
}

Write-Host "NuGet consumer smoke test passed for $Architecture."
