[CmdletBinding()]
param(
  [ValidateSet('Debug', 'Release')]
  [string] $Configuration = 'Release',

  [ValidateSet('v143', 'v145')]
  [string] $PlatformToolset = 'v145',

  [string] $WindowsSdkVersion = '10.0.28000.0',

  [string] $BuildRoot = '',

  [string] $OutputRoot = '',

  [switch] $SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$buildScript = Join-Path $repoRoot 'scripts\ci\Build-CrtSys.ps1'
$signScript = Join-Path $repoRoot 'scripts\ci\TestSign-Driver.ps1'
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
  $OutputRoot =
      Join-Path $repoRoot 'artifacts\wfp-ale-connect-block-staging'
}
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
  $sdkDirectory = $WindowsSdkVersion -replace '[^A-Za-z0-9]+', '_'
  $BuildRoot = Join-Path $repoRoot (
      "artifacts\b\wfp-ale-connect-block-runtime\" +
      "$PlatformToolset\$sdkDirectory")
}

function Assert-SafeOutputRoot([string] $Path) {
  $artifactsRoot =
      [IO.Path]::GetFullPath((Join-Path $repoRoot 'artifacts')).
          TrimEnd('\') + '\'
  $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\') + '\'
  if (-not $fullPath.StartsWith(
      $artifactsRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must stay under $artifactsRoot"
  }
}

Assert-SafeOutputRoot $BuildRoot
Assert-SafeOutputRoot $OutputRoot
$resolvedBuildRoot = [IO.Path]::GetFullPath($BuildRoot)

if (-not $SkipBuild) {
  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $buildScript `
      -Project wfp-ale-connect-block -Architecture x64 `
      -Configuration $Configuration `
      -WindowsSdkVersion $WindowsSdkVersion `
      -WdkVersion $WindowsSdkVersion `
      -PlatformToolset $PlatformToolset `
      -BuildDirectory $resolvedBuildRoot
  if ($LASTEXITCODE -ne 0) {
    throw 'The WFP ALE connect-block build failed.'
  }
}

$resolvedOutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $resolvedOutputRoot) {
  Remove-Item -LiteralPath $resolvedOutputRoot -Recurse -Force
}
$packageRoot = New-Item -ItemType Directory -Force -Path $resolvedOutputRoot

$buildRoot = Join-Path $resolvedBuildRoot $Configuration
$driverSource = Join-Path $buildRoot 'crtsys_wfp_ale_connect_block.sys'
$controllerSource = Join-Path $buildRoot (
    'crtsys_wfp_ale_connect_block_controller.exe')
$acceptanceSource = Join-Path $buildRoot (
    'crtsys_wfp_ale_connect_block_acceptance.exe')
$infSource =
    Join-Path $repoRoot (
      'examples\wfp\kernel\ale-connect-block\crtsys_wfp_ale_connect_block.inf')
foreach ($path in @(
    $driverSource, $controllerSource, $acceptanceSource, $infSource)) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Required WFP runtime artifact was not found: $path"
  }
}

$driver =
    Join-Path $packageRoot.FullName 'crtsys_wfp_ale_connect_block.sys'
Copy-Item -LiteralPath $driverSource -Destination $driver
Copy-Item -LiteralPath $controllerSource -Destination $packageRoot.FullName
Copy-Item -LiteralPath $acceptanceSource -Destination $packageRoot.FullName
Copy-Item -LiteralPath $infSource -Destination $packageRoot.FullName

$signingRoot = Join-Path $resolvedOutputRoot 'signing'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $signScript `
    -DriverPath $driver -WorkDir $signingRoot
if ($LASTEXITCODE -ne 0) {
  throw 'Signing the WFP ALE connect-block driver failed.'
}
Copy-Item -LiteralPath (
    Join-Path $signingRoot 'crtsys-test-signing.cer') `
    -Destination $packageRoot.FullName

Write-Host (
  "Prepared WFP ALE connect-block runtime package: " +
  $packageRoot.FullName)
[pscustomobject]@{
  Root = $packageRoot.FullName
  Driver = $driver
  Controller = Join-Path $packageRoot.FullName (
      'crtsys_wfp_ale_connect_block_controller.exe')
  Acceptance = Join-Path $packageRoot.FullName (
      'crtsys_wfp_ale_connect_block_acceptance.exe')
}
