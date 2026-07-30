[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $PackageRoot,

  [ValidateRange(1, 1000)]
  [int] $Iterations = 20,

  [ValidateRange(1024, 64000)]
  [int] $FirstPort = 38471
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Administrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  if (-not $principal.IsInRole(
      [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'The WFP runtime suite must run from an elevated PowerShell session.'
  }
}

function Remove-ServiceIfPresent([string] $Name) {
  & sc.exe query $Name *> $null
  if ($LASTEXITCODE -eq 0) {
    & sc.exe stop $Name *> $null
    & sc.exe delete $Name *> $null
  }
}

Assert-Administrator
$PackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
$driver =
    Join-Path $PackageRoot 'crtsys_wfp_ale_connect_block.sys'
$application =
    Join-Path $PackageRoot 'crtsys_wfp_ale_connect_block_app.exe'
$certificate = Join-Path $PackageRoot 'crtsys-test-signing.cer'
foreach ($path in @($driver, $application, $certificate)) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Required WFP runtime artifact was not found: $path"
  }
}

$service = 'CrtSysWfpAleConnectBlockAcceptance'
$certificateObject =
    [Security.Cryptography.X509Certificates.X509Certificate2]::new(
        $certificate)
try {
  $thumbprint = $certificateObject.Thumbprint
} finally {
  $certificateObject.Dispose()
}
$addedCertificates = [Collections.Generic.List[string]]::new()
try {
  foreach ($store in @('Root', 'TrustedPublisher')) {
    $storePath = "Cert:\LocalMachine\$store\$thumbprint"
    if (Test-Path -LiteralPath $storePath -PathType Leaf) {
      continue
    }
    & certutil.exe -f -addstore $store $certificate *> $null
    if ($LASTEXITCODE -ne 0) {
      throw "$store certificate import failed."
    }
    $addedCertificates.Add($storePath)
  }

  Remove-ServiceIfPresent $service
  try {
    & sc.exe create $service 'binPath=' $driver `
        'type=' 'kernel' 'start=' 'demand' |
        ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
      throw 'Creating the WFP ALE connect-block service failed.'
    }

    & sc.exe start $service | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
      throw 'Starting the WFP ALE connect-block service failed.'
    }

    for ($iteration = 0; $iteration -lt $Iterations; ++$iteration) {
      $port = $FirstPort + $iteration
      if ($port -gt 65535) {
        throw "Port range exceeded 65535 at iteration $iteration."
      }
      $output = @(& $application $port 2>&1)
      $exitCode = $LASTEXITCODE
      $output | ForEach-Object { Write-Host $_ }
      $text = $output -join [Environment]::NewLine
      if ($exitCode -ne 0) {
        throw (
          "WFP ALE connect-block iteration $iteration exited with " +
          "$exitCode.")
      }
      if (-not $text.Contains('blocked_error=10013') -or
          -not $text.Contains('restored_connect=success')) {
        throw (
          "WFP ALE connect-block iteration $iteration missed its " +
          'proof markers.')
      }
    }
  } finally {
    Remove-ServiceIfPresent $service
  }

  & sc.exe query $service *> $null
  if ($LASTEXITCODE -eq 0) {
    throw (
      'The WFP ALE connect-block service remained installed after ' +
      'the suite.')
  }

  Write-Host (
    "WFP ALE connect-block suite passed: $Iterations iterations.")
} finally {
  for ($index = $addedCertificates.Count - 1; $index -ge 0; --$index) {
    $storePath = $addedCertificates[$index]
    if (Test-Path -LiteralPath $storePath -PathType Leaf) {
      Remove-Item -LiteralPath $storePath -Force
    }
  }
}
