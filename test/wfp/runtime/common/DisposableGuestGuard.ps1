Set-StrictMode -Version Latest

function Assert-CrtSysDisposableGuest {
  [CmdletBinding()]
  param(
    [switch] $AllowDisposableGuestMutation,

    [string] $SentinelPath =
        'C:\crtsys-disposable-test-guest.sentinel'
  )

  if (-not $AllowDisposableGuestMutation) {
    throw (
      'This acceptance suite installs test drivers or certificates. ' +
      'Run it only in a disposable guest and pass ' +
      '-AllowDisposableGuestMutation explicitly.')
  }

  # IsPathFullyQualified is unavailable in the Windows PowerShell 5.1 / .NET
  # Framework runtime used by the supported test guests. Reject drive-relative
  # forms such as C:sentinel explicitly instead of relying on IsPathRooted.
  $isAbsoluteWindowsPath =
      -not [string]::IsNullOrWhiteSpace($SentinelPath) -and
      ($SentinelPath -match '^(?:[A-Za-z]:[\\/]|\\\\[^\\/]+[\\/][^\\/]+[\\/])')
  if (-not $isAbsoluteWindowsPath) {
    throw 'DisposableGuestSentinelPath must be an absolute path.'
  }
  if (-not (Test-Path -LiteralPath $SentinelPath -PathType Leaf)) {
    throw (
      "Disposable-guest sentinel was not found: $SentinelPath. " +
      'Create it deliberately in the test guest; the runner never creates it.')
  }

  $sentinel = (Get-Content -LiteralPath $SentinelPath -Raw).Trim()
  if ($sentinel -cne 'CRTSYS_DISPOSABLE_TEST_GUEST') {
    throw (
      'Disposable-guest sentinel content is invalid. Expected exactly ' +
      'CRTSYS_DISPOSABLE_TEST_GUEST.')
  }
}
