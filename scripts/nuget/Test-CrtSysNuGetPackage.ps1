param(
  [string] $PackageDirectory,

  [Parameter(Mandatory = $true)]
  [string] $Version,

  [ValidateSet('x64', 'ARM64')]
  [string] $Architecture = 'x64',

  [ValidateSet('Release')]
  [string] $Configuration = 'Release',

  [string] $WindowsSdkVersion = '10.0.22621.0',

  [string] $WdkVersion,

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

$wdkHeader = Join-Path $windowsKitsIncludeRoot "$WdkVersion\km\wdm.h"
if (-not (Test-Path $wdkHeader)) {
  throw "WDK header was not found: $wdkHeader"
}

$wdkKernelLibDirectory = Join-Path $windowsKitsRoot "Lib\$WdkVersion\km\$Architecture"
if (-not (Test-Path (Join-Path $wdkKernelLibDirectory 'ntoskrnl.lib'))) {
  throw "WDK kernel library directory is missing ntoskrnl.lib: $wdkKernelLibDirectory"
}

Write-Host "Requested Windows SDK version: $WindowsSdkVersion"
Write-Host "Resolved WDK version: $WdkVersion"

Remove-Item -Recurse -Force -Path $WorkDirectory -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $WorkDirectory | Out-Null

$testProjectSource = Join-Path $repoRoot 'test\nuget'
if (-not (Test-Path (Join-Path $testProjectSource 'crtsys_nuget_test.vcxproj'))) {
  throw "NuGet consumer test project was not found under '$testProjectSource'."
}

$testRoot = Join-Path $WorkDirectory 'test'
$testProjectDirectory = Join-Path $testRoot 'nuget'
$cmakeTestDirectory = Join-Path $testRoot 'cmake'
New-Item -ItemType Directory -Force -Path $testProjectDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $cmakeTestDirectory | Out-Null

Copy-Item -Path (Join-Path $testProjectSource '*') -Destination $testProjectDirectory -Recurse -Force
Copy-Item -Path (Join-Path $repoRoot 'test\cmake\driver') -Destination $cmakeTestDirectory -Recurse -Force
Copy-Item -Path (Join-Path $repoRoot 'test\cmake\common') -Destination $cmakeTestDirectory -Recurse -Force

$packagesDirectory = Join-Path $testProjectDirectory 'packages'
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

& $nuget.Source install nlohmann.json `
  -Version 3.12.0 `
  -Source https://api.nuget.org/v3/index.json `
  -OutputDirectory $packagesDirectory `
  -ExcludeVersion `
  -NonInteractive `
  -Verbosity detailed

if ($LASTEXITCODE -ne 0) {
  throw "nlohmann.json NuGet install failed with exit code $LASTEXITCODE."
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

$nlohmannJsonPackageRoot = Join-Path $packagesDirectory 'nlohmann.json'
foreach ($requiredPath in @(
  'build\native\nlohmann.json.targets',
  'build\native\include\nlohmann\json.hpp'
)) {
  $fullPath = Join-Path $nlohmannJsonPackageRoot $requiredPath
  if (-not (Test-Path $fullPath)) {
    throw "Installed nlohmann.json package is missing expected file: $fullPath"
  }
}

$packageReadme = Get-Content -LiteralPath (Join-Path $packageRoot 'README.md') -Raw -Encoding UTF8
if ($packageReadme -notmatch 'crtsys NuGet Package') {
  throw "Installed package README.md does not look like the NuGet package README."
}

$projectFile = Join-Path $testProjectDirectory 'crtsys_nuget_test.vcxproj'
if (-not (Test-Path $projectFile)) {
  throw "NuGet consumer test project file was not copied: $projectFile"
}

Write-Host "Building NuGet consumer test driver for $Architecture"
& $msbuild $projectFile `
  /m `
  /p:Configuration=$Configuration `
  /p:Platform=$Architecture `
  /p:WindowsTargetPlatformVersion=$WdkVersion `
  /v:minimal

if ($LASTEXITCODE -ne 0) {
  throw "MSBuild package consumer test failed with exit code $LASTEXITCODE."
}

Write-Host "NuGet consumer test driver passed for $Architecture."
