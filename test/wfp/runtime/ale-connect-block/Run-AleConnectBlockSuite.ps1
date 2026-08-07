[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $PackageRoot,

  [ValidateRange(1, 1000)]
  [int] $Iterations = 20,

  [switch] $AllowDisposableGuestMutation,

  [string] $DisposableGuestSentinelPath =
      'C:\crtsys-disposable-test-guest.sentinel'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$guardScript = @(
  Join-Path $PSScriptRoot 'DisposableGuestGuard.ps1'
  Join-Path $PSScriptRoot '..\common\DisposableGuestGuard.ps1'
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $guardScript) {
  throw 'DisposableGuestGuard.ps1 was not found.'
}
. $guardScript
Assert-CrtSysDisposableGuest `
    -AllowDisposableGuestMutation:$AllowDisposableGuestMutation `
    -SentinelPath $DisposableGuestSentinelPath

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
$controller = Join-Path $PackageRoot (
    'crtsys_wfp_ale_connect_block_controller.exe')
$acceptance = Join-Path $PackageRoot (
    'crtsys_wfp_ale_connect_block_acceptance.exe')
$certificate = Join-Path $PackageRoot 'crtsys-test-signing.cer'
foreach ($path in @($driver, $controller, $acceptance, $certificate)) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Required WFP runtime artifact was not found: $path"
  }
}

function Invoke-AleAcceptance([string] $Mode = '') {
  $ipc = Join-Path ([IO.Path]::GetTempPath()) (
      'crtsys-wfp-ale-' + [Guid]::NewGuid().ToString('N'))
  $arguments = @($controller, $ipc)
  if (-not [string]::IsNullOrWhiteSpace($Mode)) {
    $arguments += $Mode
  }
  try {
    $output = @(& $acceptance @arguments 2>&1)
    return [pscustomobject]@{
      ExitCode = $LASTEXITCODE
      Output = $output
      Text = $output -join [Environment]::NewLine
    }
  } finally {
    Remove-Item -LiteralPath $ipc -Recurse -Force `
        -ErrorAction SilentlyContinue
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

    $lifecycle = Invoke-AleAcceptance '--persistent-lifecycle'
    $lifecycle.Output | ForEach-Object { Write-Host $_ }
    if ($lifecycle.ExitCode -ne 0 -or
        -not $lifecycle.Text.Contains(
            'NTL WFP persistent lifecycle ok:')) {
      throw (
        'WFP persistent policy reconcile/uninstall self-test failed.')
    }

    $arbitration = Invoke-AleAcceptance '--arbitration'
    $arbitration.Output | ForEach-Object { Write-Host $_ }
    if ($arbitration.ExitCode -ne 0 -or
        -not $arbitration.Text.Contains(
            'NTL WFP provider-arbitration ok:')) {
      throw 'WFP independent-provider arbitration self-test failed.'
    }

    $crashRecovery = Invoke-AleAcceptance '--crash-recovery'
    $crashRecovery.Output | ForEach-Object { Write-Host $_ }
    if ($crashRecovery.ExitCode -ne 0 -or
        -not $crashRecovery.Text.Contains(
            'NTL WFP policy-process crash recovery ok:')) {
      throw 'WFP policy-process crash recovery acceptance failed.'
    }

    for ($iteration = 0; $iteration -lt $Iterations; ++$iteration) {
      $iterationResult = Invoke-AleAcceptance
      $iterationResult.Output | ForEach-Object { Write-Host $_ }
      if ($iterationResult.ExitCode -ne 0) {
        throw (
          "WFP ALE connect-block iteration $iteration exited with " +
          "$($iterationResult.ExitCode).")
      }
      if (-not $iterationResult.Text.Contains('blocked_error=10013') -or
          -not $iterationResult.Text.Contains('restored_connect=success')) {
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
