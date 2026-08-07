[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$wfpRoot = Join-Path $repoRoot 'examples\wfp'
$generator = Join-Path $repoRoot `
  'scripts\examples\Generate-WfpVisualStudioProjects.ps1'
$errors = [System.Collections.Generic.List[string]]::new()

& $generator -Check

function Get-ProjectNodes([xml] $Document, [string] $XPath) {
  $namespace = [System.Xml.XmlNamespaceManager]::new($Document.NameTable)
  $namespace.AddNamespace(
    'msb', 'http://schemas.microsoft.com/developer/msbuild/2003')
  return $Document.SelectNodes($XPath, $namespace)
}

function Get-CMakePublicTargets([string] $CMake) {
  $matches = [regex]::Matches(
    $CMake,
    '(?ms)(?:crtsys_add_driver|add_executable)\(\s*' +
      '(?<target>[A-Za-z0-9_]+)(?<body>.*?)\)')
  return @(
    $matches |
      ForEach-Object { $_.Groups['target'].Value } |
      Where-Object { $_ -notmatch '_(?:acceptance|contracts?)$' } |
      Sort-Object -Unique
  )
}

function Get-CMakeTargetSources([string] $CMake, [string] $Target) {
  $body = Get-CMakeTargetBody $CMake $Target
  if ($null -eq $body) {
    return @()
  }
  return @(
    [regex]::Matches(
      $body,
      '(?<source>(?:app|driver)[/\\][A-Za-z0-9_./\\-]+\.cpp)') |
      ForEach-Object {
        $_.Groups['source'].Value.Replace('/', '\')
      } |
      Sort-Object -Unique
  )
}

function Get-CMakeTargetBody([string] $CMake, [string] $Target) {
  $escapedTarget = [regex]::Escape($Target)
  $match = [regex]::Match(
    $CMake,
    '(?ms)(?:crtsys_add_driver|add_executable)\(\s*' +
      $escapedTarget + '(?<body>.*?)\)')
  if (-not $match.Success) {
    return $null
  }
  return $match.Groups['body'].Value
}

$sampleDirectories = @(
  foreach ($runtime in @('user', 'kernel')) {
    Get-ChildItem -LiteralPath (Join-Path $wfpRoot $runtime) -Directory |
      Where-Object Name -ne 'common'
  }
)

foreach ($sampleDirectory in $sampleDirectories) {
  $cmakePath = Join-Path $sampleDirectory.FullName 'CMakeLists.txt'
  if (-not (Test-Path -LiteralPath $cmakePath -PathType Leaf)) {
    $errors.Add("Missing WFP CMake project: $cmakePath")
    continue
  }

  $projects = @(Get-ChildItem -LiteralPath $sampleDirectory.FullName `
      -Filter '*.vcxproj' -File)
  $solutions = @(Get-ChildItem -LiteralPath $sampleDirectory.FullName `
      -Filter '*_vs.sln' -File)
  if ($solutions.Count -ne 1) {
    $errors.Add(
      "$($sampleDirectory.FullName) must contain exactly one generated solution.")
    continue
  }

  $cmake = Get-Content -LiteralPath $cmakePath -Raw
  $expectedTargets = @(Get-CMakePublicTargets $cmake)
  $actualTargets = [System.Collections.Generic.List[string]]::new()
  $solution = Get-Content -LiteralPath $solutions[0].FullName -Raw

  foreach ($project in $projects) {
    try {
      [xml] $document = Get-Content -LiteralPath $project.FullName -Raw
    } catch {
      $errors.Add("Invalid WFP MSBuild XML in $($project.FullName): " +
        $_.Exception.Message)
      continue
    }

    $projectNames = @(Get-ProjectNodes $document '//msb:ProjectName')
    if ($projectNames.Count -ne 1) {
      $errors.Add("$($project.FullName) must contain one ProjectName.")
      continue
    }
    $target = $projectNames[0].InnerText
    $actualTargets.Add($target)

    if (-not $solution.Contains(
        "`"$target`", `"$target.vcxproj`"")) {
      $errors.Add(
        "$($solutions[0].FullName) does not reference $target.vcxproj.")
    }

    $configurationTypes = @(
      Get-ProjectNodes $document '//msb:ConfigurationType' |
        ForEach-Object InnerText |
        Sort-Object -Unique
    )
    if ($configurationTypes.Count -ne 1) {
      $errors.Add("$($project.FullName) has inconsistent project types.")
      continue
    }

    if ($configurationTypes[0] -eq 'Driver') {
      $driverTypes = @(
        Get-ProjectNodes $document '//msb:DriverType' |
          ForEach-Object InnerText |
          Sort-Object -Unique)
      $entryPoints = @(
        Get-ProjectNodes $document '//msb:CrtSysWdmEntryPoint' |
          ForEach-Object InnerText |
          Sort-Object -Unique)
      if ($driverTypes.Count -ne 1 -or $driverTypes[0] -ne 'WDM') {
        $errors.Add("$($project.FullName) is not a WDM driver project.")
      }
      if ($entryPoints.Count -ne 1 -or $entryPoints[0] -ne 'NtlWfp') {
        $errors.Add("$($project.FullName) does not select NtlWfp.")
      }
      $cmakeTargetBody = Get-CMakeTargetBody $cmake $target
      $expectsKernelMsQuic =
        $null -ne $cmakeTargetBody -and
        $cmakeTargetBody -match '(?m)(?:^|\s)KERNEL_MSQUIC(?:\s|$)'
      $kernelMsQuicValues = @(
        Get-ProjectNodes $document '//msb:CrtSysUseNtlKernelMsQuic' |
          ForEach-Object InnerText |
          Sort-Object -Unique)
      $expectedKernelMsQuicValue = if ($expectsKernelMsQuic) { 'true' } else { 'false' }
      if ($kernelMsQuicValues.Count -ne 1 -or
          $kernelMsQuicValues[0] -ne $expectedKernelMsQuicValue) {
        $errors.Add(
          "$($project.FullName) must set CrtSysUseNtlKernelMsQuic=$expectedKernelMsQuicValue to match CMake.")
      }
      if (@(Get-ProjectNodes $document '//msb:Inf').Count -ne 1) {
        $errors.Add("$($project.FullName) must package exactly one INF.")
      }
    } elseif ($configurationTypes[0] -ne 'Application') {
      $errors.Add(
        "$($project.FullName) has unsupported ConfigurationType " +
        "$($configurationTypes[0]).")
    }

    $projectSources = @(
      Get-ProjectNodes $document '//msb:ItemGroup/msb:ClCompile[@Include]' |
        ForEach-Object { $_.GetAttribute('Include').Replace('/', '\') } |
        Sort-Object -Unique)
    $cmakeSources = @(Get-CMakeTargetSources $cmake $target)
    if ((Compare-Object $projectSources $cmakeSources).Count -ne 0) {
      $errors.Add(
        "$($project.FullName) compile sources do not match target $target in CMakeLists.txt.")
    }
    foreach ($source in $projectSources) {
      if (-not (Test-Path -LiteralPath (Join-Path $sampleDirectory.FullName $source) `
          -PathType Leaf)) {
        $errors.Add("$($project.FullName) references missing source $source.")
      }
    }
  }

  if ((Compare-Object @($actualTargets | Sort-Object -Unique) `
      $expectedTargets).Count -ne 0) {
    $errors.Add(
      "$($sampleDirectory.FullName) Visual Studio public targets do not match CMake.")
  }
}

if ($sampleDirectories.Count -ne 19) {
  $errors.Add(
    "Expected 19 WFP sample directories, found $($sampleDirectories.Count).")
}

if ($errors.Count -ne 0) {
  $errors | ForEach-Object { Write-Error $_ }
  throw "WFP Visual Studio project validation failed with $($errors.Count) error(s)."
}

$projectCount = @(
  foreach ($sampleDirectory in $sampleDirectories) {
    Get-ChildItem -LiteralPath $sampleDirectory.FullName `
      -Filter '*.vcxproj' -File
  }
).Count
Write-Host "Validated $projectCount WFP Visual Studio projects in $($sampleDirectories.Count) sample solutions."
