param(
  [ValidateSet('x64', 'ARM64')]
  [string[]] $Architecture = @('x64', 'ARM64'),

  [ValidateSet('Debug', 'Release')]
  [string[]] $Configuration = @('Debug', 'Release'),

  [string] $WindowsSdkVersion = '10.0.22621.0',

  [string] $OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = Join-Path $repoRoot 'artifacts\nuget-staging'
}

$platformByArchitecture = @{
  x64 = 'x64'
  ARM64 = 'ARM64'
}

foreach ($arch in $Architecture) {
  foreach ($config in $Configuration) {
    $platform = $platformByArchitecture[$arch]
    $buildDir = Join-Path $repoRoot "artifacts\build\crtsys_${arch}_$config"
    $libOutputDir = Join-Path $OutputDirectory "lib\native\$arch\$config"

    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
    New-Item -ItemType Directory -Force -Path $libOutputDir | Out-Null

    $configureArgs = @(
      '-S', $repoRoot,
      '-B', $buildDir,
      '-G', 'Visual Studio 17 2022',
      '-A', $platform,
      '-T', 'host=x64',
      "-DCMAKE_SYSTEM_VERSION=$WindowsSdkVersion",
      "-DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=$WindowsSdkVersion",
      '-DCMAKE_CXX_FLAGS=/MP',
      '-DCRTSYS_NTL_MAIN=ON',
      '-DCRTSYS_USE_LIBCNTPR=ON'
    )

    Write-Host "Configuring crtsys $arch $config with Windows SDK $WindowsSdkVersion"
    & cmake @configureArgs
    if ($LASTEXITCODE -ne 0) {
      throw "CMake configure failed with exit code $LASTEXITCODE."
    }

    Write-Host "Building crtsys $arch $config"
    & cmake --build $buildDir --config $config --target crtsys --parallel
    if ($LASTEXITCODE -ne 0) {
      throw "CMake build failed with exit code $LASTEXITCODE."
    }

    # Pick deterministic output paths first, then fallback to recursive search.
    $archLower = $arch.ToLower()
    $crtsysCandidates = @()
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

    if ($crtsysCandidates.Count -eq 0) {
      $crtsysCandidates = @(
        Get-ChildItem -Path $buildDir -Filter 'crtsys.lib' -Recurse -File |
          Sort-Object FullName
      )
    }
    if ($crtsysCandidates.Count -eq 0) {
      $available = Get-ChildItem -Path $buildDir -Filter '*.lib' -Recurse -File |
        Where-Object { $_.Name -in @('crtsys.lib', 'Ldk.lib') } |
        ForEach-Object { $_.FullName }
      throw "crtsys.lib was not found for $arch $config under $buildDir or $repoRoot\lib. Available libs: $($available -join ', ')"
    }

    $ldkCandidates = @(Get-ChildItem -Path $buildDir -Filter 'Ldk.lib' -Recurse -File | Sort-Object FullName)
    if ($ldkCandidates.Count -eq 0) {
      throw "Ldk.lib was not found under $buildDir"
    }

    Copy-Item -Path $crtsysCandidates[0].FullName -Destination (Join-Path $libOutputDir 'crtsys.lib') -Force
    Copy-Item -Path $ldkCandidates[0].FullName -Destination (Join-Path $libOutputDir 'Ldk.lib') -Force

    $pdbCandidates = Get-ChildItem -Path $buildDir -Filter '*.pdb' -Recurse -File |
      Where-Object { $_.Name -in @('crtsys.pdb', 'Ldk.pdb') }
    foreach ($pdb in $pdbCandidates) {
      Copy-Item -Path $pdb.FullName -Destination (Join-Path $libOutputDir $pdb.Name) -Force
    }

    Write-Host "Staged crtsys libraries in $libOutputDir"
  }
}
