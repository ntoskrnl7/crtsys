param(
  [ValidateSet(
    'x86-windows-static',
    'x64-windows-static',
    'arm-windows-static',
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
$bridgePath = Join-Path $portRoot 'crtsys-vcpkg.targets'
$usagePath = Join-Path $portRoot 'usage'
$uiContractPath = Join-Path $repoRoot 'test\vcpkg\msbuild-ui-contract.proj'

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
  $bridgePath,
  $usagePath,
  $uiContractPath
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
if ($manifest.license -ne 'MIT') {
  throw "Expected the vcpkg port to declare the MIT license."
}
foreach ($supportToken in @('windows', '!uwp', '!mingw', 'static', 'staticcrt')) {
  if (-not $manifest.supports.Contains($supportToken)) {
    throw "The vcpkg supports expression is missing '$supportToken'."
  }
}

Assert-FileContains -Path $portfilePath -Tokens @(
  'ONLY_STATIC_LIBRARY',
  'ONLY_STATIC_CRT',
  'vcpkg_download_distfile',
  'vcpkg_extract_source_archive',
  'crtsys_keep_package_architecture',
  'crtsys-vcpkg.targets',
  'vcpkg_install_copyright',
  'VCPKG_POLICY_SKIP_CRT_LINKAGE_CHECK'
)
Assert-FileContains -Path $bridgePath -Tokens @(
  '<CrtSysVcpkgIntegration>true</CrtSysVcpkgIntegration>',
  '<CrtSysRoot Condition=',
  'build\native\crtsys.targets',
  'CrtSysValidateVcpkgBridge'
)
Assert-FileContains -Path $usagePath -Tokens @(
  'find_package(crtsys CONFIG REQUIRED)',
  'crtsys_add_driver',
  'crtsys-vcpkg.targets',
  'Reload Visual Studio'
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
  'share\crtsys\cmake\crtsys-config.cmake',
  'share\crtsys\cmake\CrtSys.cmake',
  'share\crtsys\msbuild\crtsys-vcpkg.targets',
  'share\crtsys\usage',
  'share\crtsys\copyright',
  'build\native\crtsys.targets',
  'build\native\crtsys.xml',
  'build\native\crtsys-kmdf.xml',
  "lib\native\v143\$architectureName\Debug\crtsys.lib",
  "lib\native\v143\$architectureName\Release\crtsys.lib",
  "lib\native\v143\$architectureName\Release\Ldk.lib"
)
foreach ($relativePath in $requiredInstalledPaths) {
  $fullPath = Join-Path $packageRoot $relativePath
  if (-not (Test-Path -LiteralPath $fullPath)) {
    throw "The installed vcpkg package is missing: $fullPath"
  }
}

$unexpectedArchitectures = @('x86', 'x64', 'ARM', 'ARM64') |
  Where-Object { $_ -ne $architectureName }
foreach ($unexpectedArchitecture in $unexpectedArchitectures) {
  $unexpectedPath = Join-Path $packageRoot `
    "lib\native\v143\$unexpectedArchitecture"
  if (Test-Path -LiteralPath $unexpectedPath) {
    throw "The vcpkg package retained an unrelated architecture: $unexpectedPath"
  }
}

$msbuild = Resolve-MsBuildExecutable
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
