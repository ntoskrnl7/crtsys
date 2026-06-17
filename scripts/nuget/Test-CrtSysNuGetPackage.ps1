param(
  [string] $PackageDirectory,

  [Parameter(Mandatory = $true)]
  [string] $Version,

  [ValidateSet('Driver', 'App')]
  [string] $Consumer = 'Driver',

  [ValidateSet('x86', 'x64', 'ARM64')]
  [string] $Architecture = 'x64',

  [ValidateSet('Debug', 'Release')]
  [string] $Configuration = 'Release',

  [string] $WindowsSdkVersion = '10.0.22621.0',

  [string] $WdkVersion,

  [string] $WorkDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$isDriverConsumer = $Consumer -eq 'Driver'

if ($isDriverConsumer -and $Architecture -eq 'x86') {
  throw 'The crtsys NuGet package contains prebuilt driver libraries for x64 and ARM64 only.'
}

$msbuildPlatformByArchitecture = @{
  x86 = 'Win32'
  x64 = 'x64'
  ARM64 = 'ARM64'
}
$msbuildPlatform = $msbuildPlatformByArchitecture[$Architecture]

if ([string]::IsNullOrWhiteSpace($PackageDirectory)) {
  $PackageDirectory = Join-Path $repoRoot 'artifacts\nuget'
}

if ([string]::IsNullOrWhiteSpace($WorkDirectory)) {
  $WorkDirectory = Join-Path $repoRoot "artifacts\nuget-consumer-test\$Consumer\$Architecture\$Configuration"
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
$windowsKitsIncludeRoot = Join-Path $windowsKitsRoot 'Include'
if (-not (Test-Path $windowsKitsIncludeRoot)) {
  throw "Windows Kits include directory was not found: $windowsKitsIncludeRoot"
}

if ($isDriverConsumer -and [string]::IsNullOrWhiteSpace($WdkVersion)) {
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

if ($isDriverConsumer) {
  $wdkHeader = Join-Path $windowsKitsIncludeRoot "$WdkVersion\km\wdm.h"
  if (-not (Test-Path $wdkHeader)) {
    throw "WDK header was not found: $wdkHeader"
  }

  $wdkKernelLibDirectory = Join-Path $windowsKitsRoot "Lib\$WdkVersion\km\$Architecture"
  if (-not (Test-Path (Join-Path $wdkKernelLibDirectory 'ntoskrnl.lib'))) {
    throw "WDK kernel library directory is missing ntoskrnl.lib: $wdkKernelLibDirectory"
  }
}

Write-Host "Requested Windows SDK version: $WindowsSdkVersion"
if ($isDriverConsumer) {
  Write-Host "Resolved WDK version: $WdkVersion"
}

Remove-Item -Recurse -Force -Path $WorkDirectory -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $WorkDirectory | Out-Null

$testProjectSource = Join-Path $repoRoot 'test\nuget'
$projectNameByConsumer = @{
  Driver = 'crtsys_nuget_test.vcxproj'
  App = 'crtsys_nuget_app_test.vcxproj'
}
$projectName = $projectNameByConsumer[$Consumer]
if (-not (Test-Path (Join-Path $testProjectSource $projectName))) {
  throw "NuGet $Consumer consumer test project was not found under '$testProjectSource'."
}

$testRoot = Join-Path $WorkDirectory 'test'
$testProjectDirectory = Join-Path $testRoot 'nuget'
$cmakeTestDirectory = Join-Path $testRoot 'cmake'
New-Item -ItemType Directory -Force -Path $testProjectDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $cmakeTestDirectory | Out-Null

Copy-Item -Path (Join-Path $testProjectSource '*') -Destination $testProjectDirectory -Recurse -Force
Copy-Item -Path (Join-Path $repoRoot 'test\cmake\common') -Destination $cmakeTestDirectory -Recurse -Force
if ($isDriverConsumer) {
  Copy-Item -Path (Join-Path $repoRoot 'test\cmake\driver') -Destination $cmakeTestDirectory -Recurse -Force
} else {
  Copy-Item -Path (Join-Path $repoRoot 'test\cmake\app') -Destination $cmakeTestDirectory -Recurse -Force
}

$packagesDirectory = Join-Path $testProjectDirectory 'packages'
$nlohmannJsonPackageRoot = $null
& $nuget.Source install crtsys `
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
if (-not $packageRoot.EndsWith('\')) {
  $packageRoot += '\'
}
$requiredPackagePaths = @(
  'README.md',
  'build\native\crtsys.props',
  'build\native\crtsys.targets',
  'include\ntl\rpc\client',
  'docs\ntl-api.md'
)
if ($isDriverConsumer) {
  $requiredPackagePaths += @(
    "lib\native\$Architecture\$Configuration\crtsys.lib",
    "lib\native\$Architecture\$Configuration\Ldk.lib",
    'include\ntl\driver'
  )
}

foreach ($requiredPath in $requiredPackagePaths) {
  $fullPath = Join-Path $packageRoot $requiredPath
  if (-not (Test-Path $fullPath)) {
    throw "Installed package is missing expected file: $fullPath"
  }
}

if ($isDriverConsumer) {
  $externalPackagesDirectory = Join-Path $testProjectDirectory 'external-packages'
  & $nuget.Source install nlohmann.json `
    -Version 3.12.0 `
    -Source https://api.nuget.org/v3/index.json `
    -OutputDirectory $externalPackagesDirectory `
    -ExcludeVersion `
    -NonInteractive `
    -Verbosity detailed

  if ($LASTEXITCODE -ne 0) {
    throw "nlohmann.json NuGet install failed with exit code $LASTEXITCODE."
  }

  $nlohmannJsonPackageRoot = Join-Path $externalPackagesDirectory 'nlohmann.json'
  foreach ($requiredPath in @(
    'build\native\nlohmann.json.targets',
    'build\native\include\nlohmann\json.hpp'
  )) {
    $fullPath = Join-Path $nlohmannJsonPackageRoot $requiredPath
    if (-not (Test-Path $fullPath)) {
      throw "Installed nlohmann.json package is missing expected file: $fullPath"
    }
  }

  $nlohmannJsonPackageRoot = (Resolve-Path $nlohmannJsonPackageRoot).Path
  if (-not $nlohmannJsonPackageRoot.EndsWith('\')) {
    $nlohmannJsonPackageRoot += '\'
  }
}

$packageReadme = Get-Content -LiteralPath (Join-Path $packageRoot 'README.md') -Raw -Encoding UTF8
if ($packageReadme -notmatch 'crtsys NuGet Package') {
  throw "Installed package README.md does not look like the NuGet package README."
}

$projectFile = Join-Path $testProjectDirectory $projectName
if (-not (Test-Path $projectFile)) {
  throw "NuGet consumer test project file was not copied: $projectFile"
}

$msbuildArguments = @(
  $projectFile,
  '/m',
  "/p:Configuration=$Configuration",
  "/p:Platform=$msbuildPlatform",
  "/p:CrtSysPackageRoot=$packageRoot",
  '/v:minimal'
)
if ($isDriverConsumer) {
  $msbuildArguments += "/p:WindowsTargetPlatformVersion=$WdkVersion"
  $msbuildArguments += '/p:SignMode=Off'
  $msbuildArguments += "/p:NlohmannJsonPackageRoot=$nlohmannJsonPackageRoot"
} else {
  $msbuildArguments += "/p:WindowsTargetPlatformVersion=$WindowsSdkVersion"
}

Write-Host "Building NuGet $Consumer consumer test for $Architecture $Configuration"
& $msbuild @msbuildArguments

if ($LASTEXITCODE -ne 0) {
  throw "MSBuild package $Consumer consumer test failed with exit code $LASTEXITCODE."
}

Write-Host "NuGet $Consumer consumer test passed for $Architecture $Configuration."
