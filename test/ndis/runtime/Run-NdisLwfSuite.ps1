[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $PackageRoot,

  [ValidateRange(1, 1000)]
  [int] $Iterations = 20,

  [ValidateRange(0, 100)]
  [int] $RestartIterations = 3,

  [string] $AdapterName = '',

  [ValidateRange(0, 1440)]
  [int] $SoakMinutes = 0,

  [switch] $RequireVerifier
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

function Invoke-CheckedMonitor([string] $Path, [string] $Description) {
  $run = Invoke-MonitorApplication $Path
  $run.Output | ForEach-Object { Write-Host $_ }
  if ($run.ExitCode -ne 0 -or
      -not $run.Text.Contains('NTL NDIS LWF monitor ok:')) {
    throw "$Description failed with exit code $($run.ExitCode)."
  }
  return $run
}

function Get-MonitorCounter([string] $Text, [string] $Name) {
  $match = [regex]::Match(
      $Text, "(?:^|[ ,])$([regex]::Escape($Name))=(\d+)")
  if (-not $match.Success) {
    throw "The monitor output did not contain '$Name': $Text"
  }
  return [uint64]::Parse($match.Groups[1].Value)
}

function Wait-AdapterUp([string] $Name) {
  $deadline = (Get-Date).AddSeconds(60)
  do {
    $adapter = Get-NetAdapter -Name $Name -ErrorAction SilentlyContinue
    if (-not $adapter -or $adapter.Status -ne 'Up') {
      Start-Sleep -Milliseconds 250
    }
  } while ((Get-Date) -lt $deadline -and
           (-not $adapter -or $adapter.Status -ne 'Up'))
  if (-not $adapter -or $adapter.Status -ne 'Up') {
    throw "The NDIS stress adapter '$Name' did not return to Up."
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

if ($RequireVerifier) {
  $verifierActive = (& verifier.exe /query 2>&1 | Out-String)
  $verifierExitCode = $LASTEXITCODE
  $flagMatch = [regex]::Match(
      $verifierActive, '0x[0-9a-fA-F]+')
  $activeFlags = if ($flagMatch.Success) {
    [Convert]::ToUInt32($flagMatch.Value.Substring(2), 16)
  } else {
    [uint32] 0
  }
  if ($verifierExitCode -ne 0 -or
      -not $verifierActive.Contains("$baseName.sys") -or
      ($activeFlags -band 0x00200000) -eq 0) {
    throw (
        "Driver Verifier is not actively targeting $baseName.sys with " +
        'the NDIS/WIFI verification class. Configure it and reboot the VM ' +
        'before using -RequireVerifier.')
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

  $baseline = Invoke-CheckedMonitor `
      $application 'Initial NDIS LWF monitor run'
  $baselineRestarts = Get-MonitorCounter $baseline.Text 'restarts'
  $baselinePauses = Get-MonitorCounter $baseline.Text 'pauses'
  $baselineStatus = Get-MonitorCounter `
      $baseline.Text 'status-indications'
  $baselineNetPnp = Get-MonitorCounter `
      $baseline.Text 'net-pnp-events'

  if ($RestartIterations -gt 0) {
    if ([string]::IsNullOrWhiteSpace($AdapterName)) {
      $candidate = @(
        Get-NetAdapter -ErrorAction Stop |
          Where-Object {
            $_.Status -eq 'Up' -and
            $_.InterfaceDescription -notlike '*Loopback*'
          } |
          Sort-Object ifIndex |
          Select-Object -First 1)
      if ($candidate.Count -ne 1) {
        throw 'No Up physical adapter was available for pause/restart stress.'
      }
      $AdapterName = $candidate[0].Name
    }

    for ($restart = 1;
         $restart -le $RestartIterations;
         ++$restart) {
      Write-Host (
          "=== ndis-lwf-monitor: adapter restart " +
          "$restart/$RestartIterations ($AdapterName) ===")
      Restart-NetAdapter -Name $AdapterName -Confirm:$false
      Wait-AdapterUp $AdapterName
      $restartRun = Invoke-CheckedMonitor `
          $application "Adapter restart iteration $restart"
    }

    $restartCount = Get-MonitorCounter $restartRun.Text 'restarts'
    $pauseCount = Get-MonitorCounter $restartRun.Text 'pauses'
    $statusCount = Get-MonitorCounter `
        $restartRun.Text 'status-indications'
    $netPnpCount = Get-MonitorCounter `
        $restartRun.Text 'net-pnp-events'
    if ($restartCount -le $baselineRestarts -or
        $pauseCount -le $baselinePauses) {
      throw (
          'Adapter restart did not exercise both NDIS pause and restart: ' +
          "before=$baselinePauses/$baselineRestarts, " +
          "after=$pauseCount/$restartCount.")
    }
    if ($statusCount -le $baselineStatus -or
        $netPnpCount -le $baselineNetPnp) {
      throw (
          'Adapter restart did not exercise both status and NetPnP ' +
          "pass-through: before=$baselineStatus/$baselineNetPnp, " +
          "after=$statusCount/$netPnpCount.")
    }
  }

  for ($iteration = 1;
       $iteration -le $Iterations;
       ++$iteration) {
    Write-Host (
        "=== ndis-lwf-monitor: iteration " +
        "$iteration/$Iterations ===")
    [void](Invoke-CheckedMonitor `
        $application "NDIS LWF iteration $iteration")
  }

  if ($SoakMinutes -gt 0) {
    $soakDeadline = (Get-Date).AddMinutes($SoakMinutes)
    $soakIteration = 0
    while ((Get-Date) -lt $soakDeadline) {
      ++$soakIteration
      Write-Host (
          "=== ndis-lwf-monitor: soak iteration $soakIteration ===")
      [void](Invoke-CheckedMonitor `
          $application "NDIS LWF soak iteration $soakIteration")
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
    "$RestartIterations adapter restarts, $SoakMinutes soak minutes; " +
    'attach/restart/send/complete/receive/return/OID/status/NetPnP/' +
    'metadata/pause/detach exercised.')
