param(
  [string] $DriverPath = 'test\cmake\driver\build_x64\Debug\crtsys_test.sys',
  [string] $AppPath = 'test\cmake\app\build_x64\Debug\crtsys_test_app.exe',
  [string] $ServiceName = 'CrtSysTest',
  [switch] $RequireVerifier
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Administrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Driver load tests must run from an elevated self-hosted runner service.'
  }
}

function Assert-TestSigningEnabled {
  $bcdOutput = & bcdedit.exe /enum '{current}' 2>&1
  if ($LASTEXITCODE -ne 0) {
    throw "bcdedit failed: $($bcdOutput -join [Environment]::NewLine)"
  }

  $testSigningLine = $bcdOutput | Where-Object { $_ -match '^\s*testsigning\s+' } | Select-Object -First 1
  if (-not $testSigningLine -or $testSigningLine -notmatch '(?i)\b(yes|on|true)\b') {
    throw 'TESTSIGNING is not enabled. Run "bcdedit /set TESTSIGNING ON", disable Secure Boot if needed, reboot, and retry on this self-hosted runner.'
  }
}

function Assert-DriverVerifierTarget {
  param(
    [Parameter(Mandatory = $true)]
    [string] $DriverName
  )

  $settings = & verifier.exe /querysettings 2>&1
  if ($LASTEXITCODE -ne 0) {
    throw "verifier.exe /querysettings failed: $($settings -join [Environment]::NewLine)"
  }
  if (($settings -join [Environment]::NewLine) -notmatch
      [regex]::Escape($DriverName)) {
    throw "Driver Verifier is not configured for '$DriverName'. Configure it and reboot before running with -RequireVerifier."
  }
}

function Invoke-Sc {
  param(
    [Parameter(Mandatory = $true)]
    [string[]] $Arguments,

    [switch] $AllowFailure
  )

  & sc.exe @Arguments
  $exitCode = $LASTEXITCODE
  if ($exitCode -ne 0 -and -not $AllowFailure) {
    throw "sc.exe $($Arguments -join ' ') failed with exit code $exitCode."
  }
  return $exitCode
}

Assert-Administrator
Assert-TestSigningEnabled

$resolvedDriverPath = (Resolve-Path $DriverPath).Path
$resolvedAppPath = (Resolve-Path $AppPath).Path
if ($RequireVerifier) {
  Assert-DriverVerifierTarget -DriverName (
    [System.IO.Path]::GetFileName($resolvedDriverPath))
}

$queryExitCode = Invoke-Sc -Arguments @('query', $ServiceName) -AllowFailure
if ($queryExitCode -eq 0) {
  Invoke-Sc -Arguments @('stop', $ServiceName) -AllowFailure | Out-Null
  Start-Sleep -Seconds 2
  Invoke-Sc -Arguments @('delete', $ServiceName) -AllowFailure | Out-Null
  Start-Sleep -Seconds 2
}

try {
  Invoke-Sc -Arguments @(
    'create', $ServiceName,
    'type=', 'kernel',
    'start=', 'demand',
    'binPath=', $resolvedDriverPath,
    'DisplayName=', 'crtsys test'
  ) | Out-Null

  Invoke-Sc -Arguments @('start', $ServiceName) | Out-Null

  & $resolvedAppPath
  if ($LASTEXITCODE -ne 0) {
    throw "crtsys_test_app.exe failed with exit code $LASTEXITCODE."
  }
} finally {
  Invoke-Sc -Arguments @('stop', $ServiceName) -AllowFailure | Out-Null
  Start-Sleep -Seconds 2
  Invoke-Sc -Arguments @('delete', $ServiceName) -AllowFailure | Out-Null
}
