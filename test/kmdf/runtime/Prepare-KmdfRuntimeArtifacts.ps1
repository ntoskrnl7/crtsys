[CmdletBinding()]
param(
  [ValidateSet('Debug', 'Release')]
  [string] $DriverConfiguration = 'Debug',

  [ValidateSet('v143', 'v145')]
  [string] $PlatformToolset = 'v145',

  [string] $WindowsSdkVersion = '10.0.28000.0',

  [string] $OutputRoot =
      'D:\projects\crtsys\artifacts\kmdf-runtime-staging',

  [switch] $SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$examplesRoot = Join-Path $repoRoot 'examples\kmdf'
$buildScript = Join-Path $repoRoot 'scripts\ci\Build-CrtSys.ps1'
$signScript = Join-Path $repoRoot 'scripts\ci\TestSign-Driver.ps1'
$samples = @(
  'basic',
  'pnp',
  'echo',
  'reference',
  'bus',
  'filter-stack',
  'wmi'
)

function Find-MSBuild {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} `
      'Microsoft Visual Studio\Installer\vswhere.exe'
  if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw "vswhere.exe was not found: $vswhere"
  }
  $candidate = & $vswhere -latest -products * `
      -requires Microsoft.Component.MSBuild `
      -find 'MSBuild\**\Bin\MSBuild.exe' |
      Select-Object -First 1
  if (-not $candidate) {
    throw 'MSBuild.exe was not found.'
  }
  return $candidate
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

function Copy-UniqueLeaf(
  [IO.FileInfo] $Source,
  [string] $DestinationDirectory
) {
  $destination = Join-Path $DestinationDirectory $Source.Name
  if (Test-Path -LiteralPath $destination -PathType Leaf) {
    $existing = Get-Item -LiteralPath $destination
    if ($existing.Length -ne $Source.Length) {
      throw "Conflicting staged files have the same name: $destination"
    }
    return
  }
  Copy-Item -LiteralPath $Source.FullName -Destination $destination
}

if (-not $SkipBuild) {
  $msbuild = Find-MSBuild
  foreach ($sample in $samples) {
    $sampleRoot = Join-Path $examplesRoot $sample
    $solution = Get-ChildItem -LiteralPath $sampleRoot -Filter '*.sln' -File |
        Select-Object -First 1
    if (-not $solution) {
      throw "Visual Studio solution was not found for $sample."
    }
    & $msbuild $solution.FullName /m /t:Build `
        "/p:Configuration=$DriverConfiguration" /p:Platform=x64 `
        /p:CrtSysUseRepositoryDevelopmentFiles=true /v:minimal
    if ($LASTEXITCODE -ne 0) {
      throw "x64 WDK package build failed for $sample."
    }

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $buildScript `
        -Project "kmdf-example-$sample" -Architecture x86 `
        -Configuration Release -WindowsSdkVersion $WindowsSdkVersion `
        -WdkVersion $WindowsSdkVersion -PlatformToolset $PlatformToolset
    if ($LASTEXITCODE -ne 0) {
      throw "x86 CMake build failed for $sample."
    }
  }

  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $buildScript `
      -Project kmdf-verifier-stress -Architecture x64 `
      -Configuration Release -WindowsSdkVersion $WindowsSdkVersion `
      -WdkVersion $WindowsSdkVersion -PlatformToolset $PlatformToolset
  if ($LASTEXITCODE -ne 0) {
    throw 'KMDF verifier-stress build failed.'
  }
}

Assert-SafeOutputRoot $OutputRoot
$resolvedOutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $resolvedOutputRoot) {
  Remove-Item -LiteralPath $resolvedOutputRoot -Recurse -Force
}
$x64Root = New-Item -ItemType Directory -Force `
    -Path (Join-Path $resolvedOutputRoot 'x64')
$x86Root = New-Item -ItemType Directory -Force `
    -Path (Join-Path $resolvedOutputRoot 'x86')
$stressRoot = New-Item -ItemType Directory -Force `
    -Path (Join-Path $resolvedOutputRoot 'stress')

