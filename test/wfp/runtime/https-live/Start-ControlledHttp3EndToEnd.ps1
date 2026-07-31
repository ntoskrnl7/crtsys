[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $PackageRoot,

  [string] $LogDirectory =
      (Join-Path $PackageRoot 'controlled-http3-log'),

  [ValidateRange(2, 32)]
  [int] $Concurrency = 8
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath $PackageRoot).Path
$serverApplication =
    Join-Path $root 'crtsys_wfp_browser_https_inspection_app.exe'
$clientApplication =
    Join-Path $root 'crtsys_ntl_managed_http3_client.exe'
foreach ($required in @(
    $serverApplication, $clientApplication,
    (Join-Path $root 'msh3.dll'),
    (Join-Path $root 'msquic.dll'))) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
    throw "Required controlled HTTP/3 artifact is missing: $required"
  }
}

New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
$logRoot = (Resolve-Path -LiteralPath $LogDirectory).Path
$runDirectory = Join-Path $logRoot (
    'run-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') +
    '-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null

function Get-FreeUdpPort {
  $owner = [Net.Sockets.UdpClient]::new(
      [Net.IPEndPoint]::new([Net.IPAddress]::Loopback, 0))
  try {
    return ([Net.IPEndPoint] $owner.Client.LocalEndPoint).Port
  } finally {
    $owner.Dispose()
  }
}

function Get-RootCertificateSnapshot {
  return (@(
    Get-ChildItem Cert:\CurrentUser\Root, Cert:\LocalMachine\Root |
        ForEach-Object {
          "$($_.PSParentPath)|$($_.Thumbprint)"
        } |
        Sort-Object -Unique
  ) -join "`n")
}

function Get-ControlledKeySnapshot {
  param(
    [ValidateRange(0, [int]::MaxValue)]
    [int] $OwnerProcessId = 0
  )

  $entries = @(
    foreach ($scope in @('user', 'machine')) {
      $arguments =
          if ($scope -eq 'user') {
            @('-user', '-key')
          } else {
            @('-key')
          }
      & certutil.exe @arguments 2>&1 |
          Where-Object {
            "$_" -match (
                'crtsys-ntl-(controlled|' +
                'wfp-tls-)')
          } |
          ForEach-Object {
            $line = "$_".Trim()
            if ($OwnerProcessId -eq 0 -or
                $line.Contains("-$OwnerProcessId-")) {
              "$scope|$line"
            }
          }
    }
  )
  return (@($entries | Sort-Object -Unique) -join "`n")
}

