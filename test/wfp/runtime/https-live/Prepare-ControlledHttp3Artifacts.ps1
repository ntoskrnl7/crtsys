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
    (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
  $OutputRoot =
      Join-Path $repoRoot 'artifacts\controlled-http3-staging'
}

$artifactsRoot =
    [IO.Path]::GetFullPath(
        (Join-Path $repoRoot 'artifacts')).TrimEnd('\') + '\'
$resolvedOutput =
    [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
if (-not ($resolvedOutput + '\').StartsWith(
    $artifactsRoot, [StringComparison]::OrdinalIgnoreCase)) {
  throw "OutputRoot must stay under $artifactsRoot"
}

if (-not $SkipBuild) {
  $buildScript =
      Join-Path $repoRoot 'scripts\ci\Build-CrtSys.ps1'
  & powershell.exe -NoProfile -ExecutionPolicy Bypass `
      -File $buildScript `
      -Project 'wfp-browser-https-inspection' `
      -Architecture x64 `
      -Configuration $Configuration `
      -WindowsSdkVersion $WindowsSdkVersion `
      -WdkVersion $WindowsSdkVersion `
      -PlatformToolset $PlatformToolset
  if ($LASTEXITCODE -ne 0) {
    throw 'The browser HTTPS inspection build failed.'
  }
}

$buildRoot = Join-Path $repoRoot (
    "examples\wfp\browser-https-inspection\" +
    "build_x64_$PlatformToolset\$Configuration")
$files = @(
  'crtsys_wfp_browser_https_inspection_app.exe'
  'crtsys_ntl_managed_http3_client.exe'
  'msh3.dll'
  'msquic.dll'
)
foreach ($name in $files) {
  $source = Join-Path $buildRoot $name
  if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw "Required controlled HTTP/3 artifact was not found: $source"
  }
}

if (Test-Path -LiteralPath $resolvedOutput) {
  Remove-Item -LiteralPath $resolvedOutput -Recurse -Force
}
$package =
    New-Item -ItemType Directory -Path $resolvedOutput -Force
foreach ($name in $files) {
  Copy-Item -LiteralPath (Join-Path $buildRoot $name) `
      -Destination $package.FullName
}
Copy-Item -LiteralPath (
    Join-Path $PSScriptRoot 'Start-ControlledHttp3EndToEnd.ps1') `
    -Destination $package.FullName
Copy-Item -LiteralPath (
    Join-Path $PSScriptRoot 'CONTROLLED-HTTP3-README.ko-KR.md') `
    -Destination $package.FullName

$cmakeCache =
    Join-Path (Split-Path -Parent $buildRoot) 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cmakeCache -PathType Leaf)) {
  throw "Browser CMake cache was not found: $cmakeCache"
}
$msh3SourceMatch =
    Select-String -LiteralPath $cmakeCache `
        -Pattern '^msh3_SOURCE_DIR:STATIC=(.+)$' |
        Select-Object -First 1
if (-not $msh3SourceMatch) {
  throw 'The configured msh3 source directory was not found.'
}
$msh3Source =
    [IO.Path]::GetFullPath(
        $msh3SourceMatch.Matches[0].Groups[1].Value)
$notices = @(
  @{ Source=(Join-Path $msh3Source 'LICENSE')
     Destination='LICENSE.msh3.txt' }
  @{ Source=(Join-Path $msh3Source 'msquic\LICENSE')
     Destination='LICENSE.msquic.txt' }
  @{ Source=(Join-Path $msh3Source 'msquic\THIRD-PARTY-NOTICES')
     Destination='THIRD-PARTY-NOTICES.msquic.txt' }
)
foreach ($notice in $notices) {
  if (-not (Test-Path -LiteralPath $notice.Source -PathType Leaf)) {
    throw "Required notice was not found: $($notice.Source)"
  }
  Copy-Item -LiteralPath $notice.Source -Destination (
      Join-Path $package.FullName $notice.Destination)
}

$forbidden = @(
  Get-ChildItem -LiteralPath $package.FullName -File |
      Where-Object Extension -in @('.sys', '.inf', '.cat'))
if ($forbidden.Count -ne 0) {
  throw 'The controlled package unexpectedly contains driver artifacts.'
}

$manifestPath = Join-Path $package.FullName 'SHA256SUMS.txt'
Get-ChildItem -LiteralPath $package.FullName -File |
    Where-Object FullName -ne $manifestPath |
    Sort-Object Name |
    ForEach-Object {
      $hash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
      "$($hash.Hash.ToLowerInvariant())  $($_.Name)"
    } |
    Set-Content -LiteralPath $manifestPath -Encoding ASCII

Write-Host (
    "Prepared driverless controlled HTTP/3 package: " +
    $package.FullName)
[pscustomobject]@{
  Root = $package.FullName
  Test = Join-Path $package.FullName `
      'Start-ControlledHttp3EndToEnd.ps1'
  ContainsDriver = $false
  RequiresReboot = $false
}