$appNames = @{
  basic = 'crtsys_kmdf_ntl_sample_app.exe'
  pnp = 'crtsys_kmdf_pnp_ntl_sample_app.exe'
  echo = 'crtsys_kmdf_echo_ntl_sample_app.exe'
  reference = 'crtsys_kmdf_reference_app.exe'
  bus = 'crtsys_kmdf_bus_ntl_sample_app.exe'
  'filter-stack' = 'crtsys_kmdf_filter_stack_app.exe'
  wmi = 'crtsys_kmdf_wmi_ntl_sample_app.exe'
}

foreach ($sample in $samples) {
  $sampleRoot = Join-Path $examplesRoot $sample
  $x64Source =
      Join-Path $sampleRoot "x64\$DriverConfiguration"
  $x86Source =
      Join-Path $sampleRoot "build_x86_$PlatformToolset\Release"
  if (-not (Test-Path -LiteralPath $x64Source -PathType Container)) {
    throw "x64 package output was not found: $x64Source"
  }
  if (-not (Test-Path -LiteralPath $x86Source -PathType Container)) {
    throw "x86 application output was not found: $x86Source"
  }

  $x64Destination = New-Item -ItemType Directory -Force `
      -Path (Join-Path $x64Root.FullName $sample)
  $x86Destination = New-Item -ItemType Directory -Force `
      -Path (Join-Path $x86Root.FullName $sample)

  $packageFiles = Get-ChildItem -LiteralPath $x64Source -Recurse -File |
      Where-Object Extension -in @(
        '.sys', '.inf', '.cat', '.cer', '.bmf', '.res', '.exe'
      ) |
      Sort-Object FullName
  foreach ($file in $packageFiles) {
    Copy-UniqueLeaf $file $x64Destination.FullName
  }

  $x86App = Join-Path $x86Source $appNames[$sample]
  if (-not (Test-Path -LiteralPath $x86App -PathType Leaf)) {
    throw "x86 application was not found: $x86App"
  }
  Copy-Item -LiteralPath $x86App -Destination $x86Destination.FullName
}

# The control-device sample is loaded directly through the service control
# manager rather than through an INF catalog, so give its staged SYS an
# embedded test signature and stage the matching trust anchor.
$basicDriver =
    Join-Path $x64Root.FullName 'basic\crtsys_kmdf_ntl_sample.sys'
$basicSigningRoot = Join-Path $resolvedOutputRoot 'basic-signing'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $signScript `
    -DriverPath $basicDriver -WorkDir $basicSigningRoot
if ($LASTEXITCODE -ne 0) {
  throw 'Signing the basic control-device driver failed.'
}
Copy-Item -LiteralPath (Join-Path $basicSigningRoot 'crtsys-test-signing.cer') `
    -Destination (Join-Path $x64Root.FullName 'basic')

$stressBuildRoot =
    Join-Path $repoRoot "test\kmdf\verifier-stress\build_x64_$PlatformToolset\Release"
$stressDriver =
    Join-Path $stressBuildRoot 'crtsys_kmdf_verifier_stress.sys'
$stressApp =
    Join-Path $stressBuildRoot 'crtsys_kmdf_verifier_stress_app.exe'
if (-not (Test-Path -LiteralPath $stressDriver -PathType Leaf) -or
    -not (Test-Path -LiteralPath $stressApp -PathType Leaf)) {
  throw "Verifier-stress output was not found under $stressBuildRoot"
}

$signingRoot = Join-Path $resolvedOutputRoot 'stress-signing'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $signScript `
    -DriverPath $stressDriver -WorkDir $signingRoot
if ($LASTEXITCODE -ne 0) {
  throw 'Signing the verifier-stress driver failed.'
}
Copy-Item -LiteralPath $stressDriver -Destination $stressRoot.FullName
Copy-Item -LiteralPath $stressApp -Destination $stressRoot.FullName
Copy-Item -LiteralPath (Join-Path $signingRoot 'crtsys-test-signing.cer') `
    -Destination $stressRoot.FullName

Write-Host "Prepared KMDF x64 packages: $($x64Root.FullName)"
Write-Host "Prepared KMDF x86 applications: $($x86Root.FullName)"
Write-Host "Prepared KMDF stress fixture: $($stressRoot.FullName)"

[pscustomobject]@{
  Root = $resolvedOutputRoot
  X64Root = $x64Root.FullName
  X86Root = $x86Root.FullName
  StressRoot = $stressRoot.FullName
}
