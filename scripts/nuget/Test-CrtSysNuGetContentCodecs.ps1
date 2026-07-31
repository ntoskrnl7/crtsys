param(
  [string] $PackageRoot,

  [string] $PackageDirectory,

  [string] $Version,

  [ValidateSet('Debug', 'Release')]
  [string[]] $Configuration = @('Debug', 'Release'),

  [ValidateSet('v142', 'v143', 'v145')]
  [string] $Toolset = 'v143',

  [ValidateSet('x86', 'x64', 'ARM', 'ARM64')]
  [string] $Architecture = 'x64',

  [switch] $SkipRun,

  [ValidateSet('latest', '18', '17', '16')]
  [string] $VisualStudioMajorVersion = 'latest',

  [string] $WindowsSdkVersion = '10.0.22621.0',

  [string] $WorkDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-MsBuildPath {
  param([Parameter(Mandatory = $true)][string] $RequestedMajor)
  $vswhere = Join-Path ${env:ProgramFiles(x86)} `
    'Microsoft Visual Studio\Installer\vswhere.exe'
  if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe was not found: $vswhere"
  }
  $installations = @(
    (& $vswhere -all -products * -requires Microsoft.Component.MSBuild `
        -format json | ConvertFrom-Json) |
      Where-Object {
        $RequestedMajor -eq 'latest' -or
        ([version]$_.installationVersion).Major -eq [int]$RequestedMajor
      } |
      Sort-Object { [version]$_.installationVersion } -Descending
  )
  if ($installations.Count -eq 0) {
    throw "Visual Studio with MSBuild was not found."
  }
  foreach ($candidate in @(
      (Join-Path $installations[0].installationPath `
        'MSBuild\Current\Bin\amd64\MSBuild.exe'),
      (Join-Path $installations[0].installationPath `
        'MSBuild\Current\Bin\MSBuild.exe'))) {
    if (Test-Path $candidate) { return $candidate }
  }
  throw "MSBuild.exe was not found under $($installations[0].installationPath)."
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($WorkDirectory)) {
  $WorkDirectory = Join-Path $repoRoot `
    "artifacts\nuget-content-codec-consumer\$Toolset\$Architecture"
}
$WorkDirectory = [IO.Path]::GetFullPath($WorkDirectory)
$repoPrefix = $repoRoot.TrimEnd('\') + '\'
if (-not $WorkDirectory.StartsWith(
    $repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
  throw "WorkDirectory must be inside the repository: $repoRoot"
}

if ([string]::IsNullOrWhiteSpace($PackageRoot)) {
  if ([string]::IsNullOrWhiteSpace($PackageDirectory)) {
    $PackageDirectory = Join-Path $repoRoot 'artifacts\nuget'
  }
  if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = & (Join-Path $PSScriptRoot 'Get-CrtSysVersion.ps1')
  }
  $packagePath = Join-Path $PackageDirectory "crtsys.$Version.nupkg"
  if (-not (Test-Path $packagePath)) {
    throw "NuGet package was not found: $packagePath"
  }
  $PackageRoot = Join-Path $WorkDirectory 'package'
  Remove-Item -LiteralPath $PackageRoot -Recurse -Force `
    -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force -Path $PackageRoot | Out-Null
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  [IO.Compression.ZipFile]::ExtractToDirectory(
    [IO.Path]::GetFullPath($packagePath), $PackageRoot)
}
$PackageRoot = (Resolve-Path $PackageRoot).Path

foreach ($required in @(
    'build\native\crtsys.props',
    'build\native\crtsys.targets',
    'build\native\codecs\include\zlib.h',
    'build\native\codecs\include\brotli\encode.h',
    'include\ntl\net\inspection\standard_content_decoders',
    'include\ntl\net\inspection\standard_content_encoders')) {
  $path = Join-Path $PackageRoot $required
  if (-not (Test-Path $path)) {
    throw "NuGet content codec package input is missing: $path"
  }
}
foreach ($config in $Configuration) {
  foreach ($library in @(
      'zlibstatic.lib', 'brotlicommon.lib',
      'brotlidec.lib', 'brotlienc.lib')) {
    $path = Join-Path $PackageRoot `
      "build\native\codecs\lib\$Toolset\$Architecture\$config\$library"
    if (-not (Test-Path $path)) {
      throw "NuGet content codec library is missing: $path"
    }
  }
}

$source = Join-Path $repoRoot 'test\nuget\content-codec-consumer'
$project = Join-Path $source 'content-codec-consumer.vcxproj'
$msbuild = Resolve-MsBuildPath -RequestedMajor $VisualStudioMajorVersion
$platform = if ($Architecture -eq 'x86') { 'Win32' } else { $Architecture }
foreach ($config in $Configuration) {
  $output = Join-Path $WorkDirectory "$config\"
  & $msbuild $project /m /nologo /v:minimal `
    "/p:Configuration=$config" "/p:Platform=$platform" `
    "/p:CrtSysPackageRoot=$PackageRoot" `
    "/p:CrtSysConsumerPlatformToolset=$Toolset" `
    "/p:CrtSysConsumerLibToolset=$Toolset" `
    "/p:CrtSysConsumerWindowsSdkVersion=$WindowsSdkVersion" `
    "/p:OutDir=$output" `
    "/p:IntDir=$($output)intermediate\"
  if ($LASTEXITCODE -ne 0) {
    throw "NuGet content codec consumer build failed with exit code $LASTEXITCODE."
  }
  if (-not $SkipRun -and $Architecture -in @('x86', 'x64')) {
    $executable = Join-Path $output 'content-codec-consumer.exe'
    & $executable
    if ($LASTEXITCODE -ne 0) {
      throw "NuGet content codec consumer failed with exit code $LASTEXITCODE."
    }
  }
}

Write-Host "crtsys NuGet content codec consumer passed: $Toolset $Architecture $($Configuration -join ', ')"
