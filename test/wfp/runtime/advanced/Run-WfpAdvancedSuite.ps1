[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $PackageRoot,

  [ValidateSet('all', 'datagram-proxy', 'async-inspection',
               'flow-monitor', 'stream-edit', 'connect-redirect',
               'bind-redirect',
               'tls-inspection-proxy',
               'udp-content-filter',
               'tcp-content-filter')]
  [Alias('Sample')]
  [string] $SelectedSample = 'all',

  [ValidateRange(1, 1000)]
  [int] $Iterations = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$samples = @(
  [pscustomobject]@{
    Name = 'datagram-proxy'
    BaseName = 'crtsys_wfp_datagram_proxy'
    Service = 'CrtSysWfpDatagramProxyAcceptance'
    Marker = 'NTL WFP datagram-proxy ok:'
  },
  [pscustomobject]@{
    Name = 'async-inspection'
    BaseName = 'crtsys_wfp_async_inspection'
    Service = 'CrtSysWfpAsyncInspectionAcceptance'
    Marker = 'NTL WFP async-inspection ok:'
  },
  [pscustomobject]@{
    Name = 'flow-monitor'
    BaseName = 'crtsys_wfp_flow_monitor'
    Service = 'CrtSysWfpFlowMonitorAcceptance'
    Marker = 'NTL WFP flow-monitor ok:'
  },
  [pscustomobject]@{
    Name = 'stream-edit'
    BaseName = 'crtsys_wfp_stream_edit'
    Service = 'CrtSysWfpStreamEditAcceptance'
    Marker = 'NTL WFP stream-edit ok:'
  },
  [pscustomobject]@{
    Name = 'connect-redirect'
    BaseName = 'crtsys_wfp_connect_redirect'
    Service = 'CrtSysWfpConnectRedirectAcceptance'
    Marker = 'NTL WFP connect-redirect ok:'
  },
  [pscustomobject]@{
    Name = 'bind-redirect'
    BaseName = 'crtsys_wfp_bind_redirect'
    Service = 'CrtSysWfpBindRedirectAcceptance'
    Marker = 'NTL WFP bind-redirect ok:'
  },
  [pscustomobject]@{
    Name = 'tls-inspection-proxy'
    BaseName = 'crtsys_wfp_tls_inspection_proxy'
    Service = 'CrtSysWfpTlsInspectionProxyAcceptance'
    Marker = 'NTL WFP TLS inspection-proxy ok:'
  },
  [pscustomobject]@{
    Name = 'udp-content-filter'
    BaseName = 'crtsys_wfp_udp_content_filter'
    Service = 'CrtSysWfpUdpContentFilterAcceptance'
    Marker = 'NTL WFP UDP content-filter ok:'
    FailureMarker = 'NTL WFP UDP content-filter fail-closed self-test ok:'
  },
  [pscustomobject]@{
    Name = 'tcp-content-filter'
    BaseName = 'crtsys_wfp_tcp_content_filter'
    Service = 'CrtSysWfpTcpContentFilterAcceptance'
    Marker = 'NTL WFP TCP content-filter ok:'
    FailureMarker = 'NTL WFP TCP content-filter fail-closed self-test ok:'
  }
)
if ($SelectedSample -ne 'all') {
  $samples = @($samples | Where-Object Name -eq $SelectedSample)
}
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

function Invoke-SampleApplication {
  param(
    [Parameter(Mandatory)]
    [string] $Path,
    [string[]] $Arguments = @(),
    [ValidateRange(1, 600)]
    [int] $TimeoutSeconds = 120
  )

  $process = $null
  try {
    foreach ($argument in $Arguments) {
      if ($argument -match '[\s"]') {
        throw "Unsupported sample argument: $argument"
      }
    }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Path
    $start.Arguments = $Arguments -join ' '
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) {
      throw "Starting the sample application failed: $Path"
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
      Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
      throw (
          "Application timed out after $TimeoutSeconds seconds: $Path")
    }
    $process.WaitForExit()
    $stdoutText = $stdoutTask.GetAwaiter().GetResult()
    $stderrText = $stderrTask.GetAwaiter().GetResult()
    $text = $stdoutText + $stderrText
    $output = @(
      $text -split '\r?\n' | Where-Object { $_.Length -ne 0 })
    return [pscustomobject]@{
      ExitCode = [int] $process.ExitCode
      Output = $output
      Text = $text
    }
  } finally {
    if ($process) {
      $process.Dispose()
    }
  }
}

