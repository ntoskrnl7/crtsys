[CmdletBinding()]
param(
  [string] $RepositoryRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
  $RepositoryRoot = Split-Path -Parent (
      Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
}

function Require-Token(
    [string] $Text,
    [string] $Token,
    [string] $Context) {
  if (-not $Text.Contains($Token)) {
    throw "$Context is missing '$Token'."
  }
}

function Assert-Parses([string] $Path) {
  $tokens = $null
  $errors = $null
  [void] [Management.Automation.Language.Parser]::ParseFile(
      $Path, [ref] $tokens, [ref] $errors)
  if ($errors.Count -ne 0) {
    throw "$Path has PowerShell parse errors: $($errors[0].Message)"
  }
}

$runner = Join-Path $RepositoryRoot (
    'test\net\kernel-contracts\Run-NtlNetKernelContracts.ps1')
$guard = Join-Path $RepositoryRoot (
    'test\wfp\runtime\common\DisposableGuestGuard.ps1')
$postcheck = Join-Path $RepositoryRoot (
    'test\common\Test-VmCrashPostcheck.ps1')
foreach ($path in @($runner, $guard, $postcheck)) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Required runtime file does not exist: $path"
  }
  Assert-Parses $path
}

$text = [IO.File]::ReadAllText($runner)
foreach ($token in @(
    'AllowDisposableGuestMutation',
    'DisposableGuestSentinelPath',
    'Assert-CrtSysDisposableGuest',
    'Test-VmCrashPostcheck.ps1',
    'EVENT_BASELINE_RECORD_ID',
    'DUMP_COUNT',
    'EVENT_LOG_RESET',
    'OperationTimeoutSeconds',
    'CaptureVerifierSettings')) {
  Require-Token $text $token 'kernel network VM runner'
}

foreach ($forbidden in @(
    'Get-WinEvent -FilterHashtable @{ StartTime',
    'verifier.exe @(''/reset''',
    'Restart-Computer',
    'Stop-Computer',
    'vmrun.exe reset',
    'vmrun.exe stop',
    'vmrun.exe start')) {
  if ($text.Contains($forbidden)) {
    throw "kernel network VM runner contains forbidden mutation '$forbidden'."
  }
}

Write-Host (
    'NTL kernel network runtime packaging contract passed: disposable-guest ' +
    'guard, bounded processes, RecordId/dump postcheck, read-only Verifier.')
