[CmdletBinding()]
param(
  [ValidateSet('Debug', 'Release')]
  [string] $Configuration = 'Release',

  [ValidateSet('v143', 'v145')]
  [string] $PlatformToolset = 'v145',

  [string] $WindowsSdkVersion = '10.0.22621.0',

  [string] $OutputRoot = '',

  [switch] $SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot =
    (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$buildScript =
    Join-Path $repoRoot 'scripts\ci\Build-CrtSys.ps1'
$signScript =
    Join-Path $repoRoot 'scripts\ci\TestSign-Driver.ps1'
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
  $OutputRoot =
      Join-Path $repoRoot 'artifacts\ndis-lwf-staging'
}

$artifactsRoot =
    [IO.Path]::GetFullPath(
        (Join-Path $repoRoot 'artifacts')).TrimEnd('\') + '\'
$resolvedOutputRoot =
    [IO.Path]::GetFullPath($OutputRoot)
$checkedOutput =
    $resolvedOutputRoot.TrimEnd('\') + '\'
if (-not $checkedOutput.StartsWith(
    $artifactsRoot, [StringComparison]::OrdinalIgnoreCase)) {
  throw "OutputRoot must stay under $artifactsRoot"
}

if (-not $SkipBuild) {
  & powershell.exe -NoProfile -ExecutionPolicy Bypass `
      -File $buildScript `
      -Project ndis-lwf-monitor -Architecture x64 `
      -Configuration $Configuration `
      -WindowsSdkVersion $WindowsSdkVersion `
      -WdkVersion $WindowsSdkVersion `
      -PlatformToolset $PlatformToolset
  if ($LASTEXITCODE -ne 0) {
    throw 'The NDIS LWF monitor build failed.'
  }
}

if (Test-Path -LiteralPath $resolvedOutputRoot) {
  Remove-Item -LiteralPath $resolvedOutputRoot -Recurse -Force
}
$packageRoot =
    New-Item -ItemType Directory -Force -Path $resolvedOutputRoot
$buildRoot = Join-Path $repoRoot (
    "examples\ndis\lwf-monitor\build_x64_$PlatformToolset\" +
    $Configuration)
$baseName = 'crtsys_ndis_lwf_monitor'
$driverSource = Join-Path $buildRoot "$baseName.sys"
$applicationSource = Join-Path $buildRoot "${baseName}_app.exe"
$infSource = Join-Path $repoRoot (
    "examples\ndis\lwf-monitor\$baseName.inf")
foreach ($path in @(
    $driverSource, $applicationSource, $infSource)) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Required NDIS runtime artifact was not found: $path"
  }
}

$driver = Join-Path $packageRoot.FullName "$baseName.sys"
$catalog = Join-Path $packageRoot.FullName "$baseName.cat"
Copy-Item -LiteralPath $driverSource -Destination $driver
Copy-Item -LiteralPath $applicationSource `
    -Destination $packageRoot.FullName
Copy-Item -LiteralPath $infSource `
    -Destination $packageRoot.FullName

$driverSigningRoot =
    Join-Path $resolvedOutputRoot 'signing\driver'
& powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File $signScript -DriverPath $driver `
    -WorkDir $driverSigningRoot
if ($LASTEXITCODE -ne 0) {
  throw 'Signing the NDIS LWF driver failed.'
}
Copy-Item -LiteralPath (
    Join-Path $driverSigningRoot 'crtsys-test-signing.cer') `
    -Destination (
      Join-Path $packageRoot.FullName "$baseName-driver.cer")

$kitsRoot =
    Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$inf2Cat = Get-ChildItem -LiteralPath $kitsRoot -Recurse `
    -Filter Inf2Cat.exe |
    Where-Object { $_.FullName -match '\\x86\\Inf2Cat\.exe$' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if (-not $inf2Cat) {
  throw 'Inf2Cat.exe was not found.'
}
& $inf2Cat.FullName "/driver:$($packageRoot.FullName)" /os:10_X64
if ($LASTEXITCODE -ne 0 -or
    -not (Test-Path -LiteralPath $catalog -PathType Leaf)) {
  throw 'Creating the NDIS LWF catalog failed.'
}

$catalogSigningRoot =
    Join-Path $resolvedOutputRoot 'signing\catalog'
& powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File $signScript -DriverPath $catalog `
    -WorkDir $catalogSigningRoot
if ($LASTEXITCODE -ne 0) {
  throw 'Signing the NDIS LWF catalog failed.'
}
Copy-Item -LiteralPath (
    Join-Path $catalogSigningRoot 'crtsys-test-signing.cer') `
    -Destination (
      Join-Path $packageRoot.FullName "$baseName-catalog.cer")

Write-Host (
    "Prepared NDIS LWF runtime package: $($packageRoot.FullName)")
[pscustomobject]@{
  Root = $packageRoot.FullName
  Driver = $driver
  Catalog = $catalog
  Application =
      (Join-Path $packageRoot.FullName "${baseName}_app.exe")
}
