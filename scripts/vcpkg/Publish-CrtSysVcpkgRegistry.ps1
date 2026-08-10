param(
  [Parameter(Mandatory = $true)]
  [ValidatePattern('^\d+\.\d+\.\d+$')]
  [string] $Version,

  [Parameter(Mandatory = $true)]
  [string] $ArchivePath,

  [Parameter(Mandatory = $true)]
  [string] $SourcePortDirectory,

  [Parameter(Mandatory = $true)]
  [string] $RegistryDirectory,

  [string] $RegistryBranch = 'vcpkg-registry',

  [string] $VcpkgExe,

  [string] $GitHubOutputPath,

  [switch] $Push
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$updatePortScript = Join-Path $PSScriptRoot 'Update-CrtSysVcpkgPort.ps1'
$ArchivePath = (Resolve-Path -LiteralPath $ArchivePath).Path
$SourcePortDirectory = (Resolve-Path -LiteralPath $SourcePortDirectory).Path
$RegistryDirectory = (Resolve-Path -LiteralPath $RegistryDirectory).Path

function Invoke-Git {
  param(
    [Parameter(Mandatory = $true)]
    [string[]] $Arguments,

    [switch] $Capture
  )

  Write-Host "[git] $($Arguments -join ' ')"
  if ($Capture) {
    $output = @(& git @Arguments)
    if ($LASTEXITCODE -ne 0) {
      throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE."
    }
    return ($output -join "`n").Trim()
  }

  & git @Arguments | Out-Host
  if ($LASTEXITCODE -ne 0) {
    throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE."
  }
}

function Resolve-VcpkgExecutable {
  param([string] $RequestedPath)

  if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
    return (Resolve-Path -LiteralPath $RequestedPath).Path
  }

  $command = Get-Command vcpkg -ErrorAction SilentlyContinue
  if ($command) {
    return $command.Source
  }

  $vswhere = Join-Path ${env:ProgramFiles(x86)} `
    'Microsoft Visual Studio\Installer\vswhere.exe'
  if (Test-Path -LiteralPath $vswhere) {
    $installations = @(
      (& $vswhere -all -products * -format json | ConvertFrom-Json) |
        Sort-Object { [version]$_.installationVersion } -Descending
    )
    foreach ($installation in $installations) {
      $candidate = Join-Path $installation.installationPath 'VC\vcpkg\vcpkg.exe'
      if (Test-Path -LiteralPath $candidate) {
        return $candidate
      }
    }
  }

  throw 'vcpkg.exe was not found. Pass -VcpkgExe or add vcpkg to PATH.'
}

function Test-WorkingTreeChanges {
  param([switch] $Cached)

  $arguments = @('-C', $RegistryDirectory, 'diff', '--quiet', '--exit-code')
  if ($Cached) {
    $arguments += '--cached'
  }
  & git @arguments
  if ($LASTEXITCODE -eq 0) {
    return $false
  }
  if ($LASTEXITCODE -eq 1) {
    return $true
  }
  throw "git diff failed with exit code $LASTEXITCODE."
}

function Get-PublishedVersionEntry {
  $versionFilePath = Join-Path $RegistryDirectory 'versions\c-\crtsys.json'
  if (-not (Test-Path -LiteralPath $versionFilePath)) {
    return $null
  }

  $versionFile = Get-Content `
    -LiteralPath $versionFilePath `
    -Raw `
    -Encoding UTF8 |
      ConvertFrom-Json
  return @($versionFile.versions) |
    Where-Object { $_.'version-semver' -eq $Version } |
    Select-Object -First 1
}

function Get-CurrentBaselineVersion {
  $baselinePath = Join-Path $RegistryDirectory 'versions\baseline.json'
  if (-not (Test-Path -LiteralPath $baselinePath)) {
    return $null
  }

  $baseline = Get-Content `
    -LiteralPath $baselinePath `
    -Raw `
    -Encoding UTF8 |
      ConvertFrom-Json
  if ($null -eq $baseline.default.crtsys) {
    return $null
  }
  return [string]$baseline.default.crtsys.baseline
}

function Set-RegistryReadmeBaseline {
  param([Parameter(Mandatory = $true)][string] $Baseline)

  $readmePath = Join-Path $RegistryDirectory 'README.md'
  if (-not (Test-Path -LiteralPath $readmePath)) {
    throw "Registry README was not found: $readmePath"
  }

  $content = Get-Content -LiteralPath $readmePath -Raw -Encoding UTF8
  $matches = @([regex]::Matches($content, '(?i)[0-9a-f]{40}'))
  $existingValues = @(
    $matches |
      ForEach-Object { $_.Value.ToLowerInvariant() } |
      Sort-Object -Unique
  )
  if ($matches.Count -eq 0 -or $existingValues.Count -ne 1) {
    throw 'Expected one unambiguous stable baseline in the registry README.'
  }

  $content = [regex]::Replace(
    $content,
    '(?i)[0-9a-f]{40}',
    $Baseline.ToLowerInvariant())
  $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
  [System.IO.File]::WriteAllText($readmePath, $content, $utf8NoBom)
}

function Write-PublishOutput {
  param(
    [Parameter(Mandatory = $true)][string] $Baseline,
    [Parameter(Mandatory = $true)][string] $ArchiveSha512
  )

  if (-not [string]::IsNullOrWhiteSpace($GitHubOutputPath)) {
    Add-Content -LiteralPath $GitHubOutputPath -Encoding UTF8 -Value @(
      "baseline=$Baseline",
      "archive_sha512=$ArchiveSha512",
      "version=$Version"
    )
  }

  [pscustomobject]@{
    Version = $Version
    Baseline = $Baseline
    ArchiveSha512 = $ArchiveSha512
  }
}

$repositoryRoot = Invoke-Git `
  -Arguments @('-C', $RegistryDirectory, 'rev-parse', '--show-toplevel') `
  -Capture
if ([System.IO.Path]::GetFullPath($repositoryRoot) -ne $RegistryDirectory) {
  throw "RegistryDirectory must be the Git worktree root: $RegistryDirectory"
}

$status = Invoke-Git `
  -Arguments @('-C', $RegistryDirectory, 'status', '--porcelain') `
  -Capture
if (-not [string]::IsNullOrWhiteSpace($status)) {
  throw "The registry worktree must be clean before publishing.`n$status"
}

