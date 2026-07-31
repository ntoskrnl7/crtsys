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

function Wait-ProcessFileMarker(
  [Diagnostics.Process] $Process,
  [string] $OutputPath,
  [string] $ErrorPath,
  [string] $Marker,
  [int] $TimeoutSeconds = 30
) {
  $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  do {
    $output = Get-Content -LiteralPath $OutputPath -Raw `
        -ErrorAction SilentlyContinue
    if ($output -and $output.Contains($Marker)) {
      return
    }
    if ($Process.HasExited) {
      $Process.WaitForExit()
      $errorText = Get-Content -LiteralPath $ErrorPath -Raw `
          -ErrorAction SilentlyContinue
      throw (
        "Process exited before '$Marker'. stdout='$output' " +
        "stderr='$errorText'")
    }
    Start-Sleep -Milliseconds 100
  } while ((Get-Date) -lt $deadline)

  $output = Get-Content -LiteralPath $OutputPath -Raw `
      -ErrorAction SilentlyContinue
  $errorText = Get-Content -LiteralPath $ErrorPath -Raw `
      -ErrorAction SilentlyContinue
  throw (
    "Timed out waiting for '$Marker'. stdout='$output' " +
    "stderr='$errorText'")
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

    $lifecyclePort = $FirstPort + $Iterations
    if ($lifecyclePort -gt 65535) {
      throw 'Persistent lifecycle self-test port exceeds 65535.'
    }
    $lifecycleOutput = @(
      & $application --persistent-lifecycle-self-test $lifecyclePort 2>&1)
    $lifecycleExitCode = $LASTEXITCODE
    $lifecycleOutput | ForEach-Object { Write-Host $_ }
    $lifecycleText =
        $lifecycleOutput -join [Environment]::NewLine
    if ($lifecycleExitCode -ne 0 -or
        -not $lifecycleText.Contains(
            'NTL WFP persistent lifecycle ok:')) {
      throw (
        'WFP persistent policy reconcile/uninstall self-test failed.')
    }

    $arbitrationPort = $FirstPort + $Iterations + 1
    if ($arbitrationPort -gt 65535) {
      throw 'Provider arbitration self-test port exceeds 65535.'
    }
    $arbitrationOutput = @(
      & $application --arbitration-self-test $arbitrationPort 2>&1)
    $arbitrationExitCode = $LASTEXITCODE
    $arbitrationOutput | ForEach-Object { Write-Host $_ }
    $arbitrationText =
        $arbitrationOutput -join [Environment]::NewLine
    if ($arbitrationExitCode -ne 0 -or
        -not $arbitrationText.Contains(
            'NTL WFP provider-arbitration ok:')) {
      throw 'WFP independent-provider arbitration self-test failed.'
    }

    $crashRecoveryPort = $FirstPort + $Iterations + 2
    if ($crashRecoveryPort -gt 65535) {
      throw 'Policy-process crash recovery port exceeds 65535.'
    }
    $runId = [Guid]::NewGuid().ToString('N')
    $listenerOutput =
        Join-Path ([IO.Path]::GetTempPath()) "ntl-wfp-listener-$runId.log"
    $listenerError =
        Join-Path ([IO.Path]::GetTempPath()) "ntl-wfp-listener-$runId.err"
    $policyOutput =
        Join-Path ([IO.Path]::GetTempPath()) "ntl-wfp-policy-$runId.log"
    $policyError =
        Join-Path ([IO.Path]::GetTempPath()) "ntl-wfp-policy-$runId.err"
    $listenerProcess = $null
    $policyProcess = $null
    try {
      $listenerProcess = Start-Process -FilePath $application `
          -ArgumentList @('--listener', $crashRecoveryPort) `
          -RedirectStandardOutput $listenerOutput `
          -RedirectStandardError $listenerError `
          -WindowStyle Hidden -PassThru
      Wait-ProcessFileMarker $listenerProcess $listenerOutput $listenerError (
          'NTL WFP crash-recovery listener ready:')

      $policyProcess = Start-Process -FilePath $application `
          -ArgumentList @('--hold-policy', $crashRecoveryPort) `
          -RedirectStandardOutput $policyOutput `
          -RedirectStandardError $policyError `
          -WindowStyle Hidden -PassThru
      Wait-ProcessFileMarker $policyProcess $policyOutput $policyError (
          'NTL WFP crash-recovery policy ready:')

      $blockedProbe = @(
        & $application --probe $crashRecoveryPort 2>&1)
      $blockedExit = $LASTEXITCODE
      $blockedProbe | ForEach-Object { Write-Host $_ }
      if ($blockedExit -ne 2 -or
          -not (($blockedProbe -join "`n").Contains('error=10013'))) {
        throw 'Held dynamic policy did not block the crash-recovery probe.'
      }

      Stop-Process -Id $policyProcess.Id -Force
      $policyProcess.WaitForExit()
      $policyProcess.Dispose()
      $policyProcess = $null

      $recovered = $false
      for ($attempt = 0; $attempt -ne 100; ++$attempt) {
        $recoveryProbe = @(
          & $application --probe $crashRecoveryPort 2>&1)
        if ($LASTEXITCODE -eq 0) {
          $recovered = $true
          $recoveryProbe | ForEach-Object { Write-Host $_ }
          break
        }
        Start-Sleep -Milliseconds 50
      }
      if (-not $recovered) {
        throw 'Dynamic WFP policy did not disappear after process kill.'
      }
      Write-Host (
        'NTL WFP policy-process crash recovery ok: ' +
        'held=blocked, killed=cleanup, restarted-probe=permitted')
    } finally {
      if ($policyProcess -and -not $policyProcess.HasExited) {
        Stop-Process -Id $policyProcess.Id -Force -ErrorAction SilentlyContinue
      }
      if ($listenerProcess -and -not $listenerProcess.HasExited) {
        Stop-Process -Id $listenerProcess.Id -Force `
            -ErrorAction SilentlyContinue
      }
      foreach ($process in @($policyProcess, $listenerProcess)) {
        if ($process) { $process.Dispose() }
      }
      foreach ($log in @(
          $listenerOutput, $listenerError, $policyOutput, $policyError)) {
        Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue
      }
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
