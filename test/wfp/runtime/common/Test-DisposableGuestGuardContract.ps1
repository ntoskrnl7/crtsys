[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'DisposableGuestGuard.ps1')

function Assert-Throws {
  param(
    [Parameter(Mandatory)]
    [scriptblock] $Action,

    [Parameter(Mandatory)]
    [string] $ExpectedMessage
  )

  try {
    & $Action
  } catch {
    if ($_.Exception.Message.IndexOf(
        $ExpectedMessage, [StringComparison]::Ordinal) -lt 0) {
      throw (
        "Expected failure containing '$ExpectedMessage', got: " +
        $_.Exception.Message)
    }
    return
  }

  throw "Expected failure containing '$ExpectedMessage', but no error occurred."
}

$contractRoot = Join-Path (
    [IO.Path]::GetTempPath()) ('crtsys-disposable-guard-' + [guid]::NewGuid())
$sentinelPath = Join-Path $contractRoot 'guest.sentinel'
$missingPath = Join-Path $contractRoot 'missing.sentinel'

try {
  [void](New-Item -ItemType Directory -Path $contractRoot)
  Set-Content -LiteralPath $sentinelPath -NoNewline -Value (
      'CRTSYS_DISPOSABLE_TEST_GUEST')

  Assert-CrtSysDisposableGuest `
      -AllowDisposableGuestMutation `
      -SentinelPath $sentinelPath

  Assert-Throws -ExpectedMessage 'pass -AllowDisposableGuestMutation' -Action {
    Assert-CrtSysDisposableGuest -SentinelPath $sentinelPath
  }
  Assert-Throws -ExpectedMessage 'must be an absolute path' -Action {
    Assert-CrtSysDisposableGuest `
        -AllowDisposableGuestMutation `
        -SentinelPath 'guest.sentinel'
  }
  Assert-Throws -ExpectedMessage 'must be an absolute path' -Action {
    Assert-CrtSysDisposableGuest `
        -AllowDisposableGuestMutation `
        -SentinelPath 'C:guest.sentinel'
  }
  Assert-Throws -ExpectedMessage 'sentinel was not found' -Action {
    Assert-CrtSysDisposableGuest `
        -AllowDisposableGuestMutation `
        -SentinelPath $missingPath
  }

  Set-Content -LiteralPath $sentinelPath -NoNewline -Value 'NOT_A_TEST_GUEST'
  Assert-Throws -ExpectedMessage 'sentinel content is invalid' -Action {
    Assert-CrtSysDisposableGuest `
        -AllowDisposableGuestMutation `
        -SentinelPath $sentinelPath
  }
} finally {
  if (Test-Path -LiteralPath $contractRoot) {
    Remove-Item -LiteralPath $contractRoot -Recurse -Force
  }
}

Write-Host (
  'Disposable guest guard contract passed: explicit opt-in, absolute path, ' +
  'sentinel existence, and sentinel identity are enforced.')
