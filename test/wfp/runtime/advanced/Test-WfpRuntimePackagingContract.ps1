[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot =
    (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$advancedRoot = Join-Path $repoRoot 'test\wfp\runtime\advanced'
$httpsRoot = Join-Path $repoRoot 'test\wfp\runtime\https-live'
$aleRoot = Join-Path $repoRoot 'test\wfp\runtime\ale-connect-block'
$advancedPackager =
    Join-Path $advancedRoot 'Prepare-WfpAdvancedArtifacts.ps1'
$advancedRunner =
    Join-Path $advancedRoot 'Run-WfpAdvancedVmAcceptance.ps1'
$advancedSuite = Join-Path $advancedRoot 'Run-WfpAdvancedSuite.ps1'
$advancedSoak = Join-Path $advancedRoot 'Run-WfpAdvancedSoak.ps1'
$crashPostcheck =
    Join-Path $repoRoot 'test\common\Test-VmCrashPostcheck.ps1'
$aleVmRunner =
    Join-Path $aleRoot 'Run-AleConnectBlockVmAcceptance.ps1'
$httpsPackager =
    Join-Path $httpsRoot 'Prepare-WfpHttpsLiveArtifacts.ps1'
$controlledPackager =
    Join-Path $httpsRoot 'Prepare-ControlledHttp3Artifacts.ps1'
$httpsLiveRunner = Join-Path $httpsRoot 'Run-WfpHttpsLiveTest.ps1'
$browserWrapper =
    Join-Path $httpsRoot 'Start-WfpBrowserHttpsInspection.ps1'
$managedSuite = Join-Path $httpsRoot 'Run-WfpManagedHttp3Suite.ps1'
$httpsVmRunner = Join-Path $httpsRoot 'Run-WfpHttpsVmAcceptance.ps1'
$controlledVmRunner =
    Join-Path $httpsRoot 'Run-ControlledHttp3VmAcceptance.ps1'
$runtimeSource = Join-Path $repoRoot 'src\main.cpp'

$contractInputs = @(
  $advancedPackager
  $advancedRunner
  $advancedSuite
  $advancedSoak
  $crashPostcheck
  $aleVmRunner
  $httpsPackager
  $controlledPackager
  $httpsLiveRunner
  $browserWrapper
  $managedSuite
  $httpsVmRunner
  $controlledVmRunner
)
foreach ($path in $contractInputs) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "WFP runtime packaging contract input is missing: $path"
  }
}
if (-not (Test-Path -LiteralPath $runtimeSource -PathType Leaf)) {
  throw "CrtSys runtime source is missing: $runtimeSource"
}

function Read-Script([string] $Path) {
  $tokens = $null
  $errors = $null
  [void][Management.Automation.Language.Parser]::ParseFile(
      $Path, [ref] $tokens, [ref] $errors)
  if ($errors.Count -ne 0) {
    throw "PowerShell syntax failed for $Path`: $($errors -join '; ')"
  }
  return Get-Content -LiteralPath $Path -Raw
}

function Assert-Contains(
    [string] $Text, [string] $Token, [string] $Context) {
  if ($Text.IndexOf($Token, [StringComparison]::Ordinal) -lt 0) {
    throw "$Context is missing '$Token'."
  }
}

