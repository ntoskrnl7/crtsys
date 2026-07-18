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

  [ValidateSet('NTL', 'WDM', 'KMDF', 'NTL_KMDF')]
  [string] $DriverModel = 'NTL',

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
    Join-Path $visualStudioPath 'MSBuild\Current\Bin\amd64\MSBuild.exe'
    Join-Path $visualStudioPath 'MSBuild\Current\Bin\MSBuild.exe'
    Join-Path $visualStudioPath 'MSBuild\15.0\Bin\amd64\MSBuild.exe'
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
  $WorkDirectory = Join-Path $repoRoot "artifacts\nuget-msbuild-consumer-test\$DriverModel\$Toolset\$Architecture\$Configuration"
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

$isKmdf = $DriverModel -in @('KMDF', 'NTL_KMDF')
$kmdfVersion = '1.15'
$wdfIncludeDirectory = ''
$wdfLibraryDirectory = ''
if ($isKmdf) {
  $wdfIncludeDirectory = Join-Path $windowsKitsRoot "Include\wdf\kmdf\$kmdfVersion"
  $wdfLibraryDirectory = Join-Path $windowsKitsRoot "Lib\wdf\kmdf\$($wdkPlatformByArchitecture[$Architecture])\$kmdfVersion"
  foreach ($requiredPath in @(
    (Join-Path $wdfIncludeDirectory 'wdf.h'),
    (Join-Path $wdfLibraryDirectory 'WdfDriverEntry.lib'),
    (Join-Path $wdfLibraryDirectory 'WdfLdr.lib')
  )) {
    if (-not (Test-Path $requiredPath)) {
      throw "KMDF $kmdfVersion dependency was not found: $requiredPath"
    }
  }
}

Write-Host "Requested Windows SDK version: $WindowsSdkVersion"
Write-Host "Resolved WDK version: $WdkVersion"

if ($Toolset -eq 'v142' -and $Architecture -eq 'ARM64' -and [version]$WdkVersion -ge [version]'10.0.26100.0') {
  throw "VS2019 v142 ARM64 cannot consume WDK $WdkVersion headers here; WDK 10.0.26100.0 references ARM64 intrinsics that v142 does not provide. Install/use WDK 10.0.22621.0 for this smoke test."
}

if ($WdkVersion -ne $WindowsSdkVersion) {
  Write-Host "Using Windows SDK $WindowsSdkVersion with WDK libraries $WdkVersion for $Architecture."
}

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
  'docs\third-party-notices.md',
  'include\ntl\driver',
  'include\ntl\deps\zpp\LICENSE',
  'include\ntl\deps\zpp\README.md',
  'include\ntl\deps\zpp\serializer.h',
  'include\ntl\kmdf\driver',
  'include\ntl\kmdf\dma',
  'include\ntl\kmdf\usb',
  'include\ntl\kmdf\wmi',
  'include\ntl\kmdf\pdo',
  'include\ntl\kmdf\query_interface',
  'include\ntl\kmdf\resource_requirements',
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
$driverType = if ($isKmdf) { 'KMDF' } else { 'WDM' }
$useNtlMain = if ($DriverModel -eq 'NTL') { 'true' } else { 'false' }
$useNtlKmdfMain = if ($DriverModel -eq 'NTL_KMDF') { 'true' } else { 'false' }
$kmdfVersionProperty = if ($isKmdf) { "    <KmdfVersion>$kmdfVersion</KmdfVersion>" } else { '' }
$additionalDriverIncludes = if ($isKmdf) {
  (ConvertTo-XmlEscapedText $wdfIncludeDirectory) + ';'
} else {
  ''
}
$additionalDriverLibraries = if ($isKmdf) {
  (ConvertTo-XmlEscapedText (Join-Path $wdfLibraryDirectory 'WdfDriverEntry.lib')) + ';' +
    (ConvertTo-XmlEscapedText (Join-Path $wdfLibraryDirectory 'WdfLdr.lib')) + ';'
} else {
  ''
}
$additionalCompileOptions = ''
if ($isKmdf) {
  $additionalCompileOptions += '/wd4324 '
}
$callingConventionProperty = if ($Architecture -eq 'x86') {
  '      <CallingConvention>StdCall</CallingConvention>'
} else {
  ''
}

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
    <DriverType>$driverType</DriverType>
$kmdfVersionProperty
    <CrtSysUseDriverSupport>true</CrtSysUseDriverSupport>
    <CrtSysUseNtlMain>$useNtlMain</CrtSysUseNtlMain>
    <CrtSysUseNtlKmdfMain>$useNtlKmdfMain</CrtSysUseNtlKmdfMain>
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
      <AdditionalIncludeDirectories>$sharedInclude;$kmInclude;$kmCrtInclude;$additionalDriverIncludes%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>$escapedArchitectureDefines;WINNT=1;_WIN32_WINNT=0x0601;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <AdditionalOptions>%(AdditionalOptions) /Zc:__cplusplus $additionalCompileOptions</AdditionalOptions>
$callingConventionProperty
      <ExceptionHandling>Sync</ExceptionHandling>
      <LanguageStandard>stdcpplatest</LanguageStandard>
      <RuntimeLibrary>$runtimeLibrary</RuntimeLibrary>
      <WarningLevel>Level4</WarningLevel>
      <TreatWarningAsError>true</TreatWarningAsError>
      <ControlFlowGuard>Guard</ControlFlowGuard>
    </ClCompile>
    <Link>
      <AdditionalLibraryDirectories>$wdkKernelLibDirectoryEscaped;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <AdditionalDependencies>$ntoskrnlLib;$halLib;$wmilibLib;$securityRuntimeLib;$additionalDriverLibraries%(AdditionalDependencies)</AdditionalDependencies>
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