function Get-WfpServiceSnapshot {
  return (@(
    Get-Service -Name 'CrtSysWfpHttpsLiveTest',
        'CrtSysWfpBrowserHttpsInspection' `
        -ErrorAction SilentlyContinue |
        ForEach-Object {
          "$($_.Name)|$($_.Status)|$($_.StartType)"
        } |
        Sort-Object
  ) -join "`n")
}

function Start-ManagedClient {
  param(
    [Parameter(Mandatory)]
    [string] $Name,
    [Parameter(Mandatory)]
    [string] $Url,
    [Parameter(Mandatory)]
    [string] $Authority,
    [Parameter(Mandatory)]
    [int] $Port
  )

  $stdout = Join-Path $runDirectory "$Name.stdout.log"
  $stderr = Join-Path $runDirectory "$Name.stderr.log"
  $body = Join-Path $runDirectory "$Name.response.bin"
  $process = Start-Process -FilePath $clientApplication `
      -ArgumentList @(
        "`"$Url`"", "`"$body`"", "$Port", "`"$Authority`""
      ) -WorkingDirectory $root -WindowStyle Hidden -PassThru `
      -RedirectStandardOutput $stdout `
      -RedirectStandardError $stderr
  return [pscustomobject]@{
    Name = $Name
    Process = $process
    Stdout = $stdout
    Stderr = $stderr
    Body = $body
  }
}

function Complete-ManagedClient {
  param(
    [Parameter(Mandatory)]
    $Invocation,
    [Parameter(Mandatory)]
    [int] $ExpectedExitCode,
    [Parameter(Mandatory)]
    [AllowEmptyString()]
    [string] $ExpectedStatus
  )

  if (-not $Invocation.Process.WaitForExit(90000)) {
    Stop-Process -Id $Invocation.Process.Id -Force `
        -ErrorAction SilentlyContinue
    throw "Managed HTTP/3 client timed out: $($Invocation.Name)"
  }
  $Invocation.Process.WaitForExit()
  $stdout = if (Test-Path -LiteralPath $Invocation.Stdout) {
    [IO.File]::ReadAllText($Invocation.Stdout)
  } else {
    ''
  }
  $stderr = if (Test-Path -LiteralPath $Invocation.Stderr) {
    [IO.File]::ReadAllText($Invocation.Stderr)
  } else {
    ''
  }
  $reportedExitCode = $Invocation.Process.ExitCode
  if ($null -ne $reportedExitCode -and
      $reportedExitCode -ne $ExpectedExitCode) {
    throw (
        "Managed HTTP/3 client $($Invocation.Name) exited with " +
        "$reportedExitCode, expected $ExpectedExitCode. " +
        "stderr=$stderr")
  }
  if ($ExpectedExitCode -eq 0 -and
      (-not $stdout.Contains('protocol=h3') -or
       -not $stdout.Contains('trust=private-ca') -or
       -not $stdout.Contains("status=$ExpectedStatus") -or
       -not [string]::IsNullOrWhiteSpace($stderr))) {
    throw (
        "Managed HTTP/3 client $($Invocation.Name) did not report " +
        "the expected H3 status $ExpectedStatus.")
  }
  if ($ExpectedExitCode -ne 0 -and
      ($stdout.Contains(
          'NTL managed HTTP/3 client passed:') -or
       -not $stderr.Contains(
           'NTL managed HTTP/3 client failed:'))) {
    throw (
        "Managed HTTP/3 client $($Invocation.Name) did not " +
        'report the expected trust failure.')
  }
  return [pscustomobject]@{
    Stdout = $stdout
    Stderr = $stderr
    Body = $Invocation.Body
  }
}

$testMutex = [Threading.Mutex]::new(
    $false, 'Local\CrtSys-ControlledHttp3-EndToEnd')
$mutexAcquired = $false
$server = $null
$stopPath = $null

try {
  try {
    $mutexAcquired = $testMutex.WaitOne([TimeSpan]::FromMinutes(5))
  } catch [Threading.AbandonedMutexException] {
    $mutexAcquired = $true
  }
  if (-not $mutexAcquired) {
    throw 'Timed out waiting for another controlled HTTP/3 run.'
  }

  # The certificate stores and persisted Schannel key containers inspected
  # below are process-external resources.  Serialize the complete snapshot /
  # exercise / cleanup transaction so independent CTest configurations cannot
  # mistake another run's temporary key for a leak.
  $rootsBefore = Get-RootCertificateSnapshot
  $servicesBefore = Get-WfpServiceSnapshot
  $proxyPort = Get-FreeUdpPort
  do {
    $originPort = Get-FreeUdpPort
  } while ($originPort -eq $proxyPort)

  $serverStdout = Join-Path $runDirectory 'server.stdout.log'
  $serverStderr = Join-Path $runDirectory 'server.stderr.log'
  $inspectionAuthority =
      Join-Path $runDirectory 'ntl-browser-inspection-ca.cer'
  $originAuthority =
      Join-Path $runDirectory 'ntl-controlled-origin-ca.cer'
  $stopPath = Join-Path $runDirectory 'stop.request'

  $server = Start-Process -FilePath $serverApplication `
      -ArgumentList @(
        '--controlled-http3-e2e', "$proxyPort", "$originPort",
        "`"$runDirectory`"", '0'
      ) -WorkingDirectory $root -WindowStyle Hidden -PassThru `
      -RedirectStandardOutput $serverStdout `
      -RedirectStandardError $serverStderr
  $serverProcessId = $server.Id

  $readyDeadline = (Get-Date).AddSeconds(30)
  $ready = $false
  do {
    if ($server.HasExited) {
      $server.WaitForExit()
      $errorText = if (Test-Path -LiteralPath $serverStderr) {
        [IO.File]::ReadAllText($serverStderr)
      } else {
        ''
      }
      throw (
          "The controlled HTTP/3 server exited with " +
          "$($server.ExitCode): $errorText")
    }
    if ((Test-Path -LiteralPath $inspectionAuthority -PathType Leaf) -and
        (Test-Path -LiteralPath $originAuthority -PathType Leaf) -and
        (Test-Path -LiteralPath $serverStdout -PathType Leaf)) {
      $readyText = Get-Content -LiteralPath $serverStdout -Raw
      $ready =
          $readyText.Contains('NTL controlled HTTP/3 ready:') -and
          $readyText.Contains(
              "client-peer=127.0.0.1:$proxyPort") -and
          $readyText.Contains(
              "origin-peer=127.0.0.1:$originPort") -and
          $readyText.Contains(
              'downstream=h3, upstream=h3')
    }
    if (-not $ready) {
      Start-Sleep -Milliseconds 100
    }
  } while (-not $ready -and (Get-Date) -lt $readyDeadline)
  if (-not $ready) {
    throw 'The controlled HTTP/3 topology did not become ready.'
  }

  $positivePaths = @(
    'identity', 'gzip', 'deflate', 'br', 'delay')
  foreach ($path in $positivePaths) {
    $invocation = Start-ManagedClient `
        -Name "positive-$path" `
        -Url "https://controlled-h3.test/$path" `
        -Authority $inspectionAuthority -Port $proxyPort
    $completed = Complete-ManagedClient `
        -Invocation $invocation -ExpectedExitCode 0 `
        -ExpectedStatus '200'
    if ($path -eq 'identity') {
      $identity = [IO.File]::ReadAllText($completed.Body)
      if (-not $identity.Contains('ntl-controlled-h3') -or
          -not $identity.Contains(
              'client-h3 inspection-proxy-h3 origin-h3')) {
        throw 'The controlled identity response was not preserved.'
      }
    }
  }

  $parallel = @()
  for ($index = 0; $index -lt $Concurrency; ++$index) {
    $parallel += Start-ManagedClient `
        -Name ("parallel-{0:D2}" -f $index) `
        -Url 'https://controlled-h3.test/delay' `
        -Authority $inspectionAuthority -Port $proxyPort
  }
  foreach ($invocation in $parallel) {
    [void](Complete-ManagedClient `
        -Invocation $invocation -ExpectedExitCode 0 `
        -ExpectedStatus '200')
  }

  $oversized = Start-ManagedClient `
      -Name 'bounded-oversized' `
      -Url 'https://controlled-h3.test/oversized' `
      -Authority $inspectionAuthority -Port $proxyPort
  [void](Complete-ManagedClient `
      -Invocation $oversized -ExpectedExitCode 0 `
      -ExpectedStatus '502')

  $wrongHost = Start-ManagedClient `
      -Name 'policy-wrong-host' `
      -Url 'https://wrong-controlled-h3.test/identity' `
      -Authority $inspectionAuthority -Port $proxyPort
  [void](Complete-ManagedClient `
      -Invocation $wrongHost -ExpectedExitCode 0 `
      -ExpectedStatus '421')

  $wrongAuthority = Start-ManagedClient `
      -Name 'trust-wrong-ca' `
      -Url 'https://controlled-h3.test/identity' `
      -Authority $originAuthority -Port $proxyPort
  [void](Complete-ManagedClient `
      -Invocation $wrongAuthority -ExpectedExitCode 1 `
      -ExpectedStatus '')

  New-Item -ItemType File -Path $stopPath -Force | Out-Null
  if (-not $server.WaitForExit(30000)) {
    throw 'The controlled HTTP/3 topology did not stop cleanly.'
  }
  $server.WaitForExit()
  $serverError = [IO.File]::ReadAllText($serverStderr)
  if (($null -ne $server.ExitCode -and
       $server.ExitCode -ne 0) -or
      -not [string]::IsNullOrWhiteSpace($serverError)) {
    throw (
        "The controlled HTTP/3 topology exited with " +
        "$($server.ExitCode): $serverError")
  }

  $expectedRequests = $Concurrency + 7
  $expectedHtml = $Concurrency + 5
  $serverOutput =
      Get-Content -LiteralPath $serverStdout -Raw
  $proxyEvents =
      Get-Content -LiteralPath (
          Join-Path $runDirectory 'proxy\events.log') -Raw
  $originEvents =
      Get-Content -LiteralPath (
          Join-Path $runDirectory 'origin\events.log') -Raw
  if (-not $serverOutput.Contains(
          "proxy-requests=$expectedRequests") -or
      -not $serverOutput.Contains(
          "origin-requests=$expectedRequests") -or
      -not $serverOutput.Contains(
          'downstream=h3, upstream=h3') -or
      -not $proxyEvents.Contains(
          'encoding=gzip') -or
      -not $proxyEvents.Contains(
          'encoding=deflate') -or
      -not $proxyEvents.Contains(
          'encoding=br') -or
      -not $originEvents.Contains(
          'tls host=controlled-h3.test protocol=h3')) {
    throw (
        'The controlled run did not prove both H3 legs, bounded ' +
        'content decoding, and the expected request count.')
  }

  $proxyHtml = @(
    Get-ChildItem -LiteralPath (
        Join-Path $runDirectory 'proxy') -Filter '*.html' -File)
  if ($proxyHtml.Count -ne $expectedHtml) {
    throw (
        "Expected $expectedHtml inspected proxy HTML files, " +
        "found $($proxyHtml.Count).")
  }
  foreach ($html in $proxyHtml) {
    $text = Get-Content -LiteralPath $html.FullName -Raw
    if (-not $text.Contains('ntl-controlled-h3')) {
      throw "Decoded HTML marker is missing: $($html.FullName)"
    }
  }

  $rootsAfter = Get-RootCertificateSnapshot
  # Other configurations may concurrently create their own short-lived
  # test keys.  Every key created by this topology contains its server PID,
  # so scope the leak check to this process instead of comparing the global
  # key-container inventory.
  $keysAfter = Get-ControlledKeySnapshot `
      -OwnerProcessId $serverProcessId
  $servicesAfter = Get-WfpServiceSnapshot
  if ($rootsAfter -cne $rootsBefore) {
    throw 'A Windows root certificate store changed during the run.'
  }
  if (-not [string]::IsNullOrEmpty($keysAfter)) {
    throw 'A controlled HTTP/3 private key remained after shutdown.'
  }
  if ($servicesAfter -cne $servicesBefore) {
    throw 'A WFP service changed during the driverless run.'
  }

  @(
    'CONTROLLED_HTTP3=PASS'
    'CLIENT_TO_PROXY_H3=PASS'
    'PROXY_TO_ORIGIN_H3=PASS'
    'PRIVATE_CA_EXACT_TRUST=PASS'
    'WRONG_CA_REJECTED=PASS'
    'HOST_POLICY=PASS'
    'GZIP_DECODE=PASS'
    'DEFLATE_DECODE=PASS'
    'BROTLI_DECODE=PASS'
    'UPSTREAM_BODY_BOUND=PASS'
    "CONCURRENCY_$Concurrency=PASS"
    'ROOT_STORE_CHANGED=NO'
    'PERSISTENT_KEYS=NO'
    'WFP_DRIVER_USED=NO'
    'REBOOT_REQUIRED=NO'
    'PASS'
  ) | Set-Content -LiteralPath (
      Join-Path $runDirectory 'acceptance.result.txt') -Encoding ASCII

  Write-Host (
      "Controlled HTTP/3 end-to-end passed: " +
      "requests=$expectedRequests, html=$expectedHtml, " +
      "concurrency=$Concurrency")
  Write-Host (
      'Transport proof: client=h3, inspection=h3, origin=h3')
  Write-Host (
      'Persistent changes: root-store=none, keys=none, ' +
      'WFP-driver=not-used, reboot=no')
} finally {
  if ($server -and -not $server.HasExited) {
    if ($stopPath) {
      New-Item -ItemType File -Path $stopPath -Force `
          -ErrorAction SilentlyContinue | Out-Null
    }
    if (-not $server.WaitForExit(10000)) {
      Stop-Process -Id $server.Id -Force `
          -ErrorAction SilentlyContinue
    }
  }
  if ($mutexAcquired) {
    $testMutex.ReleaseMutex()
  }
  $testMutex.Dispose()
}

Write-Host "Controlled HTTP/3 evidence: $runDirectory"
