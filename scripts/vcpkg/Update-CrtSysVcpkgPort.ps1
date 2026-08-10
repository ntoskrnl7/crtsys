param(
  [Parameter(Mandatory = $true)]
  [ValidatePattern('^\d+\.\d+\.\d+$')]
  [string] $Version,

  [string] $ArchivePath,

  [string] $PortDirectory,

  [string] $DocumentationRoot,

  [ValidatePattern('^[0-9a-fA-F]{40}$')]
  [string] $RegistryBaseline
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($PortDirectory)) {
  $PortDirectory = Join-Path $repoRoot 'vcpkg\ports\crtsys'
}
$PortDirectory = [System.IO.Path]::GetFullPath($PortDirectory)

$manifestPath = Join-Path $PortDirectory 'vcpkg.json'
$portfilePath = Join-Path $PortDirectory 'portfile.cmake'
foreach ($requiredPath in @($manifestPath, $portfilePath)) {
  if (-not (Test-Path -LiteralPath $requiredPath)) {
    throw "Required crtsys port file was not found: $requiredPath"
  }
}

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Set-Utf8Content {
  param(
    [Parameter(Mandatory = $true)]
    [string] $Path,

    [Parameter(Mandatory = $true)]
    [string] $Content
  )

  [System.IO.File]::WriteAllText($Path, $Content, $utf8NoBom)
}

function Replace-RequiredPattern {
  param(
    [Parameter(Mandatory = $true)]
    [string] $Content,

    [Parameter(Mandatory = $true)]
    [string] $Pattern,

    [Parameter(Mandatory = $true)]
    [System.Text.RegularExpressions.MatchEvaluator] $Evaluator,

    [Parameter(Mandatory = $true)]
    [string] $Description
  )

  $regex = [regex]::new($Pattern)
  $matches = @($regex.Matches($Content))
  if ($matches.Count -ne 1) {
    throw "Expected exactly one $Description, found $($matches.Count)."
  }

  return $regex.Replace($Content, $Evaluator, 1)
}

$manifestContent = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8
$manifestContent = Replace-RequiredPattern `
  -Content $manifestContent `
  -Pattern '"version-semver"\s*:\s*"[^"]+"' `
  -Evaluator { param($match) '"version-semver": "' + $Version + '"' } `
  -Description 'version-semver field in the crtsys vcpkg manifest'
Set-Utf8Content -Path $manifestPath -Content $manifestContent

$archiveSha512 = $null
if (-not [string]::IsNullOrWhiteSpace($ArchivePath)) {
  $ArchivePath = (Resolve-Path -LiteralPath $ArchivePath).Path
  $archiveSha512 = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA512).Hash.ToLowerInvariant()

  $portfileContent = Get-Content -LiteralPath $portfilePath -Raw -Encoding UTF8
  $portfileContent = Replace-RequiredPattern `
    -Content $portfileContent `
    -Pattern '(?ms)(SHA512\s+)([0-9a-fA-F]{128})(\s*\))' `
    -Evaluator {
      param($match)
      return $match.Groups[1].Value + $archiveSha512 + $match.Groups[3].Value
    } `
    -Description 'SHA512 field in the crtsys portfile'
  Set-Utf8Content -Path $portfilePath -Content $portfileContent
}

if (-not [string]::IsNullOrWhiteSpace($RegistryBaseline)) {
  if ([string]::IsNullOrWhiteSpace($DocumentationRoot)) {
    throw 'DocumentationRoot is required when RegistryBaseline is provided.'
  }

  $DocumentationRoot = [System.IO.Path]::GetFullPath($DocumentationRoot)
  $documentationPaths = @(
    (Join-Path $DocumentationRoot 'README.md'),
    (Join-Path $DocumentationRoot 'README.ko-KR.md'),
    (Join-Path $DocumentationRoot 'vcpkg\README.md'),
    (Join-Path $DocumentationRoot 'vcpkg\README.ko-KR.md')
  )

  foreach ($documentationPath in $documentationPaths) {
    if (-not (Test-Path -LiteralPath $documentationPath)) {
      throw "Required vcpkg documentation was not found: $documentationPath"
    }

    $documentationContent = Get-Content `
      -LiteralPath $documentationPath `
      -Raw `
      -Encoding UTF8
    $baselineMatches = @([regex]::Matches(
        $documentationContent,
        '(?i)[0-9a-f]{40}'))
    $existingBaselines = @(
      $baselineMatches |
        ForEach-Object { $_.Value.ToLowerInvariant() } |
        Sort-Object -Unique
    )
    if ($baselineMatches.Count -eq 0 -or $existingBaselines.Count -ne 1) {
      throw "Expected one unambiguous registry baseline in $documentationPath."
    }

    $documentationContent = [regex]::Replace(
      $documentationContent,
      '(?i)[0-9a-f]{40}',
      $RegistryBaseline.ToLowerInvariant())
    Set-Utf8Content -Path $documentationPath -Content $documentationContent
  }
}

Write-Host "Updated crtsys vcpkg port to version $Version at $PortDirectory."
if ($null -ne $archiveSha512) {
  Write-Host "Release archive SHA-512: $archiveSha512"
}
if (-not [string]::IsNullOrWhiteSpace($RegistryBaseline)) {
  Write-Host "Updated documented registry baseline to $($RegistryBaseline.ToLowerInvariant())."
}

[pscustomobject]@{
  Version = $Version
  ArchiveSha512 = $archiveSha512
  RegistryBaseline = if ([string]::IsNullOrWhiteSpace($RegistryBaseline)) {
    $null
  } else {
    $RegistryBaseline.ToLowerInvariant()
  }
}