$mainCpp = switch ($DriverModel) {
  'NTL' { @'
#include <ntl/driver>
#include <ntl/deps/zpp/serializer.h>
#include <ntl/rpc/server>

#include <memory>
#include <string>
#include <vector>

namespace {
using package_smoke_method = ntl::rpc::make_method_t<0xC53, int, int>;
std::shared_ptr<ntl::rpc::server> package_smoke_server;
} // namespace

ntl::status ntl::main(ntl::driver& driver, const std::wstring& registry_path) {
  UNREFERENCED_PARAMETER(registry_path);

  std::vector<int> values{1, 2, 3};
  if (values.size() != 3) {
    return ntl::status(STATUS_UNSUCCESSFUL);
  }

  std::vector<unsigned char> bytes;
  zpp::serializer::memory_output_archive output(bytes);
  output(values);

  std::vector<int> restored;
  zpp::serializer::memory_input_archive input(bytes);
  input(restored);
  if (restored != values) {
    return ntl::status(STATUS_UNSUCCESSFUL);
  }

  ntl::rpc::server_options rpc_options(L"CrtSysPackageSmokeRpc");
  rpc_options.contract_version(9).capabilities(0x5);
  package_smoke_server = ntl::rpc::make_server(driver, rpc_options);
  package_smoke_server
      ->on(package_smoke_method{}, [](int value) { return value; })
      .start();

  driver.on_unload([]() { package_smoke_server.reset(); });
  return ntl::status::ok();
}
'@ }
  'WDM' { @'
#include <ntddk.h>

#include <numeric>
#include <vector>

extern "C" DRIVER_UNLOAD CrtSysPackageSmokeDriverUnload;
extern "C" DRIVER_INITIALIZE DriverEntry;

extern "C" void CrtSysPackageSmokeDriverUnload(PDRIVER_OBJECT driver_object) {
  UNREFERENCED_PARAMETER(driver_object);
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object,
                                 PUNICODE_STRING registry_path) {
  UNREFERENCED_PARAMETER(registry_path);

  const std::vector<int> values{1, 2, 3};
  if (std::accumulate(values.begin(), values.end(), 0) != 6) {
    return STATUS_UNSUCCESSFUL;
  }

  driver_object->DriverUnload = CrtSysPackageSmokeDriverUnload;
  return STATUS_SUCCESS;
}
'@ }
  'KMDF' { @'
#include <ntddk.h>

#pragma warning(push)
// Older WDK KMDF 1.15 headers trigger C4471 under v142 with /WX.
#pragma warning(disable : 4471)
#include <wdf.h>
#pragma warning(pop)

#include <numeric>
#include <vector>

extern "C" DRIVER_INITIALIZE DriverEntry;

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object,
                                 PUNICODE_STRING registry_path) {
  const std::vector<int> values{1, 2, 3};
  if (std::accumulate(values.begin(), values.end(), 0) != 6) {
    return STATUS_UNSUCCESSFUL;
  }

  WDF_DRIVER_CONFIG config;
  WDF_DRIVER_CONFIG_INIT(&config, WDF_NO_EVENT_CALLBACK);
  config.DriverInitFlags |= WdfDriverInitNonPnpDriver;
  config.EvtDriverUnload = +[](WDFDRIVER) noexcept {};
  return WdfDriverCreate(driver_object, registry_path, WDF_NO_OBJECT_ATTRIBUTES,
                         &config, WDF_NO_HANDLE);
}
'@ }
  'NTL_KMDF' { @'
#include <ntl/kmdf/all>

#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr GUID sample_interface = {
    0x3dfc4dc0, 0x6d61, 0x4a97,
    {0xa1, 0x10, 0x2a, 0xa8, 0x2f, 0x96, 0x87, 0xe1}};

struct sample_device_context {
  std::vector<int> values;
};

struct sample_file_context {
  std::string name;
};

struct sample_interrupt_context {
  ULONG count = 0;
};

struct sample_child_identity {
  ULONG serial_number;
};

struct sample_child_address {
  ULONG slot;
};

struct sample_additional_context {
  explicit sample_additional_context(ULONG value) noexcept : value(value) {}
  ULONG value;
};

struct sample_general_object_context {
  explicit sample_general_object_context(ULONG value) noexcept : value(value) {}
  ULONG value;
};

struct sample_wmi_data {
  ULONG value;
};

constexpr GUID sample_wmi_guid = {
    0x3988f399, 0xa3df, 0x4d40,
    {0xa1, 0xdd, 0x2e, 0x3f, 0x2f, 0xf1, 0x8f, 0x8c}};

constexpr GUID sample_query_interface_guid = {
    0x3f3fb9d4, 0xc11e, 0x4628,
    {0xb1, 0xc1, 0x90, 0xaf, 0xf6, 0x63, 0x10, 0xb8}};

using sample_query_value_fn = NTSTATUS(NTAPI *)(void *, ULONG *) noexcept;

struct sample_query_interface {
  INTERFACE header;
  sample_query_value_fn query_value;
};

NTSTATUS NTAPI sample_query_value(void *, ULONG *value) noexcept {
  if (!value)
    return STATUS_INVALID_PARAMETER;
  *value = 42;
  return STATUS_SUCCESS;
}

constexpr auto sample_wmi_control =
    +[](ntl::kmdf::wmi_provider, WDF_WMI_PROVIDER_CONTROL,
        bool) noexcept -> NTSTATUS { return STATUS_SUCCESS; };

constexpr auto sample_wmi_query =
    +[](ntl::kmdf::wmi_instance,
        ntl::kmdf::wmi_output_buffer output) noexcept -> NTSTATUS {
  return output.try_write(sample_wmi_data{42});
};

constexpr auto sample_wmi_set =
    +[](ntl::kmdf::wmi_instance,
        ntl::kmdf::wmi_input_buffer input) noexcept -> NTSTATUS {
  return input.try_read<sample_wmi_data>() ? STATUS_SUCCESS
                                           : STATUS_BUFFER_TOO_SMALL;
};

constexpr auto sample_wmi_method =
    +[](ntl::kmdf::wmi_instance, ULONG,
        ntl::kmdf::wmi_method_buffer buffer) noexcept -> NTSTATUS {
  return buffer.output().try_write(sample_wmi_data{84});
};

constexpr auto sample_program_dma =
    +[](ntl::kmdf::dma_transaction, ntl::kmdf::device, void*,
        WDF_DMA_DIRECTION,
        ntl::kmdf::scatter_gather_list list) noexcept {
      (void)list.at(0);
      return true;
    };

constexpr auto sample_usb_reader =
    +[](ntl::kmdf::usb_pipe, ntl::kmdf::memory, size_t,
        void*) noexcept {};

constexpr auto sample_usb_reader_failure =
    +[](ntl::kmdf::usb_pipe, ntl::status,
        USBD_STATUS) noexcept { return false; };

constexpr auto sample_standalone_dpc =
    +[](ntl::kmdf::dpc) noexcept {};

[[maybe_unused]] void compile_control_init_surface(
    ntl::kmdf::control_device_init& init) {
  init.on_shutdown<
      +[](ntl::kmdf::device) noexcept {}>(WdfDeviceShutdown);
}

[[maybe_unused]] void compile_typed_callback_surface(
    ntl::kmdf::device device, ntl::kmdf::device_init& init,
    ntl::kmdf::io_queue queue, ntl::kmdf::request request) {
  using namespace ntl::kmdf;

  WDF_IO_TYPE_CONFIG io_type_config{};
  WDF_IO_TYPE_CONFIG_INIT(&io_type_config);
  init.io_type(io_type_config)
      .device_class(sample_interface)
      .characteristics(FILE_DEVICE_SECURE_OPEN)
      .power_inrush()
      .release_hardware_order_on_failure(
          WdfReleaseHardwareOrderOnFailureAfterDescendants)
      .remove_lock_options(WDF_REMOVE_LOCK_OPTION_ACQUIRE_FOR_IO);

  object_attributes object_config;
  object_config
      .on_cleanup<+[](object) noexcept {}>()
      .on_destroy<+[](object) noexcept {}>();
  object_lock_guard object_lock{
      object{reinterpret_cast<WDFOBJECT>(device.native_handle())}};
  (void)object_lock.get();
  (void)device.try_emplace_context<sample_additional_context>(42);

  (void)request.try_mark_cancelable<
      +[](ntl::kmdf::request) noexcept {}>();
  queue.stop<+[](ntl::kmdf::io_queue, void*) noexcept {}>();
  queue.drain<+[](ntl::kmdf::io_queue, void*) noexcept {}>();
  queue.purge<+[](ntl::kmdf::io_queue, void*) noexcept {}>();
  queue.stop_and_purge<+[](ntl::kmdf::io_queue, void*) noexcept {}>();
  (void)queue.ready_notify<
      +[](ntl::kmdf::io_queue, void*) noexcept {}>();
  (void)queue.clear_ready_notify();

  auto progress = forward_progress_policy::examine<
      +[](ntl::kmdf::io_queue, PIRP) noexcept {
        return WdfIoForwardProgressActionUseReservedRequest;
      }>(1);
  progress
      .prepare_reserved_requests<
          +[](ntl::kmdf::io_queue,
              ntl::kmdf::reserved_request_resources) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>()
      .prepare_each_request<
          +[](ntl::kmdf::io_queue,
              ntl::kmdf::request_resources) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>();
  (void)progress.try_assign(queue);

  (void)request.parameters();
  (void)request.completion_parameters();
  request.format_from_wdm_stack(nullptr);
  (void)request.requestor_mode();
  (void)request.is_from_32bit_process();
  (void)request.is_reserved();
  (void)request.associated_queue();
  (void)request.try_input_memory();
  (void)request.try_output_memory();
  (void)request.try_input_mdl();
  (void)request.try_output_mdl();
  (void)request.wdm_irp();

  request_parameters parameters;
  auto found = queue.try_find(nullptr, nullptr, &parameters);
  if (found) {
    auto retrieved = queue.try_retrieve(std::move(found).value());
    if (retrieved)
      retrieved->complete(STATUS_SUCCESS);
  }
  auto next = queue.try_retrieve_next();
  if (next)
    next->complete(STATUS_SUCCESS);
  auto for_file = queue.try_retrieve_for(request.associated_file());
  if (for_file)
    for_file->complete(STATUS_SUCCESS);

  file_config<sample_file_context> files;
  files
      .on_create<
          +[](ntl::kmdf::device, ntl::kmdf::request request,
              ntl::kmdf::file file) noexcept {
            (void)file.context<sample_file_context>();
            (void)file.wdm();
            (void)file.flags();
            request.complete(STATUS_SUCCESS);
          }>()
      .on_cleanup<+[](ntl::kmdf::file) noexcept {}>()
      .on_close<+[](ntl::kmdf::file) noexcept {}>();
  init.file_objects(files);

  io_target target = device.default_io_target();
  send_options options;
  options.ignore_target_state().timeout(WDF_REL_TIMEOUT_IN_MS(100));

  object_attributes memory_attributes;
  memory_attributes.parent(device);
  auto allocated_memory = memory::try_allocate(
      sizeof(ULONG) * 4, NonPagedPoolNx, "NTLm", &memory_attributes);
  ULONG backing_storage[4]{};
  auto preallocated_memory = memory::try_preallocated(
      backing_storage, sizeof(backing_storage), &memory_attributes);
  memory_offset buffer_range(sizeof(ULONG), sizeof(ULONG) * 2);
  auto input_descriptor =
      memory_descriptor::buffer(backing_storage, sizeof(backing_storage));
  auto output_descriptor =
      memory_descriptor::buffer(backing_storage, sizeof(backing_storage));
  (void)target.try_read(&output_descriptor);
  (void)target.try_write(&input_descriptor);
  (void)target.try_ioctl(0, &input_descriptor, &output_descriptor);
  (void)target.try_internal_ioctl(0, &input_descriptor, &output_descriptor);
  (void)target.try_internal_ioctl_others(
      0, &input_descriptor, &input_descriptor, &output_descriptor);
  (void)target.wdm_device_object();
  (void)target.wdm_physical_device_object();
  (void)target.wdm_file_object();
  (void)target.wdm_file_handle();

  (void)device.try_name();
  (void)device.try_interface_string(sample_interface);
  (void)device.default_queue();
  (void)device.try_route_requests(queue, WdfRequestTypeDeviceControl);
  (void)device.pnp_state();
  (void)device.power_state();
  (void)device.power_policy_state();
  (void)device.system_power_action();
  (void)device.state();
  (void)device.wdm_device_object();
  (void)device.wdm_attached_device_object();
  (void)device.wdm_physical_device_object();
  (void)device.characteristics();
  (void)device.alignment_requirement();
  (void)device_idle_reference::try_acquire(device);
  if (allocated_memory && preallocated_memory) {
    (void)allocated_memory->try_copy_from(
        0, backing_storage, sizeof(backing_storage));
    (void)allocated_memory->try_copy_to(
        0, backing_storage, sizeof(backing_storage));
    (void)preallocated_memory->try_assign(
        backing_storage, sizeof(backing_storage));
    (void)allocated_memory->data<ULONG>();
    (void)allocated_memory->size<ULONG>();
  }

  object_attributes utility_attributes;
  utility_attributes.parent(device);
  auto general_object =
      owned_object::try_create<sample_general_object_context>(
          &utility_attributes, 42);
  if (general_object) {
    auto reference = general_object->reference();
    auto moved_reference = std::move(reference);
    (void)moved_reference.context<sample_general_object_context>().value;
  }
  object_reference device_reference{device};

  auto framework_spin = spin_lock::try_create(&utility_attributes);
  if (framework_spin) {
    framework_spin->lock();
    framework_spin->unlock();
  }
  auto framework_wait = wait_lock::try_create(&utility_attributes);
  if (framework_wait && framework_wait->try_lock()) {
    framework_wait->unlock();
  }

  auto cache = lookaside::try_create(
      sizeof(ULONG) * 4, NonPagedPoolNx, "NTLk", &utility_attributes);
  if (cache) {
    auto cached_memory = cache->try_allocate();
    if (cached_memory)
      (void)cached_memory->data<ULONG>();
  }

  auto framework_string = string::try_create(L"package smoke");
  auto framework_collection = collection::try_create(&utility_attributes);
  if (framework_string && framework_collection) {
    (void)framework_string->view();
    (void)framework_collection->try_add(*framework_string);
    framework_collection->remove(*framework_string);
  }

  auto dpc_settings = dpc_config::with_callback<sample_standalone_dpc>();
  dpc_settings.automatic_serialization(false);
  auto dpc_object = dpc::try_create(device, dpc_settings);
  if (dpc_object)
    (void)dpc_object->wdm();

  fdo_event_callbacks fdo_events;
  fdo_events
      .on_add_resource_requirements<
          +[](ntl::kmdf::device,
              ntl::kmdf::io_resource_requirements) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>()
      .on_remove_resource_requirements<
          +[](ntl::kmdf::device,
              ntl::kmdf::io_resource_requirements) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>()
      .on_remove_added_resources<
          +[](ntl::kmdf::device, ntl::kmdf::resource_list,
              ntl::kmdf::resource_list) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>();
  init.fdo_events(fdo_events);
  (void)init.physical_device_object();

  auto created_target = io_target::try_create(device);
  auto open_params = io_target_open_params::existing_device(
      WdfDeviceWdmGetDeviceObject(device.native_handle()));
  open_params
      .on_query_remove<
          +[](ntl::kmdf::io_target) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>()
      .on_remove_canceled<+[](ntl::kmdf::io_target) noexcept {}>()
      .on_remove_complete<+[](ntl::kmdf::io_target) noexcept {}>();
  if (created_target) {
    (void)created_target->try_open(open_params);
  }

  auto synchronous_request = driver_request::try_create(target);
  if (synchronous_request && allocated_memory) {
    auto owned = std::move(synchronous_request).value();
    (void)owned.try_change_target(target);
    (void)owned.try_format_read(target, allocated_memory.value(),
                                &buffer_range);
    (void)owned.try_format_write(target, allocated_memory.value(),
                                 &buffer_range);
    (void)owned.try_format_ioctl(target, 0, allocated_memory.value(),
                                 allocated_memory.value(), &buffer_range,
                                 &buffer_range);
    (void)owned.try_format_internal_ioctl_others(
        target, 0, allocated_memory.value(), allocated_memory.value(),
        allocated_memory.value(), &buffer_range, &buffer_range,
        &buffer_range);
    (void)owned.try_allocate_timer();
    send_options synchronous_options;
    synchronous_options.synchronous();
    (void)owned.try_send_and_wait(target, &synchronous_options);
  }

  auto asynchronous_request = driver_request::try_create(target);
  if (asynchronous_request) {
    auto owned = std::move(asynchronous_request).value();
    (void)std::move(owned).try_send<
        +[](ntl::kmdf::driver_request, ntl::kmdf::io_target,
            ntl::kmdf::completion_params, void*) noexcept {}>(target);
  }

  dma_enabler_config dma_config(WdfDmaProfileScatterGather64,
                                1024 * 1024);
  dma_config
      .on_fill<+[](dma_enabler) noexcept -> NTSTATUS {
        return STATUS_SUCCESS;
      }>()
      .on_flush<+[](dma_enabler) noexcept -> NTSTATUS {
        return STATUS_SUCCESS;
      }>()
      .on_enable<+[](dma_enabler) noexcept -> NTSTATUS {
        return STATUS_SUCCESS;
      }>()
      .on_disable<+[](dma_enabler) noexcept -> NTSTATUS {
        return STATUS_SUCCESS;
      }>();
  auto dma = dma_enabler::try_create(device, dma_config);
  if (dma) {
    PHYSICAL_ADDRESS device_address{};
    dma_system_profile_config system_profile(
        device_address, Width8Bits, nullptr);
    system_profile.demand_mode().looped_transfer();
    (void)dma->try_configure_system_profile(
        system_profile, WdfDmaDirectionWriteToDevice);

    common_buffer_config common_config(15);
    auto common = common_buffer::try_create(
        dma.value(), 4096, common_config);
    if (common) {
      (void)common->data<ULONG>();
      (void)common->logical_address();
      (void)common->size_bytes();
    }

    auto transaction = dma_transaction::try_create(dma.value());
    if (transaction) {
      (void)transaction->try_initialize_request<sample_program_dma>(
          request, WdfDmaDirectionWriteToDevice);
      UCHAR dma_buffer[64]{};
      (void)transaction->try_initialize<sample_program_dma>(
          nullptr, static_cast<void*>(dma_buffer), sizeof(dma_buffer),
          WdfDmaDirectionWriteToDevice);
      (void)transaction->try_initialize<sample_program_dma>(
          nullptr, size_t{0}, sizeof(dma_buffer),
          WdfDmaDirectionWriteToDevice);
      (void)transaction->try_execute();
      (void)transaction->bytes_transferred();
      (void)transaction->current_transfer_length();
      transaction->transfer_info(nullptr, nullptr);
    }
  }

  usb_device_create_config usb_create_config;
  auto usb = usb_device::try_create(device, usb_create_config);
  if (usb) {
    (void)usb->try_information();
    (void)usb->descriptor();
    auto selection = usb_select_config::single_interface();
    (void)usb->try_select(selection);

    auto interface = selection.configured_interface();
    auto setting = usb_interface_setting::index(0);
    (void)interface.try_select(setting);
    usb_pipe_information pipe_information;
    auto pipe = interface.pipe_at(0, &pipe_information);

    UCHAR bytes[64]{};
    auto descriptor = usb_memory_descriptor::buffer(bytes, sizeof(bytes));
    (void)pipe.try_read(&descriptor);
    (void)pipe.try_write(&descriptor);

    auto usb_request = driver_request::try_create(pipe.target());
    auto usb_memory = memory::try_preallocated(bytes, sizeof(bytes));
    if (usb_request && usb_memory) {
      auto owned = std::move(usb_request).value();
      (void)pipe.try_format_read(owned, usb_memory.value());
      auto setup = usb_control_setup_packet::vendor(
          BmRequestDeviceToHost, BmRequestToDevice, 1, 0, 0);
      (void)usb->try_format_control(owned, setup, usb_memory.value());
    }

    auto reader = usb_continuous_reader_config::with_completion<
        sample_usb_reader>(sizeof(bytes));
    reader.on_failure<sample_usb_reader_failure>().pending_reads(1);
    (void)pipe.try_configure_reader(reader);
  }

  request.on_completion<
      +[](ntl::kmdf::request, ntl::kmdf::io_target,
          ntl::kmdf::completion_params, void*) noexcept {}>();
  (void)std::move(request).try_send(target, &options);

  auto interrupt_settings = interrupt_config::with_isr<
      +[](ntl::kmdf::interrupt, ULONG) noexcept { return false; }>();
  interrupt_settings
      .on_dpc<
          +[](ntl::kmdf::interrupt, ntl::kmdf::object) noexcept {}>()
      .on_work_item<
          +[](ntl::kmdf::interrupt, ntl::kmdf::object) noexcept {}>()
      .on_enable<
          +[](ntl::kmdf::interrupt,
              ntl::kmdf::device) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>()
      .on_disable<
          +[](ntl::kmdf::interrupt,
              ntl::kmdf::device) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>();
  interrupt_settings.report_inactive_on_power_down(WdfUseDefault);
  auto interrupt_object = interrupt::try_create<sample_interrupt_context>(
      device, interrupt_settings, nullptr);
  if (interrupt_object) {
    (void)interrupt_object->info();
    (void)interrupt_object->try_acquire_lock();
    interrupt_object->policy(WdfIrqPolicyMachineDefault);
    WDF_INTERRUPT_EXTENDED_POLICY extended_policy{};
    WDF_INTERRUPT_EXTENDED_POLICY_INIT(&extended_policy);
    interrupt_object->extended_policy(extended_policy);
    interrupt_object->report_active();
    interrupt_object->report_inactive();
  }

  auto timer_settings = timer_config::periodic<
      +[](ntl::kmdf::timer) noexcept {}>(1000);
  timer_settings.automatic_serialization(false).tolerable_delay(10);
  auto timer_object = timer::try_create(device, timer_settings);
  if (timer_object) {
    (void)timer_object->start_after_ms(10);
    (void)timer_object->stop(false);
  }

  auto work_settings = work_item_config::with_callback<
      +[](ntl::kmdf::work_item) noexcept {}>();
  work_settings.automatic_serialization(false);
  auto work_object = work_item::try_create(device, work_settings);
  if (work_object) {
    work_object->enqueue();
  }

  using sample_child_config =
      child_list_config<sample_child_identity, sample_child_address>;
  auto child_settings = sample_child_config::with_create<
      +[](ntl::kmdf::child_list,
          const ntl::kmdf::child_identification<sample_child_identity>&,
          ntl::kmdf::pdo_init init) noexcept -> NTSTATUS {
        ntl::status status =
            init.assign_device_id(L"CrtSys\\PackageChild");
        if (status.is_err())
          return status;
        status = init.assign_instance_id(L"0");
        if (status.is_err())
          return status;
        return STATUS_NOT_SUPPORTED;
      }>();
  child_settings
      .on_scan<+[](ntl::kmdf::child_list) noexcept {}>()
      .on_compare<
          +[](ntl::kmdf::child_list,
              const ntl::kmdf::child_identification<sample_child_identity>& a,
              const ntl::kmdf::child_identification<sample_child_identity>& b)
          noexcept {
            return a.payload.serial_number == b.payload.serial_number;
          }>();
  init.default_child_list(child_settings);
  auto children = child_list::try_create(device, child_settings);
  if (children) {
    const child_identification<sample_child_identity> id{{1}};
    const child_address<sample_child_address> address{{2}};
    (void)children->add_or_update(id, address);
    auto found_child = children->find(id);
    if (found_child) {
      child_identification<sample_child_identity> round_trip;
      (void)found_child.value.retrieve_identification(round_trip);
      (void)found_child.value.parent();
    }
    auto iteration = children->iterate();
    (void)iteration.next();
  }

  auto static_init = pdo_init::try_allocate(device);
  if (static_init &&
      static_init->assign_device_id(L"CrtSys\\PackageStaticChild").is_ok() &&
      static_init->assign_instance_id(L"0").is_ok()) {
    pdo_event_callbacks pdo_events;
    pdo_events
        .on_resource_requirements_query<
            +[](pdo,
                io_resource_requirements requirements) noexcept -> NTSTATUS {
              requirements.interface_type(Internal).slot_number(0);
              auto configuration = requirements.try_create_list();
              if (!configuration)
                return configuration.status();

              PHYSICAL_ADDRESS minimum{};
              PHYSICAL_ADDRESS maximum{};
              maximum.QuadPart = 0xffff;
              (void)configuration->try_append(io_resource_descriptor::memory(
                  minimum, maximum, 0x1000, 0x1000));
              (void)configuration->try_append(io_resource_descriptor::port(
                  minimum, maximum, 8, 1));
              (void)configuration->try_append(
                  io_resource_descriptor::interrupt(1, 15));
              (void)configuration->try_append(
                  io_resource_descriptor::dma(0, 7));
              (void)configuration->try_append(
                  io_resource_descriptor::dma_v3(1, 0, DMAV3_TRANFER_WIDTH_32));
              (void)configuration->try_append(
                  io_resource_descriptor::connection(
                      CM_RESOURCE_CONNECTION_CLASS_SERIAL,
                      CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C, 1));
              (void)configuration->try_append(
                  io_resource_descriptor::device_private(1, 2, 3));
              (void)configuration->try_at(0);
              return requirements.try_append(configuration.value());
            }>()
        .on_resources_query<
            +[](pdo, resource_list resources) noexcept -> NTSTATUS {
              CM_PARTIAL_RESOURCE_DESCRIPTOR descriptor{};
              descriptor.Type = CmResourceTypeDevicePrivate;
              (void)resources.try_append(descriptor);
              (void)resources.remove(0);
              return STATUS_SUCCESS;
            }>()
        .on_eject<+[](pdo) noexcept -> NTSTATUS { return STATUS_SUCCESS; }>()
        .on_set_lock<
            +[](pdo, bool) noexcept -> NTSTATUS { return STATUS_SUCCESS; }>()
        .on_enable_wake_at_bus<
            +[](pdo, SYSTEM_POWER_STATE) noexcept -> NTSTATUS {
              return STATUS_SUCCESS;
            }>()
        .on_disable_wake_at_bus<+[](pdo) noexcept {}>()
        .on_reported_missing<+[](pdo) noexcept {}>();
    static_init->events(pdo_events);
    auto static_child = static_init->try_create();
    if (static_child)
      (void)device.try_add_static_child(static_child.value());
  }

  auto query_value = make_query_interface<sample_query_interface>(
      1, device.native_handle(), reference_query_interface_object,
      dereference_query_interface_object);
  query_value.query_value = sample_query_value;
  query_interface_config query_value_config{query_value,
                                             sample_query_interface_guid};
  (void)device.try_add_query_interface(query_value_config);
  (void)device.try_query_interface<sample_query_interface>(
      sample_query_interface_guid, 1);

  auto device_key =
      device.try_open_registry_key(PLUGPLAY_REGKEY_DEVICE, KEY_READ);
  if (device_key) {
    (void)device_key->query_dword(std::wstring(L"SampleValue"));
    (void)device_key->query_multi_string(std::wstring(L"SampleMulti"));
    (void)device_key->assign_multi_string(
        std::wstring(L"SampleMulti"), {L"one", L"two"});
  }
  (void)device.try_query_property(DevicePropertyDeviceDescription);
  (void)device.try_assign_mof_resource(L"CrtSysPackageSmoke");

  wmi_provider_config wmi_provider_settings(sample_wmi_guid);
  wmi_provider_settings
      .minimum_instance_buffer_size(sizeof(sample_wmi_data))
      .on_function_control<sample_wmi_control>();
  auto wmi_provider_object =
      wmi_provider::try_create(device, wmi_provider_settings);
  if (wmi_provider_object) {
    wmi_instance_config wmi_instance_settings(wmi_provider_object.value());
    wmi_instance_settings
        .register_automatically()
        .on_query<sample_wmi_query>()
        .on_set<sample_wmi_set>()
        .on_execute<sample_wmi_method>();
    auto wmi_instance_object =
        wmi_instance::try_create(device, wmi_instance_settings);
    if (wmi_instance_object) {
      (void)wmi_instance_object->provider();
      (void)wmi_instance_object->try_fire_event(sample_wmi_data{1});
    }
  }

  io_queue_config queue_config(WdfIoQueueDispatchSequential);
  queue_config
      .on_device_control<
          +[](ntl::kmdf::io_queue, ntl::kmdf::request, size_t, size_t,
              ULONG) noexcept {}>()
      .on_internal_device_control<
          +[](ntl::kmdf::io_queue, ntl::kmdf::request, size_t, size_t,
              ULONG) noexcept {}>()
      .on_read<
          +[](ntl::kmdf::io_queue, ntl::kmdf::request, size_t) noexcept {}>()
      .on_write<
          +[](ntl::kmdf::io_queue, ntl::kmdf::request, size_t) noexcept {}>()
      .on_default<
          +[](ntl::kmdf::io_queue, ntl::kmdf::request) noexcept {}>()
      .on_stop<
          +[](ntl::kmdf::io_queue, ntl::kmdf::request, ULONG) noexcept {}>()
      .on_resume<
          +[](ntl::kmdf::io_queue, ntl::kmdf::request) noexcept {}>()
      .on_canceled<
          +[](ntl::kmdf::io_queue, ntl::kmdf::request) noexcept {}>();

  pnp_power_callbacks pnp;
  pnp.on_prepare_hardware<
         +[](ntl::kmdf::device, resource_list raw,
             resource_list translated) noexcept -> NTSTATUS {
           if (raw.origin() != resource_origin::raw ||
               translated.origin() != resource_origin::translated) {
             return STATUS_INVALID_PARAMETER;
           }
           for (const resource_descriptor descriptor : translated) {
             (void)descriptor.memory();
             (void)descriptor.port();
             (void)descriptor.interrupt();
             (void)descriptor.dma();
             (void)descriptor.connection();
           }
           return STATUS_SUCCESS;
         }>()
      .on_release_hardware<
          +[](ntl::kmdf::device,
              resource_list) noexcept -> NTSTATUS { return STATUS_SUCCESS; }>()
      .on_d0_entry<
          +[](ntl::kmdf::device,
              WDF_POWER_DEVICE_STATE) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>()
      .on_d0_entry_post_interrupts_enabled<
          +[](ntl::kmdf::device,
              WDF_POWER_DEVICE_STATE) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>()
      .on_d0_exit<
          +[](ntl::kmdf::device,
              WDF_POWER_DEVICE_STATE) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>()
      .on_d0_exit_pre_interrupts_disabled<
          +[](ntl::kmdf::device,
              WDF_POWER_DEVICE_STATE) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>()
      .on_self_managed_io_init<
          +[](ntl::kmdf::device) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>()
      .on_self_managed_io_cleanup<+[](ntl::kmdf::device) noexcept {}>()
      .on_self_managed_io_flush<+[](ntl::kmdf::device) noexcept {}>()
      .on_self_managed_io_suspend<
          +[](ntl::kmdf::device) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>()
      .on_self_managed_io_restart<
          +[](ntl::kmdf::device) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>()
      .on_surprise_removal<+[](ntl::kmdf::device) noexcept {}>()
      .on_query_remove<
          +[](ntl::kmdf::device) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>()
      .on_query_stop<
          +[](ntl::kmdf::device) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>()
      .on_usage_notification<
          +[](ntl::kmdf::device, WDF_SPECIAL_FILE_TYPE, bool) noexcept {}>()
      .on_relations_query<
          +[](ntl::kmdf::device, DEVICE_RELATION_TYPE) noexcept {}>()
      .on_usage_notification_ex<
          +[](ntl::kmdf::device, WDF_SPECIAL_FILE_TYPE,
              bool) noexcept -> NTSTATUS { return STATUS_SUCCESS; }>();

  power_policy_callbacks power;
  power
      .on_arm_wake_from_s0<
          +[](ntl::kmdf::device) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>()
      .on_disarm_wake_from_s0<+[](ntl::kmdf::device) noexcept {}>()
      .on_wake_from_s0_triggered<+[](ntl::kmdf::device) noexcept {}>()
      .on_arm_wake_from_sx<
          +[](ntl::kmdf::device) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>()
      .on_disarm_wake_from_sx<+[](ntl::kmdf::device) noexcept {}>()
      .on_wake_from_sx_triggered<+[](ntl::kmdf::device) noexcept {}>()
      .on_arm_wake_from_sx_with_reason<
          +[](ntl::kmdf::device, bool, bool) noexcept -> NTSTATUS {
            return STATUS_SUCCESS;
          }>();

  idle_policy idle(IdleCannotWakeFromS0);
  idle.timeout(1000, DriverManagedIdleTimeout)
      .user_control(IdleDoNotAllowUserControl)
      .enabled(true)
      .power_up_on_system_wake(WdfUseDefault)
      .exclude_d3_cold(WdfUseDefault);
  (void)idle.try_apply(device);

  wake_policy wake;
  wake.device_state(PowerDeviceMaximum)
      .user_control(WakeDoNotAllowUserControl)
      .enabled(false)
      .arm_for_armed_children()
      .indicate_child_wake();
  (void)wake.try_apply(device);
}
}

ntl::status ntl::kmdf::main(driver_builder& builder,
                            const std::wstring& registry_path) {
  UNREFERENCED_PARAMETER(registry_path);

  const std::vector<int> values{1, 2, 3};
  if (std::accumulate(values.begin(), values.end(), 0) != 6) {
    return STATUS_UNSUCCESSFUL;
  }

  kmdf::driver_config config;
  config.on_device_add<
      +[](kmdf::driver driver,
          kmdf::device_init& init) noexcept -> NTSTATUS {
        (void)driver.registry_path();
        (void)driver.is_version_available(1, 15);
        (void)driver.wdm_driver_object();
        ntl::kmdf::pnp_power_callbacks pnp;
        pnp.on_prepare_hardware<
               +[](kmdf::device, kmdf::resource_list,
                   kmdf::resource_list) noexcept -> NTSTATUS {
                 return STATUS_SUCCESS;
               }>()
            .on_release_hardware<
                +[](kmdf::device,
                    kmdf::resource_list) noexcept -> NTSTATUS {
                  return STATUS_SUCCESS;
                }>()
            .on_d0_entry<
                +[](kmdf::device,
                    WDF_POWER_DEVICE_STATE) noexcept -> NTSTATUS {
                  return STATUS_SUCCESS;
                }>()
            .on_d0_exit<
                +[](kmdf::device,
                    WDF_POWER_DEVICE_STATE) noexcept -> NTSTATUS {
                  return STATUS_SUCCESS;
                }>();

        init.io_type(WdfDeviceIoBuffered).pnp_power(pnp);

        ntl::kmdf::file_config<sample_file_context> files;
        files.on_create<
            +[](kmdf::device, kmdf::request request,
                kmdf::file) noexcept {
              request.complete(STATUS_SUCCESS);
            }>();
        init.file_objects(files);

        ntl::kmdf::object_attributes attributes;
        attributes.execution_level(WdfExecutionLevelPassive);
        auto created = init.try_create<sample_device_context>(&attributes);
        if (!created) {
          return created.status();
        }

        ntl::status status =
            created.value().try_create_interface(sample_interface);
        if (status.is_err()) {
          return status;
        }

        ntl::kmdf::io_queue_config queue_config(
            WdfIoQueueDispatchSequential);
        queue_config.on_default<
            +[](kmdf::io_queue, kmdf::request request) noexcept {
              request.complete(STATUS_NOT_SUPPORTED);
            }>();
        queue_config.power_managed(WdfTrue);
        auto queue =
            ntl::kmdf::io_queue::try_create(created.value(), queue_config);
        if (!queue) {
          return queue.status();
        }
        return STATUS_SUCCESS;
      }>();
  auto created = builder.try_create(config);
  return created ? ntl::status::ok() : created.status();
}
'@ }
}

Set-Content -LiteralPath $mainPath -Value $mainCpp -Encoding UTF8

$msbuildArgs = @(
  $projectPath,
  '/m',
  '/v:m',
  "/p:Configuration=$Configuration",
  "/p:Platform=$platform"
)

Write-Host "Building NuGet native MSBuild $DriverModel consumer for crtsys $Toolset $Architecture $Configuration"
& $msbuild @msbuildArgs
if ($LASTEXITCODE -ne 0) {
  throw "MSBuild NuGet native consumer failed with exit code $LASTEXITCODE."
}

$driverPath = Join-Path $projectDirectory "bin\$Configuration\$targetName.sys"
if (-not (Test-Path $driverPath)) {
  throw "MSBuild NuGet native consumer driver was not produced: $driverPath"
}

Write-Host "NuGet native MSBuild $DriverModel consumer test passed: $driverPath"
