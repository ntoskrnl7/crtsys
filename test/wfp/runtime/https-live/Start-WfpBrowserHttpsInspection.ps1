[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $PackageRoot,

  [string] $BrowserPath,

  [uri] $Url = 'https://example.com/',

  [uri[]] $Urls,

  [switch] $RequireQuicBlockedFallback,

  [string] $NetLogPath,

  [string] $LogDirectory =
      (Join-Path $PackageRoot 'browser-https-logs'),

  [ValidateRange(0, 3600)]
  [int] $DurationSeconds = 0,

  [string] $ServiceName = 'CrtSysWfpBrowserHttpsInspection'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

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

function Stop-TestBrowser([string] $ProfilePath) {
  $escaped = [Regex]::Escape($ProfilePath)
  $processes = @(
    Get-CimInstance Win32_Process -Filter "Name = 'msedge.exe'" |
        Where-Object {
          $_.CommandLine -and $_.CommandLine -match $escaped
        }
  )
  foreach ($process in $processes) {
    Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
  }
}

Assert-Administrator
$root = (Resolve-Path -LiteralPath $PackageRoot).Path
$driver = Join-Path $root 'crtsys_wfp_browser_https_inspection.sys'
$application =
    Join-Path $root 'crtsys_wfp_browser_https_inspection_app.exe'
foreach ($required in @($driver, $application)) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
    throw "Required browser HTTPS artifact is missing: $required"
  }
}
if (-not $BrowserPath) {
  $BrowserPath = Find-Edge
}
$BrowserPath = (Resolve-Path -LiteralPath $BrowserPath).Path
if (-not $Urls -or $Urls.Count -eq 0) {
  $Urls = @($Url)
}
foreach ($targetUrl in $Urls) {
  if ($targetUrl.Scheme -ne 'https' -or $targetUrl.Port -ne 443) {
    throw 'Every browser inspection URL must use HTTPS port 443.'
  }
}
New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
$LogDirectory = (Resolve-Path -LiteralPath $LogDirectory).Path
$profile = Join-Path $LogDirectory (
    'edge-profile-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $profile | Out-Null
$proxyStdout = Join-Path $LogDirectory 'proxy.stdout.log'
$proxyStderr = Join-Path $LogDirectory 'proxy.stderr.log'
$caPath = Join-Path $LogDirectory 'ntl-browser-inspection-ca.cer'
$stopPath = Join-Path $LogDirectory 'stop.request'
$proxy = $null
$importedThumbprint = $null
$certificate = $null
$sessionStartedUtc = [DateTime]::UtcNow

Remove-Item -LiteralPath $proxyStdout, $proxyStderr, $caPath, $stopPath `
    -Force -ErrorAction SilentlyContinue
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
    $ready = Test-Path -LiteralPath $caPath -PathType Leaf
    if ($ready -and (Test-Path -LiteralPath $proxyStdout)) {
      $readyText = [string](Get-Content -LiteralPath $proxyStdout -Raw)
      $ready = $readyText.Contains(
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
      'Temporary test CA trusted for this machine: ' +
      $importedThumbprint)
  Write-Warning (
      'WFP scopes by browser executable path, so every process using ' +
      "$BrowserPath is inspected until this test stops.")

$browserArguments = @(
    "`"--user-data-dir=$profile`"",
    '--no-first-run',
    '--no-default-browser-check',
    '--new-window'
  )
  if (-not [string]::IsNullOrWhiteSpace($NetLogPath)) {
    $resolvedNetLogPath = [IO.Path]::GetFullPath($NetLogPath)
    $netLogParent = Split-Path -Parent $resolvedNetLogPath
    if ([string]::IsNullOrWhiteSpace($netLogParent)) {
      throw 'NetLogPath must include a parent directory.'
    }
    New-Item -ItemType Directory -Path $netLogParent -Force |
        Out-Null
    Remove-Item -LiteralPath $resolvedNetLogPath -Force `
        -ErrorAction SilentlyContinue
    $browserArguments +=
        "`"--log-net-log=$resolvedNetLogPath`""
  }
  $browserArguments += @(
    $Urls | ForEach-Object { $_.AbsoluteUri }
  )
  Start-Process -FilePath $BrowserPath `
      -ArgumentList $browserArguments | Out-Null

  if ($DurationSeconds -eq 0) {
    Write-Host 'Browse HTTPS pages in the opened window.'
    Read-Host 'Press Enter to stop inspection' | Out-Null
  } else {
    $deadline = (Get-Date).AddSeconds($DurationSeconds)
    do {
      Start-Sleep -Milliseconds 250
      $html = @(
        Get-ChildItem -LiteralPath $LogDirectory -Filter '*.html' `
            -File -ErrorAction SilentlyContinue |
            Where-Object {
              $_.LastWriteTimeUtc -ge $sessionStartedUtc
            }
      )
      $events = if (Test-Path -LiteralPath (
          Join-Path $LogDirectory 'events.log')) {
        Get-Content -LiteralPath (
            Join-Path $LogDirectory 'events.log') -Raw
      } else {
        ''
      }
      $completed = @(
        $Urls | Where-Object {
          $targetDnsHost = $_.DnsSafeHost
          $hasHtml = @(
            $html | Where-Object {
              $_.BaseName.EndsWith(
                  ('-' + $targetDnsHost),
                  [StringComparison]::OrdinalIgnoreCase)
            }
          ).Count -gt 0
          $hasHtml
        }
      )
    } while ($completed.Count -lt $Urls.Count -and
        (Get-Date) -lt $deadline)
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
  if ($RequireQuicBlockedFallback -and
      -not $proxyOutput.Contains(
          'quic-policy=blocked-for-tcp-fallback')) {
    throw 'The browser runtime did not report fail-closed QUIC policy.'
  }
  $html = @(
    Get-ChildItem -LiteralPath $LogDirectory -Filter '*.html' -File |
        Where-Object {
          $_.LastWriteTimeUtc -ge $sessionStartedUtc
        }
  )
  $events = Get-Content -LiteralPath (
      Join-Path $LogDirectory 'events.log') -Raw
  foreach ($targetUrl in $Urls) {
    $targetHost = $targetUrl.DnsSafeHost
    $hostHtml = @(
      $html | Where-Object {
        $_.BaseName.EndsWith(
            ('-' + $targetHost),
            [StringComparison]::OrdinalIgnoreCase)
      }
    )
    if ($hostHtml.Count -eq 0) {
      throw (
          "No decrypted HTML response was logged for $targetHost.")
    }
  }
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
  Stop-TestBrowser $profile
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