Assert-Administrator
$PackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path

foreach ($sample in $samples) {
  foreach ($extension in @('.sys', '_app.exe', '.cer')) {
    $path = Join-Path $PackageRoot "$($sample.BaseName)$extension"
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
      throw "Required WFP runtime artifact was not found: $path"
    }
  }
  $certificate = Join-Path $PackageRoot "$($sample.BaseName).cer"
  & certutil.exe -f -addstore Root $certificate *> $null
  if ($LASTEXITCODE -ne 0) {
    throw "Root certificate import failed for $($sample.Name)."
  }
  & certutil.exe -f -addstore TrustedPublisher $certificate *> $null
  if ($LASTEXITCODE -ne 0) {
    throw "Publisher certificate import failed for $($sample.Name)."
  }
}

foreach ($sample in $samples) {
  $driver = Join-Path $PackageRoot "$($sample.BaseName).sys"
  $application = Join-Path $PackageRoot "$($sample.BaseName)_app.exe"
  Remove-ServiceIfPresent $sample.Service
  try {
    Write-Host "=== $($sample.Name): driver load ==="
    & sc.exe create $sample.Service 'binPath=' $driver `
        'type=' 'kernel' 'start=' 'demand' |
        ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
      throw "Creating the $($sample.Name) service failed."
    }

    & sc.exe start $sample.Service | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
      throw "Starting the $($sample.Name) service failed."
    }

    if ($sample.Name -eq 'stream-edit') {
      Write-Host '=== stream-edit: coroutine socket self-test ==='
      $selfTest =
          Invoke-SampleApplication $application @('--coroutine-self-test')
      $selfTest.Output | ForEach-Object { Write-Host $_ }
      if ($selfTest.ExitCode -ne 0 -or
          -not $selfTest.Text.Contains(
              'NTL WFP stream-edit coroutine self-test ok:')) {
        throw 'The stream-edit coroutine socket self-test failed.'
      }
    }
    if ($null -ne $sample.PSObject.Properties['FailureMarker']) {
      Write-Host "=== $($sample.Name): fail-closed self-test ==="
      $selfTest =
          Invoke-SampleApplication $application @('--failure-self-test')
      $selfTest.Output | ForEach-Object { Write-Host $_ }
      if ($selfTest.ExitCode -ne 0 -or
          -not $selfTest.Text.Contains($sample.FailureMarker)) {
        throw "The $($sample.Name) fail-closed self-test failed."
      }
    }
    for ($iteration = 1; $iteration -le $Iterations; ++$iteration) {
      Write-Host "=== $($sample.Name): iteration $iteration/$Iterations ==="
      $run = Invoke-SampleApplication $application
      $run.Output | ForEach-Object { Write-Host $_ }
      if ($run.ExitCode -ne 0) {
        throw (
          "$($sample.Name) iteration $iteration exited with " +
          "$($run.ExitCode).")
      }
      if (-not $run.Text.Contains($sample.Marker)) {
        throw (
          "$($sample.Name) iteration $iteration missed its proof marker.")
      }
    }
  } finally {
    Write-Host "=== $($sample.Name): driver unload ==="
    Remove-ServiceIfPresent $sample.Service
  }

  & sc.exe query $sample.Service *> $null
  if ($LASTEXITCODE -eq 0) {
    throw "$($sample.Name) service remained installed after the suite."
  }
}

Write-Host (
  "Advanced WFP suite passed: $($samples.Count) samples, " +
  "$Iterations iterations each.")
