[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $VmxPath,

  [Parameter(Mandatory)]
  [Security.SecureString] $VmPassword,

  [string] $GuestUser = 'test',

  [Parameter(Mandatory)]
  [Security.SecureString] $GuestPassword,

  [string] $VmrunPath =
      'C:\Program Files\VMware\VMware Workstation\vmrun.exe',

  [string] $GuestRoot = 'C:\crtsys-wfp-https',

  [string] $DisposableGuestSentinelPath =
      'C:\crtsys-disposable-test-guest.sentinel',

  [string] $StagingRoot = '',

  [string] $LogRoot = '',

  [Parameter(Mandatory)]
  [ValidateNotNullOrEmpty()]
  [string] $HostName,

  [switch] $AllowUnavailableRevocation,

  [Parameter(Mandatory)]
  [uri] $BrowserUrl,

  [ValidateRange(10, 600)]
  [int] $BrowserDurationSeconds = 60,

  [switch] $RequireQuicBlockedFallback = $true,

  [switch] $IncludeManagedHttp3,

  [switch] $SkipControlledHost
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$crashPostcheckScript = Join-Path $PSScriptRoot (
    '..\..\..\common\Test-VmCrashPostcheck.ps1')
if ([string]::IsNullOrWhiteSpace($StagingRoot)) {
  $StagingRoot = Join-Path $repoRoot 'artifacts\wfp-https-live-staging'
}
if ([string]::IsNullOrWhiteSpace($LogRoot)) {
  $LogRoot = Join-Path $repoRoot 'artifacts\wfp-https-vm-acceptance'
}

function ConvertTo-PlainText([Security.SecureString] $Value) {
  $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Value)
  try {
    return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
  } finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
  }
}

function ConvertTo-PowerShellLiteral([string] $Value) {
  return "'" + ($Value -replace "'", "''") + "'"
}

if (-not (Test-Path -LiteralPath $VmrunPath -PathType Leaf)) {
  throw "vmrun.exe was not found: $VmrunPath"
}
if (-not (Test-Path -LiteralPath $VmxPath -PathType Leaf)) {
  throw "VMX file was not found: $VmxPath"
}
if (-not (Test-Path -LiteralPath $StagingRoot -PathType Container)) {
  throw "The HTTPS staging package was not found: $StagingRoot"
}
if (-not (Test-Path -LiteralPath $crashPostcheckScript -PathType Leaf)) {
  throw "The VM crash postcheck script was not found: $crashPostcheckScript"
}
if ($BrowserUrl.Scheme -ne 'https') {
  throw 'BrowserUrl must use HTTPS.'
}

