[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $PackageRoot,

  [ValidateRange(1, 1000)]
  [int] $Iterations = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Administrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal =
      [Security.Principal.WindowsPrincipal]::new($identity)
  if (-not $principal.IsInRole(
      [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'The NDIS LWF suite requires an elevated PowerShell.'
  }
}

function Remove-Filter {
  & netcfg.exe -u crtsys_ntl_lwf_monitor *> $null
  if ($LASTEXITCODE -notin @(0, 1)) {
    throw "netcfg uninstall failed with $LASTEXITCODE."
  }
}

function Invoke-MonitorApplication([string] $Path) {
  $process = $null
  try {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Path
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) {
      throw 'Starting the NDIS LWF monitor application failed.'
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(120000)) {
      Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
      throw 'The NDIS LWF monitor application timed out.'
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
$baseName = 'crtsys_ndis_lwf_monitor'
$inf = Join-Path $PackageRoot "$baseName.inf"
$application = Join-Path $PackageRoot "${baseName}_app.exe"
$driverCertificate =
    Join-Path $PackageRoot "$baseName-driver.cer"
$catalogCertificate =
    Join-Path $PackageRoot "$baseName-catalog.cer"
foreach ($path in @(
    $inf, $application, $driverCertificate,
    $catalogCertificate,
    (Join-Path $PackageRoot "$baseName.sys"),
    (Join-Path $PackageRoot "$baseName.cat"))) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Required NDIS LWF artifact is missing: $path"
  }
}

foreach ($certificate in @(
    $driverCertificate, $catalogCertificate)) {
  & certutil.exe -f -addstore Root $certificate *> $null
  if ($LASTEXITCODE -ne 0) {
    throw "Root certificate import failed: $certificate"
  }
  & certutil.exe -f -addstore TrustedPublisher $certificate *> $null
  if ($LASTEXITCODE -ne 0) {
    throw "Publisher certificate import failed: $certificate"
  }
}

Remove-Filter
try {
  Write-Host '=== ndis-lwf-monitor: component install ==='
  & netcfg.exe -v -l $inf -c s -i crtsys_ntl_lwf_monitor |
      ForEach-Object { Write-Host $_ }
  if ($LASTEXITCODE -ne 0) {
    throw "netcfg install failed with $LASTEXITCODE."
  }

  $deadline = (Get-Date).AddSeconds(30)
  do {
    $service = Get-Service -Name $baseName `
        -ErrorAction SilentlyContinue
    if (-not $service -or
        $service.Status -ne 'Running') {
      Start-Sleep -Milliseconds 100
    }
  } while (
      (Get-Date) -lt $deadline -and
      (-not $service -or
       $service.Status -ne 'Running'))
  if (-not $service -or $service.Status -ne 'Running') {
    throw 'The NDIS LWF service did not reach Running.'
  }

  for ($iteration = 1;
       $iteration -le $Iterations;
       ++$iteration) {
    Write-Host (
        "=== ndis-lwf-monitor: iteration " +
        "$iteration/$Iterations ===")
    $run = Invoke-MonitorApplication $application
    $run.Output | ForEach-Object { Write-Host $_ }
    if ($run.ExitCode -ne 0 -or
        -not $run.Text.Contains('NTL NDIS LWF monitor ok:')) {
      throw (
          "NDIS LWF iteration $iteration failed with " +
          "exit code $($run.ExitCode).")
    }
  }
} finally {
  Write-Host '=== ndis-lwf-monitor: component uninstall ==='
  Remove-Filter
}

$deadline = (Get-Date).AddSeconds(30)
do {
  $remaining = Get-Service -Name $baseName `
      -ErrorAction SilentlyContinue
  if ($remaining) {
    Start-Sleep -Milliseconds 100
  }
} while ($remaining -and (Get-Date) -lt $deadline)
if ($remaining) {
  throw 'The NDIS LWF service remained after component uninstall.'
}
Write-Host (
    "NDIS LWF suite passed: $Iterations iterations, " +
    'attach/restart/send/complete/pause/detach exercised.')
