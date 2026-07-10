param(
  [string] $PackageDirectory,

  [Parameter(Mandatory = $true)]
  [string] $Version,

  [ValidateSet('x86', 'x64', 'ARM', 'ARM64')]
  [string] $Architecture = 'x64',

  [ValidateSet('Debug', 'Release')]
  [string] $Configuration = 'Release',

  [ValidateSet('v142', 'v143', 'v145')]
  [string] $Toolset = 'v143',

  [string] $WindowsSdkVersion = '10.0.22621.0',

  [string] $WdkVersion,

  [string] $WorkDirectory,

  [string] $NuGetExe,

  [ValidateSet('latest', '18', '17', '16', '15')]
  [string] $VisualStudioMajorVersion = 'latest'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function ConvertTo-XmlEscapedText {
  param([Parameter(Mandatory = $true)][string] $Text)
  return [System.Security.SecurityElement]::Escape($Text)
}

function Resolve-MsBuildPath {
  param([Parameter(Mandatory = $true)][string] $RequestedMajor)

  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe was not found: $vswhere"
  }

  $installationsJson = & $vswhere -all -products * -requires Microsoft.Component.MSBuild -format json
  $installations = @(($installationsJson | ConvertFrom-Json) | ForEach-Object { $_ })
  if ($RequestedMajor -ne 'latest') {
    $installations = @(
      $installations |
        Where-Object { ([version]$_.installationVersion).Major -eq [int]$RequestedMajor }
    )
  }

  $installations = @(
    $installations |
      Sort-Object { [version]$_.installationVersion } -Descending
  )
  if ($installations.Count -eq 0) {
    throw "Visual Studio with MSBuild was not found."
  }

  $visualStudioPath = $installations[0].installationPath
  $msbuildCandidates = @(
    Join-Path $visualStudioPath 'MSBuild\Current\Bin\MSBuild.exe'
    Join-Path $visualStudioPath 'MSBuild\15.0\Bin\MSBuild.exe'
  )
  $msbuild = $msbuildCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
  if ([string]::IsNullOrWhiteSpace($msbuild)) {
    throw "MSBuild.exe was not found under: $visualStudioPath"
  }

  Write-Host "Resolved Visual Studio path: $visualStudioPath"
  return $msbuild
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

if ([string]::IsNullOrWhiteSpace($PackageDirectory)) {
  $PackageDirectory = Join-Path $repoRoot 'artifacts\nuget'
}

if ([string]::IsNullOrWhiteSpace($WorkDirectory)) {
  $WorkDirectory = Join-Path $repoRoot "artifacts\nuget-msbuild-consumer-test\$Toolset\$Architecture\$Configuration"
}

$PackageDirectory = (Resolve-Path $PackageDirectory).Path
$packagePath = Join-Path $PackageDirectory "crtsys.$Version.nupkg"
if (-not (Test-Path $packagePath)) {
  throw "NuGet package was not found: $packagePath"
}

if ([string]::IsNullOrWhiteSpace($NuGetExe)) {
  $nuget = Get-Command nuget -ErrorAction SilentlyContinue
  if (-not $nuget) {
    throw "nuget command was not found. Pass -NuGetExe or add nuget.exe to PATH."
  }

  $NuGetExe = $nuget.Source
} else {
  $NuGetExe = (Resolve-Path $NuGetExe).Path
}

$programFilesX86 = ${env:ProgramFiles(x86)}
$windowsKitsRoot = Join-Path $programFilesX86 'Windows Kits\10'
$windowsKitsIncludeRoot = Join-Path $windowsKitsRoot 'Include'
if (-not (Test-Path $windowsKitsIncludeRoot)) {
  throw "Windows Kits include directory was not found: $windowsKitsIncludeRoot"
}

if ([string]::IsNullOrWhiteSpace($WdkVersion)) {
  $preferredWdkHeader = Join-Path $windowsKitsIncludeRoot "$WindowsSdkVersion\km\wdm.h"
  if (Test-Path $preferredWdkHeader) {
    $WdkVersion = $WindowsSdkVersion
  } else {
    $wdkVersionCandidates = @(
      Get-ChildItem -Path $windowsKitsIncludeRoot -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
        Where-Object { Test-Path (Join-Path $_.FullName 'km\wdm.h') } |
        Sort-Object { [version]$_.Name }
    )

    if ($wdkVersionCandidates.Count -eq 0) {
      throw "No installed WDK with km\wdm.h was found under $windowsKitsIncludeRoot."
    }

    $WdkVersion = $wdkVersionCandidates[-1].Name
  }
}

$wdkPlatformByArchitecture = @{
  x86 = 'x86'
  x64 = 'x64'
  ARM = 'arm'
  ARM64 = 'arm64'
}
$wdkKernelLibDirectory = Join-Path $windowsKitsRoot "Lib\$WdkVersion\km\$($wdkPlatformByArchitecture[$Architecture])"
foreach ($requiredLib in @('ntoskrnl.lib', 'hal.lib', 'wmilib.lib', 'libcntpr.lib', 'cng.lib', 'aux_klib.lib')) {
  if (-not (Test-Path (Join-Path $wdkKernelLibDirectory $requiredLib))) {
    throw "WDK kernel library directory is missing $requiredLib`: $wdkKernelLibDirectory"
  }
}
$securityRuntimePath = $null
foreach ($securityRuntime in @('BufferOverflowK.lib', 'bufferoverflowfastfailk.lib')) {
  $candidateSecurityRuntimePath = Join-Path $wdkKernelLibDirectory $securityRuntime
  if (Test-Path $candidateSecurityRuntimePath) {
    $securityRuntimePath = $candidateSecurityRuntimePath
    break
  }
}
if ([string]::IsNullOrWhiteSpace($securityRuntimePath)) {
  throw "WDK kernel library directory is missing BufferOverflowK.lib or bufferoverflowfastfailk.lib: $wdkKernelLibDirectory"
}

Write-Host "Requested Windows SDK version: $WindowsSdkVersion"
Write-Host "Resolved WDK version: $WdkVersion"

if ($Toolset -eq 'v142' -and $Architecture -eq 'ARM64' -and [version]$WdkVersion -ge [version]'10.0.26100.0') {
  throw "VS2019 v142 ARM64 cannot consume WDK $WdkVersion headers here; WDK 10.0.26100.0 references ARM64 intrinsics that v142 does not provide. Install/use WDK 10.0.22621.0 for this smoke test."
}

if ($WdkVersion -ne $WindowsSdkVersion) {
  Write-Host "Requested WDK $WindowsSdkVersion was not usable for $Architecture. Using WDK $WdkVersion."
}

$WindowsSdkVersion = $WdkVersion

$msbuild = Resolve-MsBuildPath -RequestedMajor $VisualStudioMajorVersion

$WorkDirectory = [System.IO.Path]::GetFullPath($WorkDirectory)
$repoRootPrefix = $repoRoot.TrimEnd('\') + '\'
if (-not $WorkDirectory.StartsWith($repoRootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "WorkDirectory must be inside the repository: $repoRoot"
}

Remove-Item -Recurse -Force -Path $WorkDirectory -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $WorkDirectory | Out-Null

$projectDirectory = Join-Path $WorkDirectory 'consumer'
$packagesDirectory = Join-Path $WorkDirectory 'packages'
New-Item -ItemType Directory -Force -Path $projectDirectory | Out-Null

& $NuGetExe install crtsys `
  -Version $Version `
  -Source $PackageDirectory `
  -OutputDirectory $packagesDirectory `
  -NonInteractive `
  -Verbosity detailed

if ($LASTEXITCODE -ne 0) {
  throw "nuget install failed with exit code $LASTEXITCODE."
}

$packageRoot = Join-Path $packagesDirectory "crtsys.$Version"
if (-not (Test-Path $packageRoot)) {
  $installedPackageCandidates = @(
    Get-ChildItem -Path $packagesDirectory -Directory |
      Where-Object { $_.Name -like 'crtsys.*' } |
      Sort-Object Name
  )

  if ($installedPackageCandidates.Count -ne 1) {
    throw "Expected exactly one installed crtsys package under '$packagesDirectory', found $($installedPackageCandidates.Count)."
  }

  $packageRoot = $installedPackageCandidates[0].FullName
}

$packageRoot = (Resolve-Path $packageRoot).Path

foreach ($requiredPath in @(
  'build\native\crtsys.props',
  'build\native\crtsys.targets',
  'include\ntl\driver',
  "build\native\lib\native\$Toolset\$Architecture\$Configuration\crtsys.lib",
  "build\native\lib\native\$Toolset\$Architecture\$Configuration\Ldk.lib"
)) {
  $fullPath = Join-Path $packageRoot $requiredPath
  if (-not (Test-Path $fullPath)) {
    throw "Installed package is missing expected file: $fullPath"
  }
}

$platformByArchitecture = @{
  x86 = 'Win32'
  x64 = 'x64'
  ARM = 'ARM'
  ARM64 = 'ARM64'
}
$machineByArchitecture = @{
  x86 = 'X86'
  x64 = 'X64'
  ARM = 'ARM'
  ARM64 = 'ARM64'
}
$definesByArchitecture = @{
  x86 = 'WIN32;_X86_;i386=1;STD_CALL'
  x64 = 'WIN32;_WIN64;_AMD64_;AMD64'
  ARM = 'WIN32;_ARM_;ARM'
  ARM64 = 'WIN32;_WIN64;_ARM64_;ARM64'
}

$platform = $platformByArchitecture[$Architecture]
$machine = $machineByArchitecture[$Architecture]
$architectureDefines = $definesByArchitecture[$Architecture]
$runtimeLibrary = if ($Configuration -eq 'Debug') { 'MultiThreadedDebug' } else { 'MultiThreaded' }

$sharedInclude = ConvertTo-XmlEscapedText (Join-Path $windowsKitsIncludeRoot "$WdkVersion\shared")
$kmInclude = ConvertTo-XmlEscapedText (Join-Path $windowsKitsIncludeRoot "$WdkVersion\km")
$kmCrtInclude = ConvertTo-XmlEscapedText (Join-Path $windowsKitsIncludeRoot "$WdkVersion\km\crt")
$ntoskrnlLib = ConvertTo-XmlEscapedText (Join-Path $wdkKernelLibDirectory 'ntoskrnl.lib')
$halLib = ConvertTo-XmlEscapedText (Join-Path $wdkKernelLibDirectory 'hal.lib')
$wmilibLib = ConvertTo-XmlEscapedText (Join-Path $wdkKernelLibDirectory 'wmilib.lib')
$securityRuntimeLib = ConvertTo-XmlEscapedText $securityRuntimePath
$wdkKernelLibDirectoryEscaped = ConvertTo-XmlEscapedText $wdkKernelLibDirectory
$escapedWindowsSdkVersion = ConvertTo-XmlEscapedText $WindowsSdkVersion
$escapedToolset = ConvertTo-XmlEscapedText $Toolset
$escapedConfiguration = ConvertTo-XmlEscapedText $Configuration
$escapedPlatform = ConvertTo-XmlEscapedText $platform
$escapedArchitectureDefines = ConvertTo-XmlEscapedText $architectureDefines
$crtsysProps = ConvertTo-XmlEscapedText (Join-Path $packageRoot 'build\native\crtsys.props')
$crtsysTargets = ConvertTo-XmlEscapedText (Join-Path $packageRoot 'build\native\crtsys.targets')

$targetName = 'CrtSysMsBuildPackageSmoke'
$projectPath = Join-Path $projectDirectory "$targetName.vcxproj"
$mainPath = Join-Path $projectDirectory 'main.cpp'

$vcxproj = @"
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" ToolsVersion="17.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="$escapedConfiguration|$escapedPlatform">
      <Configuration>$escapedConfiguration</Configuration>
      <Platform>$escapedPlatform</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <ProjectGuid>{08B03E15-69C3-4D63-A607-06F87BF64DDE}</ProjectGuid>
    <Keyword>Win32Proj</Keyword>
    <ProjectName>$targetName</ProjectName>
    <WindowsTargetPlatformVersion>$escapedWindowsSdkVersion</WindowsTargetPlatformVersion>
    <DriverType>WDM</DriverType>
    <CrtSysUseDriverSupport>true</CrtSysUseDriverSupport>
    <CrtSysExpectedLibToolset>$escapedToolset</CrtSysExpectedLibToolset>
  </PropertyGroup>
  <Import Project="$crtsysProps" Condition="Exists('$crtsysProps')" />
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Label="Configuration" Condition="'`$(Configuration)|`$(Platform)'=='$escapedConfiguration|$escapedPlatform'">
    <ConfigurationType>Application</ConfigurationType>
    <CharacterSet>Unicode</CharacterSet>
    <PlatformToolset>$escapedToolset</PlatformToolset>
  </PropertyGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.props" />
  <PropertyGroup>
    <OutDir>`$(MSBuildThisFileDirectory)bin\`$(Configuration)\</OutDir>
    <IntDir>`$(MSBuildThisFileDirectory)obj\`$(Configuration)\</IntDir>
    <TargetName>$targetName</TargetName>
    <TargetExt>.sys</TargetExt>
    <GenerateManifest>false</GenerateManifest>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalIncludeDirectories>$sharedInclude;$kmInclude;$kmCrtInclude;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>$escapedArchitectureDefines;WINNT=1;_WIN32_WINNT=0x0601;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <AdditionalOptions>%(AdditionalOptions) /Zc:__cplusplus</AdditionalOptions>
      <ExceptionHandling>Sync</ExceptionHandling>
      <LanguageStandard>stdcpplatest</LanguageStandard>
      <RuntimeLibrary>$runtimeLibrary</RuntimeLibrary>
      <WarningLevel>Level4</WarningLevel>
      <TreatWarningAsError>true</TreatWarningAsError>
    </ClCompile>
    <Link>
      <AdditionalLibraryDirectories>$wdkKernelLibDirectoryEscaped;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <AdditionalDependencies>$ntoskrnlLib;$halLib;$wmilibLib;$securityRuntimeLib;%(AdditionalDependencies)</AdditionalDependencies>
      <IgnoreAllDefaultLibraries>true</IgnoreAllDefaultLibraries>
      <SubSystem>Native</SubSystem>
      <AdditionalOptions>%(AdditionalOptions) /DRIVER /MACHINE:$machine</AdditionalOptions>
      <GenerateDebugInformation>true</GenerateDebugInformation>
    </Link>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClCompile Include="main.cpp" />
  </ItemGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.targets" />
  <Import Project="$crtsysTargets" Condition="Exists('$crtsysTargets')" />
</Project>
"@

Set-Content -LiteralPath $projectPath -Value $vcxproj -Encoding UTF8

$mainCpp = @'
#include <ntl/driver>

#include <string>
#include <vector>

ntl::status ntl::main(ntl::driver& driver, const std::wstring& registry_path) {
  UNREFERENCED_PARAMETER(registry_path);

  std::vector<int> values{1, 2, 3};
  if (values.size() != 3) {
    return ntl::status(STATUS_UNSUCCESSFUL);
  }

  driver.on_unload([]() {});
  return ntl::status::ok();
}
'@

Set-Content -LiteralPath $mainPath -Value $mainCpp -Encoding UTF8

$msbuildArgs = @(
  $projectPath,
  '/m',
  '/v:m',
  "/p:Configuration=$Configuration",
  "/p:Platform=$platform"
)

Write-Host "Building NuGet native MSBuild consumer for crtsys $Toolset $Architecture $Configuration"
& $msbuild @msbuildArgs
if ($LASTEXITCODE -ne 0) {
  throw "MSBuild NuGet native consumer failed with exit code $LASTEXITCODE."
}

$driverPath = Join-Path $projectDirectory "bin\$Configuration\$targetName.sys"
if (-not (Test-Path $driverPath)) {
  throw "MSBuild NuGet native consumer driver was not produced: $driverPath"
}

Write-Host "NuGet native MSBuild consumer test passed: $driverPath"
