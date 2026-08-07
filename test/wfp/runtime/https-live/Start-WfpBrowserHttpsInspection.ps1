[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $PackageRoot,

  [string] $BrowserPath,

  [uri] $Url,

  [uri[]] $Urls,

  [switch] $RequireQuicBlockedFallback,

  [string] $LogDirectory =
      (Join-Path $PackageRoot 'browser-https-logs'),

  [ValidateRange(0, 3600)]
  [int] $DurationSeconds = 0,

  [ValidateRange(0, 300)]
  [int] $BrowserProcessWaitSeconds = 30,

  [string] $ServiceName = 'CrtSysWfpBrowserHttpsInspection',

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
    throw 'Browser HTTPS inspection must run from an elevated PowerShell session.'
  }
}

function Remove-TestService([string] $Name) {
  & sc.exe query $Name *> $null
  if ($LASTEXITCODE -eq 0) {
    & sc.exe stop $Name *> $null
    & sc.exe delete $Name *> $null
  }
}

function Find-Edge {
  $candidates = @(
    (Join-Path -Path ${env:ProgramFiles(x86)} -ChildPath `
        'Microsoft\Edge\Application\msedge.exe'),
    (Join-Path -Path $env:ProgramFiles -ChildPath `
        'Microsoft\Edge\Application\msedge.exe')
  )
  foreach ($candidate in $candidates) {
    if ($candidate -and
        (Test-Path -LiteralPath $candidate -PathType Leaf)) {
      return $candidate
    }
  }
  throw 'Microsoft Edge was not found; pass -BrowserPath explicitly.'
}

