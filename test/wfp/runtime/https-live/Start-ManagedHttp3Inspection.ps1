[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $PackageRoot,

  [Parameter(Mandatory)]
  [uri] $Url,

  [string] $LogDirectory =
      (Join-Path $PackageRoot 'managed-http3-log')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($Url.Scheme -ne 'https' -or $Url.Port -ne 443) {
  throw 'The managed client URL must use HTTPS port 443.'
}

$root = (Resolve-Path -LiteralPath $PackageRoot).Path
$proxyApplication =
    Join-Path $root (
        'crtsys_wfp_browser_https_inspection_' +
        'http3_proxy_service.exe')
$clientApplication =
    Join-Path $root (
        'crtsys_wfp_browser_https_inspection_' +
        'managed_client_acceptance.exe')
$unrelatedAuthority =
    Join-Path $root 'crtsys_wfp_browser_https_inspection.cer'
foreach ($required in @(
    $proxyApplication, $clientApplication, $unrelatedAuthority,
    (Join-Path $root 'msh3.dll'),
    (Join-Path $root 'msquic.dll'))) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
    throw "Required managed HTTP/3 artifact is missing: $required"
  }
}

New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
$LogDirectory = (Resolve-Path -LiteralPath $LogDirectory).Path
$proxyStdout = Join-Path $LogDirectory 'proxy.stdout.log'
$proxyStderr = Join-Path $LogDirectory 'proxy.stderr.log'
$clientStdout = Join-Path $LogDirectory 'client.stdout.log'
$clientStderr = Join-Path $LogDirectory 'client.stderr.log'
$clientBody = Join-Path $LogDirectory 'client-response.bin'
$rejectedClientStdout =
    Join-Path $LogDirectory 'client-unrelated-ca.stdout.log'
$rejectedClientStderr =
    Join-Path $LogDirectory 'client-unrelated-ca.stderr.log'
$rejectedClientBody =
    Join-Path $LogDirectory 'client-unrelated-ca-response.bin'
$caPath = Join-Path $LogDirectory 'ntl-browser-inspection-ca.cer'
$stopPath = Join-Path $LogDirectory 'stop.request'
$eventsPath = Join-Path $LogDirectory 'events.log'
$proxy = $null

Remove-Item -LiteralPath $proxyStdout, $proxyStderr,
    $clientStdout, $clientStderr, $clientBody, $caPath,
    $rejectedClientStdout, $rejectedClientStderr,
    $rejectedClientBody,
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
        '--managed-http3-proxy',
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
      throw "The managed HTTP/3 proxy exited with $($proxy.ExitCode)."
    }
    $ready =
        (Test-Path -LiteralPath $caPath -PathType Leaf) -and
        (Test-Path -LiteralPath $proxyStdout -PathType Leaf)
    if ($ready) {
      $readyText =
          Get-Content -LiteralPath $proxyStdout -Raw
      $ready =
          $readyText -like (
              "*NTL managed HTTP/3 inspection ready: " +
              "listen=127.0.0.1:$inspectionPort*")
    }
    if (-not $ready) {
      Start-Sleep -Milliseconds 100
    }
  } while (-not $ready -and (Get-Date) -lt $readyDeadline)
  if (-not $ready) {
    throw 'The managed HTTP/3 proxy did not become ready.'
  }

  $client = Start-Process -FilePath $clientApplication `
      -ArgumentList @(
        "`"$($Url.AbsoluteUri)`"",
        "`"$clientBody`"",
        "$inspectionPort",
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
        "The managed HTTP/3 client exited with " +
        "$($client.ExitCode): $clientError")
  }

  $rejectedClient = Start-Process -FilePath $clientApplication `
      -ArgumentList @(
        "`"$($Url.AbsoluteUri)`"",
        "`"$rejectedClientBody`"",
        "$inspectionPort",
        "`"$unrelatedAuthority`""
      ) -WindowStyle Hidden -PassThru -Wait `
      -RedirectStandardOutput $rejectedClientStdout `
      -RedirectStandardError $rejectedClientStderr
  if ($rejectedClient.ExitCode -eq 0) {
    throw 'The managed HTTP/3 client accepted an unrelated CA.'
  }

  New-Item -ItemType File -Path $stopPath -Force | Out-Null
  if (-not $proxy.WaitForExit(20000)) {
    throw 'The managed HTTP/3 proxy did not stop cleanly.'
  }
  $proxy.WaitForExit()

  $clientOutput = Get-Content -LiteralPath $clientStdout -Raw
  $proxyOutput = Get-Content -LiteralPath $proxyStdout -Raw
  $events = Get-Content -LiteralPath $eventsPath -Raw
  $hostName = $Url.DnsSafeHost
  if (-not $clientOutput.Contains('protocol=h3') -or
      -not $clientOutput.Contains('trust=private-ca') -or
      -not $proxyOutput.Contains('delivered-requests=1') -or
      -not $events.Contains("tls host=$hostName protocol=h3")) {
    throw 'The run did not prove an inspected HTTP/3 request.'
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
      'Managed HTTP/3 trust rejection passed: unrelated-ca')
  Write-Host (
      "Managed HTTP/3 inspection passed: $($html[0].FullName)")
} finally {
  if ($proxy -and -not $proxy.HasExited) {
    New-Item -ItemType File -Path $stopPath -Force `
        -ErrorAction SilentlyContinue | Out-Null
    if (-not $proxy.WaitForExit(5000)) {
      Stop-Process -Id $proxy.Id -Force -ErrorAction SilentlyContinue
    }
  }
}

Write-Host "Managed HTTP/3 logs: $LogDirectory"
