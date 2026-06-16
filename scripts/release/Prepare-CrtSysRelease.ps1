param(
  [Parameter(Mandatory = $true)]
  [ValidatePattern('^\d+\.\d+\.\d+$')]
  [string] $Version,

  [string] $Branch = 'main',

  [switch] $Push,

  [switch] $AllowNonMainBranch
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$versionHeader = Join-Path $repoRoot 'include\.internal\version'
$tagName = "v$Version"

function Invoke-Git {
  param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Arguments
  )

  & git @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE."
  }
}

function Test-GitRefExists {
  param([string] $Ref)

  & git rev-parse --verify --quiet $Ref *> $null
  return $LASTEXITCODE -eq 0
}

function Ensure-TrimmedString {
  param(
    [Parameter(Mandatory = $true)]
    [AllowNull()]
    [object] $Value,

    [Parameter(Mandatory = $true)]
    [string] $Name
  )

  if ($null -eq $Value) {
    throw "$Name was empty." 
  }

  if ($Value -is [string]) {
    return $Value.Trim()
  }

  return "${Value}".Trim()
}

Push-Location $repoRoot
try {
  Invoke-Git rev-parse --show-toplevel *> $null

  $currentBranch = Ensure-TrimmedString -Value (& git branch --show-current) -Name 'Current branch'
  if (-not $AllowNonMainBranch -and $currentBranch -ne $Branch) {
    throw "Release preparation must run on '$Branch'. Current branch is '$currentBranch'."
  }

  Invoke-Git fetch origin $Branch --tags
  if (-not $AllowNonMainBranch) {
    $localHead = Ensure-TrimmedString -Value (& git rev-parse HEAD) -Name 'Local HEAD'
    $remoteHead = Ensure-TrimmedString -Value (& git rev-parse "origin/$Branch") -Name 'Remote HEAD'
    if ($localHead -ne $remoteHead) {
      throw "Local '$Branch' must match origin/$Branch before preparing a release."
    }
  }

  $status = & git status --porcelain
  if ($null -ne $status -and -not [string]::IsNullOrWhiteSpace(($status -join "`n"))) {
    throw "Working tree must be clean before preparing a release."
  }

  if (Test-GitRefExists "refs/tags/$tagName") {
    throw "Local tag already exists: $tagName"
  }

  & git ls-remote --exit-code --tags origin $tagName *> $null
  if ($LASTEXITCODE -eq 0) {
    throw "Remote tag already exists on origin: $tagName"
  }
  if ($LASTEXITCODE -ne 2) {
    throw "Could not check whether remote tag exists on origin: $tagName"
  }

  $parts = $Version.Split('.')
  $major = $parts[0]
  $minor = $parts[1]
  $patch = $parts[2]

  if (-not (Test-Path $versionHeader)) {
    throw "Version header was not found: $versionHeader"
  }

  $currentVersion = Ensure-TrimmedString -Value (& (Join-Path $repoRoot 'scripts\nuget\Get-CrtSysVersion.ps1')) -Name 'Current version'
  if ($currentVersion -eq $Version) {
    throw "Version header is already $Version."
  }

  $content = Get-Content -LiteralPath $versionHeader -Raw
  $content = [regex]::Replace(
    $content,
    '(?m)^(#define\s+CRTSYS_VERSION_MAJOR\s+)\d+(\s*)$',
    { param($match) "$($match.Groups[1].Value)$major$($match.Groups[2].Value)" })
  $content = [regex]::Replace(
    $content,
    '(?m)^(#define\s+CRTSYS_VERSION_MINOR\s+)\d+(\s*)$',
    { param($match) "$($match.Groups[1].Value)$minor$($match.Groups[2].Value)" })
  $content = [regex]::Replace(
    $content,
    '(?m)^(#define\s+CRTSYS_VERSION_PATCH\s+)\d+(\s*)$',
    { param($match) "$($match.Groups[1].Value)$patch$($match.Groups[2].Value)" })

  $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
  [System.IO.File]::WriteAllText($versionHeader, $content, $utf8NoBom)

  $resolvedVersion = Ensure-TrimmedString -Value (& (Join-Path $repoRoot 'scripts\nuget\Get-CrtSysVersion.ps1')) -Name 'Resolved version'
  if ($resolvedVersion -ne $Version) {
    throw "Version header update failed. Expected $Version, got $resolvedVersion."
  }

  Invoke-Git diff --check
  Invoke-Git add -- include/.internal/version
  Invoke-Git commit -m "release crtsys $Version"
  Invoke-Git tag -a $tagName -m "crtsys $Version"

  if ($Push) {
    Invoke-Git push origin $currentBranch
    Invoke-Git push origin $tagName
    Write-Host "Pushed $currentBranch and $tagName. The Package workflow should start from the tag."
  } else {
    Write-Host "Prepared release commit and tag locally."
    Write-Host "Push with:"
    Write-Host "  git push origin $currentBranch"
    Write-Host "  git push origin $tagName"
  }
} catch {
  Write-Error $_
  exit 1
} finally {
  Pop-Location
}
