[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $PackageRoot,

  [Parameter(Mandatory)]
  [uri] $Url,

  [string] $LogDirectory =
      (Join-Path $PackageRoot 'wfp-managed-http3-log')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($Url.Scheme -ne 'https' -or $Url.Port -ne 443) {
  throw 'The WFP managed HTTP/3 URL must use HTTPS port 443.'
}

$root = (Resolve-Path -LiteralPath $PackageRoot).Path
$proxyApplication =
    Join-Path $root 'crtsys_wfp_browser_https_inspection_app.exe'
$clientApplication =
    Join-Path $root 'crtsys_ntl_managed_http3_client.exe'
foreach ($required in @(
    $proxyApplication, $clientApplication,
    (Join-Path $root 'msh3.dll'),
    (Join-Path $root 'msquic.dll'))) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
    throw "Required WFP HTTP/3 artifact is missing: $required"
  }
}

$driver = Get-Service -Name crtsys_wfp_browser_https_inspection `
    -ErrorAction SilentlyContinue
if (-not $driver -or $driver.Status -ne 'Running') {
  throw (
      'crtsys_wfp_browser_https_inspection must already be installed ' +
      'and running in the test VM.')
}

New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
$LogDirectory = (Resolve-Path -LiteralPath $LogDirectory).Path
$proxyStdout = Join-Path $LogDirectory 'proxy.stdout.log'
$proxyStderr = Join-Path $LogDirectory 'proxy.stderr.log'
$clientStdout = Join-Path $LogDirectory 'client.stdout.log'
$clientStderr = Join-Path $LogDirectory 'client.stderr.log'
$clientBody = Join-Path $LogDirectory 'client-response.bin'
$caPath = Join-Path $LogDirectory 'ntl-browser-inspection-ca.cer'
$stopPath = Join-Path $LogDirectory 'stop.request'
$eventsPath = Join-Path $LogDirectory 'events.log'
$proxy = $null

Remove-Item -LiteralPath $proxyStdout, $proxyStderr,
    $clientStdout, $clientStderr, $clientBody, $caPath,
    $stopPath, $eventsPath -Force -ErrorAction SilentlyContinue

try {
  $portOwner = [Net.Sockets.UdpClient]::new(
      [Net.IPEndPoint]::new([Net.IPAddress]::Loopback, 0))
  try {
    $inspectionPort =
        ([Net.IPEndPoint] $portOwner.Client.LocalEndPoint).Port
  } finally {
    $portOwner.Dispose()
  }

  $proxy = Start-Process -FilePath $proxyApplication `
      -ArgumentList @(
        '--wfp-managed-http3-proxy',
        "`"$clientApplication`"",
        "$inspectionPort",
        "`"$LogDirectory`"",
        '0'
      ) -WindowStyle Hidden -PassThru `
      -RedirectStandardOutput $proxyStdout `
      -RedirectStandardError $proxyStderr

  $readyDeadline = (Get-Date).AddSeconds(30)
  do {
    if ($proxy.HasExited) {
      $proxy.WaitForExit()
      $errorText = if (Test-Path -LiteralPath $proxyStderr) {
        Get-Content -LiteralPath $proxyStderr -Raw
      } else {
        ''
      }
      throw (
          "The WFP HTTP/3 proxy exited with $($proxy.ExitCode): " +
          $errorText)
    }
    $ready =
        (Test-Path -LiteralPath $caPath -PathType Leaf) -and
        (Test-Path -LiteralPath $proxyStdout -PathType Leaf)
    if ($ready) {
      $readyText = Get-Content -LiteralPath $proxyStdout -Raw
      $ready =
          $readyText.Contains(
              'NTL WFP managed HTTP/3 inspection ready:')
    }
    if (-not $ready) {
      Start-Sleep -Milliseconds 100
    }
  } while (-not $ready -and (Get-Date) -lt $readyDeadline)
  if (-not $ready) {
    throw 'The WFP managed HTTP/3 proxy did not become ready.'
  }

  $client = Start-Process -FilePath $clientApplication `
      -ArgumentList @(
        "`"$($Url.AbsoluteUri)`"",
        "`"$clientBody`"",
        '--trust-ca',
        "`"$caPath`""
      ) -WindowStyle Hidden -PassThru -Wait `
      -RedirectStandardOutput $clientStdout `
      -RedirectStandardError $clientStderr
  if ($client.ExitCode -ne 0) {
    $clientError = if (Test-Path -LiteralPath $clientStderr) {
      Get-Content -LiteralPath $clientStderr -Raw
    } else {
      ''
    }
    throw (
        "The WFP managed client exited with $($client.ExitCode): " +
        $clientError)
  }

  New-Item -ItemType File -Path $stopPath -Force | Out-Null
  if (-not $proxy.WaitForExit(20000)) {
    throw 'The WFP managed HTTP/3 proxy did not stop cleanly.'
  }
  $proxy.WaitForExit()

  $clientOutput = Get-Content -LiteralPath $clientStdout -Raw
  $proxyOutput = Get-Content -LiteralPath $proxyStdout -Raw
  $events = Get-Content -LiteralPath $eventsPath -Raw
  $hostName = $Url.DnsSafeHost
  if (-not $clientOutput.Contains('protocol=h3') -or
      -not $clientOutput.Contains('trust=private-ca') -or
      -not $clientOutput.Contains('peer=original-destination') -or
      -not $proxyOutput.Contains('delivered-requests=1') -or
      -not $events.Contains("tls host=$hostName protocol=h3")) {
    throw (
        'The run did not prove WFP UDP/443 redirect into the ' +
        'HTTP/3 inspection endpoint.')
  }
  $html = @(
    Get-ChildItem -LiteralPath $LogDirectory -Filter '*.html' -File |
        Where-Object {
          $_.BaseName.EndsWith(
              ('-' + $hostName),
              [StringComparison]::OrdinalIgnoreCase)
        }
  )
  if ($html.Count -eq 0) {
    throw "No inspected HTML was logged for $hostName."
  }

  Write-Host $clientOutput.Trim()
  Write-Host (
      "WFP managed HTTP/3 redirect passed: $($html[0].FullName)")
} finally {
  if ($proxy -and -not $proxy.HasExited) {
    New-Item -ItemType File -Path $stopPath -Force `
        -ErrorAction SilentlyContinue | Out-Null
    if (-not $proxy.WaitForExit(5000)) {
      Stop-Process -Id $proxy.Id -Force -ErrorAction SilentlyContinue
    }
  }
}

Write-Host "WFP managed HTTP/3 logs: $LogDirectory"
