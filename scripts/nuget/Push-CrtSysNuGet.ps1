param(
  [string] $PackageDirectory,
  [string] $Source = 'https://api.nuget.org/v3/index.json',
  [string] $ApiKey = $env:NUGET_API_KEY,
  [switch] $SkipDuplicate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($PackageDirectory)) {
  $PackageDirectory = Join-Path $repoRoot 'artifacts\nuget'
}

if ([string]::IsNullOrWhiteSpace($ApiKey)) {
  throw 'NUGET_API_KEY is required to publish packages.'
}

$packages = @(Get-ChildItem -Path $PackageDirectory -Filter '*.nupkg' -File | Sort-Object FullName)
if ($packages.Count -eq 0) {
  throw "No .nupkg files were found in $PackageDirectory"
}

foreach ($package in $packages) {
  $arguments = @(
    'nuget', 'push', $package.FullName,
    '--source', $Source,
    '--api-key', $ApiKey,
    '--no-symbols'
  )

  if ($SkipDuplicate) {
    $arguments += '--skip-duplicate'
  }

  Write-Host "Publishing $($package.Name) to $Source"
  & dotnet @arguments
  if ($LASTEXITCODE -ne 0) {
    throw "dotnet nuget push failed with exit code $LASTEXITCODE."
  }
}
