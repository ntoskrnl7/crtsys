[CmdletBinding()]
param(
  [string] $RepositoryRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
  $RepositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
}

function Fail([string] $Message) {
  throw "WFP example pair parity: $Message"
}

$root = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar)
$rootPrefix = $root + [IO.Path]::DirectorySeparatorChar

function Resolve-RepositoryPath([string] $RelativePath) {
  if ([string]::IsNullOrWhiteSpace($RelativePath) -or
      [IO.Path]::IsPathRooted($RelativePath)) {
    Fail "manifest path must be a nonempty relative path: '$RelativePath'"
  }
  $resolved = [IO.Path]::GetFullPath((Join-Path $root $RelativePath))
  if (-not $resolved.StartsWith(
      $rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    Fail "manifest path escapes the repository: '$RelativePath'"
  }
  return $resolved
}

function Read-RequiredFile([string] $RelativePath) {
  $path = Resolve-RepositoryPath $RelativePath
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    Fail "required file does not exist: '$RelativePath'"
  }
  return [IO.File]::ReadAllText($path)
}

function Assert-Contains(
    [string] $Text,
    [string] $Token,
    [string] $Context) {
  if ([string]::IsNullOrEmpty($Token) -or
      -not $Text.Contains($Token)) {
    Fail "$Context does not contain '$Token'"
  }
}

function Assert-UniqueStrings(
    [object[]] $Values,
    [string] $Context,
    [string] $Pattern = '') {
  if ($null -eq $Values -or $Values.Count -eq 0) {
    Fail "$Context must not be empty"
  }
  $seen = @{}
  foreach ($valueObject in $Values) {
    $value = [string] $valueObject
    if ([string]::IsNullOrWhiteSpace($value)) {
      Fail "$Context contains an empty value"
    }
    if ($Pattern -and $value -cnotmatch $Pattern) {
      Fail "$Context contains invalid value '$value'"
    }
    $key = $value.ToLowerInvariant()
    if ($seen.ContainsKey($key)) {
      Fail "$Context contains duplicate '$value'"
    }
    $seen[$key] = $true
  }
}

$manifestPath = Resolve-RepositoryPath 'test/wfp/example-pairs.json'
try {
  $manifest = [IO.File]::ReadAllText($manifestPath) | ConvertFrom-Json
} catch {
  Fail "example-pairs.json is not valid JSON: $($_.Exception.Message)"
}
if ($manifest.version -ne 1) {
  Fail "unsupported manifest version '$($manifest.version)'"
}
foreach ($property in @(
    'supportDirectories', 'sharedPolicies', 'pairs', 'kernelOnly', 'capabilityProofs',
    'protocolBoundaries')) {
  if ($null -eq $manifest.PSObject.Properties[$property]) {
    Fail "manifest is missing '$property'"
  }
}

$pairNames = @($manifest.pairs | ForEach-Object { [string] $_.name })
$kernelOnlyNames = @(
  $manifest.kernelOnly | ForEach-Object { [string] $_.name })
Assert-UniqueStrings $pairNames 'pair names' '^[a-z0-9]+(?:-[a-z0-9]+)*$'
Assert-UniqueStrings $kernelOnlyNames 'kernel-only names' `
    '^[a-z0-9]+(?:-[a-z0-9]+)*$'
foreach ($name in $kernelOnlyNames) {
  if ($pairNames -contains $name) {
    Fail "'$name' is both paired and kernel-only"
  }
}

$sharedPolicyNames = @(
  $manifest.sharedPolicies | ForEach-Object { [string] $_.name })
Assert-UniqueStrings $sharedPolicyNames 'shared policy names' `
    '^[a-z0-9]+(?:-[a-z0-9]+)*$'
