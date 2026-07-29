[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $PackageRoot,

  [string] $BrowserPath,

  [Alias('Url')]
  [uri[]] $Urls = @(
    'https://www.google.com/',
    'https://example.com/'
  ),

  [ValidateRange(1024, 65535)]
  [int] $ListenPort = 44330,

  [string] $LogDirectory =
      (Join-Path $PackageRoot 'browser-http3-spki-logs'),

  [switch] $Interactive,

  [ValidateRange(10, 3600)]
  [int] $DurationSeconds = 60
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

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
    Stop-Process -Id $process.ProcessId -Force `
        -ErrorAction SilentlyContinue
  }
}

function Remove-TestProfile(
    [string] $ProfilePath,
    [string] $ParentPath) {
  $resolvedProfile = [IO.Path]::GetFullPath($ProfilePath)
  $resolvedParent =
      [IO.Path]::GetFullPath($ParentPath).TrimEnd('\') + '\'
  if (-not ($resolvedProfile.TrimEnd('\') + '\').StartsWith(
      $resolvedParent, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The isolated Edge profile escaped the test log directory.'
  }
  for ($attempt = 1; $attempt -le 20; ++$attempt) {
    if (-not (Test-Path -LiteralPath $resolvedProfile)) {
      return
    }
    Stop-TestBrowser $resolvedProfile
    Remove-Item -LiteralPath $resolvedProfile -Recurse -Force `
        -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $resolvedProfile) {
      Start-Sleep -Milliseconds 250
    }
  }
  if (Test-Path -LiteralPath $resolvedProfile) {
    throw (
        "The isolated Edge profile could not be removed: " +
        $resolvedProfile)
  }
}

$root = (Resolve-Path -LiteralPath $PackageRoot).Path
$application =
    Join-Path $root 'crtsys_wfp_browser_https_inspection_app.exe'
$msh3 = Join-Path $root 'msh3.dll'
$msquic = Join-Path $root 'msquic.dll'
foreach ($required in @($application, $msh3, $msquic)) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
    throw "Required HTTP/3 inspection artifact is missing: $required"
  }
}
if (-not $BrowserPath) {
  $BrowserPath = Find-Edge
}
$BrowserPath = (Resolve-Path -LiteralPath $BrowserPath).Path
if ($Urls.Count -eq 0) {
  throw 'At least one HTTP/3 inspection URL is required.'
}
$targetHosts = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
foreach ($targetUrl in $Urls) {
  if ($targetUrl.Scheme -ne 'https' -or
      $targetUrl.Port -ne 443) {
    throw 'Every HTTP/3 inspection URL must use HTTPS port 443.'
  }
  $targetHost = $targetUrl.DnsSafeHost
  if ([string]::IsNullOrWhiteSpace($targetHost) -or
      $targetHost -match '[/\\:]') {
    throw 'Every HTTP/3 inspection URL must contain one DNS host name.'
  }
  [void] $targetHosts.Add($targetHost)
}
$hostName = $Urls[0].DnsSafeHost