$currentBaselineVersion = Get-CurrentBaselineVersion
$publishedEntry = Get-PublishedVersionEntry
if ($null -ne $publishedEntry -and $currentBaselineVersion -ne $Version) {
  throw "Refusing to republish non-current crtsys $Version; registry baseline is $currentBaselineVersion."
}
if ($null -eq $publishedEntry -and
    -not [string]::IsNullOrWhiteSpace($currentBaselineVersion) -and
    [version]$Version -le [version]$currentBaselineVersion) {
  throw "New registry version $Version must be greater than $currentBaselineVersion."
}

$destinationPortDirectory = Join-Path $RegistryDirectory 'ports\crtsys'
$registryPrefix = $RegistryDirectory.TrimEnd('\') + '\'
$resolvedDestination = [System.IO.Path]::GetFullPath($destinationPortDirectory)
if (-not $resolvedDestination.StartsWith(
    $registryPrefix,
    [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "Resolved port destination escaped the registry: $resolvedDestination"
}

if (Test-Path -LiteralPath $destinationPortDirectory) {
  Remove-Item -LiteralPath $destinationPortDirectory -Recurse -Force
}
New-Item -ItemType Directory -Force -Path (Split-Path $destinationPortDirectory) |
  Out-Null
Copy-Item -LiteralPath $SourcePortDirectory `
  -Destination $destinationPortDirectory `
  -Recurse

$updateResult = & $updatePortScript `
  -Version $Version `
  -ArchivePath $ArchivePath `
  -PortDirectory $destinationPortDirectory
$archiveSha512 = [string]$updateResult.ArchiveSha512

$VcpkgExe = Resolve-VcpkgExecutable -RequestedPath $VcpkgExe
& $VcpkgExe format-manifest `
  (Join-Path $destinationPortDirectory 'vcpkg.json') |
    Out-Host
if ($LASTEXITCODE -ne 0) {
  throw "vcpkg format-manifest failed with exit code $LASTEXITCODE."
}

if ($null -ne $publishedEntry) {
  if (Test-WorkingTreeChanges) {
    throw "crtsys $Version is already published with different port contents. Publish a new port-version instead of rewriting history."
  }

  $currentPortTree = Invoke-Git `
    -Arguments @('-C', $RegistryDirectory, 'rev-parse', 'HEAD:ports/crtsys') `
    -Capture
  if ($currentPortTree -ne [string]$publishedEntry.'git-tree') {
    throw "Published crtsys $Version points to $($publishedEntry.'git-tree'), but the registry port tree is $currentPortTree."
  }

  $stableBaseline = Invoke-Git `
    -Arguments @('-C', $RegistryDirectory, 'log', '-1', '--format=%H', '--', 'versions/baseline.json') `
    -Capture
} else {
  Invoke-Git -Arguments @('-C', $RegistryDirectory, 'add', '--', 'ports/crtsys')
  if (Test-WorkingTreeChanges -Cached) {
    Invoke-Git -Arguments @(
      '-C', $RegistryDirectory, 'commit', '-m', "Add crtsys $Version port")
  }

  Push-Location $RegistryDirectory
  try {
    & $VcpkgExe x-add-version `
      '--x-builtin-ports-root=./ports' `
      '--x-builtin-registry-versions-dir=./versions' `
      crtsys |
        Out-Host
    if ($LASTEXITCODE -ne 0) {
      throw "vcpkg x-add-version failed with exit code $LASTEXITCODE."
    }
  } finally {
    Pop-Location
  }

  Invoke-Git -Arguments @('-C', $RegistryDirectory, 'add', '--', 'versions')
  if (-not (Test-WorkingTreeChanges -Cached)) {
    throw 'vcpkg x-add-version did not update the registry versions database.'
  }
  Invoke-Git -Arguments @(
    '-C', $RegistryDirectory, 'commit', '-m', "Add crtsys $Version registry version")
  $stableBaseline = Invoke-Git `
    -Arguments @('-C', $RegistryDirectory, 'rev-parse', 'HEAD') `
    -Capture
}

Set-RegistryReadmeBaseline -Baseline $stableBaseline
Invoke-Git -Arguments @('-C', $RegistryDirectory, 'add', '--', 'README.md')
if (Test-WorkingTreeChanges -Cached) {
  Invoke-Git -Arguments @(
    '-C', $RegistryDirectory, 'commit', '-m', 'Document the stable registry baseline')
}

if ($Push) {
  Invoke-Git -Arguments @(
    '-C', $RegistryDirectory, 'push', 'origin', "HEAD:refs/heads/$RegistryBranch")
}

Write-Host "Published crtsys $Version with stable registry baseline $stableBaseline."
Write-PublishOutput -Baseline $stableBaseline -ArchiveSha512 $archiveSha512
