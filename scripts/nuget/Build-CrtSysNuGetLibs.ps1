param(
  [ValidateSet('x86', 'x64', 'ARM', 'ARM64')]
  [string[]] $Architecture = @('x86', 'x64', 'ARM64'),

  [ValidateSet('Debug', 'Release')]
  [string[]] $Configuration = @('Debug', 'Release'),

  [string[]] $Toolset = @('v143'),

  [string] $WindowsSdkVersion = '10.0.22621.0',

  [string] $WdkVersion = '',

  [string] $OutputDirectory,

  [switch] $LibrariesOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = Join-Path $repoRoot 'artifacts\nuget-staging'
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

if (-not $LibrariesOnly) {
  & (Join-Path $PSScriptRoot 'Stage-CrtSysMsQuicHeader.ps1') `
    -OutputDirectory (Join-Path $OutputDirectory 'msquic')
}

$platformByArchitecture = @{
  x86 = 'Win32'
  x64 = 'x64'
  ARM = 'ARM'
  ARM64 = 'ARM64'
}

$generatorByToolset = @{
  v142 = 'Visual Studio 17 2022'
  v143 = 'Visual Studio 17 2022'
  v145 = 'Visual Studio 18 2026'
}

foreach ($toolsetName in $Toolset) {
  foreach ($arch in $Architecture) {
    foreach ($config in $Configuration) {
    $platform = $platformByArchitecture[$arch]
    $generatorPlatform = "$platform,version=$WindowsSdkVersion"
    $effectiveWdkVersion = $WdkVersion
    if ([string]::IsNullOrWhiteSpace($effectiveWdkVersion)) {
      $effectiveWdkVersion = $WindowsSdkVersion
    }
    $useLibcntpr = 'ON'
    $buildKernelContentCodecs = if ($LibrariesOnly) { 'OFF' } else { 'ON' }
    $toolchainKey = "sdk_$($WindowsSdkVersion.Replace('.', '_'))_wdk_$($effectiveWdkVersion.Replace('.', '_'))"

    $buildDir = Join-Path $repoRoot "artifacts\build\crtsys_${toolsetName}_${arch}_${config}_$toolchainKey"
    $libOutputDir = Join-Path $OutputDirectory "lib\native\$toolsetName\$arch\$config"

    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
    New-Item -ItemType Directory -Force -Path $libOutputDir | Out-Null

    $configureArgs = @(
      '-S', $repoRoot,
      '-B', $buildDir,
      '-G', $generatorByToolset[$toolsetName],
      '-A', $generatorPlatform,
      '-T', $(if ($toolsetName -eq 'v142') { 'v142,host=x64' } else { 'host=x64' }),
      "-DCRTSYS_WDK_VERSION=$effectiveWdkVersion",
      "-DLDK_WDK_VERSION=$effectiveWdkVersion",
      "-DCMAKE_SYSTEM_VERSION=$WindowsSdkVersion",
      "-DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=$WindowsSdkVersion",
      '-DCMAKE_CXX_FLAGS=/MP',
      '-DCRTSYS_NTL_MAIN=ON',
      '-DCRTSYS_USE_PREBUILT=OFF',
      "-DCRTSYS_BUILD_NTL_KERNEL_CONTENT_CODECS=$buildKernelContentCodecs",
      "-DCRTSYS_USE_LIBCNTPR=$useLibcntpr"
    )

    Write-Host "Configuring crtsys $toolsetName $arch $config with Windows SDK $WindowsSdkVersion and WDK $effectiveWdkVersion"
    & cmake @configureArgs
    if ($LASTEXITCODE -ne 0) {
      throw "CMake configure failed with exit code $LASTEXITCODE."
    }

    Write-Host "Building crtsys $toolsetName $arch $config"
    & cmake --build $buildDir --config $config --target crtsys --parallel
    if ($LASTEXITCODE -ne 0) {
      throw "CMake build failed with exit code $LASTEXITCODE."
    }

    if (-not $LibrariesOnly) {
      Write-Host "Building NTL kernel content codecs $toolsetName $arch $config"
      & cmake --build $buildDir --config $config --target `
          crtsys_ntl_kernel_zlib crtsys_ntl_kernel_brotli --parallel
      if ($LASTEXITCODE -ne 0) {
        throw "Kernel content codec build failed with exit code $LASTEXITCODE."
      }
    }

    # Debug libraries stay under the build tree. Release libraries may also be
    # emitted to lib/<arch> by the top-level CMake target properties.
    $archLower = $arch.ToLower()
    $crtsysCandidates = @(
      Get-ChildItem -Path $buildDir -Filter 'crtsys.lib' -Recurse -File |
        Sort-Object FullName
    )
    if ($crtsysCandidates.Count -eq 0) {
      $preferredLibPaths = @(
        Join-Path $repoRoot "lib\$arch\crtsys.lib"
        Join-Path $repoRoot "lib\$archLower\crtsys.lib"
      )
      foreach ($libPath in $preferredLibPaths | Select-Object -Unique) {
        if (Test-Path $libPath) {
          $crtsysCandidates += Get-Item -Path $libPath
          break
        }
      }
    }
    if ($crtsysCandidates.Count -eq 0) {
      $available = Get-ChildItem -Path $buildDir -Filter '*.lib' -Recurse -File |
        Where-Object { $_.Name -in @('crtsys.lib', 'Ldk.lib') } |
        ForEach-Object { $_.FullName }
      throw "crtsys.lib was not found for $toolsetName $arch $config under $buildDir or $repoRoot\lib. Available libs: $($available -join ', ')"
    }

    $ldkSourceDir = $null
    $cmakeCachePath = Join-Path $buildDir 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cmakeCachePath -PathType Leaf) {
      $ldkSourceEntry = Get-Content -LiteralPath $cmakeCachePath |
        Where-Object { $_ -match '^Ldk_SOURCE_DIR:[^=]+=(.+)$' } |
        Select-Object -First 1
      if ($ldkSourceEntry -and $ldkSourceEntry -match '^Ldk_SOURCE_DIR:[^=]+=(.+)$') {
        $ldkSourceDir = $Matches[1]
      }
    }

    $ldkCandidates = @()
    if ($ldkSourceDir -and (Test-Path -LiteralPath $ldkSourceDir -PathType Container)) {
      $ldkConfigurationPaths = if ($config -eq 'Release') {
        @(
          (Join-Path $ldkSourceDir "lib\$archLower\Ldk.lib"),
          (Join-Path $ldkSourceDir "lib\$archLower\$config\Ldk.lib")
        )
      } else {
        @(
          (Join-Path $ldkSourceDir "lib\$archLower\$config\Ldk.lib"),
          (Join-Path $ldkSourceDir "lib\$archLower\Ldk.lib")
        )
      }
      foreach ($ldkPath in $ldkConfigurationPaths) {
        if (Test-Path -LiteralPath $ldkPath -PathType Leaf) {
          $ldkCandidates += Get-Item -LiteralPath $ldkPath
          break
        }
      }
    }
    if ($ldkCandidates.Count -eq 0) {
      $ldkCandidates = @(
        Get-ChildItem -Path $buildDir -Filter 'Ldk.lib' -Recurse -File |
          Sort-Object FullName
      )
    }
    if ($ldkCandidates.Count -eq 0) {
      throw "Ldk.lib was not found under the build directory or Ldk source directory: $buildDir, $ldkSourceDir"
    }

    Copy-Item -Path $crtsysCandidates[0].FullName -Destination (Join-Path $libOutputDir 'crtsys.lib') -Force
    Copy-Item -Path $ldkCandidates[0].FullName -Destination (Join-Path $libOutputDir 'Ldk.lib') -Force

    $pdbCandidates = @(
      Get-ChildItem -Path $buildDir -Filter '*.pdb' -Recurse -File |
        Where-Object { $_.Name -in @('crtsys.pdb', 'Ldk.pdb') }
    )
    if ($ldkSourceDir -and (Test-Path -LiteralPath $ldkSourceDir -PathType Container)) {
      $pdbCandidates += @(
        Get-ChildItem -Path $ldkSourceDir -Filter 'Ldk.pdb' -Recurse -File |
          Sort-Object LastWriteTime -Descending |
          Select-Object -First 1
      )
    }
    foreach ($pdb in $pdbCandidates) {
      Copy-Item -Path $pdb.FullName -Destination (Join-Path $libOutputDir $pdb.Name) -Force
    }

    if (-not $LibrariesOnly) {
      $codecBuildDir = Join-Path $repoRoot (
        "artifacts\build\content_codecs_${toolsetName}_${arch}_${config}_$toolchainKey")
      $codecInstallDir = Join-Path $codecBuildDir 'install'
      $codecSourceDir = Join-Path $repoRoot 'test\nuget\content-codecs'
      $codecConfigureArgs = @(
        '-S', $codecSourceDir,
        '-B', $codecBuildDir,
        '-G', $generatorByToolset[$toolsetName],
        '-A', $generatorPlatform,
        '-T', $(if ($toolsetName -eq 'v142') { 'v142,host=x64' } else { 'host=x64' }),
        "-DCRTSYS_ROOT:PATH=$($repoRoot.Replace('\', '/'))",
        "-DCMAKE_SYSTEM_VERSION:STRING=$WindowsSdkVersion",
        "-DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION:STRING=$WindowsSdkVersion"
      )

      Write-Host "Configuring NTL content codecs $toolsetName $arch $config"
      & cmake @codecConfigureArgs
      if ($LASTEXITCODE -ne 0) {
        throw "Content codec configure failed with exit code $LASTEXITCODE."
      }
      & cmake --build $codecBuildDir --config $config --target `
          crtsys_nuget_content_codecs --parallel
      if ($LASTEXITCODE -ne 0) {
        throw "Content codec build failed with exit code $LASTEXITCODE."
      }
      & cmake --install $codecBuildDir --config $config --prefix $codecInstallDir `
          --component CrtSysContentCodecs
      if ($LASTEXITCODE -ne 0) {
        throw "Content codec install failed with exit code $LASTEXITCODE."
      }

      $codecLibOutputDir = Join-Path $OutputDirectory (
        "codecs\lib\$toolsetName\$arch\$config")
      $codecIncludeOutputDir = Join-Path $OutputDirectory 'codecs\include'
      New-Item -ItemType Directory -Force -Path $codecLibOutputDir | Out-Null
      New-Item -ItemType Directory -Force -Path $codecIncludeOutputDir | Out-Null
      Copy-Item -Path (Join-Path $codecInstallDir 'lib\*.lib') `
        -Destination $codecLibOutputDir -Force
      Copy-Item -Path (Join-Path $codecInstallDir 'include\*') `
        -Destination $codecIncludeOutputDir -Recurse -Force

      $kernelCodecLibOutputDir = Join-Path $OutputDirectory (
        "kernel-codecs\lib\$toolsetName\$arch\$config")
      New-Item -ItemType Directory -Force -Path $kernelCodecLibOutputDir | Out-Null
      foreach ($kernelCodecName in @(
        'crtsys_ntl_kernel_zlib.lib',
        'crtsys_ntl_kernel_brotli.lib'
      )) {
        $kernelCodecCandidates = @(
          Get-ChildItem -Path $buildDir -Filter $kernelCodecName -Recurse -File |
            Sort-Object FullName
        )
        if ($kernelCodecCandidates.Count -ne 1) {
          throw "Expected exactly one $kernelCodecName under $buildDir, found $($kernelCodecCandidates.Count)."
        }
        Copy-Item -LiteralPath $kernelCodecCandidates[0].FullName `
          -Destination (Join-Path $kernelCodecLibOutputDir $kernelCodecName) -Force
      }

      Write-Host "Staged crtsys libraries in $libOutputDir, user codecs in $codecLibOutputDir, and kernel codecs in $kernelCodecLibOutputDir"
    } else {
      Write-Host "Staged crtsys libraries in $libOutputDir"
    }
    }
  }
}
