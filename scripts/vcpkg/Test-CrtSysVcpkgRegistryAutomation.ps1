param(
  [string] $WorkDirectory,

  [string] $VcpkgExe
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sourcePortDirectory = Join-Path $repoRoot 'vcpkg\ports\crtsys'
$publishScript = Join-Path $PSScriptRoot 'Publish-CrtSysVcpkgRegistry.ps1'
$versionScript = Join-Path $repoRoot 'scripts\nuget\Get-CrtSysVersion.ps1'

if ([string]::IsNullOrWhiteSpace($WorkDirectory)) {
  $WorkDirectory = Join-Path $repoRoot '.local\vcpkg-registry-automation-contract'
}
$WorkDirectory = [System.IO.Path]::GetFullPath($WorkDirectory)
$repoRootPrefix = $repoRoot.TrimEnd('\') + '\'
if (-not $WorkDirectory.StartsWith(
    $repoRootPrefix,
    [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "WorkDirectory must stay inside the repository: $WorkDirectory"
}

foreach ($requiredPath in @(
  $sourcePortDirectory,
  $publishScript,
  $versionScript
)) {
  if (-not (Test-Path -LiteralPath $requiredPath)) {
    throw "Required registry automation input was not found: $requiredPath"
  }
}

$currentVersion = [string](& $versionScript)
if ($currentVersion -notmatch '^(\d+)\.(\d+)\.(\d+)$') {
  throw "Could not parse current crtsys version: $currentVersion"
}
$nextVersion = '{0}.{1}.{2}' -f $Matches[1], $Matches[2], ([int]$Matches[3] + 1)

Remove-Item -LiteralPath $WorkDirectory -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $WorkDirectory | Out-Null

$registryDirectory = Join-Path $WorkDirectory 'registry'
$archivePath = Join-Path $WorkDirectory 'dummy-prebuilt.zip'
New-Item -ItemType Directory -Force -Path $registryDirectory | Out-Null
[System.IO.File]::WriteAllBytes(
  $archivePath,
  [System.Text.Encoding]::UTF8.GetBytes('crtsys registry automation contract'))

function Invoke-Git {
  param([Parameter(Mandatory = $true)][string[]] $Arguments)

  & git @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE."
  }
}

Invoke-Git @('-C', $registryDirectory, 'init', '--initial-branch=vcpkg-registry')
Invoke-Git @('-C', $registryDirectory, 'config', 'user.name', 'crtsys automation test')
Invoke-Git @('-C', $registryDirectory, 'config', 'user.email', 'automation-test@crtsys.invalid')
Invoke-Git @('-C', $registryDirectory, 'config', 'core.autocrlf', 'false')

$initialBaseline = '0000000000000000000000000000000000000000'
$registryReadme = @'
# crtsys vcpkg registry contract

The current stable baseline is `{0}`.

    "baseline": "{0}"
'@ -f $initialBaseline
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText(
  (Join-Path $registryDirectory 'README.md'),
  $registryReadme,
  $utf8NoBom)
Invoke-Git @('-C', $registryDirectory, 'add', '--', 'README.md')
Invoke-Git @('-C', $registryDirectory, 'commit', '-m', 'Initialize test registry')

$commonArguments = @{
  ArchivePath = $archivePath
  SourcePortDirectory = $sourcePortDirectory
  RegistryDirectory = $registryDirectory
}
if (-not [string]::IsNullOrWhiteSpace($VcpkgExe)) {
  $commonArguments.VcpkgExe = $VcpkgExe
}

$firstPublish = & $publishScript @commonArguments -Version $currentVersion
if ($firstPublish.Version -ne $currentVersion) {
  throw "Initial registry publish returned '$($firstPublish.Version)'."
}

$nextPublish = & $publishScript @commonArguments -Version $nextVersion
if ($nextPublish.Version -ne $nextVersion) {
  throw "Next registry publish returned '$($nextPublish.Version)'."
}

$headBeforeRetry = (& git -C $registryDirectory rev-parse HEAD).Trim()
$retryPublish = & $publishScript @commonArguments -Version $nextVersion
$headAfterRetry = (& git -C $registryDirectory rev-parse HEAD).Trim()
if ($headBeforeRetry -ne $headAfterRetry) {
  throw 'Idempotent registry retry created an unexpected commit.'
}
if ($retryPublish.Baseline -ne $nextPublish.Baseline) {
  throw 'Idempotent registry retry changed the stable baseline.'
}

$rollbackRejected = $false
try {
  & $publishScript @commonArguments -Version $currentVersion | Out-Null
} catch {
  if ($_.Exception.Message -notmatch 'Refusing to republish non-current') {
    throw
  }
  $rollbackRejected = $true
}
if (-not $rollbackRejected) {
  throw 'Registry automation did not reject a baseline rollback.'
}

$baselinePath = Join-Path $registryDirectory 'versions\baseline.json'
$versionPath = Join-Path $registryDirectory 'versions\c-\crtsys.json'
$baseline = Get-Content -LiteralPath $baselinePath -Raw -Encoding UTF8 |
  ConvertFrom-Json
$versions = Get-Content -LiteralPath $versionPath -Raw -Encoding UTF8 |
  ConvertFrom-Json
if ($baseline.default.crtsys.baseline -ne $nextVersion) {
  throw "Expected registry baseline $nextVersion, got $($baseline.default.crtsys.baseline)."
}
if (@($versions.versions).Count -ne 2) {
  throw "Expected two registry versions, got $(@($versions.versions).Count)."
}

$status = @(& git -C $registryDirectory status --porcelain)
if ($status.Count -ne 0) {
  throw "Registry automation left a dirty worktree:`n$($status -join "`n")"
}

Write-Host "crtsys vcpkg registry automation passed: $currentVersion -> $nextVersion."
