param(
  [string] $PackageDirectory,

  [Parameter(Mandatory = $true)]
  [string] $Version,

  [ValidateSet('Driver', 'App')]
  [string] $Consumer = 'Driver',

  [ValidateSet('x86', 'x64', 'ARM', 'ARM64')]
  [string] $Architecture = 'x64',

  [ValidateSet('Debug', 'Release')]
  [string] $Configuration = 'Release',

  [ValidateSet('v142', 'v143', 'v145')]
  [string] $Toolset = 'v143',

  [string] $WindowsSdkVersion = '10.0.22621.0',

  [string] $WdkVersion,

  [switch] $SkipDriverBuild,

  [string] $WorkDirectory,

  [string] $NuGetExe,

  [ValidateSet('latest', '18', '17', '16', '15')]
  [string] $VisualStudioMajorVersion = 'latest'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$isDriverConsumer = $Consumer -eq 'Driver'

$msbuildPlatformByArchitecture = @{
  x86 = 'Win32'
  x64 = 'x64'
  ARM = 'ARM'
  ARM64 = 'ARM64'
}
$msbuildPlatform = $msbuildPlatformByArchitecture[$Architecture]

if ([string]::IsNullOrWhiteSpace($PackageDirectory)) {
  $PackageDirectory = Join-Path $repoRoot 'artifacts\nuget'
}

if ([string]::IsNullOrWhiteSpace($WorkDirectory)) {
  $WorkDirectory = Join-Path $repoRoot "artifacts\nuget-consumer-test\$Consumer\$Toolset\$Architecture\$Configuration"
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

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
  throw "vswhere.exe was not found: $vswhere"
}

$windowsKitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$windowsKitsIncludeRoot = Join-Path $windowsKitsRoot 'Include'
if (-not (Test-Path $windowsKitsIncludeRoot)) {
  throw "Windows Kits include directory was not found: $windowsKitsIncludeRoot"
}

$visualStudioInstallationsJson = & $vswhere -all -products * -requires Microsoft.Component.MSBuild -format json
$visualStudioInstallations = @(($visualStudioInstallationsJson | ConvertFrom-Json) | ForEach-Object { $_ })
if ($VisualStudioMajorVersion -ne 'latest') {
  $visualStudioInstallations = @(
    $visualStudioInstallations |
      Where-Object { ([version]$_.installationVersion).Major -eq [int]$VisualStudioMajorVersion }
  )
}

$visualStudioInstallations = @(
  $visualStudioInstallations |
    Sort-Object { [version]$_.installationVersion } -Descending
)
if ($visualStudioInstallations.Count -eq 0) {
  throw "Visual Studio with MSBuild was not found."
}

$visualStudioPath = $visualStudioInstallations[0].installationPath
$msbuildCandidates = @(
  Join-Path $visualStudioPath 'MSBuild\Current\Bin\MSBuild.exe'
  Join-Path $visualStudioPath 'MSBuild\15.0\Bin\MSBuild.exe'
)
$msbuild = $msbuildCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($msbuild)) {
  throw "MSBuild.exe was not found under: $visualStudioPath"
}

if ($isDriverConsumer -and -not $SkipDriverBuild -and [string]::IsNullOrWhiteSpace($WdkVersion)) {
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

if ($isDriverConsumer -and -not $SkipDriverBuild) {
  $wdkHeader = Join-Path $windowsKitsIncludeRoot "$WdkVersion\km\wdm.h"
  if (-not (Test-Path $wdkHeader)) {
    throw "WDK header was not found: $wdkHeader"
  }

  $wdkPlatformByArchitecture = @{
    x86 = 'x86'
    x64 = 'x64'
    ARM = 'arm'
    ARM64 = 'arm64'
  }
  $wdkKernelLibDirectory = Join-Path $windowsKitsRoot "Lib\$WdkVersion\km\$($wdkPlatformByArchitecture[$Architecture])"
  if (-not (Test-Path (Join-Path $wdkKernelLibDirectory 'ntoskrnl.lib'))) {
    throw "WDK kernel library directory is missing ntoskrnl.lib: $wdkKernelLibDirectory"
  }
  if (-not (Test-Path (Join-Path $wdkKernelLibDirectory 'libcntpr.lib'))) {
    throw "WDK kernel library directory is missing libcntpr.lib: $wdkKernelLibDirectory"
  }
}

Write-Host "Requested Windows SDK version: $WindowsSdkVersion"
Write-Host "Resolved Visual Studio path: $visualStudioPath"
if ($isDriverConsumer -and -not $SkipDriverBuild) {
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
  $driverTestDirectory = Join-Path $cmakeTestDirectory 'driver'
  New-Item -ItemType Directory -Force -Path $driverTestDirectory | Out-Null
  Copy-Item -Path (Join-Path $repoRoot 'test\cmake\driver\src') -Destination $driverTestDirectory -Recurse -Force
} else {
  $appTestDirectory = Join-Path $cmakeTestDirectory 'app'
  New-Item -ItemType Directory -Force -Path $appTestDirectory | Out-Null
  Copy-Item -Path (Join-Path $repoRoot 'test\cmake\app\src') -Destination $appTestDirectory -Recurse -Force
}

$packagesDirectory = Join-Path $testProjectDirectory 'packages'
$nlohmannJsonPackageRoot = $null
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
    "build\native\lib\native\$Toolset\$Architecture\$Configuration\crtsys.lib",
    "build\native\lib\native\$Toolset\$Architecture\$Configuration\Ldk.lib",
    'include\ntl\driver'
  )
}

foreach ($requiredPath in $requiredPackagePaths) {
  $fullPath = Join-Path $packageRoot $requiredPath
  if (-not (Test-Path $fullPath)) {
    throw "Installed package is missing expected file: $fullPath"
  }
}

if ($isDriverConsumer -and -not $SkipDriverBuild) {
  $externalPackagesDirectory = Join-Path $testProjectDirectory 'external-packages'
  & $NuGetExe install nlohmann.json `
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

if ($SkipDriverBuild) {
  Write-Host "NuGet $Consumer package layout validation passed for $Architecture $Configuration."
  return
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
  "/p:CrtSysLibToolset=$Toolset",
  "/p:CrtSysExpectedLibToolset=$Toolset",
  '/v:minimal'
)
if ($isDriverConsumer) {
  $msbuildArguments += "/p:WindowsTargetPlatformVersion=$WdkVersion"
  $msbuildArguments += '/p:SignMode=Off'
  $msbuildArguments += "/p:NlohmannJsonPackageRoot=$nlohmannJsonPackageRoot"
} else {
  $msbuildArguments += "/p:PlatformToolset=$Toolset"
  $msbuildArguments += "/p:WindowsTargetPlatformVersion=$WindowsSdkVersion"
}

Write-Host "Building NuGet $Consumer consumer test for $Architecture $Configuration"
& $msbuild @msbuildArguments

if ($LASTEXITCODE -ne 0) {
  throw "MSBuild package $Consumer consumer test failed with exit code $LASTEXITCODE."
}

Write-Host "NuGet $Consumer consumer test passed for $Architecture $Configuration."
