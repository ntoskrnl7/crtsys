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
  'include\ntl\kmdf\dma',
  'include\ntl\kmdf\usb',
  'include\ntl\kmdf\child_list',
  'include\ntl\kmdf\registry',
  'include\ntl\kmdf\property',
  'include\ntl\kmdf\object',
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

constexpr auto sample_program_dma =
    +[](ntl::kmdf::dma_transaction, ntl::kmdf::device, void*,
        WDF_DMA_DIRECTION,
        ntl::kmdf::scatter_gather_list) noexcept { return true; };

constexpr auto sample_usb_reader =
    +[](ntl::kmdf::usb_pipe, ntl::kmdf::memory, size_t,
        void*) noexcept {};

constexpr auto sample_usb_reader_failure =
    +[](ntl::kmdf::usb_pipe, ntl::status,
        USBD_STATUS) noexcept { return false; };

constexpr auto sample_standalone_dpc =
    +[](ntl::kmdf::dpc) noexcept {};

struct sample_general_object_context {
  explicit sample_general_object_context(ULONG value) noexcept : value(value) {}
  ULONG value;
};

[[maybe_unused]] void compile_common_object_surface(
    ntl::kmdf::device device) {
  using namespace ntl::kmdf;

  object_attributes attributes;
  attributes.parent(device);

  auto general = owned_object::try_create<sample_general_object_context>(
      &attributes, 42);
  if (general) {
    auto reference = general->reference();
    auto moved_reference = std::move(reference);
    (void)moved_reference.context<sample_general_object_context>().value;
  }
  object_reference device_reference{device};

  auto spin = spin_lock::try_create(&attributes);
  if (spin) {
    spin->lock();
    spin->unlock();
  }

  auto wait = wait_lock::try_create(&attributes);
  if (wait && wait->try_lock())
    wait->unlock();

  auto cache = lookaside::try_create(
      sizeof(ULONG) * 4, NonPagedPoolNx, "NTLk", &attributes);
  if (cache) {
    auto allocation = cache->try_allocate();
    if (allocation)
      (void)allocation->data<ULONG>();
  }

  auto label = string::try_create(L"release smoke");
  auto objects = collection::try_create(&attributes);
  if (label && objects) {
    (void)label->view();
    (void)objects->try_add(*label);
    objects->remove(*label);
  }

  auto dpc_settings = dpc_config::with_callback<sample_standalone_dpc>();
  dpc_settings.automatic_serialization(false);
  (void)dpc::try_create(device, dpc_settings);
}

[[maybe_unused]] void compile_dma_surface(
    ntl::kmdf::device device, ntl::kmdf::request request) {
  using namespace ntl::kmdf;

  dma_enabler_config config(WdfDmaProfileScatterGather64, 1024 * 1024);
  config.on_enable<+[](dma_enabler) noexcept -> NTSTATUS {
    return STATUS_SUCCESS;
  }>();
  auto enabler = dma_enabler::try_create(device, config);
  if (!enabler)
    return;

  PHYSICAL_ADDRESS device_address{};
  dma_system_profile_config system_profile(
      device_address, Width8Bits, nullptr);
  (void)enabler->try_configure_system_profile(
      system_profile, WdfDmaDirectionReadFromDevice);

  common_buffer_config aligned(15);
  (void)common_buffer::try_create(enabler.value(), 4096, aligned);

  auto transaction = dma_transaction::try_create(enabler.value());
  if (transaction) {
    (void)transaction->try_initialize_request<sample_program_dma>(
        request, WdfDmaDirectionReadFromDevice);
    UCHAR dma_buffer[64]{};
    (void)transaction->try_initialize<sample_program_dma>(
        nullptr, static_cast<void*>(dma_buffer), sizeof(dma_buffer),
        WdfDmaDirectionReadFromDevice);
  }
}

[[maybe_unused]] void compile_usb_surface(ntl::kmdf::device device) {
  using namespace ntl::kmdf;

  usb_device_create_config create_config;
  auto usb = usb_device::try_create(device, create_config);
  if (!usb)
    return;

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

  auto request = driver_request::try_create(pipe.target());
  auto transfer = memory::try_preallocated(bytes, sizeof(bytes));
  if (request && transfer) {
    auto owned = std::move(request).value();
    (void)pipe.try_format_read(owned, transfer.value());
    auto setup = usb_control_setup_packet::vendor(
        BmRequestDeviceToHost, BmRequestToDevice, 1, 0, 0);
    (void)usb->try_format_control(owned, setup, transfer.value());
  }

  auto reader = usb_continuous_reader_config::with_completion<
      sample_usb_reader>(sizeof(bytes));
  reader.on_failure<sample_usb_reader_failure>().pending_reads(1);
  (void)pipe.try_configure_reader(reader);
}

[[maybe_unused]] void compile_resource_and_power_surface(
    ntl::kmdf::device device, ntl::kmdf::resource_list resources) {
  using namespace ntl::kmdf;

  for (const resource_descriptor descriptor : resources) {
    (void)descriptor.memory();
    (void)descriptor.port();
    (void)descriptor.interrupt();
    (void)descriptor.dma();
    (void)descriptor.connection();
  }

  idle_policy idle(IdleCannotWakeFromS0);
  idle.timeout(1000, DriverManagedIdleTimeout)
      .user_control(IdleDoNotAllowUserControl)
      .enabled(true)
      .exclude_d3_cold(WdfUseDefault);
  (void)idle.try_apply(device);

  wake_policy wake;
  wake.device_state(PowerDeviceMaximum)
      .user_control(WakeDoNotAllowUserControl)
      .enabled(false);
  (void)wake.try_apply(device);
}

[[maybe_unused]] void compile_manual_queue_surface(
    ntl::kmdf::io_queue queue, ntl::kmdf::request request,
    ntl::kmdf::file file) {
  using namespace ntl::kmdf;

  request_parameters parameters;
  auto found = queue.try_find(nullptr, &file, &parameters);
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
}
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