New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
$LogDirectory = (Resolve-Path -LiteralPath $LogDirectory).Path
$profile = Join-Path $LogDirectory (
    'edge-profile-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $profile | Out-Null
$proxyStdout = Join-Path $LogDirectory 'http3-proxy.stdout.log'
$proxyStderr = Join-Path $LogDirectory 'http3-proxy.stderr.log'
$caPath = Join-Path $LogDirectory 'ntl-browser-inspection-ca.cer'
$spkiPath = Join-Path $LogDirectory 'ntl-browser-http3-spki.txt'
$stopPath = Join-Path $LogDirectory 'stop.request'
$sessionStartedUtc = [DateTime]::UtcNow
$proxy = $null

Remove-Item -LiteralPath $proxyStdout, $proxyStderr, $caPath, `
    $spkiPath, $stopPath -Force -ErrorAction SilentlyContinue

try {
  $proxy = Start-Process -FilePath $application `
      -ArgumentList @(
        '--http3-spki-proxy',
        $hostName,
        $ListenPort.ToString(
            [Globalization.CultureInfo]::InvariantCulture),
        "`"$LogDirectory`"",
        '0'
      ) -WindowStyle Hidden -PassThru `
      -RedirectStandardOutput $proxyStdout `
      -RedirectStandardError $proxyStderr

  $readyDeadline = (Get-Date).AddSeconds(30)
  do {
    if ($proxy.HasExited) {
      $errorText = if (Test-Path -LiteralPath $proxyStderr) {
        Get-Content -LiteralPath $proxyStderr -Raw
      } else {
        ''
      }
      throw (
          "The HTTP/3 inspection proxy exited with " +
          "$($proxy.ExitCode): $errorText")
    }
    $ready =
        (Test-Path -LiteralPath $caPath -PathType Leaf) -and
        (Test-Path -LiteralPath $spkiPath -PathType Leaf) -and
        (Test-Path -LiteralPath $proxyStdout -PathType Leaf)
    if ($ready) {
      $readyText =
          Get-Content -LiteralPath $proxyStdout -Raw
      $ready = $null -ne $readyText -and
          $readyText.Contains(
              'NTL browser HTTP/3 diagnostic ready:')
    }
    if (-not $ready) {
      Start-Sleep -Milliseconds 100
    }
  } while (-not $ready -and (Get-Date) -lt $readyDeadline)
  if (-not $ready) {
    throw 'The HTTP/3 inspection proxy did not become ready.'
  }

  $spki = (Get-Content -LiteralPath $spkiPath -Raw).Trim()
  if ($spki -notmatch '^[A-Za-z0-9+/]+={0,2}$') {
    throw 'The proxy emitted an invalid base64 SPKI hash.'
  }

  $hostRule =
      "MAP *:443 127.0.0.1:${ListenPort}, EXCLUDE localhost"
  $browserArguments = @(
    "`"--user-data-dir=$profile`"",
    '--no-first-run',
    '--no-default-browser-check',
    '--no-proxy-server',
    '--enable-quic',
    '--origin-to-force-quic-on=*',
    "`"--host-resolver-rules=$hostRule`"",
    "--ignore-certificate-errors-spki-list=$spki",
    '--new-window'
  )
  $browserArguments += @(
    $Urls | ForEach-Object { $_.AbsoluteUri }
  )
  Start-Process -FilePath $BrowserPath `
      -ArgumentList $browserArguments | Out-Null

  if ($Interactive) {
    Write-Host (
        'The isolated HTTP/3 browser is ready. Browse ordinary HTTPS ' +
        'port-443 sites, then press Enter here to stop.')
    [void] (Read-Host)
    $deadline = Get-Date
  } else {
    $deadline = (Get-Date).AddSeconds($DurationSeconds)
  }
  do {
    if (-not $Interactive) {
      Start-Sleep -Milliseconds 250
    }
    $html = @(
      Get-ChildItem -LiteralPath $LogDirectory -Filter '*.html' `
          -File -ErrorAction SilentlyContinue |
          Where-Object {
            $_.LastWriteTimeUtc -ge $sessionStartedUtc
          }
    )
    $completedHosts = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $html) {
      foreach ($targetHost in $targetHosts) {
        if ($file.BaseName.EndsWith(
            ('-' + $targetHost),
            [StringComparison]::OrdinalIgnoreCase)) {
          [void] $completedHosts.Add($targetHost)
        }
      }
    }
  } while (-not $Interactive -and
      $completedHosts.Count -lt $targetHosts.Count -and
      (Get-Date) -lt $deadline)

  if (-not $Interactive -and
      $completedHosts.Count -lt $targetHosts.Count) {
    $missingHosts = @(
      $targetHosts | Where-Object {
        -not $completedHosts.Contains($_)
      }
    )
    throw (
        'No decrypted HTTP/3 HTML response was logged for: ' +
        ($missingHosts -join ', '))
  }

  New-Item -ItemType File -Path $stopPath -Force | Out-Null
  if (-not $proxy.WaitForExit(20000)) {
    throw 'The HTTP/3 inspection proxy did not stop cleanly.'
  }
  $proxy.WaitForExit()
  $proxyOutput = Get-Content -LiteralPath $proxyStdout -Raw
  if (-not $proxyOutput.Contains(
      'NTL browser HTTP/3 diagnostic stopped:') -or
      -not $proxyOutput.Contains('downstream=h3, upstream=h3')) {
    throw 'The proxy did not prove a clean HTTP/3 inspection session.'
  }
  $undeliveredHosts = @(
    $targetHosts | Where-Object {
      -not $proxyOutput.Contains(
          "NTL HTTP/3 inspected: host=$_, downstream=h3, upstream=h3,")
    }
  )
  if ($undeliveredHosts.Count -ne 0) {
    throw (
        'The proxy inspected but did not deliver HTTP/3 responses for: ' +
        ($undeliveredHosts -join ', '))
  }

  Write-Host (
      "Browser HTTP/3 SPKI diagnostic passed: $($html.Count) " +
      "HTML file(s), hosts=$($completedHosts.Count), downstream=h3.")
  $html | ForEach-Object { Write-Host "  $($_.FullName)" }
} finally {
  Stop-TestBrowser $profile
  if ($proxy -and -not $proxy.HasExited) {
    New-Item -ItemType File -Path $stopPath -Force `
        -ErrorAction SilentlyContinue | Out-Null
    if (-not $proxy.WaitForExit(5000)) {
      Stop-Process -Id $proxy.Id -Force -ErrorAction SilentlyContinue
    }
  }
  Remove-TestProfile $profile $LogDirectory
}

Write-Host "Browser HTTP/3 SPKI diagnostic logs: $LogDirectory"