if ([string]::Join('|', @($sharedPolicyNames | Sort-Object)) -cne
    [string]::Join('|', @($pairNames | Sort-Object))) {
  Fail 'every paired example must declare exactly one shared policy'
}
foreach ($policy in $manifest.sharedPolicies) {
  $name = [string] $policy.name
  $policyText = Read-RequiredFile ([string] $policy.path)
  $tokens = @($policy.contains)
  Assert-UniqueStrings $tokens "$name shared policy tokens"
  foreach ($token in $tokens) {
    Assert-Contains $policyText ([string] $token) "$name shared policy"
  }
  foreach ($sideName in @('user', 'kernel')) {
    $propertyName = "${sideName}Consumers"
    $consumers = @($policy.$propertyName)
    if ($consumers.Count -eq 0) {
      Fail "$name shared policy has no $sideName consumer"
    }
    foreach ($consumer in $consumers) {
      $consumerText = Read-RequiredFile ([string] $consumer.path)
      $consumerTokens = @($consumer.contains)
      Assert-UniqueStrings $consumerTokens `
          "$name shared policy $sideName consumer tokens"
      foreach ($token in $consumerTokens) {
        Assert-Contains $consumerText ([string] $token) `
            "$name shared policy $sideName consumer '$($consumer.path)'"
      }
    }
  }
}

$buildScript = Read-RequiredFile 'scripts/ci/Build-CrtSys.ps1'
$workflow = Read-RequiredFile '.github/workflows/cmake.yml'
$pairByName = @{}
$declaredTargetsBySide = @{}
$ctestTargetsBySide = @{}
$cmakeBySide = @{}

