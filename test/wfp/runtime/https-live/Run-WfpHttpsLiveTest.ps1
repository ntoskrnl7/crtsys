[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $PackageRoot,

  [Parameter(Mandatory)]
  [ValidateNotNullOrEmpty()]
  [string] $HostName,

  [string] $ServiceName = 'CrtSysWfpHttpsLiveTest',

  [switch] $AllowUnavailableRevocation,

  [string] $LogPath =
      (Join-Path $PackageRoot 'https-live-host.log'),

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
    throw 'The live HTTPS test must run from an elevated PowerShell session.'
  }
}

function Remove-TestService([string] $Name) {
  & sc.exe query $Name *> $null
  if ($LASTEXITCODE -eq 0) {
    & sc.exe stop $Name *> $null
    & sc.exe delete $Name *> $null
  }
}

Assert-Administrator
$root = (Resolve-Path -LiteralPath $PackageRoot).Path
$driver = Join-Path $root 'crtsys_wfp_tls_inspection_proxy.sys'
$driverInf = Join-Path $root 'crtsys_wfp_tls_inspection_proxy.inf'
$application =
    Join-Path $root 'crtsys_wfp_tls_inspection_proxy_live_acceptance.exe'
foreach ($required in @($driver, $driverInf, $application)) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
    throw "Required live HTTPS artifact is missing: $required"
  }
}

$stdout = "$LogPath.stdout"
$stderr = "$LogPath.stderr"
Remove-Item -LiteralPath $stdout, $stderr -Force -ErrorAction SilentlyContinue
Remove-TestService $ServiceName

try {
  & sc.exe create $ServiceName type= kernel start= demand `
      binPath= $driver | ForEach-Object { Write-Host $_ }
  if ($LASTEXITCODE -ne 0) {
    throw 'Creating the live HTTPS test driver service failed.'
  }
  & sc.exe start $ServiceName | ForEach-Object { Write-Host $_ }
  if ($LASTEXITCODE -ne 0) {
    throw 'Starting the live HTTPS test driver failed.'
  }

  $applicationArguments = @('--inspect-host', $HostName)
  if ($AllowUnavailableRevocation) {
    $applicationArguments += '--allow-unavailable-revocation'
  }
  $process = Start-Process -FilePath $application `
      -ArgumentList $applicationArguments `
      -WindowStyle Hidden -Wait `
      -PassThru -RedirectStandardOutput $stdout `
      -RedirectStandardError $stderr
  $output = @(
    Get-Content -LiteralPath $stdout -ErrorAction SilentlyContinue
    Get-Content -LiteralPath $stderr -ErrorAction SilentlyContinue
  )
  $output | Set-Content -LiteralPath $LogPath -Encoding utf8
  $output | ForEach-Object { Write-Host $_ }
  $text = $output -join [Environment]::NewLine
  if ($process.ExitCode -ne 0 -or
      -not $text.Contains('NTL WFP live HTTPS inspection ok:')) {
    throw "The live HTTPS proof failed with exit code $($process.ExitCode)."
  }
} finally {
  Remove-TestService $ServiceName
  Remove-Item -LiteralPath $stdout, $stderr -Force -ErrorAction SilentlyContinue
}

Write-Host "Live HTTPS test passed. Log: $LogPath"
