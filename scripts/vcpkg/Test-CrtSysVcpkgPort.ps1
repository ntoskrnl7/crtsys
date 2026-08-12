param(
  [ValidateSet(
    'x86-windows-static',
    'x64-windows-static',
    'arm64-windows-static'
  )]
  [string] $Triplet = 'x64-windows-static',

  [string] $VcpkgExe,

  [string] $WorkDirectory,

  [string] $DownloadsDirectory,

  [switch] $ContractOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$portRoot = Join-Path $repoRoot 'vcpkg\ports\crtsys'
$manifestPath = Join-Path $portRoot 'vcpkg.json'
$portfilePath = Join-Path $portRoot 'portfile.cmake'
$compatibilityPatchPath = Join-Path $portRoot 'fix-offline-source-build.patch'
$bridgePath = Join-Path $portRoot 'crtsys-vcpkg.targets'
$initScriptPath = Join-Path $portRoot 'tools\crtsys-vs-init.ps1'
$initCommandPath = Join-Path $portRoot 'tools\crtsys-vs-init.cmd'
$usagePath = Join-Path $portRoot 'usage'
$uiContractPath = Join-Path $repoRoot 'test\vcpkg\msbuild-ui-contract.proj'
$uiInitContractPath = Join-Path $repoRoot `
  'test\vcpkg\msbuild-init-contract.proj'
$uiInitTestPath = Join-Path $PSScriptRoot `
  'Test-CrtSysVcpkgVisualStudioInit.ps1'
$updatePortPath = Join-Path $PSScriptRoot 'Update-CrtSysVcpkgPort.ps1'
$prepareOfficialPortPath = Join-Path $PSScriptRoot `
  'Prepare-CrtSysOfficialVcpkgUpdate.ps1'
$officialConsumerTestPath = Join-Path $PSScriptRoot `
  'Test-CrtSysOfficialVcpkgConsumer.ps1'
$publishRegistryPath = Join-Path $PSScriptRoot `
  'Publish-CrtSysVcpkgRegistry.ps1'
$registryAutomationTestPath = Join-Path $PSScriptRoot `
  'Test-CrtSysVcpkgRegistryAutomation.ps1'
$prepareReleasePath = Join-Path $repoRoot `
  'scripts\release\Prepare-CrtSysRelease.ps1'
$packageWorkflowPath = Join-Path $repoRoot '.github\workflows\package.yml'
$officialWorkflowPath = Join-Path $repoRoot `
  '.github\workflows\vcpkg-official.yml'

function Assert-FileContains {
  param(
    [Parameter(Mandatory = $true)]
    [string] $Path,

    [Parameter(Mandatory = $true)]
    [string[]] $Tokens
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    throw "Required file was not found: $Path"
  }

  $content = Get-Content -LiteralPath $Path -Raw
  foreach ($token in $Tokens) {
    if (-not $content.Contains($token)) {
      throw "Expected '$Path' to contain '$token'."
    }
  }
}

function Assert-FileDoesNotContain {
  param(
    [Parameter(Mandatory = $true)]
    [string] $Path,

    [Parameter(Mandatory = $true)]
    [string[]] $Tokens
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    throw "Required file was not found: $Path"
  }

  $content = Get-Content -LiteralPath $Path -Raw
  foreach ($token in $Tokens) {
    if ($content.Contains($token)) {
      throw "Expected '$Path' not to contain '$token'."
    }
  }
}

function Resolve-VcpkgExecutable {
  param([string] $RequestedPath)

  if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
    return (Resolve-Path -LiteralPath $RequestedPath).Path
  }

  $command = Get-Command vcpkg -ErrorAction SilentlyContinue
  if ($command) {
    return $command.Source
  }

  $vswhere = Join-Path ${env:ProgramFiles(x86)} `
    'Microsoft Visual Studio\Installer\vswhere.exe'
  if (Test-Path -LiteralPath $vswhere) {
    $installations = @(
      (& $vswhere -all -products * -format json | ConvertFrom-Json) |
        Sort-Object { [version]$_.installationVersion } -Descending
    )
    foreach ($installation in $installations) {
      $candidate = Join-Path $installation.installationPath 'VC\vcpkg\vcpkg.exe'
      if (Test-Path -LiteralPath $candidate) {
        return $candidate
      }
    }
  }

  throw 'vcpkg.exe was not found. Pass -VcpkgExe or add vcpkg to PATH.'
}

function Resolve-MsBuildExecutable {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} `
    'Microsoft Visual Studio\Installer\vswhere.exe'
  if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "vswhere.exe was not found: $vswhere"
  }

  $installations = @(
    (& $vswhere -all -products * -requires Microsoft.Component.MSBuild `
        -format json | ConvertFrom-Json) |
      Sort-Object { [version]$_.installationVersion } -Descending
  )
  foreach ($installation in $installations) {
    foreach ($relativePath in @(
      'MSBuild\Current\Bin\amd64\MSBuild.exe',
      'MSBuild\Current\Bin\MSBuild.exe'
    )) {
      $candidate = Join-Path $installation.installationPath $relativePath
      if (Test-Path -LiteralPath $candidate) {
        return $candidate
      }
    }
  }

  throw 'MSBuild.exe was not found.'
}

foreach ($requiredPath in @(
  $manifestPath,
  $portfilePath,
  $compatibilityPatchPath,
  $bridgePath,
  $initScriptPath,
  $initCommandPath,
  $usagePath,
  $uiContractPath,
  $uiInitContractPath,
  $uiInitTestPath,
  $updatePortPath,
  $prepareOfficialPortPath,
  $officialConsumerTestPath,
  $publishRegistryPath,
  $registryAutomationTestPath,
  $prepareReleasePath,
  $packageWorkflowPath,
  $officialWorkflowPath
)) {
  if (-not (Test-Path -LiteralPath $requiredPath)) {
    throw "Required vcpkg packaging file was not found: $requiredPath"
  }
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$projectVersion = & (Join-Path $repoRoot 'scripts\nuget\Get-CrtSysVersion.ps1')
if ($manifest.name -ne 'crtsys') {
  throw "Expected vcpkg port name 'crtsys', got '$($manifest.name)'."
}
if ($manifest.'version-semver' -ne $projectVersion) {
  throw "The vcpkg port version '$($manifest.'version-semver')' does not match crtsys '$projectVersion'."
}
if ($manifest.license -ne 'MIT AND BSD-2-Clause AND BSD-3-Clause') {
  throw 'Expected the vcpkg port to declare all distributed licenses.'
}
foreach ($feature in @('content-codecs', 'msquic-headers')) {
  if (-not ($manifest.features.PSObject.Properties.Name -contains $feature)) {
    throw "The vcpkg port is missing the optional '$feature' feature."
  }
}
foreach ($supportToken in @(
  'windows',
  '!uwp',
  '!mingw',
  'static',
  'staticcrt',
  'x86',
  'x64',
  'arm64'
)) {
  if (-not $manifest.supports.Contains($supportToken)) {
    throw "The vcpkg supports expression is missing '$supportToken'."
  }
}

$portfileTokens = @(
  'ONLY_STATIC_LIBRARY',
  'ONLY_STATIC_CRT',
  'vcpkg_from_github',
  'vcpkg_cmake_configure',
  'WINDOWS_USE_MSBUILD',
  'vcpkg_cmake_install',
  'lib/manual-link',
  'crtsys-vcpkg.targets',
  'crtsys-vs-init.ps1',
  'crtsys-vs-init.cmd',
  'vcpkg_install_copyright',
  'requires Microsoft Visual Studio with the C++ workload',
  'Microsoft.Windows.SDK.CPP',
  'Microsoft.Windows.WDK.',
  'WDKContentRoot',
  'CRTSYS_WDK_VERSION',
  '10.0.28000.2526',
  'docs/third-party-notices.md',
  '${LDK_SOURCE_PATH}/LICENSE',
  '${RAW_PDB_SOURCE_PATH}/LICENSE',
  '${UCXXRT_SOURCE_PATH}/LICENSE'
)
if ([version]$projectVersion -lt [version]'0.1.42') {
  $portfileTokens += 'fix-offline-source-build.patch'
  Assert-FileContains -Path $compatibilityPatchPath -Tokens @(
    'NOT CRTSYS_NATIVE_ARCH STREQUAL "ARM64" AND',
    'target_link_libraries(crtsys PRIVATE WDK::NTOSKRNL)',
    'target_link_options(crtsys PUBLIC "/INCLUDE:iscntrl")',
    'list(APPEND _crtsys_prebuilt_link_libraries WDK::NTOSKRNL)',
    'NOT _crtsys_driver_arch STREQUAL "ARM64"',
    'target_link_options(${_target} PRIVATE "/INCLUDE:iscntrl")',
    'src/custom/crt/ctype_arm64.c'
  )
}
Assert-FileContains -Path $portfilePath -Tokens $portfileTokens
if ([version]$projectVersion -ge [version]'0.1.42') {
  Assert-FileDoesNotContain -Path $portfilePath -Tokens @(
    'fix-offline-source-build.patch'
  )
}
Assert-FileDoesNotContain -Path $portfilePath -Tokens @(
  'prebuilt.zip',
  'VCPKG_POLICY_SKIP_CRT_LINKAGE_CHECK',
  'VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES'
)
Assert-FileContains -Path $bridgePath -Tokens @(
  '<CrtSysVcpkgIntegration>true</CrtSysVcpkgIntegration>',
  '<CrtSysRoot Condition=',
  'debug\lib\manual-link',
  'lib\manual-link',
  'build\native\crtsys.targets',
  'CrtSysValidateVcpkgBridge'
)
Assert-FileContains -Path $usagePath -Tokens @(
  'find_package(crtsys CONFIG REQUIRED)',
  'crtsys_add_driver',
  'crtsys-vs-init',
  'reload the'
)
Assert-FileContains -Path $initScriptPath -Tokens @(
  'VcpkgEnableManifest',
  'VcpkgTriplet',
  'crtsys-vcpkg.targets',
  'crtsys-vcpkg-init:props:begin',
  'crtsys-vcpkg-init:targets:begin',
  '[switch] $Remove'
)
Assert-FileContains -Path $uiInitContractPath -Tokens @(
  'Directory.Build.props',
  'Directory.Build.targets',
  'CrtSysVcpkgIntegration',
  'PropertyPageSchema'
)
Assert-FileContains -Path $updatePortPath -Tokens @(
  'version-semver',
  'Get-FileHash',
  'SHA512',
  'RegistryBaseline',
  'Encoding UTF8'
)
Assert-FileContains -Path $prepareOfficialPortPath -Tokens @(
  'AllowNewPort',
  'x-add-version crtsys',
  'x-add-version --all',
  'x-ci-verify-versions',
  'Official crtsys'
)
Assert-FileContains -Path $officialConsumerTestPath -Tokens @(
  'crtsys-vs-init.cmd',
  'msbuild-init-contract.proj',
  'msbuild-ui-contract.proj',
  'crtsys_install_consumer.sys',
  'Visual Studio 17 2022'
)
Assert-FileContains -Path $publishRegistryPath -Tokens @(
  'x-add-version',
  'Set-PublishedVersionTree',
  'HEAD:ports/crtsys',
  'version-semver',
  'git-tree',
  'Refusing to republish non-current',
  'Publish a new port-version instead of rewriting history',
  'HEAD:refs/heads/$RegistryBranch'
)
Assert-FileContains -Path $registryAutomationTestPath -Tokens @(
  'Idempotent registry retry',
  'Refusing to republish non-current',
  'Registry automation did not reject a baseline rollback',
  'Assert-RegistryVersionTree',
  'read-tree'
)
Assert-FileContains -Path $prepareReleasePath -Tokens @(
  'Update-CrtSysVcpkgPort.ps1',
  'vcpkg/ports/crtsys/vcpkg.json'
)
Assert-FileContains -Path $packageWorkflowPath -Tokens @(
  'publish-vcpkg-registry:',
  'Publish-CrtSysVcpkgRegistry.ps1',
  'Update-CrtSysVcpkgPort.ps1',
  'steps.registry.outputs.baseline',
  'HEAD:main'
)
Assert-FileContains -Path $officialWorkflowPath -Tokens @(
  'workflow_dispatch:',
  'mode:',
  'validate',
  'submit',
  'VCPKG_UPSTREAM_TOKEN',
  'Prepare-CrtSysOfficialVcpkgUpdate.ps1',
  'Test-CrtSysOfficialVcpkgConsumer.ps1',
  'microsoft/vcpkg',
  'cancel-in-progress: false'
)

$portfile = Get-Content -LiteralPath $portfilePath -Raw
$sha512Match = [regex]::Match(
  $portfile,
  '(?ms)SHA512\s+([0-9a-fA-F]{128})(?:\s|\))'
)
if (-not $sha512Match.Success) {
  throw 'The vcpkg port does not contain a valid SHA-512 release hash.'
}

Write-Host "crtsys vcpkg port contract passed for version $projectVersion."
& $uiInitTestPath
if ($ContractOnly) {
  return
}

$VcpkgExe = Resolve-VcpkgExecutable -RequestedPath $VcpkgExe
if ([string]::IsNullOrWhiteSpace($WorkDirectory)) {
  $WorkDirectory = Join-Path $repoRoot ".local\vcpkg-port-test\$Triplet"
}
if ([string]::IsNullOrWhiteSpace($DownloadsDirectory)) {
  $DownloadsDirectory = Join-Path $repoRoot '.local\vcpkg-downloads'
}

$WorkDirectory = [System.IO.Path]::GetFullPath($WorkDirectory)
$DownloadsDirectory = [System.IO.Path]::GetFullPath($DownloadsDirectory)
$repoRootPrefix = $repoRoot.TrimEnd('\') + '\'
foreach ($controlledPath in @($WorkDirectory, $DownloadsDirectory)) {
  if (-not $controlledPath.StartsWith(
      $repoRootPrefix,
      [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Test paths must stay inside the repository: $controlledPath"
  }
}

Remove-Item -LiteralPath $WorkDirectory -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $WorkDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $DownloadsDirectory | Out-Null

$installRoot = Join-Path $WorkDirectory 'installed'
$buildtreesRoot = Join-Path $WorkDirectory 'buildtrees'
$packagesRoot = Join-Path $WorkDirectory 'packages'
$testManifest = [ordered]@{
  name = 'crtsys-vcpkg-port-test'
  'version-string' = '0'
  dependencies = @('crtsys')
}
$testManifest | ConvertTo-Json -Depth 4 |
  Set-Content -LiteralPath (Join-Path $WorkDirectory 'vcpkg.json') -Encoding UTF8
$installArguments = @(
  'install',
  "--triplet=$Triplet",
  "--overlay-ports=$(Join-Path $repoRoot 'vcpkg\ports')",
  "--x-install-root=$installRoot",
  "--x-buildtrees-root=$buildtreesRoot",
  "--x-packages-root=$packagesRoot",
  "--downloads-root=$DownloadsDirectory"
)

Write-Host "Installing crtsys:$Triplet with $VcpkgExe"
Push-Location $WorkDirectory
try {
  & $VcpkgExe @installArguments
  if ($LASTEXITCODE -ne 0) {
    throw "vcpkg install failed with exit code $LASTEXITCODE."
  }
} finally {
  Pop-Location
}

$packageRoot = Join-Path $installRoot $Triplet
$architectureName = switch -Regex ($Triplet) {
  '^x86-' { 'x86'; break }
  '^x64-' { 'x64'; break }
  '^arm64-' { 'ARM64'; break }
  '^arm-' { 'ARM'; break }
  default { throw "Unable to map triplet architecture: $Triplet" }
}

$requiredInstalledPaths = @(
  'include\ntl\driver',
  'share\crtsys\crtsys-config.cmake',
  'share\crtsys\CrtSys.cmake',
  'share\crtsys\msbuild\crtsys-vcpkg.targets',
  'share\crtsys\usage',
  'share\crtsys\copyright',
  'tools\crtsys\crtsys-vs-init.ps1',
  'tools\crtsys\crtsys-vs-init.cmd',
  'build\native\crtsys.targets',
  'build\native\crtsys.xml',
  'build\native\crtsys-kmdf.xml',
  'debug\lib\manual-link\crtsys.lib',
  'debug\lib\manual-link\Ldk.lib',
  'lib\manual-link\crtsys.lib',
  'lib\manual-link\Ldk.lib'
)
foreach ($relativePath in $requiredInstalledPaths) {
  $fullPath = Join-Path $packageRoot $relativePath
  if (-not (Test-Path -LiteralPath $fullPath)) {
    throw "The installed vcpkg package is missing: $fullPath"
  }
}

$installedInitializer = Join-Path $packageRoot `
  'tools\crtsys\crtsys-vs-init.cmd'
& $installedInitializer -ProjectRoot $WorkDirectory -Triplet $Triplet
if ($LASTEXITCODE -ne 0) {
  throw "Installed crtsys-vs-init command failed with exit code $LASTEXITCODE."
}

$generatedPropsPath = Join-Path $WorkDirectory 'Directory.Build.props'
$generatedTargetsPath = Join-Path $WorkDirectory 'Directory.Build.targets'
Assert-FileContains -Path $generatedPropsPath -Tokens @(
  'VcpkgEnableManifest',
  $Triplet
)
Assert-FileContains -Path $generatedTargetsPath -Tokens @(
  'crtsys-vcpkg-init:targets:begin',
  'crtsys-vcpkg.targets'
)

$msbuild = Resolve-MsBuildExecutable
& $msbuild $uiInitContractPath `
  /nologo `
  /verbosity:minimal `
  /target:Validate `
  "/property:ConsumerRoot=$WorkDirectory" `
  "/property:ConsumerInstallRoot=$installRoot"
if ($LASTEXITCODE -ne 0) {
  throw "The generated vcpkg MSBuild integration failed with exit code $LASTEXITCODE."
}

& $installedInitializer -ProjectRoot $WorkDirectory -Remove
if ($LASTEXITCODE -ne 0) {
  throw "Installed crtsys-vs-init -Remove failed with exit code $LASTEXITCODE."
}
foreach ($generatedPath in @($generatedPropsPath, $generatedTargetsPath)) {
  if (Test-Path -LiteralPath $generatedPath) {
    throw "Installed initializer left a generated file behind: $generatedPath"
  }
}

$uiContractCases = @(
  @{ Name = 'Default'; DriverType = 'WDM'; Wdm = 'Default'; Kmdf = '' },
  @{ Name = 'NtlWdm'; DriverType = 'WDM'; Wdm = 'NtlWdm'; Kmdf = '' },
  @{ Name = 'NtlKmdf'; DriverType = 'KMDF'; Wdm = ''; Kmdf = 'NtlKmdf' },
  @{ Name = 'NtlMinifilter'; DriverType = 'WDM'; Wdm = 'NtlMinifilter'; Kmdf = '' },
  @{ Name = 'NtlWfp'; DriverType = 'WDM'; Wdm = 'NtlWfp'; Kmdf = '' }
)
foreach ($uiContractCase in $uiContractCases) {
  & $msbuild $uiContractPath `
    /nologo `
    /verbosity:minimal `
    /target:Validate `
    "/property:CrtSysPackageRoot=$packageRoot" `
    "/property:DriverType=$($uiContractCase.DriverType)" `
    "/property:CrtSysWdmEntryPoint=$($uiContractCase.Wdm)" `
    "/property:CrtSysKmdfEntryPoint=$($uiContractCase.Kmdf)" `
    "/property:ExpectedDriverModel=$($uiContractCase.Name)"
  if ($LASTEXITCODE -ne 0) {
    throw "The vcpkg MSBuild UI contract for $($uiContractCase.Name) failed with exit code $LASTEXITCODE."
  }
}

Write-Host "crtsys vcpkg install and MSBuild UI contract passed: $packageRoot"