foreach ($pair in $manifest.pairs) {
  $name = [string] $pair.name
  $pairByName[$name] = $pair
  $capabilities = @($pair.requiredCapabilities)
  Assert-UniqueStrings $capabilities "$name requiredCapabilities" `
      '^[a-z0-9]+(?:-[a-z0-9]+)*$'

  foreach ($sideName in @('user', 'kernel')) {
    $side = $pair.$sideName
    if ($null -eq $side) {
      Fail "$name is missing its $sideName side"
    }
    $directory = "examples/wfp/$sideName/$name"
    foreach ($required in @('CMakeLists.txt', 'README.md', 'README.ko-KR.md')) {
      [void] (Read-RequiredFile "$directory/$required")
    }

    $fixtureDirectory = [string] $side.fixtureDirectory
    if ([string]::IsNullOrWhiteSpace($fixtureDirectory)) {
      Fail "$name/$sideName has no separate runtime traffic fixture directory"
    }
    $fixturePath = Resolve-RepositoryPath $fixtureDirectory
    if (-not (Test-Path -LiteralPath $fixturePath -PathType Container) -or
        $null -eq (Get-ChildItem -LiteralPath $fixturePath -File -Recurse |
            Select-Object -First 1)) {
      Fail "$name/$sideName has no nonempty runtime traffic fixture directory"
    }

    $cmakeFiles = if ($side.PSObject.Properties['cmakeFiles']) {
      @($side.cmakeFiles)
    } else {
      @("$directory/CMakeLists.txt")
    }
    Assert-UniqueStrings $cmakeFiles "$name/$sideName CMake files"
    if ($cmakeFiles -notcontains "$directory/CMakeLists.txt") {
      Fail "$name/$sideName CMake files omit the example CMakeLists.txt"
    }
    $cmake = [string]::Join(
        "`n", @($cmakeFiles | ForEach-Object {
          Read-RequiredFile ([string] $_)
        }))
    Assert-Contains $cmake $fixtureDirectory `
        "$name/$sideName CMake acceptance fixture source"
    $targets = @($side.targets)
    Assert-UniqueStrings $targets "$name/$sideName targets" `
        '^[A-Za-z0-9_]+$'

    $declaredTargets = @(
      [Regex]::Matches(
          $cmake,
          '(?ms)(?:crtsys_add_driver|add_executable|add_library|add_custom_target)\s*' +
          '\(\s*([A-Za-z0-9_]+)') |
          ForEach-Object { $_.Groups[1].Value } |
          Sort-Object -Unique
    )
    Assert-UniqueStrings $declaredTargets "$name/$sideName CMake targets" `
        '^[A-Za-z0-9_]+$'
    $manifestTargets = @($targets | Sort-Object -Unique)
    if ([string]::Join('|', $declaredTargets) -cne
        [string]::Join('|', $manifestTargets)) {
      Fail (
        "$name/$sideName targets differ: CMake [$declaredTargets], " +
        "manifest [$manifestTargets]")
    }
    $sideKey = "$name/$sideName"
    $declaredTargetsBySide[$sideKey] = $declaredTargets
    $cmakeBySide[$sideKey] = $cmake

    $ctestTargets = @()
    foreach ($testMatch in [Regex]::Matches(
        $cmake, '(?ms)add_test\s*\((.*?)\)')) {
      $body = $testMatch.Groups[1].Value
      $commandMatch = [Regex]::Match(
          $body, '(?ms)\bCOMMAND\s+([A-Za-z0-9_]+)')
      $nameMatch = [Regex]::Match(
          $body, '(?ms)\bNAME\s+([A-Za-z0-9_]+)')
      $candidate = if ($commandMatch.Success) {
        $commandMatch.Groups[1].Value
      } elseif ($nameMatch.Success) {
        $nameMatch.Groups[1].Value
      } else {
        ([Regex]::Match($body, '^\s*([A-Za-z0-9_]+)')).Groups[1].Value
      }
      if ($declaredTargets -contains $candidate) {
        $ctestTargets += $candidate
      }
    }
    foreach ($loopMatch in [Regex]::Matches(
        $cmake,
        '(?ms)foreach\s*\(\s*target\s+(.*?)\)(.*?)' +
        'endforeach\s*\([^\)]*\)')) {
      $body = $loopMatch.Groups[2].Value
      if ($body -notmatch (
          '(?ms)add_test\s*\([^\)]*' +
          '\$\{target\}[^\)]*\$\{target\}[^\)]*\)')) {
        continue
      }
      $loopTargets = @(
        [Regex]::Matches(
            $loopMatch.Groups[1].Value, '[A-Za-z0-9_]+') |
            ForEach-Object { $_.Value })
      foreach ($loopTarget in $loopTargets) {
        if ($declaredTargets -contains $loopTarget) {
          $ctestTargets += $loopTarget
        }
      }
    }
    $ctestTargetsBySide[$sideKey] = @($ctestTargets | Sort-Object -Unique)

    foreach ($target in $targets) {
      $escaped = [Regex]::Escape([string] $target)
      if ($cmake -notmatch (
          '(?ms)(?:crtsys_add_driver|add_executable|add_library|add_custom_target)\s*\(\s*' +
          $escaped + '(?:\s|\))')) {
        Fail "$name/$sideName target '$target' is not defined by CMake"
      }
    }

    $project = [string] $side.buildProject
    if ([string]::IsNullOrWhiteSpace($project)) {
      Fail "$name/$sideName has no buildProject"
    }
    Assert-Contains $buildScript "'$project'" `
        "$name/$sideName Build-CrtSys project list"
    Assert-Contains $workflow "'$project'" `
        "$name/$sideName GitHub Actions project matrix"

    if ($null -eq $side.evidence -or $side.evidence.Count -eq 0) {
      Fail "$name/$sideName has no structural evidence"
    }
    foreach ($evidence in $side.evidence) {
      $text = Read-RequiredFile ([string] $evidence.path)
      foreach ($token in @($evidence.contains)) {
        Assert-Contains $text ([string] $token) `
            "$name/$sideName evidence '$($evidence.path)'"
      }
    }
  }
}