function Get-ObservedBrowserProcess([string] $Path) {
  $fullPath = [IO.Path]::GetFullPath($Path)
  $processName = [IO.Path]::GetFileName($fullPath).Replace("'", "''")
  return @(
    Get-CimInstance Win32_Process `
        -Filter "Name = '$processName'" -ErrorAction SilentlyContinue |
        Where-Object {
          $_.ExecutablePath -and
          [IO.Path]::GetFullPath($_.ExecutablePath).Equals(
              $fullPath, [StringComparison]::OrdinalIgnoreCase)
        }
  )
}

function Wait-ObservedBrowserProcess(
    [string] $Path,
    [int] $WaitSeconds) {
  $deadline = (Get-Date).AddSeconds($WaitSeconds)
  do {
    $processes = @(Get-ObservedBrowserProcess $Path)
    if ($processes.Count -gt 0) {
      return $processes
    }
    if ((Get-Date) -ge $deadline) {
      break
    }
    Start-Sleep -Milliseconds 250
  } while ($true)

  throw (
      'No already-running browser process matches BrowserPath. Start the ' +
      'browser normally, without test flags or a temporary profile, and ' +
      'then run this wrapper again.')
}

Assert-Administrator
$root = (Resolve-Path -LiteralPath $PackageRoot).Path
$driver = Join-Path $root 'crtsys_wfp_browser_https_inspection.sys'
$driverInf = Join-Path $root 'crtsys_wfp_browser_https_inspection.inf'
$application =
    Join-Path $root 'crtsys_wfp_browser_https_inspection_controller.exe'
$evidenceAnalyzer =
    Join-Path $root 'Test-WfpBrowserTransportEvidence.ps1'
foreach ($required in @(
    $driver, $driverInf, $application, $evidenceAnalyzer)) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
    throw "Required browser HTTPS artifact is missing: $required"
  }
}
if (-not $BrowserPath) {
  $BrowserPath = Find-Edge
}
$BrowserPath = (Resolve-Path -LiteralPath $BrowserPath).Path
$observedBrowserProcesses = @(
  Wait-ObservedBrowserProcess $BrowserPath $BrowserProcessWaitSeconds
)

if (-not $Urls -or $Urls.Count -eq 0) {
  if ($null -ne $Url) {
    $Urls = @($Url)
  } else {
    $Urls = @()
  }
}
foreach ($targetUrl in $Urls) {
  if ($targetUrl.Scheme -ne 'https' -or $targetUrl.Port -ne 443) {
    throw 'Every expected browser inspection URL must use HTTPS port 443.'
  }
}
$expectedHosts = @(
  $Urls | ForEach-Object { $_.DnsSafeHost } | Sort-Object -Unique
)

New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
$LogDirectory = (Resolve-Path -LiteralPath $LogDirectory).Path
$proxyStdout = Join-Path $LogDirectory 'proxy.stdout.log'
$proxyStderr = Join-Path $LogDirectory 'proxy.stderr.log'
$caPath = Join-Path $LogDirectory 'ntl-browser-inspection-ca.cer'
$stopPath = Join-Path $LogDirectory 'stop.request'
$inventoryPath =
    Join-Path $LogDirectory 'wfp-policy-diagnostics.log'
$transportEvidencePath =
    Join-Path $LogDirectory 'browser-transport-evidence.json'
$proxy = $null
$importedThumbprint = $null
$certificate = $null
$sessionStartedUtc = [DateTime]::UtcNow

Remove-Item -LiteralPath $proxyStdout, $proxyStderr, $caPath, $stopPath,
    $inventoryPath, $transportEvidencePath -Force `
    -ErrorAction SilentlyContinue
Remove-TestService $ServiceName

try {
  & sc.exe create $ServiceName type= kernel start= demand `
      binPath= $driver | ForEach-Object { Write-Host $_ }
  if ($LASTEXITCODE -ne 0) {
    throw 'Creating the browser HTTPS driver service failed.'
  }
  & sc.exe start $ServiceName | ForEach-Object { Write-Host $_ }
  if ($LASTEXITCODE -ne 0) {
    throw 'Starting the browser HTTPS driver failed.'
  }

  $proxy = Start-Process -FilePath $application `
      -ArgumentList @(
        "`"$BrowserPath`"",
        "`"$LogDirectory`"",
        '0'
      ) -WindowStyle Hidden -PassThru `
      -RedirectStandardOutput $proxyStdout `
      -RedirectStandardError $proxyStderr

  $readyDeadline = (Get-Date).AddSeconds(20)
  do {
    if ($proxy.HasExited) {
      throw "The browser inspection proxy exited with $($proxy.ExitCode)."
    }
    $ready =
        (Test-Path -LiteralPath $caPath -PathType Leaf) -and
        (Test-Path -LiteralPath $inventoryPath -PathType Leaf)
    if ($ready -and (Test-Path -LiteralPath $proxyStdout)) {
      $readyText = Get-Content -LiteralPath $proxyStdout -Raw
      $ready = $null -ne $readyText -and
          $readyText.Contains(
              'NTL WFP browser HTTPS inspection ready:')
    }
    if (-not $ready) {
      Start-Sleep -Milliseconds 100
    }
  } while (-not $ready -and (Get-Date) -lt $readyDeadline)
  if (-not $ready) {
    throw 'The browser inspection proxy did not become ready.'
  }

  $certificate =
      [Security.Cryptography.X509Certificates.X509Certificate2]::new(
          $caPath)
  $importedThumbprint = $certificate.Thumbprint
  & certutil.exe -f -addstore Root $caPath | ForEach-Object {
    Write-Verbose $_
  }
  if ($LASTEXITCODE -ne 0 -or -not $importedThumbprint) {
    throw 'Importing the temporary browser inspection CA failed.'
  }

  Write-Host (
      'Browser HTTPS inspection is observing the already-running browser: ' +
      "$BrowserPath (PID $((@($observedBrowserProcesses.ProcessId) -join ', '))).")
  Write-Warning (
      'WFP scopes by executable path, so every process using that browser ' +
      'executable is inspected until this wrapper stops.')
  Write-Host (
      'No browser was launched, terminated, reprofiled, or given command-line ' +
      'feature, certificate, QUIC, ECH, or logging switches.')

  if ($DurationSeconds -eq 0) {
    Write-Host (
        'Use the already-open browser normally. Visit the HTTPS pages to ' +
        'inspect, then return here.')
    Read-Host 'Press Enter to stop inspection' | Out-Null
  } else {
    Write-Host (
        "Observing existing browser traffic for $DurationSeconds second(s). " +
        'Navigate in the already-open browser while this interval is active.')
    $deadline = (Get-Date).AddSeconds($DurationSeconds)
    while ((Get-Date) -lt $deadline) {
      Start-Sleep -Milliseconds 250
      if ($proxy.HasExited) {
        throw "The browser inspection proxy exited with $($proxy.ExitCode)."
      }
    }
  }

  New-Item -ItemType File -Path $stopPath -Force | Out-Null
  if (-not $proxy.WaitForExit(15000)) {
    throw 'The browser inspection proxy did not stop cleanly.'
  }
  $proxy.WaitForExit()
  $proxyOutput = Get-Content -LiteralPath $proxyStdout -Raw
  if (-not $proxyOutput.Contains(
      'NTL WFP browser HTTPS inspection stopped:')) {
    throw 'The browser inspection proxy did not report a clean stop.'
  }

  $evidenceArguments = @{
    ProxyLogPath = $proxyStdout
    PolicyInventoryPath = $inventoryPath
    LogDirectory = $LogDirectory
    ExpectedHost = $expectedHosts
    SessionStartedUtc = $sessionStartedUtc
    ResultPath = $transportEvidencePath
    RequireObservedUdpBlock = $RequireQuicBlockedFallback
  }
  & $evidenceAnalyzer @evidenceArguments |
      ForEach-Object { Write-Host $_ }

  $html = @(
    Get-ChildItem -LiteralPath $LogDirectory -Filter '*.html' -File |
        Where-Object {
          $_.LastWriteTimeUtc -ge $sessionStartedUtc
        }
  )
  Write-Host (
      "Browser HTTPS inspection passed: $($html.Count) HTML file(s).")
  $html | ForEach-Object { Write-Host "  $($_.FullName)" }
} finally {
  if ($proxy -and -not $proxy.HasExited) {
    New-Item -ItemType File -Path $stopPath -Force `
        -ErrorAction SilentlyContinue | Out-Null
    if (-not $proxy.WaitForExit(5000)) {
      Stop-Process -Id $proxy.Id -Force -ErrorAction SilentlyContinue
    }
  }
  if ($importedThumbprint) {
    Remove-Item -LiteralPath (
        'Cert:\LocalMachine\Root\' + $importedThumbprint) `
        -Force -ErrorAction SilentlyContinue
  }
  if ($certificate) {
    $certificate.Dispose()
  }
  Remove-TestService $ServiceName
}

Write-Host "Browser HTTPS logs: $LogDirectory"
