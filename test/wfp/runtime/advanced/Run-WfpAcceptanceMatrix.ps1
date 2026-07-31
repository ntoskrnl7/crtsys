[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $MatrixPath,

  [Parameter(Mandatory)]
  [Security.SecureString] $VmPassword,

  [Parameter(Mandatory)]
  [Management.Automation.PSCredential] $GuestCredential,

  [string] $VmrunPath =
      'C:\Program Files\VMware\VMware Workstation\vmrun.exe',

  [string] $EvidenceRoot = '',

  [switch] $SkipBuild,

  [switch] $StopOnFailure
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$runner = Join-Path $PSScriptRoot 'Run-WfpAdvancedVmAcceptance.ps1'
$matrix = Get-Content -LiteralPath (
    Resolve-Path -LiteralPath $MatrixPath) -Raw | ConvertFrom-Json
if ([int] $matrix.version -ne 1) {
  throw "Unsupported WFP acceptance matrix version: $($matrix.version)"
}
$rows = @($matrix.rows)
if ($rows.Count -eq 0) {
  throw 'The WFP acceptance matrix contains no rows.'
}
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
  $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
  $EvidenceRoot =
      Join-Path $repoRoot "artifacts\wfp-acceptance-matrix\$stamp"
}
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$artifactsRoot =
    [IO.Path]::GetFullPath((Join-Path $repoRoot 'artifacts')).
        TrimEnd('\') + '\'
if (-not ($EvidenceRoot.TrimEnd('\') + '\').StartsWith(
    $artifactsRoot, [StringComparison]::OrdinalIgnoreCase)) {
  throw "EvidenceRoot must stay under $artifactsRoot"
}
New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null

function Read-Property(
  [object] $Object, [string] $Name, [object] $Default
) {
  $property = $Object.PSObject.Properties[$Name]
  if ($null -eq $property -or $null -eq $property.Value) {
    return $Default
  }
  return $property.Value
}

$seenNames = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
$results = [Collections.Generic.List[object]]::new()
foreach ($row in $rows) {
  $name = [string] (Read-Property $row 'name' '')
  if ($name -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$') {
    throw "Invalid matrix row name: $name"
  }
  if (-not $seenNames.Add($name)) {
    throw "Duplicate matrix row name: $name"
  }

  $architecture = [string] (
      Read-Property $row 'architecture' 'x64')
  if ($architecture -notin @('x64', 'ARM64')) {
    throw "Invalid architecture in matrix row '$name': $architecture"
  }
  $productType = [string] (
      Read-Property $row 'productType' 'Any')
  if ($productType -notin @('Any', 'Client', 'Server')) {
    throw "Invalid productType in matrix row '$name': $productType"
  }

  $rowRoot = Join-Path $EvidenceRoot $name
  $parameters = @{
    VmxPath = [string] (Read-Property $row 'vmxPath' '')
    VmPassword = $VmPassword
    GuestUser = $GuestCredential.UserName
    GuestPassword = $GuestCredential.Password
    RestartMode = [string] (
        Read-Property $row 'restartMode' 'Manual')
    ManualRestartTimeoutSeconds = [int] (
        Read-Property $row 'manualRestartTimeoutSeconds' 900)
    VmrunPath = $VmrunPath
    GuestRoot = [string] (
        Read-Property $row 'guestRoot' 'C:\crtsys-wfp-advanced')
    StagingRoot = Join-Path $rowRoot 'staging'
    PrebuiltRoot = [string] (
        Read-Property $row 'prebuiltRoot' '')
    LogRoot = Join-Path $rowRoot 'evidence'
    PlatformToolset = [string] (
        Read-Property $row 'platformToolset' 'v145')
    Architecture = $architecture
    WindowsSdkVersion = [string] (
        Read-Property $row 'windowsSdkVersion' '10.0.26100.0')
    RestoreDriverFileName = @(
      Read-Property $row 'restoreDriverFileName' @())
    RestoreBootMode = [string] (
        Read-Property $row 'restoreBootMode' 'Persistent')
    VerifierFlags = [string] (
        Read-Property $row 'verifierFlags' '0x209BB')
    RestoreVerifierFlags = [string] (
        Read-Property $row 'restoreVerifierFlags' 'standard')
    Iterations = [int] (Read-Property $row 'iterations' 20)
    LowResourceMode = [string] (
        Read-Property $row 'lowResourceMode' 'Systematic')
    LowResourceProbability = [int] (
        Read-Property $row 'lowResourceProbability' 10000)
    LowResourceRunsPerSample = [int] (
        Read-Property $row 'lowResourceRunsPerSample' 1)
    SystematicInjectionPassesPerSample = [int] (
        Read-Property $row 'systematicInjectionPassesPerSample' 4)
    SelectedWfpSample = @(
      Read-Property $row 'samples' @('all'))
    ExpectedProductType = $productType
    MinimumBuild = [int] (
        Read-Property $row 'minimumBuild' 0)
    RequireHvci = [bool] (
        Read-Property $row 'requireHvci' $false)
    SkipLowResourcePass = [bool] (
        Read-Property $row 'skipLowResourcePass' $false)
    SkipBuild = [bool] $SkipBuild
  }
  $managedHttp3Url = [string] (
      Read-Property $row 'managedHttp3Url' '')
  if (-not [string]::IsNullOrWhiteSpace($managedHttp3Url)) {
    $parameters.ManagedHttp3Url = [uri] $managedHttp3Url
  }

  $started = [DateTime]::UtcNow
  Write-Host "=== WFP matrix row: $name ==="
  try {
    & $runner @parameters
    $results.Add([pscustomobject]@{
      Name = $name
      Status = 'PASS'
      Architecture = $architecture
      ProductType = $productType
      StartedAtUtc = $started.ToString('o')
      CompletedAtUtc = [DateTime]::UtcNow.ToString('o')
      Evidence = $parameters.LogRoot
      Error = $null
    })
  } catch {
    $results.Add([pscustomobject]@{
      Name = $name
      Status = 'FAIL'
      Architecture = $architecture
      ProductType = $productType
      StartedAtUtc = $started.ToString('o')
      CompletedAtUtc = [DateTime]::UtcNow.ToString('o')
      Evidence = $parameters.LogRoot
      Error = $_.Exception.Message
    })
    if ($StopOnFailure) {
      break
    }
  }
}

$summary = [pscustomobject]@{
  SchemaVersion = 1
  CapturedAtUtc = [DateTime]::UtcNow.ToString('o')
  MatrixSource = [IO.Path]::GetFullPath($MatrixPath)
  Results = @($results)
}
$summaryPath = Join-Path $EvidenceRoot 'matrix-summary.json'
$summary | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $summaryPath -Encoding UTF8

$failures = @($results | Where-Object Status -ne 'PASS')
if ($results.Count -ne $rows.Count -or $failures.Count -ne 0) {
  throw (
      "WFP acceptance matrix failed. Evidence: $summaryPath")
}
Write-Host "WFP acceptance matrix passed: $summaryPath"