foreach ($sample in $manifest.kernelOnly) {
  $name = [string] $sample.name
  $directory = "examples/wfp/kernel/$name"
  foreach ($required in @('CMakeLists.txt', 'README.md', 'README.ko-KR.md')) {
    [void] (Read-RequiredFile "$directory/$required")
  }
  if ([string]::IsNullOrWhiteSpace([string] $sample.mechanism)) {
    Fail "$name has no kernel-only mechanism"
  }
  $targets = @($sample.targets)
  Assert-UniqueStrings $targets "$name kernel-only targets" `
      '^[A-Za-z0-9_]+$'
  $cmake = Read-RequiredFile "$directory/CMakeLists.txt"
  $declaredTargets = @(
    [Regex]::Matches(
        $cmake,
        '(?ms)(?:crtsys_add_driver|add_executable|add_library)\s*' +
        '\(\s*([A-Za-z0-9_]+)') |
        ForEach-Object { $_.Groups[1].Value } |
        Sort-Object -Unique
  )
  $manifestTargets = @($targets | Sort-Object -Unique)
  if ([string]::Join('|', $declaredTargets) -cne
      [string]::Join('|', $manifestTargets)) {
    Fail (
      "$name kernel-only targets differ: CMake [$declaredTargets], " +
      "manifest [$manifestTargets]")
  }
  $fixtureDirectory = [string] $sample.fixtureDirectory
  $fixturePath = Resolve-RepositoryPath $fixtureDirectory
  if (-not (Test-Path -LiteralPath $fixturePath -PathType Container) -or
      $null -eq (Get-ChildItem -LiteralPath $fixturePath -File -Recurse |
          Select-Object -First 1)) {
    Fail "$name has no nonempty runtime fixture directory"
  }
  Assert-Contains $cmake $fixtureDirectory `
      "$name CMake acceptance fixture source"
  $project = [string] $sample.buildProject
  Assert-Contains $buildScript "'$project'" "$name Build-CrtSys project list"
  Assert-Contains $workflow "'$project'" "$name GitHub Actions project matrix"
}

function Get-NonemptyDirectoryNames([string] $RelativeRoot) {
  $path = Resolve-RepositoryPath $RelativeRoot
  return @(
    Get-ChildItem -LiteralPath $path -Directory | Where-Object {
      $null -ne (Get-ChildItem -LiteralPath $_.FullName -File -Recurse |
          Select-Object -First 1)
    } | ForEach-Object { $_.Name }
  )
}

$expectedUser = @($pairNames + @($manifest.supportDirectories.user)) |
    Sort-Object -Unique
$expectedKernel = @(
    $pairNames + $kernelOnlyNames + @($manifest.supportDirectories.kernel)) |
    Sort-Object -Unique
$actualUser = @(Get-NonemptyDirectoryNames 'examples/wfp/user') |
    Sort-Object -Unique
$actualKernel = @(Get-NonemptyDirectoryNames 'examples/wfp/kernel') |
    Sort-Object -Unique
if ([string]::Join('|', $expectedUser) -cne
    [string]::Join('|', $actualUser)) {
  Fail "user directories differ: expected [$expectedUser], actual [$actualUser]"
}
if ([string]::Join('|', $expectedKernel) -cne
    [string]::Join('|', $actualKernel)) {
  Fail (
    "kernel directories differ: expected [$expectedKernel], " +
    "actual [$actualKernel]")
}

function Read-InfSection(
    [string] $Text,
    [string] $SectionName,
    [string] $Context) {
  $match = [Regex]::Match(
      $Text,
      '(?ims)^\[' + [Regex]::Escape($SectionName) +
          '\]\s*\r?\n(.*?)(?=^\[|\z)')
  if (-not $match.Success) {
    Fail "$Context is missing [$SectionName]"
  }
  return $match.Groups[1].Value
}

