param(
  [string] $Version,
  [string] $OutputDirectory,
  [string] $NuGetExe,

  [string[]] $Toolset = @('v143')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

if ([string]::IsNullOrWhiteSpace($Version)) {
  $Version = & (Join-Path $PSScriptRoot 'Get-CrtSysVersion.ps1')
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = Join-Path $repoRoot 'artifacts\nuget'
}

$manifest = Join-Path $repoRoot 'nuget\crtsys.nuspec'
$stagingDirectory = Join-Path $repoRoot 'artifacts\nuget-staging'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$Toolset = @(
  $Toolset |
    ForEach-Object { $_ -split ',' } |
    ForEach-Object { $_.Trim() } |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
)
if ($Toolset.Count -eq 0) {
  throw "At least one crtsys prebuilt MSVC toolset must be specified."
}
foreach ($selectedToolset in $Toolset) {
  if (@('v142', 'v143', 'v145') -notcontains $selectedToolset) {
    throw "Unsupported crtsys prebuilt MSVC toolset: $selectedToolset. Supported toolsets are v142, v143, and v145."
  }
}

function Get-CrtSysPrebuiltArchitectures {
  param(
    [Parameter(Mandatory = $true)]
    [string] $ToolsetName
  )

  if ($ToolsetName -eq 'v145') {
    return @('x86', 'x64', 'ARM64')
  }

  return @('x86', 'x64', 'ARM', 'ARM64')
}

function Remove-UnselectedStagedLibraries {
  param(
    [Parameter(Mandatory = $true)]
    [string] $NativeDirectory,

    [Parameter(Mandatory = $true)]
    [string[]] $SelectedToolsets
  )

  if (-not (Test-Path $NativeDirectory)) {
    return
  }

  foreach ($toolsetDirectory in Get-ChildItem -Path $NativeDirectory -Directory) {
    if ($SelectedToolsets -notcontains $toolsetDirectory.Name) {
      Remove-Item -LiteralPath $toolsetDirectory.FullName -Recurse -Force
      continue
    }

    $selectedArchitectures = @(Get-CrtSysPrebuiltArchitectures -ToolsetName $toolsetDirectory.Name)
    foreach ($architectureDirectory in Get-ChildItem -Path $toolsetDirectory.FullName -Directory) {
      if ($selectedArchitectures -notcontains $architectureDirectory.Name) {
        Remove-Item -LiteralPath $architectureDirectory.FullName -Recurse -Force
        continue
      }

      foreach ($configurationDirectory in Get-ChildItem -Path $architectureDirectory.FullName -Directory) {
        if ($configurationDirectory.Name -ne 'Debug' -and $configurationDirectory.Name -ne 'Release') {
          Remove-Item -LiteralPath $configurationDirectory.FullName -Recurse -Force
        }
      }
    }
  }
}

foreach ($toolsetName in $Toolset) {
  foreach ($arch in @(Get-CrtSysPrebuiltArchitectures -ToolsetName $toolsetName)) {
    foreach ($config in @('Debug', 'Release')) {
      foreach ($library in @('crtsys.lib', 'Ldk.lib')) {
        $requiredPath = "lib\native\$toolsetName\$arch\$config\$library"
        $fullPath = Join-Path $stagingDirectory $requiredPath
        if (-not (Test-Path $fullPath)) {
          throw "Required prebuilt library is missing: $fullPath. Run scripts\nuget\Build-CrtSysNuGetLibs.ps1 first."
        }
      }
    }
  }
}

Remove-UnselectedStagedLibraries `
  -NativeDirectory (Join-Path $stagingDirectory 'lib\native') `
  -SelectedToolsets @($Toolset)

Write-Host "Packing crtsys $Version to $OutputDirectory"
if ([string]::IsNullOrWhiteSpace($NuGetExe)) {
  $nuget = Get-Command nuget -ErrorAction SilentlyContinue
  if (-not $nuget) {
    throw "nuget command was not found. Pass -NuGetExe or add nuget.exe to PATH."
  }

  $NuGetExe = $nuget.Source
} else {
  $NuGetExe = (Resolve-Path $NuGetExe).Path
}

& $NuGetExe pack $manifest `
  -OutputDirectory $OutputDirectory `
  -Version $Version `
  -NonInteractive

if ($LASTEXITCODE -ne 0) {
  throw "nuget pack failed with exit code $LASTEXITCODE."
}
