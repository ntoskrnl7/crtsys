param(
  [Parameter(Mandatory = $true)]
  [ValidatePattern('^\d+\.\d+\.\d+$')]
  [string] $Version,

  [Parameter(Mandatory = $true)]
  [string] $ReleaseArchivePath,

  [Parameter(Mandatory = $true)]
  [string] $VcpkgRepositoryDirectory,

  [string] $SourcePortDirectory,

  [string] $VcpkgExe,

  [switch] $AllowNewPort
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$updatePortScript = Join-Path $PSScriptRoot 'Update-CrtSysVcpkgPort.ps1'
if ([string]::IsNullOrWhiteSpace($SourcePortDirectory)) {
  $SourcePortDirectory = Join-Path $repoRoot 'vcpkg\ports\crtsys'
}

$ReleaseArchivePath = (Resolve-Path -LiteralPath $ReleaseArchivePath).Path
$VcpkgRepositoryDirectory = (
  Resolve-Path -LiteralPath $VcpkgRepositoryDirectory
).Path
$SourcePortDirectory = (Resolve-Path -LiteralPath $SourcePortDirectory).Path

foreach ($requiredPath in @(
  $ReleaseArchivePath,
  $VcpkgRepositoryDirectory,
  $SourcePortDirectory,
  $updatePortScript,
  (Join-Path $VcpkgRepositoryDirectory '.git'),
  (Join-Path $VcpkgRepositoryDirectory 'ports'),
  (Join-Path $VcpkgRepositoryDirectory 'versions\baseline.json'),
  (Join-Path $SourcePortDirectory 'vcpkg.json'),
  (Join-Path $SourcePortDirectory 'portfile.cmake'),
  (Join-Path $SourcePortDirectory 'ldk-copyright')
)) {
  if (-not (Test-Path -LiteralPath $requiredPath)) {
    throw "Required official vcpkg update input was not found: $requiredPath"
  }
}

if ([string]::IsNullOrWhiteSpace($VcpkgExe)) {
  $VcpkgExe = Join-Path $VcpkgRepositoryDirectory 'vcpkg.exe'
}
$VcpkgExe = (Resolve-Path -LiteralPath $VcpkgExe).Path

$targetPortDirectory = Join-Path $VcpkgRepositoryDirectory 'ports\crtsys'
$portsRootPrefix = (
  (Resolve-Path (Join-Path $VcpkgRepositoryDirectory 'ports')).Path.TrimEnd('\') +
  '\'
)
$targetPortFullPath = [System.IO.Path]::GetFullPath($targetPortDirectory)
if (-not $targetPortFullPath.StartsWith(
    $portsRootPrefix,
    [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "Refusing to update a port outside the official ports directory: $targetPortFullPath"
}

$existingManifestPath = Join-Path $targetPortFullPath 'vcpkg.json'
$existingVersion = $null
if (Test-Path -LiteralPath $existingManifestPath) {
  $existingManifest = Get-Content -LiteralPath $existingManifestPath -Raw |
    ConvertFrom-Json
  $existingVersion = [string]$existingManifest.'version-semver'
  if ([string]::IsNullOrWhiteSpace($existingVersion)) {
    throw "The existing official crtsys port has no version-semver: $existingManifestPath"
  }
  if ([version]$Version -le [version]$existingVersion) {
    throw "Official crtsys $existingVersion is not older than requested $Version."
  }
} elseif (-not $AllowNewPort) {
  throw @"
The official crtsys port is not present in microsoft/vcpkg yet. Wait for the
initial port pull request to merge, or use -AllowNewPort for validation only.
"@
}

if (Test-Path -LiteralPath $targetPortFullPath) {
  Remove-Item -LiteralPath $targetPortFullPath -Recurse -Force
}
Copy-Item -LiteralPath $SourcePortDirectory `
  -Destination $targetPortFullPath `
  -Recurse

& $updatePortScript `
  -Version $Version `
  -ArchivePath $ReleaseArchivePath `
  -PortDirectory $targetPortFullPath | Out-Host

$manifestPath = Join-Path $targetPortFullPath 'vcpkg.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.PSObject.Properties.Name -contains 'port-version') {
  $manifest.PSObject.Properties.Remove('port-version')
  $manifest | ConvertTo-Json -Depth 20 |
    Set-Content -LiteralPath $manifestPath -Encoding UTF8
}

& $VcpkgExe format-manifest $manifestPath
if ($LASTEXITCODE -ne 0) {
  throw "vcpkg format-manifest failed with exit code $LASTEXITCODE."
}

# microsoft/vcpkg stores port files with LF endings even on Windows. Normalize
# the copied upstream template before calculating its versions DB git tree.
$utf8WithoutBom = [System.Text.UTF8Encoding]::new($false)
Get-ChildItem -LiteralPath $targetPortFullPath -Recurse -File |
  ForEach-Object {
    $content = [System.IO.File]::ReadAllText($_.FullName)
    $content = $content.Replace("`r`n", "`n").Replace("`r", "`n")
    [System.IO.File]::WriteAllText(
      $_.FullName,
      $content,
      $utf8WithoutBom
    )
  }

Push-Location $VcpkgRepositoryDirectory
try {
  & $VcpkgExe x-add-version crtsys
  if ($LASTEXITCODE -ne 0) {
    throw "vcpkg x-add-version crtsys failed with exit code $LASTEXITCODE."
  }

  & $VcpkgExe x-add-version --all
  if ($LASTEXITCODE -ne 0) {
    throw "vcpkg x-add-version --all failed with exit code $LASTEXITCODE."
  }

  & $VcpkgExe x-ci-verify-versions
  if ($LASTEXITCODE -ne 0) {
    throw "vcpkg x-ci-verify-versions failed with exit code $LASTEXITCODE."
  }
} finally {
  Pop-Location
}

$versionDatabasePath = Join-Path `
  $VcpkgRepositoryDirectory `
  'versions\c-\crtsys.json'
$versionDatabase = Get-Content -LiteralPath $versionDatabasePath -Raw |
  ConvertFrom-Json
$versionEntries = @(
  @($versionDatabase.versions) |
    Where-Object { $_.'version-semver' -eq $Version }
)
if ($versionEntries.Count -ne 1) {
  throw "Expected one official versions DB entry for $Version, found $($versionEntries.Count)."
}

$updatedManifest = Get-Content -LiteralPath $manifestPath -Raw |
  ConvertFrom-Json
$archiveSha512 = (
  Get-FileHash -LiteralPath $ReleaseArchivePath -Algorithm SHA512
).Hash.ToLowerInvariant()

[pscustomobject]@{
  Version = $Version
  PreviousVersion = $existingVersion
  ArchiveSha512 = $archiveSha512
  GitTree = [string]$versionEntries[0].'git-tree'
  PortDirectory = $targetPortFullPath
  ManifestLicense = [string]$updatedManifest.license
}
