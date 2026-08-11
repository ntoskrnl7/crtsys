param(
  [Parameter(Mandatory = $true)]
  [ValidateSet('x86', 'x64', 'ARM', 'ARM64')]
  [string] $Architecture,

  [Parameter(Mandatory = $true)]
  [ValidateSet('Debug', 'Release')]
  [string] $Configuration,

  [Parameter(Mandatory = $true)]
  [ValidateSet('v142', 'v143', 'v145')]
  [string] $PlatformToolset,

  [string] $WindowsSdkVersion = '10.0.22621.0',

  [string] $WdkVersion = '',

  [switch] $IncludeContentCodecs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$nativeAssetRoot = Join-Path $repoRoot 'build\native'
$builder = Join-Path $repoRoot 'scripts\nuget\Build-CrtSysNuGetLibs.ps1'

$parameters = @{
  Architecture = @($Architecture)
  Configuration = @($Configuration)
  Toolset = @($PlatformToolset)
  WindowsSdkVersion = $WindowsSdkVersion
  OutputDirectory = $nativeAssetRoot
}
if (-not [string]::IsNullOrWhiteSpace($WdkVersion)) {
  $parameters.WdkVersion = $WdkVersion
}
if (-not $IncludeContentCodecs) {
  $parameters.LibrariesOnly = $true
}

Write-Host (
  "Preparing shared crtsys libraries for " +
  "$PlatformToolset/$Architecture/$Configuration")
& $builder @parameters

$stagedLibraryDir = Join-Path $nativeAssetRoot (
  "lib\native\$PlatformToolset\$Architecture\$Configuration")
$prebuiltLibraryDir = Join-Path $repoRoot (
  "lib\native\$PlatformToolset\$Architecture\$Configuration")

foreach ($library in @('crtsys.lib', 'Ldk.lib')) {
  $path = Join-Path $stagedLibraryDir $library
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "The shared prebuilt library was not staged: $path"
  }
}

New-Item -ItemType Directory -Force -Path $prebuiltLibraryDir | Out-Null
Get-ChildItem -LiteralPath $stagedLibraryDir -File | ForEach-Object {
  Copy-Item -LiteralPath $_.FullName -Destination $prebuiltLibraryDir -Force
}

if ($IncludeContentCodecs) {
  $requiredAssets = @(
    "codecs\lib\$PlatformToolset\$Architecture\$Configuration\zlibstatic.lib",
    "codecs\lib\$PlatformToolset\$Architecture\$Configuration\brotlicommon.lib",
    "codecs\lib\$PlatformToolset\$Architecture\$Configuration\brotlidec.lib",
    "codecs\lib\$PlatformToolset\$Architecture\$Configuration\brotlienc.lib",
    "kernel-codecs\lib\$PlatformToolset\$Architecture\$Configuration\crtsys_ntl_kernel_zlib.lib",
    "kernel-codecs\lib\$PlatformToolset\$Architecture\$Configuration\crtsys_ntl_kernel_brotli.lib",
    'msquic\include\msquic.h'
  )
  foreach ($relativePath in $requiredAssets) {
    $path = Join-Path $nativeAssetRoot $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
      throw "The shared prebuilt dependency asset was not staged: $path"
    }
  }
}

Write-Host "Shared crtsys prebuilt root: $repoRoot"
