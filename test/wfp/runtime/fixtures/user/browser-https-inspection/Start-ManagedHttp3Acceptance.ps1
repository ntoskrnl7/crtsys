[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ServicePath,

  [Parameter(Mandatory = $true)]
  [string]$ClientPath,

  [Parameter(Mandatory = $true)]
  [uri]$Url,

  [Parameter(Mandatory = $true)]
  [ValidateRange(1, 65535)]
  [int]$ListenPort,

  [Parameter(Mandatory = $true)]
  [string]$LogDirectory,

  [int]$ReadyTimeoutSeconds = 30
)

$ErrorActionPreference = 'Stop'
$service = (Resolve-Path -LiteralPath $ServicePath).Path
$client = (Resolve-Path -LiteralPath $ClientPath).Path
$log = [System.IO.Path]::GetFullPath($LogDirectory)
[System.IO.Directory]::CreateDirectory($log) | Out-Null
$ready = Join-Path $log 'service.ready'
$stop = Join-Path $log 'stop.request'
$ca = Join-Path $log 'ntl-browser-inspection-ca.cer'
$body = Join-Path $log 'managed-http3-response.bin'
Remove-Item -LiteralPath $ready, $stop -Force -ErrorAction SilentlyContinue

function Quote-ProcessArgument([string]$Value) {
  return '"' + $Value.Replace('"', '\"') + '"'
}

$arguments = @(
  '--wfp-managed-http3-proxy',
  (Quote-ProcessArgument $client),
  $ListenPort.ToString([Globalization.CultureInfo]::InvariantCulture),
  (Quote-ProcessArgument $log)
)
$process = Start-Process -FilePath $service -ArgumentList $arguments `
  -PassThru -NoNewWindow

try {
  $deadline = [DateTime]::UtcNow.AddSeconds($ReadyTimeoutSeconds)
  while (-not (Test-Path -LiteralPath $ready)) {
    if ($process.HasExited) {
      throw "HTTP/3 proxy service exited before ready (exit $($process.ExitCode))."
    }
    if ([DateTime]::UtcNow -ge $deadline) {
      throw "Timed out waiting for HTTP/3 proxy service ready signal: $ready"
    }
    Start-Sleep -Milliseconds 50
  }

  & $client $Url.AbsoluteUri $body '--trust-ca' $ca
  if ($LASTEXITCODE -ne 0) {
    throw "Managed HTTP/3 traffic client failed with exit code $LASTEXITCODE."
  }
}
finally {
  New-Item -ItemType File -Path $stop -Force | Out-Null
  if (-not $process.WaitForExit(20000)) {
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    throw 'HTTP/3 proxy service did not stop after stop.request.'
  }
}

if ($process.ExitCode -ne 0) {
  throw "HTTP/3 proxy service failed with exit code $($process.ExitCode)."
}
if (-not (Test-Path -LiteralPath $body)) {
  throw "Managed HTTP/3 client did not produce response evidence: $body"
}

Write-Host "browser HTTPS managed HTTP/3 acceptance PASS: $body"
