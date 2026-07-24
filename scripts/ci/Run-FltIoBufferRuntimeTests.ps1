param(
  [string] $AppPath =
    'test\flt\runtime\build_x64\Debug\crtsys_flt_io_buffer_runtime_test_app.exe',
  [string] $ServiceName = 'CrtSysFltIoBufferRuntimeTest',
  [string] $DriverName = 'crtsys_flt_io_buffer_runtime_test.sys',
  [switch] $RequireVerifier
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Administrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  if (-not $principal.IsInRole(
      [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Minifilter runtime tests must run from an elevated test environment.'
  }
}

function Assert-TestSigningEnabled {
  $bcdOutput = & bcdedit.exe /enum '{current}' 2>&1
  if ($LASTEXITCODE -ne 0) {
    throw "bcdedit failed: $($bcdOutput -join [Environment]::NewLine)"
  }

  $testSigningLine = $bcdOutput |
    Where-Object { $_ -match '^\s*testsigning\s+' } |
    Select-Object -First 1
  if (-not $testSigningLine -or
      $testSigningLine -notmatch '(?i)\b(yes|on|true)\b') {
    throw 'TESTSIGNING is not enabled on the minifilter test machine.'
  }
}

function Assert-DriverVerifierTarget {
  param(
    [Parameter(Mandatory = $true)]
    [string] $Name
  )

  $settings = & verifier.exe /querysettings 2>&1
  if ($LASTEXITCODE -ne 0) {
    throw "verifier.exe /querysettings failed: $($settings -join [Environment]::NewLine)"
  }
  if (($settings -join [Environment]::NewLine) -notmatch
      [regex]::Escape($Name)) {
    throw "Driver Verifier is not configured for '$Name'."
  }
}

Assert-Administrator
Assert-TestSigningEnabled

$resolvedApp = (Resolve-Path $AppPath).Path
& sc.exe query $ServiceName | Out-Null
if ($LASTEXITCODE -ne 0) {
  throw "The installed minifilter service '$ServiceName' was not found."
}
if ($RequireVerifier) {
  Assert-DriverVerifierTarget -Name $DriverName
}

& $resolvedApp
if ($LASTEXITCODE -ne 0) {
  throw "The minifilter I/O buffer runtime app failed with exit code $LASTEXITCODE."
}

$loadedFilters = & fltmc.exe filters 2>&1
if ($LASTEXITCODE -ne 0) {
  throw "fltmc filters failed: $($loadedFilters -join [Environment]::NewLine)"
}
if (($loadedFilters -join [Environment]::NewLine) -match
    [regex]::Escape($ServiceName)) {
  throw "The runtime app left '$ServiceName' loaded after completion."
}

Write-Host 'NTL minifilter I/O buffer runtime and unload verification passed.'
