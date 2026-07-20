[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$examplesRoot = Join-Path $repoRoot 'examples'
$errors = [System.Collections.Generic.List[string]]::new()

function Get-RepositoryRelativePath([string] $Path) {
  return $Path.Substring($repoRoot.Length).TrimStart('\', '/')
}

foreach ($requiredFile in @('Directory.Build.props', 'Directory.Build.targets')) {
  $path = Join-Path $examplesRoot $requiredFile
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    $errors.Add("Missing shared example project file: $path")
  }
}

$sharedPropsPath = Join-Path $examplesRoot 'Directory.Build.props'
if (Test-Path -LiteralPath $sharedPropsPath -PathType Leaf) {
  $sharedProps = Get-Content -LiteralPath $sharedPropsPath -Raw
  foreach ($requiredToken in @(
      'CrtSysDevelopmentPackageRoot',
      'CrtSysUseRepositoryDevelopmentFiles',
      'CrtSysUsePackageReference',
      'ExcludeRestorePackageImports',
      'build\native\crtsys.props')) {
    if (-not $sharedProps.Contains($requiredToken)) {
      $errors.Add("examples/Directory.Build.props is missing $requiredToken.")
    }
  }
}

$sharedTargetsPath = Join-Path $examplesRoot 'Directory.Build.targets'
if (Test-Path -LiteralPath $sharedTargetsPath -PathType Leaf) {
  $sharedTargets = Get-Content -LiteralPath $sharedTargetsPath -Raw
  foreach ($requiredToken in @(
      'nuget\build\native\crtsys.targets',
      'build\native\crtsys.targets',
      'CrtSysUseRepositoryDevelopmentFiles',
      '$(CrtSysRepositoryRoot)include')) {
    if (-not $sharedTargets.Contains($requiredToken)) {
      $errors.Add("examples/Directory.Build.targets is missing $requiredToken.")
    }
  }
}

$expectedCondition = "'`$(CrtSysUsePackageReference)' == 'true'"
$trackedProjectPaths = @(& git -C $repoRoot ls-files -- 'examples/**/*.vcxproj')
if ($LASTEXITCODE -ne 0) {
  throw 'Could not enumerate tracked Visual Studio example projects.'
}

$untrackedProjectPaths = @(
  & git -C $repoRoot ls-files --others --exclude-standard -- 'examples/**/*.vcxproj'
)
if ($LASTEXITCODE -ne 0) {
  throw 'Could not enumerate untracked Visual Studio example projects.'
}

$projects = @(
  @($trackedProjectPaths + $untrackedProjectPaths) |
    Sort-Object -Unique |
    ForEach-Object { Get-Item -LiteralPath (Join-Path $repoRoot $_) }
)

foreach ($project in $projects) {
  try {
    [xml] $document = Get-Content -LiteralPath $project.FullName -Raw
  } catch {
    $errors.Add("Invalid MSBuild XML in $($project.FullName): $($_.Exception.Message)")
    continue
  }

  $namespace = [System.Xml.XmlNamespaceManager]::new($document.NameTable)
  $namespace.AddNamespace('msb', 'http://schemas.microsoft.com/developer/msbuild/2003')
  $references = $document.SelectNodes("//msb:PackageReference[@Include='crtsys']", $namespace)

  if ($references.Count -ne 1) {
    $relativePath = Get-RepositoryRelativePath $project.FullName
    $errors.Add("$relativePath must contain exactly one crtsys PackageReference.")
  }

  foreach ($reference in $references) {
    $itemGroupCondition = [string] $reference.ParentNode.GetAttribute('Condition')
    $referenceCondition = [string] $reference.GetAttribute('Condition')
    if ($itemGroupCondition -ne $expectedCondition -and
        $referenceCondition -ne $expectedCondition) {
      $relativePath = Get-RepositoryRelativePath $project.FullName
      $errors.Add("$relativePath must condition its crtsys PackageReference on $expectedCondition.")
    }
  }

  $localPackageProperties = $document.SelectNodes(
      '//msb:CrtSysRepositoryRoot | //msb:CrtSysDevelopmentPackageRoot | //msb:CrtSysUseRepositoryDevelopmentFiles | //msb:CrtSysUsePackageReference | //msb:CrtSysPackageVersion | //msb:ExcludeRestorePackageImports | //msb:RestoreProjectStyle',
      $namespace)
  if ($localPackageProperties.Count -ne 0) {
    $relativePath = Get-RepositoryRelativePath $project.FullName
    $errors.Add("$relativePath duplicates package-selection properties owned by examples/Directory.Build.props.")
  }
}

if ($errors.Count -ne 0) {
  $errors | ForEach-Object { Write-Error $_ }
  throw "Example project convention validation failed with $($errors.Count) error(s)."
}

Write-Host "Validated $($projects.Count) Visual Studio example projects."