function Assert-DoesNotContain(
    [string] $Text, [string] $Token, [string] $Context) {
  if ($Text.IndexOf(
      $Token, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
    throw "$Context retained forbidden text '$Token'."
  }
}

function Assert-Before(
    [string] $Text, [string] $First, [string] $Second,
    [string] $Context) {
  $firstIndex = $Text.IndexOf($First, [StringComparison]::Ordinal)
  $secondIndex = $Text.IndexOf($Second, [StringComparison]::Ordinal)
  if ($firstIndex -lt 0 -or $secondIndex -lt 0 -or
      $firstIndex -ge $secondIndex) {
    throw "$Context must place '$First' before '$Second'."
  }
}

$advancedPackagerText = Read-Script $advancedPackager
$advancedRunnerText = Read-Script $advancedRunner
$advancedSuiteText = Read-Script $advancedSuite
$advancedSoakText = Read-Script $advancedSoak
$crashPostcheckText = Read-Script $crashPostcheck
$aleVmRunnerText = Read-Script $aleVmRunner
$httpsPackagerText = Read-Script $httpsPackager
$controlledPackagerText = Read-Script $controlledPackager
$httpsLiveRunnerText = Read-Script $httpsLiveRunner
$browserWrapperText = Read-Script $browserWrapper
$managedSuiteText = Read-Script $managedSuite
$httpsVmRunnerText = Read-Script $httpsVmRunner
$controlledVmRunnerText = Read-Script $controlledVmRunner
$runtimeSourceText = Get-Content -LiteralPath $runtimeSource -Raw

$runtimeInitializeStart = $runtimeSourceText.IndexOf(
    'CrtSysInitializeRuntime(_In_', [StringComparison]::Ordinal)
$runtimeUninitializeStart = $runtimeSourceText.IndexOf(
    'CrtSysUninitializeRuntime (', [StringComparison]::Ordinal)
if ($runtimeInitializeStart -lt 0 -or $runtimeUninitializeStart -lt 0 -or
    $runtimeInitializeStart -ge $runtimeUninitializeStart) {
  throw 'CrtSys runtime initialization functions could not be isolated.'
}
$runtimeInitializeText = $runtimeSourceText.Substring(
    $runtimeInitializeStart,
    $runtimeUninitializeStart - $runtimeInitializeStart)
$runtimeUninitializeText = $runtimeSourceText.Substring(
    $runtimeUninitializeStart)
Assert-Before $runtimeInitializeText `
    'status = CrtSyspInitializeEnvironment();' `
    'if (_initterm_e(__xi_a, __xi_z) != 0)' `
    'CrtSys partial-initialization rollback contract'
Assert-Before $runtimeInitializeText `
    'if (_initterm_e(__xi_a, __xi_z) != 0)' `
    'CrtSyspExitHandlersAvailable = TRUE;' `
    'CrtSys exit-handler activation contract'
Assert-Before $runtimeUninitializeText `
    'if (CrtSyspExitHandlersAvailable)' '_cexit();' `
    'CrtSys conditional UCRT teardown contract'

$pairs = @(
  [pscustomobject]@{
    Name = 'connect-redirect'
    UserDriver = 'crtsys_wfp_connect_redirect'
    UserApps = @(
      'crtsys_wfp_connect_redirect_proxy_service'
      'crtsys_wfp_connect_redirect_acceptance'
    )
    KernelDriver = 'crtsys_wfp_kernel_connect_redirect'
    KernelApps = @(
      'crtsys_wfp_kernel_connect_redirect_controller'
      'crtsys_wfp_kernel_connect_redirect_acceptance'
    )
  }
  [pscustomobject]@{
    Name = 'tls-inspection-proxy'
    UserDriver = 'crtsys_wfp_tls_inspection_proxy'
    UserApps = @(
      'crtsys_wfp_tls_inspection_proxy_service'
      'crtsys_wfp_tls_inspection_proxy_acceptance'
      'crtsys_wfp_tls_inspection_proxy_live_acceptance'
    )
    KernelDriver = 'crtsys_wfp_kernel_tls_inspection_proxy'
    KernelApps = @(
      'crtsys_wfp_kernel_tls_inspection_proxy_controller'
      'crtsys_wfp_kernel_tls_inspection_proxy_acceptance'
    )
  }
  [pscustomobject]@{
    Name = 'browser-https-inspection'
    UserDriver = 'crtsys_wfp_browser_https_inspection'
    UserApps = @(
      'crtsys_wfp_browser_https_inspection_controller'
      'crtsys_wfp_browser_https_inspection_http3_proxy_service'
      'crtsys_wfp_browser_https_inspection_acceptance'
      'crtsys_wfp_browser_https_inspection_managed_client_acceptance'
    )
    KernelDriver = 'crtsys_wfp_kernel_browser_https_inspection'
    KernelApps = @(
      'crtsys_wfp_kernel_browser_https_inspection_controller'
      'crtsys_wfp_kernel_browser_https_inspection_acceptance'
    )
  }
  [pscustomobject]@{
    Name = 'http3-inspection'
    UserDriver = 'crtsys_wfp_http3_inspection_driver'
    UserApps = @(
      'crtsys_wfp_http3_inspection_service'
      'crtsys_wfp_http3_inspection_acceptance'
    )
    KernelDriver = 'crtsys_wfp_kernel_http3_inspection'
    KernelApps = @(
      'crtsys_wfp_kernel_http3_inspection_controller'
      'crtsys_wfp_kernel_http3_inspection_acceptance'
    )
  }
  [pscustomobject]@{
    Name = 'udp-content-filter'
    UserDriver = 'crtsys_wfp_udp_content_filter'
    UserApps = @(
      'crtsys_wfp_udp_content_filter_policy_service'
      'crtsys_wfp_udp_content_filter_acceptance'
    )
    KernelDriver = 'crtsys_wfp_kernel_udp_content_filter'
    KernelApps = @(
      'crtsys_wfp_kernel_udp_content_filter_controller'
      'crtsys_wfp_kernel_udp_content_filter_acceptance'
    )
  }
  [pscustomobject]@{
    Name = 'tcp-content-filter'
    UserDriver = 'crtsys_wfp_tcp_content_filter'
    UserApps = @(
      'crtsys_wfp_tcp_content_filter_policy_service'
      'crtsys_wfp_tcp_content_filter_acceptance'
    )
    KernelDriver = 'crtsys_wfp_kernel_tcp_content_filter'
    KernelApps = @(
      'crtsys_wfp_kernel_tcp_content_filter_controller'
      'crtsys_wfp_kernel_tcp_content_filter_acceptance'
    )
  }
)

foreach ($pair in $pairs) {
  foreach ($runtime in @('user', 'kernel')) {
    $sampleRoot = Join-Path $repoRoot (
        "examples\wfp\$runtime\$($pair.Name)")
    $cmakePath = Join-Path $sampleRoot 'CMakeLists.txt'
    $driverBaseName = if ($runtime -eq 'user') {
      $pair.UserDriver
    } else {
      $pair.KernelDriver
    }
    $appTargets = if ($runtime -eq 'user') {
      @($pair.UserApps)
    } else {
      @($pair.KernelApps)
    }
    $infPath = Join-Path $sampleRoot "$driverBaseName.inf"
    foreach ($path in @($cmakePath, $infPath)) {
      if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw (
          "The $runtime/$($pair.Name) paired artifact is missing: $path")
      }
    }

    $cmakeText = Get-Content -LiteralPath $cmakePath -Raw
    if ($runtime -eq 'user' -and
        $pair.Name -eq 'browser-https-inspection') {
      $fixtureCMake = Join-Path $repoRoot (
          'test\wfp\runtime\fixtures\user\browser-https-inspection\' +
          'CMakeLists.txt')
      if (-not (Test-Path -LiteralPath $fixtureCMake -PathType Leaf)) {
        throw "The browser HTTPS fixture CMake file is missing: $fixtureCMake"
      }
      $cmakeText += "`n" + (Get-Content -LiteralPath $fixtureCMake -Raw)
    }
    $infText = Get-Content -LiteralPath $infPath -Raw
    Assert-Contains $cmakeText $driverBaseName (
        "$runtime/$($pair.Name) CMake driver target")
    foreach ($appTarget in $appTargets) {
      Assert-Contains $cmakeText $appTarget (
          "$runtime/$($pair.Name) CMake application target")
    }
    Assert-Contains $cmakeText "$driverBaseName.inf" (
        "$runtime/$($pair.Name) CMake INF artifact")
    Assert-Contains $infText '[DefaultInstall.NTamd64]' (
        "$runtime/$($pair.Name) INF")
    Assert-Contains $infText '[DefaultInstall.NTarm64]' (
        "$runtime/$($pair.Name) INF")
    Assert-Contains $infText "ServiceBinary=%13%\$driverBaseName.sys" (
        "$runtime/$($pair.Name) INF service binary")
  }

  if ($pair.Name -eq 'browser-https-inspection') {
    Assert-Contains $httpsPackagerText `
        "Project = 'wfp-browser-https-inspection'" 'HTTPS packager'
    Assert-Contains $httpsPackagerText $pair.UserDriver 'HTTPS packager'
    foreach ($appTarget in $pair.UserApps) {
      Assert-Contains $httpsPackagerText "$appTarget.exe" (
          'HTTPS packager browser application set')
    }
  } else {
    Assert-Contains $advancedPackagerText "Project = 'wfp-$($pair.Name)'" (
        'advanced user sample table')
    Assert-Contains $advancedPackagerText $pair.UserDriver (
        'advanced user artifact table')
  }
  Assert-Contains $advancedPackagerText (
      "Project = 'wfp-kernel-$($pair.Name)'") (
      'advanced kernel sample table')
  Assert-Contains $advancedPackagerText $pair.KernelDriver (
      'advanced kernel artifact table')
  foreach ($appTarget in $pair.KernelApps) {
    Assert-Contains $advancedPackagerText $appTarget (
        'advanced kernel application table')
  }
}

$kernelOwningExamples = @(
  [pscustomobject]@{
    Name = 'kernel connect redirect'
    Path = Join-Path $repoRoot (
        'examples\wfp\kernel\connect-redirect\driver\main.cpp')
    Required = @('io::with_async_transport', 'kernel::join_bidirectional')
  }
  [pscustomobject]@{
    Name = 'kernel TLS inspection proxy'
    Path = Join-Path $repoRoot (
        'examples\wfp\kernel\tls-inspection-proxy\driver\main.cpp')
    Required = @(
      'redirected_tls_session::create'
      'standard_redirected_tls_inspection::create'
    )
  }
  [pscustomobject]@{
    Name = 'kernel browser TCP service'
    Path = Join-Path $repoRoot (
        'examples\wfp\kernel\browser-https-inspection\driver\tcp_service.hpp')
    Required = @(
      'redirected_tls_session::create'
      'origin_security_provider_'
      'dispatcher_'
    )
  }
)
foreach ($example in $kernelOwningExamples) {
  if (-not (Test-Path -LiteralPath $example.Path -PathType Leaf)) {
    throw "$($example.Name) source is missing: $($example.Path)"
  }
  $text = Get-Content -LiteralPath $example.Path -Raw
  foreach ($token in $example.Required) {
    Assert-Contains $text $token "$($example.Name) owning lifetime"
  }
  Assert-DoesNotContain $text 'stop_and_drain_at_' (
      "$($example.Name) manual owner-boundary drain")
}

$kernelTlsHttp2 = Join-Path $repoRoot (
    'include\ntl\net\kernel\redirected_tls_inspection')
$kernelTlsHttp2Text = Get-Content -LiteralPath $kernelTlsHttp2 -Raw
$kernelTlsHttp2Start = $kernelTlsHttp2Text.IndexOf(
    'task<ntl::status> run_http2', [StringComparison]::Ordinal)
if ($kernelTlsHttp2Start -lt 0) {
  throw 'kernel redirected TLS inspection is missing run_http2.'
}
$kernelTlsHttp2RunText = $kernelTlsHttp2Text.Substring($kernelTlsHttp2Start)
foreach ($token in @(
    'kernel_proxy_session_workspace'
    'workspace_pool'
    'prepare_kernel_proxy_session'
    'requires_upstream'
    'run_kernel_proxy_session')) {
  Assert-Contains $kernelTlsHttp2Text $token (
      'kernel TLS bounded HTTP/2 session')
}
Assert-Before $kernelTlsHttp2RunText 'prepare_kernel_proxy_session' `
    'origin->with_connection' 'kernel TLS request preflight'
foreach ($token in @('http2_wire_frame', 'read_http2_frame')) {
  Assert-DoesNotContain $kernelTlsHttp2Text $token (
      'kernel TLS stack-resident HTTP/2 frame')
}

$kernelBrowserTls = Join-Path $repoRoot (
    'examples\wfp\kernel\browser-https-inspection\driver\tcp_session.hpp')
$kernelBrowserTlsText = Get-Content -LiteralPath $kernelBrowserTls -Raw
foreach ($token in @(
    'standard_redirected_tls_inspection'
    '.advertise_extended_connect = true')) {
  Assert-Contains $kernelBrowserTlsText $token (
      'kernel browser standard redirected TLS composition')
}

foreach ($token in @(
    "Name = 'kernel-http3-inspection'"
    'AcceptanceUsesControllerIpc = $true'
    'ReloadDriversBetweenIterations = $true'
    '"$($sample.Name)-ipc-$iteration"'
    '@($controller, $acceptanceIpcDirectory)')) {
  Assert-Contains $advancedSuiteText $token (
      'kernel HTTP/3 controller/IPC acceptance invocation')
}

$tlsLifecycle = Get-Content -LiteralPath (Join-Path $repoRoot (
    'examples\wfp\shared\tls_runtime_control.hpp')) -Raw
$tlsFixture = Get-Content -LiteralPath (Join-Path $repoRoot (
    'test\wfp\runtime\fixtures\common\tls_runtime_fixture.hpp')) -Raw
Assert-Contains $tlsLifecycle '--identity-thumbprint-file' (
    'TLS exact identity lifecycle')
Assert-Contains $tlsFixture 'CERT_FIND_HASH' (
    'TLS exact identity fixture selection')
Assert-DoesNotContain $tlsFixture 'CERT_FIND_SUBJECT_STR_W' (
    'TLS ambiguous subject identity selection')
foreach ($controller in @(
    'examples\wfp\user\tls-inspection-proxy\app\main.cpp'
    'examples\wfp\kernel\tls-inspection-proxy\app\main.cpp')) {
  $controllerText = Get-Content -LiteralPath (
      Join-Path $repoRoot $controller) -Raw
  Assert-Contains $controllerText 'identity_thumbprint_file' (
      "$controller exact identity publication")
}

$kernelConnectAcceptance = Join-Path $repoRoot (
    'test\wfp\runtime\fixtures\kernel\connect-redirect\main.cpp')
$kernelConnectAcceptanceText =
    Get-Content -LiteralPath $kernelConnectAcceptance -Raw
Assert-Contains $kernelConnectAcceptanceText 'if (argc == 1)' (
    'kernel connect zero-argument packaged acceptance')
Assert-Contains $kernelConnectAcceptanceText (
    'crtsys_wfp_kernel_connect_redirect_controller.exe') (
    'kernel connect sibling controller discovery')

$fragmentedDatagramContract =
    'crtsys_wfp_datagram_proxy_fragmented_buffer_contract'
foreach ($entry in @(
    @{ Text=$advancedPackagerText; Context='advanced packager' },
    @{ Text=$advancedSuiteText; Context='advanced suite' })) {
  Assert-Contains $entry.Text $fragmentedDatagramContract (
      "$($entry.Context) fragmented NBL load contract")
}
Assert-Contains $advancedRunnerText "$fragmentedDatagramContract.sys" (
    'advanced VM driver inventory fragmented NBL load contract')
$fragmentedContractInf = Join-Path $repoRoot (
    'test\wfp\runtime\contracts\kernel\datagram-proxy\' +
    "$fragmentedDatagramContract.inf")
if (-not (Test-Path -LiteralPath $fragmentedContractInf -PathType Leaf)) {
  throw "The fragmented NBL load-contract INF is missing: $fragmentedContractInf"
}

foreach ($appTarget in @(
    'crtsys_wfp_browser_https_inspection_acceptance'
    'crtsys_wfp_browser_https_inspection_managed_client_acceptance')) {
  Assert-Contains $controlledPackagerText "$appTarget.exe" (
      'controlled HTTP/3 fixture package')
}
Assert-DoesNotContain $controlledPackagerText (
    'crtsys_wfp_browser_https_inspection_app.exe') (
    'controlled HTTP/3 fixture package')

Assert-Contains $advancedPackagerText `
    'examples\wfp\$($sample.Runtime)\$sourceDirectory\' `
    'advanced source-directory routing'
Assert-Contains $httpsPackagerText `
    'examples\wfp\user\$($sample.Directory)\' `
    'HTTPS user-runtime routing'

foreach ($entry in @(
    @{ Name='HTTPS packager'; Text=$httpsPackagerText },
    @{ Name='controlled HTTP/3 packager'; Text=$controlledPackagerText })) {
  Assert-Contains $entry.Text "[ValidateSet('x64', 'ARM64')]" $entry.Name
  Assert-Contains $entry.Text '-Architecture $Architecture' $entry.Name
  Assert-Contains $entry.Text 'build_${Architecture}_' $entry.Name
  Assert-DoesNotContain $entry.Text 'build_x64_' $entry.Name
  Assert-DoesNotContain $entry.Text '-Architecture x64' $entry.Name
}
Assert-Contains $advancedRunnerText "'-Architecture', `$Architecture" (
    'advanced HTTPS-package architecture propagation')
Assert-Contains $advancedRunnerText (
    'wfp-https-live-staging-$($Architecture.ToLowerInvariant())') (
    'advanced architecture-specific HTTPS staging')
Assert-Contains $advancedRunnerText 'Test-VmCrashPostcheck.ps1' (
    'advanced VM crash postcheck deployment')
Assert-Contains $advancedRunnerText "'-File'," (
    'advanced VM crash postcheck file invocation')
Assert-Contains $advancedRunnerText 'if ($guestFailureMessage)' (
    'advanced VM deferred suite failure after crash postcheck')
Assert-Contains $advancedRunnerText '[switch] $RuntimeOnly' (
    'advanced VM explicit runtime-only mode')
Assert-Contains $advancedRunnerText 'if (-not $RuntimeOnly)' (
    'advanced VM strict Verifier gate default')
Assert-Contains $advancedRunnerText '[switch] $RequireLowResourcesSimulation' (
    'advanced VM explicit Low Resources requirement')
Assert-Contains $advancedRunnerText '$lowResourcesFlags' (
    'advanced VM Low Resources flag verification')
Assert-Contains $advancedRunnerText (
    'Get-DriverVerifierIntentionalFailureCount') (
    'advanced VM actual Low Resources injection evidence')
Assert-Contains $advancedRunnerText 'low-resources-evidence.json' (
    'advanced VM Low Resources evidence artifact')
Assert-Contains $advancedRunnerText 'runtime-cleanup-evidence.json' (
    'advanced VM external cleanup evidence artifact')
Assert-Contains $advancedRunnerText 'GracefulFailClosedObserved' (
    'advanced VM measured Low Resources fail-closed outcome')
Assert-Contains $advancedRunnerText 'RemainingDriverCount' (
    'advanced VM Low Resources driver cleanup verification')
Assert-Contains $advancedRunnerText 'RemainingProcessCount' (
    'advanced VM Low Resources process cleanup verification')
Assert-Contains $advancedRunnerText '-Encoding Unicode -Append' (
    'advanced VM readable single-encoding guest evidence logs')
if ($advancedRunnerText -match
    '(?m)Out-File\s*`\r?\n\s*-(?:LiteralPath|Encoding)') {
  throw (
    'advanced VM guest here-string must keep Out-File path and encoding ' +
    'arguments on the same source line; outer interpolation consumes the ' +
    'continuation and produces an invalid guest command.')
}
Assert-Contains $advancedRunnerText 'Get-DriverVerifierLoadCounts' (
    'advanced VM per-run Verifier load/unload parser')
Assert-Contains $advancedRunnerText 'verifier-load-unload-evidence.json' (
    'advanced VM per-run Verifier load/unload evidence artifact')
Assert-Contains $advancedRunnerText (
    '[switch] $RequireActiveIpsecSecurityAssociation') (
    'advanced VM explicit active IPsec requirement')
Assert-Contains $advancedRunnerText 'Get-NetIPsecQuickModeSA' (
    'advanced VM active IPsec SA evidence')
Assert-Contains $advancedRunnerText 'ipsec-quick-mode-sa-before.json' (
    'advanced VM pre-traffic IPsec SA evidence')
Assert-Contains $advancedRunnerText 'ipsec-quick-mode-sa-after.json' (
    'advanced VM post-traffic IPsec SA evidence')
Assert-Contains $advancedRunnerText (
    "-SpecializedObservationTrafficTarget set to the protected peer") (
    'advanced VM IPsec protected-peer requirement')
Assert-Contains $advancedRunnerText 'claimed as Verifier targets.' (
    'advanced VM runtime-only evidence label')
foreach ($token in @(
    'SpecializedObservationTrafficTarget',
    'SpecializedObservationRequireMac',
    'SpecializedObservationRequireVSwitch',
    'SpecializedObservationTrafficDurationMs')) {
  Assert-Contains $advancedRunnerText $token (
      'advanced VM specialized topology propagation')
  Assert-Contains $advancedSuiteText $token (
      'advanced suite specialized topology gate')
}
Assert-Contains $advancedSuiteText "'--require-mac'" (
    'specialized required MAC classify gate')
Assert-Contains $advancedSuiteText "'--require-vswitch'" (
    'specialized required vSwitch classify gate')
Assert-Contains $advancedSuiteText "'--traffic-target'" (
    'specialized controlled traffic target')
Assert-Contains $crashPostcheckText '$_.RecordId -gt $baselineRecordId' (
    'VM crash postcheck RecordId boundary')
Assert-Contains $crashPostcheckText 'Microsoft-Windows-Kernel-Power' (
    'VM crash postcheck provider filter')
Assert-Contains $crashPostcheckText '$baselineDumps -notcontains $_' (
    'VM crash postcheck dump fingerprint boundary')
foreach ($entry in @(
    @{ Name='advanced VM runner'; Text=$advancedRunnerText },
    @{ Name='advanced soak runner'; Text=$advancedSoakText },
    @{ Name='ALE VM runner'; Text=$aleVmRunnerText },
    @{ Name='HTTPS VM runner'; Text=$httpsVmRunnerText },
    @{ Name='controlled HTTP/3 VM runner'; Text=$controlledVmRunnerText })) {
  Assert-Contains $entry.Text 'Test-VmCrashPostcheck.ps1' (
      "$($entry.Name) common crash postcheck")
  Assert-Contains $entry.Text '-CaptureBaseline' (
      "$($entry.Name) crash baseline capture")
  Assert-DoesNotContain $entry.Text 'StartTime' (
      "$($entry.Name) timestamp crash boundary")
}

foreach ($entry in @(
    @{ Name='advanced suite'; Text=$advancedSuiteText;
       Inf='$($driverSpecification.BaseName).inf' },
    @{ Name='TLS live runner'; Text=$httpsLiveRunnerText;
       Inf='crtsys_wfp_tls_inspection_proxy.inf' },
    @{ Name='browser wrapper'; Text=$browserWrapperText;
       Inf='crtsys_wfp_browser_https_inspection.inf' },
    @{ Name='managed HTTP/3 suite'; Text=$managedSuiteText;
       Inf='crtsys_wfp_browser_https_inspection.inf' })) {
  Assert-Contains $entry.Text $entry.Inf "$($entry.Name) INF preflight"
}

$runtimeScripts = @(
  Get-ChildItem -LiteralPath $advancedRoot, $httpsRoot, $aleRoot `
      -Filter '*.ps1' -File |
      Where-Object Name -notlike '*Contract.ps1'
)
foreach ($script in $runtimeScripts) {
  $text = Read-Script $script.FullName
  $context = $script.FullName.Substring($repoRoot.Length + 1)

  if ($text -match
      '(?im)^\s*(?:&\s*)?(?:shutdown(?:\.exe)?|bcdedit(?:\.exe)?|' +
      'Restart-Computer|Stop-Computer)\b') {
    throw "$context contains a boot-state mutation command."
  }
  if ($text -match
      '(?im)^.*verifier(?:\.exe)?\s+/(?:reset|standard|flags|driver|all|' +
      'adddriver|deletedriver|bootmode|volatile)\b') {
    throw "$context contains a Driver Verifier mutation command."
  }
  if ($text -match
      "(?is)Invoke-Vmrun.{0,500}-Arguments\s+@\(\s*'" +
      '(?:start|stop|reset|suspend)' + "'") {
    throw "$context contains a VM power-state command."
  }
  if ($text -match '(?is)Start-Process\s+-FilePath\s+\$BrowserPath') {
    throw "$context launches the observed browser."
  }
  if ($text -match
      '(?im)^.*Stop-Process.*(?:BrowserPath|msedge|chrome)') {
    throw "$context terminates a browser process."
  }
  foreach ($token in @(
      '--user-data-dir', '--ignore-certificate-errors',
      '--ignore-certificate-errors-spki-list', '--origin-to-force-quic-on',
      '--disable-quic', '--enable-quic', '--host-resolver-rules',
      '--disable-features', '--enable-features', 'taskkill',
      'Set-ItemProperty', 'New-ItemProperty', 'Remove-ItemProperty')) {
    Assert-DoesNotContain $text $token $context
  }
}

Write-Host (
  'WFP runtime source/package contract passed: paired user/kernel names, ' +
  'x64/ARM64 routing, INF preflight, browser no-mutation, VM no-reboot, ' +
  'Driver Verifier read-only invariants, and partial CRT rollback safety.')