$artifactsRoot =
    [IO.Path]::GetFullPath((Join-Path $repoRoot 'artifacts')).TrimEnd('\') + '\'
$resolvedLogRoot = [IO.Path]::GetFullPath($LogRoot)
if (-not ($resolvedLogRoot.TrimEnd('\') + '\').StartsWith(
    $artifactsRoot, [StringComparison]::OrdinalIgnoreCase)) {
  throw "LogRoot must stay under $artifactsRoot"
}
if (Test-Path -LiteralPath $resolvedLogRoot) {
  Remove-Item -LiteralPath $resolvedLogRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $resolvedLogRoot -Force | Out-Null

$vmPasswordText = ConvertTo-PlainText $VmPassword
$guestPasswordText = ConvertTo-PlainText $GuestPassword

function Invoke-Vmrun {
  param(
    [Parameter(Mandatory)]
    [string[]] $Arguments,
    [switch] $Guest,
    [switch] $AllowFailure,
    [switch] $Quiet
  )

  $prefix = @('-T', 'ws')
  if ($vmPasswordText.Length -ne 0) {
    $prefix += @('-vp', $vmPasswordText)
  }
  if ($Guest) {
    $prefix += @('-gu', $GuestUser, '-gp', $guestPasswordText)
  }
  $allArguments = $prefix + $Arguments
  $output = @(& $VmrunPath @allArguments 2>&1)
  $exitCode = $LASTEXITCODE
  if (-not $Quiet) {
    $output | ForEach-Object { Write-Host $_ }
  }
  if ($exitCode -ne 0 -and -not $AllowFailure) {
    $displayArguments =
        for ($index = 0; $index -lt $allArguments.Count; ++$index) {
          if ($index -gt 0 -and
              $allArguments[$index - 1] -in @('-vp', '-gp')) {
            '<redacted>'
          } else {
            $allArguments[$index]
          }
        }
    throw "vmrun $($displayArguments -join ' ') failed with exit code $exitCode."
  }
  return [pscustomobject]@{ ExitCode = $exitCode; Output = $output }
}

function Invoke-GuestScript {
  param(
    [Parameter(Mandatory)]
    [string] $Script,
    [switch] $AllowFailure,
    [switch] $Quiet
  )
  $encoded = [Convert]::ToBase64String(
      [Text.Encoding]::Unicode.GetBytes($Script))
  return Invoke-Vmrun -Guest -AllowFailure:$AllowFailure -Quiet:$Quiet `
      -Arguments @(
        'runProgramInGuest', $VmxPath,
        'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
        '-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-EncodedCommand', $encoded
      )
}

function Copy-ToGuest([string] $Source, [string] $Destination) {
  Invoke-Vmrun -Guest -Quiet -Arguments @(
    'copyFileFromHostToGuest', $VmxPath, $Source, $Destination
  ) | Out-Null
}

function Copy-FromGuest([string] $Source, [string] $Destination) {
  Invoke-Vmrun -Guest -Quiet -Arguments @(
    'copyFileFromGuestToHost', $VmxPath, $Source, $Destination
  ) | Out-Null
}

function Wait-GuestReady {
  for ($attempt = 1; $attempt -le 90; ++$attempt) {
    $result = Invoke-GuestScript -Script 'exit 0' -AllowFailure -Quiet
    if ($result.ExitCode -eq 0) {
      Write-Host "Guest operations ready after attempt $attempt."
      return
    }
    Start-Sleep -Seconds 2
  }
  throw 'Guest operations did not become ready within 180 seconds.'
}

Wait-GuestReady

$guestRootLiteral = ConvertTo-PowerShellLiteral $GuestRoot
$guestParent = Split-Path -Parent $GuestRoot
if ([string]::IsNullOrWhiteSpace($guestParent) -or
    $GuestRoot.Length -le 3 -or
    -not $GuestRoot.StartsWith('C:\', [StringComparison]::OrdinalIgnoreCase)) {
  throw 'GuestRoot must be a specific directory below C:\.'
}

Invoke-GuestScript -Script @"
`$root = $guestRootLiteral
if (`$root -ne 'C:\' -and `$root.Length -gt 3 -and
    (Test-Path -LiteralPath `$root)) {
  Remove-Item -LiteralPath `$root -Recurse -Force
}
New-Item -ItemType Directory -Path `$root -Force | Out-Null
"@ | Out-Null

foreach ($file in Get-ChildItem -LiteralPath $StagingRoot -File) {
  Copy-ToGuest $file.FullName (Join-Path $GuestRoot $file.Name)
}
Copy-ToGuest $crashPostcheckScript (
    Join-Path $GuestRoot 'Test-VmCrashPostcheck.ps1')

$hostNameLiteral = ConvertTo-PowerShellLiteral $HostName
$allowUnavailableRevocationLiteral =
    if ($AllowUnavailableRevocation) { '$true' } else { '$false' }
$requireQuicBlockedFallbackLiteral =
    if ($RequireQuicBlockedFallback) { '$true' } else { '$false' }
$includeManagedHttp3Literal =
    if ($IncludeManagedHttp3) { '$true' } else { '$false' }
$skipControlledHostLiteral =
    if ($SkipControlledHost) { '$true' } else { '$false' }
$browserUrlLiteral = ConvertTo-PowerShellLiteral $BrowserUrl.AbsoluteUri
$sentinelLiteral =
    ConvertTo-PowerShellLiteral $DisposableGuestSentinelPath
$guestLog = Join-Path $GuestRoot 'vm-acceptance.log'
$guestEvidence = Join-Path $GuestRoot 'vm-acceptance-evidence.zip'
$guestLogLiteral = ConvertTo-PowerShellLiteral $guestLog
$guestEvidenceLiteral = ConvertTo-PowerShellLiteral $guestEvidence

$guestProgram = @"
Set-StrictMode -Version Latest
`$ErrorActionPreference = 'Stop'
`$root = $guestRootLiteral
`$log = $guestLogLiteral
`$evidence = Join-Path `$root 'evidence'
`$crashPostcheck = Join-Path `$root 'Test-VmCrashPostcheck.ps1'
`$eventBaseline = Join-Path `$root 'crash-event-baseline.txt'
`$dumpBaseline = Join-Path `$root 'crash-dump-baseline.txt'
`$postcheck = Join-Path `$root 'postcheck.txt'
`$sentinelPath = $sentinelLiteral
if (-not (Test-Path -LiteralPath `$sentinelPath -PathType Leaf) -or
    (Get-Content -LiteralPath `$sentinelPath -Raw).Trim() -cne
        'CRTSYS_DISPOSABLE_TEST_GUEST') {
  throw (
    'The disposable-guest sentinel is missing or invalid. The VM runner ' +
    'never creates it because that decision belongs to the operator.')
}
& `$crashPostcheck -EventBaselinePath `$eventBaseline `
    -DumpBaselinePath `$dumpBaseline -CaptureBaseline
`$installed = [Collections.Generic.List[string]]::new()
function Get-DriverVerifierSnapshot {
  return (@(
    & verifier.exe /querysettings 2>&1 |
        ForEach-Object { "`$_".TrimEnd() }
  ) -join "``n").Trim()
}
`$verifierBefore = Get-DriverVerifierSnapshot
try {
  New-Item -ItemType Directory -Path `$evidence -Force | Out-Null
  foreach (`$certificatePath in @(Get-ChildItem -LiteralPath `$root -Filter 'crtsys_wfp_*.cer' -File)) {
    `$certificate =
        [Security.Cryptography.X509Certificates.X509Certificate2]::new(
            `$certificatePath.FullName)
    foreach (`$storeName in @('Root', 'TrustedPublisher')) {
      `$store = [Security.Cryptography.X509Certificates.X509Store]::new(
          `$storeName, 'LocalMachine')
      try {
        `$store.Open(
            [Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
        `$matches = `$store.Certificates.Find(
            [Security.Cryptography.X509Certificates.X509FindType]::
                FindByThumbprint,
            `$certificate.Thumbprint, `$false)
        if (`$matches.Count -eq 0) {
          `$store.Add(`$certificate)
          `$installed.Add("`$storeName|`$(`$certificate.Thumbprint)")
        }
      } finally {
        `$store.Dispose()
      }
    }
    `$certificate.Dispose()
  }

  if (-not $skipControlledHostLiteral) {
    `$controlledHostArguments = @{
      PackageRoot = `$root
      HostName = $hostNameLiteral
      AllowUnavailableRevocation = $allowUnavailableRevocationLiteral
      AllowDisposableGuestMutation = `$true
      DisposableGuestSentinelPath = `$sentinelPath
    }
    & (Join-Path `$root 'Run-WfpHttpsLiveTest.ps1') @controlledHostArguments *>&1 |
        Tee-Object -FilePath (Join-Path `$evidence 'controlled-host.log')
  }

  `$browserArguments = @{
    PackageRoot = `$root
    Url = $browserUrlLiteral
    LogDirectory = Join-Path `$root 'browser-log'
    DurationSeconds = $BrowserDurationSeconds
    RequireQuicBlockedFallback = $requireQuicBlockedFallbackLiteral
    AllowDisposableGuestMutation = `$true
    DisposableGuestSentinelPath = `$sentinelPath
  }
  & (Join-Path `$root 'Start-WfpBrowserHttpsInspection.ps1') @browserArguments *>&1 |
      Tee-Object -FilePath (Join-Path `$evidence 'browser-run.log')

  if ($includeManagedHttp3Literal) {
    `$managedArguments = @{
      PackageRoot = `$root
      Url = $browserUrlLiteral
      LogDirectory = Join-Path `$root 'managed-http3-log'
    }
    & (Join-Path `$root 'Start-ManagedHttp3Inspection.ps1') @managedArguments *>&1 |
        Tee-Object -FilePath (Join-Path `$evidence 'managed-http3-run.log')
  }

  if (-not $skipControlledHostLiteral) {
    Copy-Item -LiteralPath (Join-Path `$root 'https-live-host.log') -Destination `$evidence
  }
  Get-ChildItem -LiteralPath (Join-Path `$root 'browser-log') -File |
      Where-Object Extension -in @('.log', '.html', '.json') |
      Copy-Item -Destination `$evidence
  if ($includeManagedHttp3Literal) {
    Get-ChildItem -LiteralPath (Join-Path `$root 'managed-http3-log') -File |
        Where-Object Extension -in @('.log', '.html', '.bin') |
        ForEach-Object {
          Copy-Item -LiteralPath `$_.FullName -Destination (
              Join-Path `$evidence ('managed-' + `$_.Name))
        }
  }
  `$services = @(
    Get-Service -Name 'CrtSysWfpHttpsLiveTest',
        'CrtSysWfpBrowserHttpsInspection' -ErrorAction SilentlyContinue
  )
  if (`$services.Count -ne 0) {
    throw 'A temporary WFP driver service remained after the tests.'
  }

  & `$crashPostcheck -EventBaselinePath `$eventBaseline `
      -DumpBaselinePath `$dumpBaseline -OutputPath `$postcheck
  `$postcheckText = Get-Content -LiteralPath `$postcheck -Raw
  if (`$postcheckText -notmatch 'EVENT_COUNT=0' -or
      `$postcheckText -notmatch 'DUMP_COUNT=0' -or
      `$postcheckText -notmatch 'EVENT_LOG_RESET=0') {
    throw "Crash postcheck failed: `$postcheckText"
  }
  Copy-Item -LiteralPath `$postcheck -Destination `$evidence -Force
  `$verifierAfter = Get-DriverVerifierSnapshot
  if (`$verifierAfter -cne `$verifierBefore) {
    throw 'Driver Verifier settings changed during HTTPS acceptance.'
  }

  @(
    $(if ($SkipControlledHost) {
        "'CONTROLLED_HOST=SKIPPED'"
      } else {
        "'CONTROLLED_HOST=PASS'"
      })
    'BROWSER_HTTPS=PASS'
    $(if ($RequireQuicBlockedFallback) {
        "'BROWSER_QUIC_BLOCKED_FALLBACK=PASS'"
      } else {
        "'BROWSER_TRANSPORT=PASS'"
      })
    $(if ($IncludeManagedHttp3) {
        "'MANAGED_HTTP3=PASS'"
      } else {
        "'MANAGED_HTTP3=SKIPPED'"
      })
    'TEMP_SERVICES_REMOVED=PASS'
    'NEW_CRASH_EVENTS=0'
    'NEW_DUMPS=0'
    'RESTART_REQUESTED=NO'
    'DRIVER_VERIFIER_CHANGED=NO'
    'PASS'
  ) | Set-Content -LiteralPath `$log -Encoding UTF8
} catch {
  `$_ | Out-String | Set-Content -LiteralPath `$log -Encoding UTF8
  Add-Content -LiteralPath `$log -Value 'FAIL'
  Copy-Item -LiteralPath `$log -Destination (Join-Path `$evidence 'failure.log') -Force
  foreach (`$failedRun in @(
      @{Path=(Join-Path `$root 'browser-log'); Prefix='browser-'},
      @{Path=(Join-Path `$root 'managed-http3-log'); Prefix='managed-'})) {
    if (Test-Path -LiteralPath `$failedRun.Path -PathType Container) {
      Get-ChildItem -LiteralPath `$failedRun.Path -File |
          Where-Object Extension -in @('.log', '.html', '.json') |
          ForEach-Object {
            Copy-Item -LiteralPath `$_.FullName -Destination (
                Join-Path `$evidence (`$failedRun.Prefix + `$_.Name)) -Force
          }
    }
  }
  throw
} finally {
  foreach (`$entry in `$installed) {
    `$parts = `$entry.Split('|', 2)
    Remove-Item -LiteralPath ("Cert:\LocalMachine\`$(`$parts[0])\`$(`$parts[1])") -Force -ErrorAction SilentlyContinue
  }
  if (Test-Path -LiteralPath $guestEvidenceLiteral) {
    Remove-Item -LiteralPath $guestEvidenceLiteral -Force
  }
  if (Test-Path -LiteralPath `$evidence) {
    Compress-Archive -Path (Join-Path `$evidence '*') -DestinationPath $guestEvidenceLiteral -Force
  }
}
"@

$guestProgramPath =
    Join-Path $resolvedLogRoot 'Run-WfpHttpsVmGuestAcceptance.ps1'
[IO.File]::WriteAllText(
    $guestProgramPath, $guestProgram,
    [Text.UTF8Encoding]::new($false))
$parseErrors = $null
[void][Management.Automation.Language.Parser]::ParseFile(
    $guestProgramPath, [ref]$null, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) {
  throw "Generated guest script is invalid: $($parseErrors -join '; ')"
}
$guestProgramDestination =
    Join-Path $GuestRoot 'Run-WfpHttpsVmGuestAcceptance.ps1'
Copy-ToGuest $guestProgramPath $guestProgramDestination
$result = Invoke-Vmrun -Guest -AllowFailure -Arguments @(
  'runProgramInGuest', $VmxPath,
  'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
  '-NoProfile', '-ExecutionPolicy', 'Bypass',
  '-File', $guestProgramDestination
)

Copy-FromGuest $guestLog (Join-Path $resolvedLogRoot 'vm-acceptance.log')
Copy-FromGuest $guestEvidence (
    Join-Path $resolvedLogRoot 'vm-acceptance-evidence.zip')

$cleanupResult = Invoke-GuestScript -AllowFailure -Quiet -Script @'
$services = @(
  Get-Service -Name 'CrtSysWfpHttpsLiveTest',
      'CrtSysWfpBrowserHttpsInspection' -ErrorAction SilentlyContinue
)
$certificates = @(
  Get-ChildItem Cert:\LocalMachine\Root, Cert:\CurrentUser\Root |
      Where-Object {
        $_.Subject -like '*NTL Browser HTTPS Inspection*'
      }
)
$processes = @(
  Get-Process -Name 'crtsys_wfp_*', 'crtsys_ntl_*' `
      -ErrorAction SilentlyContinue
)
if ($services.Count -ne 0 -or
    $certificates.Count -ne 0 -or
    $processes.Count -ne 0) {
  exit 1
}
exit 0
'@
if ($cleanupResult.ExitCode -ne 0) {
  throw 'VM post-cleanup found a temporary service, CA, or sample process.'
}

$acceptanceText =
    Get-Content -LiteralPath (
        Join-Path $resolvedLogRoot 'vm-acceptance.log') -Raw
if ($result.ExitCode -ne 0 -or $acceptanceText -notmatch '(?m)^PASS\s*$') {
  throw "The HTTPS VM acceptance test failed with $($result.ExitCode)."
}

Write-Host (
    "WFP HTTPS VM acceptance passed without restart: $resolvedLogRoot")
