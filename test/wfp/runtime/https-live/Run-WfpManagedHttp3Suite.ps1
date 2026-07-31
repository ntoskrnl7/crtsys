[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $PackageRoot,

  [Parameter(Mandatory)]
  [uri] $Url
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Administrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  if (-not $principal.IsInRole(
      [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'The WFP managed HTTP/3 suite requires an elevated PowerShell.'
  }
}

function Remove-DriverService {
  & sc.exe query crtsys_wfp_browser_https_inspection *> $null
  if ($LASTEXITCODE -eq 0) {
    & sc.exe stop crtsys_wfp_browser_https_inspection *> $null
    & sc.exe delete crtsys_wfp_browser_https_inspection *> $null
  }
}

function Wait-DriverServiceRemoved {
  $deadline = (Get-Date).AddSeconds(30)
  do {
    & sc.exe query crtsys_wfp_browser_https_inspection *> $null
    if ($LASTEXITCODE -ne 0) {
      return
    }
    Start-Sleep -Milliseconds 100
  } while ((Get-Date) -lt $deadline)
  throw 'The browser inspection driver service remained after the suite.'
}

Assert-Administrator
$PackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
$driver = Join-Path $PackageRoot (
    'crtsys_wfp_browser_https_inspection.sys')
$certificate = Join-Path $PackageRoot (
    'crtsys_wfp_browser_https_inspection.cer')
$testScript = Join-Path $PackageRoot (
    'Start-WfpManagedHttp3Inspection.ps1')
foreach ($path in @($driver, $certificate, $testScript)) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Required WFP managed HTTP/3 artifact is missing: $path"
  }
}

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
      throw (
        "Importing the browser inspection driver certificate into " +
        "$store failed.")
    }
    $addedCertificates.Add($storePath)
  }

  Remove-DriverService
  try {
    & sc.exe create crtsys_wfp_browser_https_inspection `
        'binPath=' $driver 'type=' 'kernel' 'start=' 'demand' |
        ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
      throw 'Creating the browser inspection driver service failed.'
    }
    & sc.exe start crtsys_wfp_browser_https_inspection |
        ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
      throw 'Starting the browser inspection driver service failed.'
    }

    & $testScript -PackageRoot $PackageRoot -Url $Url
    if ($LASTEXITCODE -ne 0) {
      throw 'The WFP managed HTTP/3 redirect test failed.'
    }
  } finally {
    Remove-DriverService
    Wait-DriverServiceRemoved
  }

  Write-Host 'WFP managed HTTP/3 driver suite passed.'
} finally {
  for ($index = $addedCertificates.Count - 1; $index -ge 0; --$index) {
    $storePath = $addedCertificates[$index]
    if (Test-Path -LiteralPath $storePath -PathType Leaf) {
      Remove-Item -LiteralPath $storePath -Force
    }
  }
}
