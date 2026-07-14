param(
  [string] $ReleaseDirectory,

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

  [string] $WdkVersion = '',

  [switch] $SkipDriverBuild,

  [string] $WorkDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

if ([string]::IsNullOrWhiteSpace($ReleaseDirectory)) {
  $ReleaseDirectory = Join-Path $repoRoot 'artifacts\release'
}

if ([string]::IsNullOrWhiteSpace($WorkDirectory)) {
  $WorkDirectory = Join-Path $repoRoot "artifacts\release-consumer-test\$DriverModel\$Toolset\$Architecture\$Configuration"
}

$WorkDirectory = [System.IO.Path]::GetFullPath($WorkDirectory)
$repoRootPrefix = $repoRoot.TrimEnd('\') + '\'
if (-not $WorkDirectory.StartsWith($repoRootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "WorkDirectory must be inside the repository: $repoRoot"
}

$ReleaseDirectory = (Resolve-Path $ReleaseDirectory).Path
$prebuiltZipPath = Join-Path $ReleaseDirectory "crtsys-$Version-prebuilt.zip"
if (-not (Test-Path $prebuiltZipPath)) {
  throw "Prebuilt release zip was not found: $prebuiltZipPath"
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
$generatorByToolset = @{
  v142 = 'Visual Studio 17 2022'
  v143 = 'Visual Studio 17 2022'
  v145 = 'Visual Studio 18 2026'
}

Remove-Item -Recurse -Force -Path $WorkDirectory -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $WorkDirectory | Out-Null

$unpackDirectory = Join-Path $WorkDirectory 'prebuilt'
Expand-Archive -Path $prebuiltZipPath -DestinationPath $unpackDirectory -Force

$bundleRoot = Join-Path $unpackDirectory "crtsys-$Version"
if (-not (Test-Path (Join-Path $bundleRoot 'cmake\CrtSys.cmake'))) {
  if (Test-Path (Join-Path $unpackDirectory 'cmake\CrtSys.cmake')) {
    $bundleRoot = $unpackDirectory
  } else {
    $bundleRoot = Get-ChildItem -Path $unpackDirectory -Directory |
      Where-Object { Test-Path (Join-Path $_.FullName 'cmake\CrtSys.cmake') } |
      Select-Object -First 1 -ExpandProperty FullName
  }
}

if ([string]::IsNullOrWhiteSpace($bundleRoot) -or -not (Test-Path (Join-Path $bundleRoot 'cmake\CrtSys.cmake'))) {
  throw "Unpacked prebuilt release bundle does not contain cmake\CrtSys.cmake."
}

foreach ($requiredPath in @(
  "lib\native\$Toolset\$Architecture\$Configuration\crtsys.lib",
  "lib\native\$Toolset\$Architecture\$Configuration\Ldk.lib",
  'share\crtsys\cmake\crtsys-config.cmake',
  'share\crtsys\cmake\crtsys-config-version.cmake',
  'share\crtsys\cmake\CrtSys.cmake',
  'include\ntl\driver',
  'include\ntl\kmdf\driver',
  'include\ntl\kmdf\timer',
  'include\ntl\kmdf\work_item',
  'include\ntl\kmdf\child_list',
  'include\ntl\kmdf\registry',
  'include\ntl\kmdf\property',
  'include\.internal\adjust_link_order'
)) {
  $fullPath = Join-Path $bundleRoot $requiredPath
  if (-not (Test-Path $fullPath)) {
    throw "Prebuilt release bundle is missing expected file: $fullPath"
  }
}

if ($SkipDriverBuild) {
  Write-Host "Prebuilt release asset layout validation passed for $Architecture $Configuration."
  return
}

$consumerDirectory = Join-Path $WorkDirectory 'consumer'
New-Item -ItemType Directory -Force -Path $consumerDirectory | Out-Null

$cmakeBundleRoot = $bundleRoot.Replace('\', '/')
$driverDeclaration = switch ($DriverModel) {
  'NTL' { @'
set(CRTSYS_NTL_MAIN ON)
crtsys_add_driver(crtsys_release_asset_smoke main.cpp)
'@ }
  'WDM' { @'
set(CRTSYS_NTL_MAIN OFF)
crtsys_add_driver(crtsys_release_asset_smoke main.cpp)
'@ }
  'KMDF' { @'
set(CRTSYS_NTL_MAIN OFF)
crtsys_add_driver(crtsys_release_asset_smoke KMDF 1.15 main.cpp)
'@ }
  'NTL_KMDF' { @'
set(CRTSYS_NTL_MAIN OFF)
crtsys_add_driver(crtsys_release_asset_smoke KMDF 1.15 NTL main.cpp)
'@ }
}

$cmakeLists = @"
cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(crtsys_release_asset_smoke LANGUAGES C CXX)

find_package(crtsys CONFIG REQUIRED PATHS "$cmakeBundleRoot" NO_DEFAULT_PATH)

$driverDeclaration
target_compile_features(crtsys_release_asset_smoke PRIVATE cxx_std_17)
"@
Set-Content -LiteralPath (Join-Path $consumerDirectory 'CMakeLists.txt') -Value $cmakeLists -Encoding UTF8

$mainCpp = switch ($DriverModel) {
  'NTL' { @'
#include <string>
#include <vector>

#include <ntl/driver>

ntl::status ntl::main(ntl::driver& driver,
                      const std::wstring& registry_path) {
  (void)registry_path;

  std::vector<int> values = {1, 2, 3};
  driver.on_unload([values]() {
    (void)values;
  });

  return ntl::status::ok();
}
'@ }
  'WDM' { @'
#include <ntddk.h>

#include <numeric>
#include <vector>

extern "C" DRIVER_UNLOAD CrtSysReleaseSmokeDriverUnload;
extern "C" DRIVER_INITIALIZE DriverEntry;

extern "C" void CrtSysReleaseSmokeDriverUnload(PDRIVER_OBJECT driver_object) {
  UNREFERENCED_PARAMETER(driver_object);
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object,
                                 PUNICODE_STRING registry_path) {
  UNREFERENCED_PARAMETER(registry_path);

  const std::vector<int> values{1, 2, 3};
  if (std::accumulate(values.begin(), values.end(), 0) != 6) {
    return STATUS_UNSUCCESSFUL;
  }

  driver_object->DriverUnload = CrtSysReleaseSmokeDriverUnload;
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
#include <vector>

namespace {
constexpr GUID sample_interface = {
    0x3dfc4dc0, 0x6d61, 0x4a97,
    {0xa1, 0x10, 0x2a, 0xa8, 0x2f, 0x96, 0x87, 0xe1}};
}

ntl::status ntl::kmdf::main(driver_builder& builder,
                            const std::wstring& registry_path) {
  (void)registry_path;

  const std::vector<int> values{1, 2, 3};
  if (std::accumulate(values.begin(), values.end(), 0) != 6) {
    return STATUS_UNSUCCESSFUL;
  }

  kmdf::driver_config config;
  config.on_device_add<
      +[](kmdf::driver, kmdf::device_init& init) noexcept -> NTSTATUS {
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

        ntl::kmdf::object_attributes attributes;
        attributes.execution_level(WdfExecutionLevelPassive);
        auto created = init.try_create(&attributes);
        if (!created) {
          return created.status();
        }

        ntl::kmdf::object_attributes memory_attributes;
        memory_attributes.parent(created.value());
        auto transfer = ntl::kmdf::memory::try_allocate(
            sizeof(ULONG) * 4, NonPagedPoolNx, "NTLm",
            &memory_attributes);
        if (!transfer) {
          return transfer.status();
        }
        ULONG sample_data[4]{1, 2, 3, 4};
        ntl::status status = transfer->try_copy_from(
            0, sample_data, sizeof(sample_data));
        if (status.is_err()) {
          return status;
        }

        auto outgoing = ntl::kmdf::driver_request::try_create(
            created->default_io_target());
        if (!outgoing) {
          return outgoing.status();
        }

        status = created.value().try_create_interface(sample_interface);
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
Set-Content -LiteralPath (Join-Path $consumerDirectory 'main.cpp') -Value $mainCpp -Encoding UTF8

$buildDirectory = Join-Path $WorkDirectory "build_${Toolset}_$Architecture"
$configureArgs = @(
  '-S', $consumerDirectory,
  '-B', $buildDirectory,
  '-G', $generatorByToolset[$Toolset],
  '-A', $generatorPlatform,
  '-T', $(if ($Toolset -eq 'v142') { 'v142,host=x64' } else { 'host=x64' }),
  "-DCRTSYS_PREBUILT_TOOLSET=$Toolset",
  "-DCRTSYS_WDK_VERSION=$WdkVersion",
  "-DLDK_WDK_VERSION=$WdkVersion",
  "-DCMAKE_SYSTEM_VERSION=$WindowsSdkVersion",
  "-DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=$WindowsSdkVersion"
)

Write-Host "Configuring prebuilt release asset $DriverModel consumer for $Architecture $Configuration with Windows SDK $WindowsSdkVersion and WDK $WdkVersion"
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
  throw "CMake configure failed with exit code $LASTEXITCODE."
}

Write-Host "Building prebuilt release asset $DriverModel consumer for $Architecture $Configuration"
& cmake --build $buildDirectory --config $Configuration --target crtsys_release_asset_smoke --parallel
if ($LASTEXITCODE -ne 0) {
  throw "CMake build failed with exit code $LASTEXITCODE."
}

$driverPath = Join-Path $buildDirectory "$Configuration\crtsys_release_asset_smoke.sys"
if (-not (Test-Path $driverPath)) {
  throw "Prebuilt release asset consumer driver was not produced: $driverPath"
}

Write-Host "Prebuilt release asset $DriverModel consumer test passed: $driverPath"
