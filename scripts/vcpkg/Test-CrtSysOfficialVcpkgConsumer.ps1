param(
  [Parameter(Mandatory = $true)]
  [string] $VcpkgRepositoryDirectory,

  [ValidateSet('x64-windows-static')]
  [string] $Triplet = 'x64-windows-static',

  [string] $WindowsSdkVersion = '10.0.22621.0',

  [string] $WorkDirectory,

  [switch] $KeepWorkDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$VcpkgRepositoryDirectory = (
  Resolve-Path -LiteralPath $VcpkgRepositoryDirectory
).Path
if ([string]::IsNullOrWhiteSpace($WorkDirectory)) {
  $WorkDirectory = Join-Path $repoRoot '.local\official-vcpkg-consumer-test'
}
$WorkDirectory = [System.IO.Path]::GetFullPath($WorkDirectory)
$repoRootPrefix = $repoRoot.TrimEnd('\') + '\'
if (-not $WorkDirectory.StartsWith(
    $repoRootPrefix,
    [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "WorkDirectory must stay inside the crtsys repository: $WorkDirectory"
}

$installRoot = Join-Path $VcpkgRepositoryDirectory 'installed'
$packageRoot = Join-Path $installRoot $Triplet
$initializer = Join-Path $packageRoot 'tools\crtsys\crtsys-vs-init.cmd'
$cmakePackage = Join-Path $packageRoot 'share\crtsys\crtsys-config.cmake'
$uiContract = Join-Path $repoRoot 'test\vcpkg\msbuild-ui-contract.proj'
$uiInitContract = Join-Path $repoRoot 'test\vcpkg\msbuild-init-contract.proj'
$consumerSource = Join-Path $repoRoot 'test\cmake\install-consumer'

foreach ($requiredPath in @(
  $initializer,
  $cmakePackage,
  $uiContract,
  $uiInitContract,
  (Join-Path $consumerSource 'CMakeLists.txt')
)) {
  if (-not (Test-Path -LiteralPath $requiredPath)) {
    throw "Required official vcpkg consumer input was not found: $requiredPath"
  }
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

function Resolve-VisualStudioGenerator {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} `
    'Microsoft Visual Studio\Installer\vswhere.exe'
  $installation = @(
    (& $vswhere -all -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -format json | ConvertFrom-Json) |
      Sort-Object { [version]$_.installationVersion } -Descending
  ) | Select-Object -First 1
  if (-not $installation) {
    throw 'A Visual Studio C++ installation was not found.'
  }

  $major = ([version]$installation.installationVersion).Major
  switch ($major) {
    18 { return 'Visual Studio 18 2026' }
    17 { return 'Visual Studio 17 2022' }
    default {
      throw "Unsupported Visual Studio version: $($installation.installationVersion)"
    }
  }
}

function Write-Utf8Text {
  param(
    [Parameter(Mandatory = $true)][string] $Path,
    [Parameter(Mandatory = $true)][string] $Content
  )

  [System.IO.File]::WriteAllText(
    $Path,
    $Content,
    [System.Text.UTF8Encoding]::new($false)
  )
}

if (Test-Path -LiteralPath $WorkDirectory) {
  Remove-Item -LiteralPath $WorkDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $WorkDirectory -Force | Out-Null
Write-Utf8Text `
  -Path (Join-Path $WorkDirectory 'vcpkg.json') `
  -Content @'
{
  "name": "crtsys-official-vcpkg-consumer-test",
  "version-string": "0",
  "dependencies": [
    "crtsys"
  ]
}
'@

$generatedProps = Join-Path $WorkDirectory 'Directory.Build.props'
$generatedTargets = Join-Path $WorkDirectory 'Directory.Build.targets'
try {
  & $initializer -ProjectRoot $WorkDirectory -Triplet $Triplet
  if ($LASTEXITCODE -ne 0) {
    throw "crtsys-vs-init failed with exit code $LASTEXITCODE."
  }

  $msbuild = Resolve-MsBuildExecutable
  & $msbuild $uiInitContract `
    /nologo `
    /verbosity:minimal `
    /target:Validate `
    "/property:ConsumerRoot=$WorkDirectory" `
    "/property:ConsumerInstallRoot=$installRoot"
  if ($LASTEXITCODE -ne 0) {
    throw "Official vcpkg initializer contract failed with exit code $LASTEXITCODE."
  }

  $uiContractCases = @(
    @{ Name = 'Default'; DriverType = 'WDM'; Wdm = 'Default'; Kmdf = '' },
    @{ Name = 'NtlWdm'; DriverType = 'WDM'; Wdm = 'NtlWdm'; Kmdf = '' },
    @{ Name = 'NtlKmdf'; DriverType = 'KMDF'; Wdm = ''; Kmdf = 'NtlKmdf' },
    @{ Name = 'NtlMinifilter'; DriverType = 'WDM'; Wdm = 'NtlMinifilter'; Kmdf = '' },
    @{ Name = 'NtlWfp'; DriverType = 'WDM'; Wdm = 'NtlWfp'; Kmdf = '' }
  )
  foreach ($uiContractCase in $uiContractCases) {
    & $msbuild $uiContract `
      /nologo `
      /verbosity:minimal `
      /target:Validate `
      "/property:CrtSysPackageRoot=$packageRoot" `
      "/property:DriverType=$($uiContractCase.DriverType)" `
      "/property:CrtSysWdmEntryPoint=$($uiContractCase.Wdm)" `
      "/property:CrtSysKmdfEntryPoint=$($uiContractCase.Kmdf)" `
      "/property:ExpectedDriverModel=$($uiContractCase.Name)"
    if ($LASTEXITCODE -ne 0) {
      throw "Official vcpkg UI contract $($uiContractCase.Name) failed with exit code $LASTEXITCODE."
    }
  }

  & $initializer -ProjectRoot $WorkDirectory -Remove
  if ($LASTEXITCODE -ne 0) {
    throw "crtsys-vs-init -Remove failed with exit code $LASTEXITCODE."
  }

  $buildDirectory = Join-Path $WorkDirectory 'cmake-build'
  $generator = Resolve-VisualStudioGenerator
  $configureArguments = @(
    '-S', $consumerSource,
    '-B', $buildDirectory,
    '-G', $generator,
    '-A', 'x64',
    "-DCRTSYS_PACKAGE_ROOT=$packageRoot",
    '-DCRTSYS_INSTALL_CONSUMER_ENABLE_KERNEL_MSQUIC=OFF',
    '-DCRTSYS_INSTALL_CONSUMER_ENABLE_KERNEL_CONTENT_CODECS=OFF',
    '-DCRTSYS_INSTALL_CONSUMER_ENABLE_USER_CONTENT_CODECS=OFF',
    "-DCRTSYS_WDK_VERSION=$WindowsSdkVersion",
    "-DLDK_WDK_VERSION=$WindowsSdkVersion",
    "-DCMAKE_SYSTEM_VERSION=$WindowsSdkVersion",
    "-DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=$WindowsSdkVersion"
  )
  & cmake @configureArguments
  if ($LASTEXITCODE -ne 0) {
    throw "Official vcpkg CMake consumer configure failed with exit code $LASTEXITCODE."
  }

  foreach ($configuration in @('Debug', 'Release')) {
    & cmake --build $buildDirectory `
      --config $configuration `
      --target crtsys_install_consumer `
      --parallel
    if ($LASTEXITCODE -ne 0) {
      throw "Official vcpkg CMake $configuration consumer build failed with exit code $LASTEXITCODE."
    }

    $driverPath = Join-Path `
      $buildDirectory `
      "$configuration\crtsys_install_consumer.sys"
    if (-not (Test-Path -LiteralPath $driverPath)) {
      throw "Official vcpkg CMake consumer did not produce a driver: $driverPath"
    }
    Write-Host "Official vcpkg $configuration consumer passed: $driverPath"
  }
} finally {
  if ((Test-Path -LiteralPath $initializer) -and
      ((Test-Path -LiteralPath $generatedProps) -or
       (Test-Path -LiteralPath $generatedTargets))) {
    & $initializer -ProjectRoot $WorkDirectory -Remove | Out-Host
  }
  if (-not $KeepWorkDirectory -and
      (Test-Path -LiteralPath $WorkDirectory)) {
    Remove-Item -LiteralPath $WorkDirectory -Recurse -Force
  }
}
