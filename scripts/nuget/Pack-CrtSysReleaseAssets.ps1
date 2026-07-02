param(
  [string] $Version,
  [string] $OutputDirectory,

  [string[]] $Toolset = @('v143')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

if ([string]::IsNullOrWhiteSpace($Version)) {
  $Version = & (Join-Path $PSScriptRoot 'Get-CrtSysVersion.ps1')
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = Join-Path $repoRoot 'artifacts\release'
}

$nugetDirectory = Join-Path $repoRoot 'artifacts\nuget'
$stagingDirectory = Join-Path $repoRoot 'artifacts\nuget-staging'
$workDirectory = Join-Path $repoRoot 'artifacts\release-staging'
$bundleRoot = Join-Path $workDirectory "crtsys-$Version"
$bundleCMakePackageDirectory = Join-Path $bundleRoot 'share\crtsys\cmake'
$prebuiltZipPath = Join-Path $OutputDirectory "crtsys-$Version-prebuilt.zip"
$checksumPath = Join-Path $OutputDirectory "crtsys-$Version-SHA256SUMS.txt"
$packagePath = Join-Path $nugetDirectory "crtsys.$Version.nupkg"
$releasePackagePath = Join-Path $OutputDirectory "crtsys.$Version.nupkg"

if (-not (Test-Path $packagePath)) {
  throw "NuGet package was not found: $packagePath. Run scripts\nuget\Pack-CrtSysNuGet.ps1 first."
}

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
        $requiredPath = Join-Path $stagingDirectory "lib\native\$toolsetName\$arch\$config\$library"
        if (-not (Test-Path $requiredPath)) {
          throw "Required prebuilt release asset file is missing: $requiredPath."
        }
      }
    }
  }
}

Remove-UnselectedStagedLibraries `
  -NativeDirectory (Join-Path $stagingDirectory 'lib\native') `
  -SelectedToolsets @($Toolset)

Remove-Item -Recurse -Force -Path $workDirectory -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $bundleRoot | Out-Null
New-Item -ItemType Directory -Force -Path $bundleCMakePackageDirectory | Out-Null

Copy-Item -Path (Join-Path $repoRoot 'README.md') -Destination $bundleRoot -Force
Copy-Item -Path (Join-Path $repoRoot 'LICENSE') -Destination $bundleRoot -Force
Copy-Item -Path (Join-Path $repoRoot 'docs') -Destination (Join-Path $bundleRoot 'docs') -Recurse -Force
Copy-Item -Path (Join-Path $repoRoot 'include') -Destination (Join-Path $bundleRoot 'include') -Recurse -Force
Copy-Item -Path (Join-Path $repoRoot 'cmake') -Destination (Join-Path $bundleRoot 'cmake') -Recurse -Force
Copy-Item -Path (Join-Path $repoRoot 'cmake\*') -Destination $bundleCMakePackageDirectory -Recurse -Force
Copy-Item -Path (Join-Path $repoRoot 'nuget\build') -Destination (Join-Path $bundleRoot 'build') -Recurse -Force
Copy-Item -Path (Join-Path $stagingDirectory 'lib') -Destination (Join-Path $bundleRoot 'lib') -Recurse -Force

$versionMajor = ($Version -split '\.')[0]
$configCMake = @"
get_filename_component(PACKAGE_PREFIX_DIR "`${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

set(crtsys_ROOT "`${PACKAGE_PREFIX_DIR}")
set(CRTSYS_ROOT "`${PACKAGE_PREFIX_DIR}")

include("`${CMAKE_CURRENT_LIST_DIR}/CrtSys.cmake")
"@
Set-Content -LiteralPath (Join-Path $bundleCMakePackageDirectory 'crtsys-config.cmake') -Value $configCMake -Encoding ASCII

$configVersionCMake = @"
set(PACKAGE_VERSION "$Version")

if(PACKAGE_FIND_VERSION)
  if(PACKAGE_FIND_VERSION VERSION_GREATER PACKAGE_VERSION)
    set(PACKAGE_VERSION_COMPATIBLE FALSE)
  elseif(NOT PACKAGE_FIND_VERSION_MAJOR EQUAL $versionMajor)
    set(PACKAGE_VERSION_COMPATIBLE FALSE)
  else()
    set(PACKAGE_VERSION_COMPATIBLE TRUE)
    if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)
      set(PACKAGE_VERSION_EXACT TRUE)
    endif()
  endif()
endif()
"@
Set-Content -LiteralPath (Join-Path $bundleCMakePackageDirectory 'crtsys-config-version.cmake') -Value $configVersionCMake -Encoding ASCII

$bundleReadme = @"
# crtsys $Version Prebuilt Release Bundle

This archive is an offline-friendly prebuilt bundle built by the GitHub Actions
Package workflow.

Contents:

- include/: public and internal compatibility headers
- cmake/: CMake helpers; CrtSys.cmake links prebuilt libraries from this bundle
- share/crtsys/cmake/: CMake package config for find_package(crtsys CONFIG)
- build/native/: native MSBuild props and targets from the NuGet package
- lib/native/: prebuilt crtsys.lib and Ldk.lib by MSVC toolset, architecture, and configuration
- docs/: repository documentation

The prebuilt driver libraries are organized as
lib/native/<toolset>/<arch>/<config>. The release bundle can include v142 and
v143 libraries for x86, x64, ARM, and ARM64, plus v145 libraries for x86, x64,
and ARM64. Validate the final driver with the Windows, WDK, SDK, Visual Studio,
architecture, Driver Verifier, and code integrity settings that you ship.

For Visual Studio/MSBuild consumers, the .nupkg attached to the same GitHub
Release is usually the easiest offline install path.
"@
Set-Content -LiteralPath (Join-Path $bundleRoot 'README.release.md') -Value $bundleReadme -Encoding UTF8

Remove-Item -Force -Path $prebuiltZipPath -ErrorAction SilentlyContinue
Remove-Item -Force -Path $releasePackagePath -ErrorAction SilentlyContinue
Remove-Item -Force -Path $checksumPath -ErrorAction SilentlyContinue

Compress-Archive -Path $bundleRoot -DestinationPath $prebuiltZipPath -CompressionLevel Optimal
Copy-Item -Path $packagePath -Destination $releasePackagePath -Force

Get-FileHash -Algorithm SHA256 -Path $prebuiltZipPath, $releasePackagePath |
  ForEach-Object { "$($_.Hash.ToLowerInvariant())  $([System.IO.Path]::GetFileName($_.Path))" } |
  Set-Content -LiteralPath $checksumPath -Encoding ASCII

Write-Host "Created release assets:"
Get-ChildItem -Path $OutputDirectory -File | Sort-Object FullName | ForEach-Object {
  Write-Host "  $($_.FullName)"
}