foreach ($sideName in @('user', 'kernel')) {
  $supportDirectories = @($manifest.supportDirectories.$sideName)
  $sideRoot = Resolve-RepositoryPath "examples/wfp/$sideName"
  $sampleDirectories = @(
    Get-ChildItem -LiteralPath $sideRoot -Directory | Where-Object {
      $supportDirectories -notcontains $_.Name -and
      $null -ne (Get-ChildItem -LiteralPath $_.FullName -File -Recurse |
          Select-Object -First 1)
    })
  foreach ($sampleDirectory in $sampleDirectories) {
    $infFiles = @(
      Get-ChildItem -LiteralPath $sampleDirectory.FullName -File -Recurse `
          -Filter '*.inf' | Where-Object {
        $_.FullName -notmatch '[\\/]build(?:[\\/_.-]|$)' -and
        $_.FullName -notmatch '[\\/]CMakeFiles[\\/]'
      })
    if ($infFiles.Count -eq 0) {
      Fail "$sideName/$($sampleDirectory.Name) has no source INF"
    }
    foreach ($infFile in $infFiles) {
      $relativeInf = $infFile.FullName.Substring($rootPrefix.Length)
      $infText = [IO.File]::ReadAllText($infFile.FullName)
      $amd64Install = Read-InfSection $infText `
          'DefaultInstall.NTamd64' $relativeInf
      $amd64Services = Read-InfSection $infText `
          'DefaultInstall.NTamd64.Services' $relativeInf
      $arm64Install = Read-InfSection $infText `
          'DefaultInstall.NTarm64' $relativeInf
      $arm64Services = Read-InfSection $infText `
          'DefaultInstall.NTarm64.Services' $relativeInf
      $copyFiles = Read-InfSection $infText 'DriverCopyFiles' $relativeInf
      $serviceInstall = Read-InfSection $infText 'ServiceInstall' $relativeInf
      $destinationDirs = Read-InfSection $infText 'DestinationDirs' $relativeInf
      $sourceFiles = Read-InfSection $infText 'SourceDisksFiles' $relativeInf
      $sourceNames = Read-InfSection $infText 'SourceDisksNames' $relativeInf
      $strings = Read-InfSection $infText 'Strings' $relativeInf

      if ($destinationDirs -notmatch
          '(?im)^\s*DefaultDestDir\s*=\s*13\s*$') {
        Fail "$relativeInf must install through Driver Store DIRID 13"
      }
      if ($infText -match '(?im)^\s*Class\s*=\s*Sample\s*$') {
        Fail "$relativeInf uses the reserved Sample setup class"
      }

      foreach ($install in @($amd64Install, $arm64Install)) {
        if ($install -notmatch '(?im)^\s*CopyFiles\s*=\s*DriverCopyFiles\s*$') {
          Fail "$relativeInf architecture install does not copy DriverCopyFiles"
        }
      }
      $amd64ServiceMatch = [Regex]::Match(
          $amd64Services, '(?im)^\s*AddService\s*=\s*([^,\r\n]+)')
      $arm64ServiceMatch = [Regex]::Match(
          $arm64Services, '(?im)^\s*AddService\s*=\s*([^,\r\n]+)')
      if (-not $amd64ServiceMatch.Success -or
          -not $arm64ServiceMatch.Success -or
          $amd64ServiceMatch.Groups[1].Value.Trim() -cne
              $arm64ServiceMatch.Groups[1].Value.Trim()) {
        Fail "$relativeInf amd64/arm64 AddService names differ"
      }
      $sysMatch = [Regex]::Match(
          $copyFiles, '(?im)^\s*([^;\s=]+\.sys)\s*$')
      if (-not $sysMatch.Success) {
        Fail "$relativeInf DriverCopyFiles has no .sys payload"
      }
      $sysName = $sysMatch.Groups[1].Value
      if ($serviceInstall -notmatch (
          '(?im)^\s*ServiceBinary\s*=\s*%13%\\' +
          [Regex]::Escape($sysName) + '\s*$')) {
        Fail (
          "$relativeInf ServiceBinary does not use Driver Store DIRID 13 " +
          "for '$sysName'")
      }
      if ($sourceFiles -notmatch (
          '(?im)^\s*' + [Regex]::Escape($sysName) + '\s*=\s*1\s*$')) {
        Fail "$relativeInf SourceDisksFiles does not package '$sysName'"
      }
      if ($sourceNames -notmatch '(?im)^\s*1\s*=\s*%DiskName%\s*,') {
        Fail "$relativeInf SourceDisksNames has no disk 1"
      }
      if ($strings -notmatch '(?im)^\s*DiskName\s*=\s*"[^"]+"\s*$') {
        Fail "$relativeInf Strings has no nonempty DiskName"
      }
    }
  }
}

$proofIds = @(
  $manifest.capabilityProofs | ForEach-Object { [string] $_.id })
Assert-UniqueStrings $proofIds 'capability proof ids' `
    '^[a-z0-9]+(?:-[a-z0-9]+)*$'
$covered = @{}
foreach ($pairName in $pairNames) {
  foreach ($sideName in @('user', 'kernel')) {
    $covered["$pairName/$sideName"] = @{}
  }
}

foreach ($proof in $manifest.capabilityProofs) {
  $pairName = [string] $proof.pair
  $sideName = [string] $proof.side
  if (-not $pairByName.ContainsKey($pairName) -or
      @('user', 'kernel') -notcontains $sideName) {
    Fail "proof '$($proof.id)' names an unknown pair or side"
  }
  if (@('contract', 'e2e-target', 'runtime') -notcontains
      [string] $proof.kind) {
    Fail "proof '$($proof.id)' has invalid kind '$($proof.kind)'"
  }
  $side = $pairByName[$pairName].$sideName
  $target = [string] $proof.target
  if (@($side.targets) -notcontains $target) {
    Fail "proof '$($proof.id)' names unregistered target '$target'"
  }
  $source = [string] $proof.source
  $sourceText = Read-RequiredFile $source
  Assert-Contains $sourceText ([string] $proof.marker) `
      "proof '$($proof.id)' source"
  $requiredTokens = @($proof.requiredTokens)
  Assert-UniqueStrings $requiredTokens "proof '$($proof.id)' requiredTokens"
  foreach ($token in $requiredTokens) {
    Assert-Contains $sourceText ([string] $token) `
        "proof '$($proof.id)' source"
  }
  if ([string] $proof.kind -eq 'contract') {
    if (@($ctestTargetsBySide["$pairName/$sideName"]) -notcontains
        $target) {
      Fail "contract proof '$($proof.id)' target is not registered with CTest"
    }
  }
  if ([string] $proof.kind -eq 'runtime') {
    if ($null -eq $proof.PSObject.Properties['runner']) {
      Fail "runtime proof '$($proof.id)' has no runner"
    }
    $runnerText = Read-RequiredFile ([string] $proof.runner)
    Assert-Contains $runnerText ([string] $proof.marker) `
        "proof '$($proof.id)' runner"
    $runnerRequiredTokens = @($proof.runnerRequiredTokens)
    Assert-UniqueStrings $runnerRequiredTokens `
        "runtime proof '$($proof.id)' runnerRequiredTokens"
    foreach ($token in $runnerRequiredTokens) {
      Assert-Contains $runnerText ([string] $token) `
          "proof '$($proof.id)' runner"
    }
  }
  $requiredCapabilities = @(
    $pairByName[$pairName].requiredCapabilities | ForEach-Object {
      [string] $_
    })
  $proofCapabilities = @($proof.covers)
  Assert-UniqueStrings $proofCapabilities "proof '$($proof.id)' covers" `
      '^[a-z0-9]+(?:-[a-z0-9]+)*$'
  foreach ($capabilityObject in $proofCapabilities) {
    $capability = [string] $capabilityObject
    if ($requiredCapabilities -notcontains $capability) {
      Fail "proof '$($proof.id)' covers unknown capability '$capability'"
    }
    $covered["$pairName/$sideName"][$capability] = $true
  }
}

foreach ($pair in $manifest.pairs) {
  foreach ($sideName in @('user', 'kernel')) {
    $sideKey = "$($pair.name)/$sideName"
    foreach ($target in @($ctestTargetsBySide[$sideKey])) {
      $ctestProof = @(
        $manifest.capabilityProofs | Where-Object {
          [string] $_.pair -eq [string] $pair.name -and
          [string] $_.side -eq $sideName -and
          [string] $_.target -eq $target
        })
      if ($ctestProof.Count -eq 0) {
        Fail (
          "$sideKey CTest target '$target' has no capability proof")
      }
    }
  }
}

foreach ($pair in $manifest.pairs) {
  foreach ($sideName in @('user', 'kernel')) {
    foreach ($capabilityObject in @($pair.requiredCapabilities)) {
      $capability = [string] $capabilityObject
      if (-not $covered["$($pair.name)/$sideName"].ContainsKey($capability)) {
        Fail "$($pair.name)/$sideName capability '$capability' has no proof"
      }
    }
  }
}

$boundaryIds = @(
  $manifest.protocolBoundaries | ForEach-Object { [string] $_.id })
Assert-UniqueStrings $boundaryIds 'protocol boundary ids' `
    '^[a-z0-9]+(?:-[a-z0-9]+)*$'
foreach ($boundary in $manifest.protocolBoundaries) {
  $text = Read-RequiredFile ([string] $boundary.path)
  foreach ($token in @($boundary.contains)) {
    Assert-Contains $text ([string] $token) `
        "protocol boundary '$($boundary.id)'"
  }
}

$pairedRoots = @()
foreach ($pairName in $pairNames) {
  $pairedRoots += Resolve-RepositoryPath "examples/wfp/user/$pairName"
  $pairedRoots += Resolve-RepositoryPath "examples/wfp/kernel/$pairName"
}
$sourceExtensions = @('.c', '.cc', '.cpp', '.cxx', '.h', '.hpp', '.inl')
function Get-ExampleSourceFiles([string[]] $Roots) {
  return @(
    Get-ChildItem -LiteralPath $Roots -File -Recurse | Where-Object {
      $sourceExtensions -contains $_.Extension.ToLowerInvariant() -and
      $_.FullName -notmatch '[\\/]build(?:[\\/_.-]|$)' -and
      $_.FullName -notmatch '[\\/]CMakeFiles[\\/]'
    })
}
$pairedSourceFiles = Get-ExampleSourceFiles $pairedRoots
$unsafePermit = @(
  $pairedSourceFiles | Select-String -SimpleMatch 'callout_unavailable::permit')
if ($unsafePermit.Count -ne 0) {
  Fail "paired examples contain callout_unavailable::permit"
}

$kernelCode = Get-ExampleSourceFiles @(
    (Resolve-RepositoryPath 'examples/wfp/kernel'))
$userImports = @(
  $kernelCode | Select-String -Pattern '(?:\.\.[\\/])+user[\\/]|examples[\\/]wfp[\\/]user')
if ($userImports.Count -ne 0) {
  Fail "kernel example source imports user example implementation"
}

$fixtureCode = Get-ExampleSourceFiles @(
    (Resolve-RepositoryPath 'test/wfp/runtime/fixtures/user'),
    (Resolve-RepositoryPath 'test/wfp/runtime/fixtures/kernel'))
$fixtureControlViolations = @(
  $fixtureCode | Select-String -Pattern (
      'ntl::wfp|<ntl/wfp|\bFwpm[A-Z]|\bDeviceIoControl\b|' +
      '\bOpenSCManager(?:A|W)?\b|\bCreateService(?:A|W)?\b|' +
      '\bStartService(?:A|W)?\b|\bControlService\b|' +
      '\bDeleteService\b'))
if ($fixtureControlViolations.Count -ne 0) {
  Fail (
    'runtime traffic fixtures contain WFP policy, driver-control, or SCM ' +
    'ownership code')
}

$productCode = Get-ExampleSourceFiles @(
    (Resolve-RepositoryPath 'examples/wfp/user'),
    (Resolve-RepositoryPath 'examples/wfp/kernel')) | Where-Object {
  $_.FullName -notmatch '[\\/]test[\\/]'
}
$productJudgmentViolations = @(
  $productCode | Select-String -Pattern (
      '"[^"\r\n]*\bPASS\b|\bself_test[_A-Za-z0-9]*\b|' +
      '\b(?:class|struct)\s+[A-Za-z0-9_]*fixture\b'))
if ($productJudgmentViolations.Count -ne 0) {
  Fail (
    'product example source contains acceptance verdicts or synthetic ' +
    'self-test fixtures')
}
$productFixtureImports = @(
  $productCode | Select-String -Pattern (
      'test[\\/]wfp[\\/]runtime[\\/]fixtures|' +
      '#include\s*[<"][^>"]*fixture[^>"]*[>"]'))
if ($productFixtureImports.Count -ne 0) {
  Fail 'product example source imports runtime traffic-fixture implementation'
}

Write-Host (
  "WFP example pair parity contract passed: $($pairNames.Count) pairs, " +
  "$($kernelOnlyNames.Count) kernel-only primitives, " +
  "$($proofIds.Count) capability proofs.")
