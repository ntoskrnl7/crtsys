[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $PackageRoot,

  [ValidateSet('all', 'datagram-proxy', 'async-inspection',
               'flow-monitor', 'stream-edit', 'connect-redirect',
               'bind-redirect',
               'tls-inspection-proxy',
               'http3-inspection',
               'udp-content-filter',
               'tcp-content-filter',
               'specialized-observation',
               'kernel-connect-redirect',
               'kernel-tls-inspection-proxy',
               'kernel-browser-https-inspection',
               'kernel-http3-inspection',
               'kernel-udp-content-filter',
               'kernel-tcp-content-filter')]
  [Alias('Sample')]
  [string] $SelectedSample = 'all',

  [ValidateRange(1, 1000)]
  [int] $Iterations = 20,

  [string] $SpecializedObservationTrafficTarget = '',

  [switch] $SpecializedObservationRequireMac,

  [switch] $SpecializedObservationRequireVSwitch,

  [ValidateRange(100, 300000)]
  [int] $SpecializedObservationTrafficDurationMs = 5000,

  [switch] $AllowDisposableGuestMutation,

  [string] $DisposableGuestSentinelPath =
      'C:\crtsys-disposable-test-guest.sentinel'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Write-Host '=== runtime suite entered ==='

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

$samples = @(
  [pscustomobject]@{
    Name = 'datagram-proxy'
    BaseName = 'crtsys_wfp_datagram_proxy'
    ControllerBaseName = 'crtsys_wfp_datagram_proxy_controller'
    AcceptanceBaseName = 'crtsys_wfp_datagram_proxy_acceptance'
    Service = 'CrtSysWfpDatagramProxyAcceptance'
    Marker = 'Kernel datagram-proxy acceptance PASS:'
    AdditionalDrivers = @(
      [pscustomobject]@{
        BaseName =
            'crtsys_wfp_datagram_proxy_fragmented_buffer_contract'
        Service =
            'CrtSysWfpDatagramFragmentedBufferContractAcceptance'
      }
    )
  },
  [pscustomobject]@{
    Name = 'async-inspection'
    BaseName = 'crtsys_wfp_async_inspection'
    ControllerBaseName = 'crtsys_wfp_async_inspection_controller'
    AcceptanceBaseName = 'crtsys_wfp_async_inspection_acceptance'
    Service = 'CrtSysWfpAsyncInspectionAcceptance'
    Marker = 'NTL WFP async-inspection ok:'
    AcceptanceUsesControllerIpc = $true
  },
  [pscustomobject]@{
    Name = 'flow-monitor'
    BaseName = 'crtsys_wfp_flow_monitor'
    ControllerBaseName = 'crtsys_wfp_flow_monitor_controller'
    AcceptanceBaseName = 'crtsys_wfp_flow_monitor_acceptance'
    Service = 'CrtSysWfpFlowMonitorAcceptance'
    Marker = 'Kernel flow-monitor acceptance PASS:'
  },
  [pscustomobject]@{
    Name = 'stream-edit'
    BaseName = 'crtsys_wfp_stream_edit'
    ControllerBaseName = 'crtsys_wfp_stream_edit_controller'
    AcceptanceBaseName = 'crtsys_wfp_stream_edit_acceptance'
    Service = 'CrtSysWfpStreamEditAcceptance'
    Marker = 'Kernel stream-edit acceptance PASS:'
  },
  [pscustomobject]@{
    Name = 'connect-redirect'
    BaseName = 'crtsys_wfp_connect_redirect'
    ControllerBaseName = 'crtsys_wfp_connect_redirect_proxy_service'
    AcceptanceBaseName = 'crtsys_wfp_connect_redirect_acceptance'
    Service = 'CrtSysWfpConnectRedirectAcceptance'
    Marker = 'NTL WFP connect-redirect ok:'
    AcceptanceUsesControllerIpc = $true
    RequiredMarkerTokens = @(
      'IPv4='
      'IPv6='
      'coroutine_up='
      'coroutine_down='
      'restored=direct'
      'unavailable_proxy=blocked'
      'origin_bypass=none'
      'failure_restored=IPv4/IPv6'
    )
  },
  [pscustomobject]@{
    Name = 'bind-redirect'
    BaseName = 'crtsys_wfp_bind_redirect'
    ControllerBaseName = 'crtsys_wfp_bind_redirect_controller'
    AcceptanceBaseName = 'crtsys_wfp_bind_redirect_acceptance'
    Service = 'CrtSysWfpBindRedirectAcceptance'
    Marker = 'NTL WFP bind-redirect ok:'
  },
  [pscustomobject]@{
    Name = 'tls-inspection-proxy'
    BaseName = 'crtsys_wfp_tls_inspection_proxy'
    ControllerBaseName = 'crtsys_wfp_tls_inspection_proxy_service'
    AcceptanceBaseName = 'crtsys_wfp_tls_inspection_proxy_acceptance'
    Service = 'CrtSysWfpTlsInspectionProxyAcceptance'
    Marker = 'NTL WFP TLS inspection acceptance PASS:'
    RequiredMarkerTokens = @(
      'permit=4'
      'block=4'
      'ipv4=verified'
      'ipv6=verified'
      'tls_legs=2'
      'http1=bounded'
      'h1_request_transformed=2'
      'h1_response_transformed=2'
      'h1_origin_blocked=2'
      'http2=bounded'
      'hpack=bounded'
      'h2_redirected=4'
      'h2_alpn=4'
      'h2_origin_forwarded=2'
      'h2_origin_blocked=2'
      'sni=dynamic'
      'identity_handoff=8'
      'restored=direct'
    )
  },
  [pscustomobject]@{
    Name = 'udp-content-filter'
    BaseName = 'crtsys_wfp_udp_content_filter'
    ControllerBaseName = 'crtsys_wfp_udp_content_filter_policy_service'
    AcceptanceBaseName = 'crtsys_wfp_udp_content_filter_acceptance'
    Service = 'CrtSysWfpUdpContentFilterAcceptance'
    Marker = 'NTL WFP UDP content-filter acceptance PASS:'
    FailureMarker = 'NTL WFP UDP content-filter failure acceptance PASS:'
    RequiredMarkerTokens = @(
      'complete-udp=6'
      'permit=2'
      'policy-block=2'
      'malformed=2'
      'structured-record=used'
      'coroutine=used'
      'ipv4=pass'
      'ipv6=pass'
      'restored=success'
    )
    FailureRequiredMarkerTokens = @(
      'malformed=blocked'
      'ipv4/ipv6-timeout=blocked'
      'quota=bounded'
      'late-permit=rejected'
      'session-loss=cancelled'
    )
  },
  [pscustomobject]@{
    Name = 'tcp-content-filter'
    BaseName = 'crtsys_wfp_tcp_content_filter'
    ControllerBaseName = 'crtsys_wfp_tcp_content_filter_policy_service'
    AcceptanceBaseName = 'crtsys_wfp_tcp_content_filter_acceptance'
    Service = 'CrtSysWfpTcpContentFilterAcceptance'
    Marker = 'NTL WFP TCP content-filter acceptance PASS:'
    FailureMarker = 'NTL WFP TCP content-filter failure acceptance PASS:'
    RequiredMarkerTokens = @(
      'complete-tcp=8'
      'permit=4'
      'policy-block=2'
      'malformed=2'
      'same-flow=2-per-family'
      'tcp-prefix-split=handled'
      'outbound=pass-through'
      'structured-record=used'
      'coroutine=used'
      'ipv4=pass'
      'ipv6=pass'
      'restored=success'
    )
    FailureRequiredMarkerTokens = @(
      'framing=blocked'
      'ipv4/ipv6-timeout=flow-dropped'
      'late-permit=rejected'
      'session-loss=cancelled'
    )
  },
  [pscustomobject]@{
    Name = 'specialized-observation'
    BaseName = 'crtsys_wfp_specialized_observation'
    ControllerBaseName = 'crtsys_wfp_specialized_observation_controller'
    AcceptanceBaseName = 'crtsys_wfp_specialized_observation_acceptance'
    Service = 'CrtSysWfpSpecializedObservationAcceptance'
    Marker = 'Kernel specialized-observation acceptance PASS:'
  },
  [pscustomobject]@{
    Name = 'kernel-connect-redirect'
    BaseName = 'crtsys_wfp_kernel_connect_redirect'
    ControllerBaseName = 'crtsys_wfp_kernel_connect_redirect_controller'
    AcceptanceBaseName = 'crtsys_wfp_kernel_connect_redirect_acceptance'
    Service = 'CrtSysWfpKernelConnectRedirectAcceptance'
    Marker = 'Kernel connect-redirect PASS:'
    RequiredMarkerTokens = @(
      'IPv4/IPv6 ALE redirect'
      'original-destination WSK relay'
      'redirect records'
      'counters'
      'restored=IPv4/IPv6'
      'failed_origin=blocked'
      'failed_counter=2'
      'failure_restored=IPv4/IPv6'
      'cleanup'
    )
  },
  [pscustomobject]@{
    Name = 'http3-inspection'
    BaseName = 'crtsys_wfp_http3_inspection'
    DriverBaseName = 'crtsys_wfp_http3_inspection_driver'
    ControllerBaseName = 'crtsys_wfp_http3_inspection_service'
    AcceptanceBaseName = 'crtsys_wfp_http3_inspection_acceptance'
    Service = 'CrtSysWfpHttp3InspectionGateAcceptance'
    Marker = 'controlled-msquic-http3: WFP gate PASS'
    AcceptanceUsesControllerIpc = $true
    RequiredFiles = @('msquic.dll')
    RequiredMarkerTokens = @(
      'wfp_ipv4_delta='
      'wfp_ipv6_delta='
      'original_v4_port='
      'original_v6_port='
      'process_id='
      'app_hash='
      'tuple=IPv4/IPv6'
      'policy_lifetime=ephemeral'
      'policy_removed_direct=IPv4/IPv6'
      'counter_unchanged=yes'
      'unavailable_callout=blocked'
      'origin_hit=0'
      'webtransport_block=IPv4/IPv6'
      'inactive_after_403=yes'
    )
  },
  [pscustomobject]@{
    Name = 'kernel-tls-inspection-proxy'
    BaseName = 'crtsys_wfp_kernel_tls_inspection_proxy'
    ControllerBaseName = 'crtsys_wfp_kernel_tls_inspection_proxy_controller'
    AcceptanceBaseName = 'crtsys_wfp_kernel_tls_inspection_proxy_acceptance'
    Service = 'CrtSysWfpKernelTlsInspectionAcceptance'
    Marker = 'Kernel TLS inspection acceptance PASS:'
    RequiredMarkerTokens = @(
      'IPv4/IPv6 original destination'
      'redirect records'
      'two-leg system-validated Schannel'
      'ALPN HTTP/1.1+HTTP/2'
      'sni=identity-selected'
      'common request/response transforms'
      'h1_permit=1'
      'h1_block=1'
      'h1_request_transformed=1'
      'h1_response_transformed=1'
      'h2_permit=1'
      'h2_block=1'
      'h2_request_transformed=1'
      'h2_response_transformed=1'
      'fail-closed block'
      'malformed/timeout'
      'bounded capture'
      'restored=IPv4/IPv6'
      'cleanup'
    )
  },
  [pscustomobject]@{
    Name = 'kernel-browser-https-inspection'
    BaseName = 'crtsys_wfp_kernel_browser_https_inspection'
    ControllerBaseName =
        'crtsys_wfp_kernel_browser_https_inspection_controller'
    AcceptanceBaseName =
        'crtsys_wfp_kernel_browser_https_inspection_acceptance'
    Service = 'CrtSysWfpKernelBrowserHttpsAcceptance'
    Marker = 'Kernel browser HTTPS inspection PASS:'
    TimeoutSeconds = 600
    RequiredFiles = @('msquic.dll')
    RequiresMsQuicProvider = $true
    RequiredMarkerTokens = @(
      'http1_policy_pipeline=pass'
      'http1_compression=pass'
      'http1_grpc=pass'
      'http1_websocket=pass'
      'http1_ipv4_ipv6_wfp=pass'
      'http1_pipelining=pass'
      'http2_policy_pipeline=pass'
      'http2_compression=pass'
      'http2_grpc=pass'
      'http2_websocket=pass'
      'http2_extended_connect=pass'
      'http2_unsupported_connect=blocked'
      'http2_ipv4_ipv6_wfp=pass'
      'http2_multiplex=pass'
      'http2_flow_control=pass'
      'http2_goaway=pass'
      'http3_policy_pipeline=pass'
      'http3_compression=pass'
      'http3_grpc=pass'
      'http3_qpack=pass'
      'http3_inbound_peer_settings=pass'
      'http3_origin_h3_negotiated=pass'
      'http3_origin_peer_settings=pass'
      'http3_origin_qpack_acknowledgement=pass'
      'http3_webtransport=pass'
      'http3_capsule_stream=pass'
      'http3_single_connection_multiplex=pass'
      'http3_reverse_completion=pass'
      'http3_stream_local_block=pass'
      'http3_stream_local_reset=pass'
      'http3_aggregate_quota=pass'
      'http3_origin_single_qpack_encoder_stream=pass'
      'http3_multiplex_clean_drain=pass'
      'http3_webtransport_policy_rejected=pass'
      'http3_webtransport_rejection_no_session=pass'
      'http3_unsupported_extended_connect_fail_closed=pass'
      'http3_invalid_request_headers_fail_closed=pass'
      'http3_negative_origin_isolation=pass'
      'tcp_wfp_redirect=pass'
      'udp_wfp_relay=pass'
      'origin_system_validation=pass'
      'origin_exact_pin=pass'
      'origin_mtls=pass'
      'origin_negative_cases=pass'
      'origin_fallback_h2=pass'
      'origin_fallback_http1=pass'
      'origin_fallback_non_safe=blocked'
      'origin_fallback_security=blocked'
      'origin_security_replace_rollback=pass'
      'dynamic_sni=pass'
      'identity_replacement=pass'
      'unknown_sni_fail_closed=pass'
      'churn_over_quota=pass'
      'permit_block=pass'
      'clean_drain=pass'
    )
  },
  [pscustomobject]@{
    Name = 'kernel-http3-inspection'
    BaseName = 'crtsys_wfp_kernel_http3_inspection'
    ControllerBaseName = 'crtsys_wfp_kernel_http3_inspection_controller'
    AcceptanceBaseName = 'crtsys_wfp_kernel_http3_inspection_acceptance'
    Service = 'CrtSysWfpKernelHttp3Acceptance'
    Marker = 'Kernel HTTP/3 inspection PASS:'
    TimeoutSeconds = 600
    AcceptanceUsesControllerIpc = $true
    ReloadDriversBetweenIterations = $true
    RequiredFiles = @('msquic.dll')
    RequiresMsQuicProvider = $true
    RequiredMarkerTokens = @(
      'IPv4/IPv6 WFP'
      'kernel MsQuic TLS 1.3'
      'SETTINGS'
      'dynamic QPACK resume/ack'
      'gzip/deflate/Brotli HTML'
      'Extended CONNECT/WebTransport'
      'streams/datagram/capsule/reliable-reset'
      'permit/block'
      'webtransport-block=pass'
      'unavailable-callout=blocked'
      'origin-hit=0'
      'restored=IPv4/IPv6'
      '96-connection sequential churn'
      'capture'
      'cleanup'
    )
  },
  [pscustomobject]@{
    Name = 'kernel-udp-content-filter'
    BaseName = 'crtsys_wfp_kernel_udp_content_filter'
    ControllerBaseName = 'crtsys_wfp_kernel_udp_content_filter_controller'
    AcceptanceBaseName = 'crtsys_wfp_kernel_udp_content_filter_acceptance'
    Service = 'CrtSysWfpKernelUdpContentFilterAcceptance'
    Marker = 'Kernel UDP content-filter acceptance PASS:'
    RequiredMarkerTokens = @(
      'IPv4/IPv6 typed permit'
      'block'
      'malformed fail-close'
      'statistics'
      'cleanup'
    )
  },
  [pscustomobject]@{
    Name = 'kernel-tcp-content-filter'
    BaseName = 'crtsys_wfp_kernel_tcp_content_filter'
    ControllerBaseName = 'crtsys_wfp_kernel_tcp_content_filter_controller'
    AcceptanceBaseName = 'crtsys_wfp_kernel_tcp_content_filter_acceptance'
    Service = 'CrtSysWfpKernelTcpContentFilterAcceptance'
    Marker = 'Kernel TCP content-filter acceptance PASS:'
    RequiredMarkerTokens = @(
      'IPv4/IPv6 same-flow framing'
      'typed record policy'
      'malformed fail-close'
      'fragmentation'
      'outbound pass-through'
      'statistics'
      'cleanup'
    )
  }
)
if ($SelectedSample -ne 'all') {
  $samples = @($samples | Where-Object Name -eq $SelectedSample)
}

function Get-SampleApplicationFileNames([object] $Sample) {
  $hasController =
      $null -ne $Sample.PSObject.Properties['ControllerBaseName']
  $hasAcceptance =
      $null -ne $Sample.PSObject.Properties['AcceptanceBaseName']
  if ($hasController -or $hasAcceptance) {
    if (-not ($hasController -and $hasAcceptance)) {
      throw (
        "$($Sample.Name) must declare both ControllerBaseName and " +
        'AcceptanceBaseName.')
    }
    return @(
      "$($Sample.ControllerBaseName).exe"
      "$($Sample.AcceptanceBaseName).exe"
    )
  }
  $baseName = if ($Sample.PSObject.Properties['AppBaseName']) {
    $Sample.AppBaseName
  } else {
    $Sample.BaseName
  }
  return @("$baseName`_app.exe")
}

function Get-SampleAcceptanceFileName([object] $Sample) {
  if ($Sample.PSObject.Properties['AcceptanceBaseName']) {
    return "$($Sample.AcceptanceBaseName).exe"
  }
  $baseName = if ($Sample.PSObject.Properties['AppBaseName']) {
    $Sample.AppBaseName
  } else {
    $Sample.BaseName
  }
  return "$baseName`_app.exe"
}
function Assert-Administrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  if (-not $principal.IsInRole(
      [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'The WFP runtime suite must run from an elevated PowerShell session.'
  }
}

function Remove-ServiceIfPresent([string] $Name) {
  & sc.exe query $Name *> $null
  if ($LASTEXITCODE -eq 0) {
    & sc.exe stop $Name *> $null
    & sc.exe delete $Name *> $null
    $deadline = (Get-Date).AddSeconds(30)
    do {
      & sc.exe query $Name *> $null
      if ($LASTEXITCODE -ne 0) {
        return
      }
      Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw "Timed out removing the $Name service."
  }
}

function Install-DriverService(
    [object] $DriverSpecification,
    [string] $PackageRoot,
    [string] $SampleName) {
  $driver = Join-Path $PackageRoot (
      "$($DriverSpecification.BaseName).sys")
  Write-Host (
      "=== $SampleName`: $($DriverSpecification.BaseName) load ===")
  & sc.exe create $DriverSpecification.Service 'binPath=' $driver `
      'type=' 'kernel' 'start=' 'demand' |
      ForEach-Object { Write-Host $_ }
  if ($LASTEXITCODE -ne 0) {
    throw "Creating the $($DriverSpecification.Service) service failed."
  }

  & sc.exe start $DriverSpecification.Service |
      ForEach-Object { Write-Host $_ }
  if ($LASTEXITCODE -ne 0) {
    throw "Starting the $($DriverSpecification.Service) service failed."
  }
}

function Invoke-SampleApplication {
  param(
    [Parameter(Mandatory)]
    [string] $Path,
    [string[]] $Arguments = @(),
    [ValidateRange(1, 600)]
    [int] $TimeoutSeconds = 120
  )

  $process = $null
  try {
    foreach ($argument in $Arguments) {
      if ($argument -match '[\s"]') {
        throw "Unsupported sample argument: $argument"
      }
    }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Path
    $start.Arguments = $Arguments -join ' '
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) {
      throw "Starting the sample application failed: $Path"
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
      Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
      throw (
          "Application timed out after $TimeoutSeconds seconds: $Path")
    }
    $process.WaitForExit()
    $stdoutText = $stdoutTask.GetAwaiter().GetResult()
    $stderrText = $stderrTask.GetAwaiter().GetResult()
    $text = $stdoutText + $stderrText
    $output = @(
      $text -split '\r?\n' | Where-Object { $_.Length -ne 0 })
    return [pscustomobject]@{
      ExitCode = [int] $process.ExitCode
      Output = $output
      Text = $text
    }
  } finally {
    if ($process) {
      $process.Dispose()
    }
  }
}

function Wait-FileMarker(
  [string] $Path, [string] $Marker, [int] $TimeoutSeconds = 30
) {
  $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  do {
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
      $text = Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue
      if ($text -and $text.Contains($Marker)) {
        return
      }
    }
    Start-Sleep -Milliseconds 100
  } while ((Get-Date) -lt $deadline)
  throw "Timed out waiting for '$Marker' in $Path"
}

function Get-SampleDriverSpecifications([object] $Sample) {
  $primaryBaseName = if ($Sample.PSObject.Properties['DriverBaseName']) {
    $Sample.DriverBaseName
  } else {
    $Sample.BaseName
  }
  $result = @(
    [pscustomobject]@{
      BaseName = $primaryBaseName
      Service = $Sample.Service
    }
  )
  if ($Sample.PSObject.Properties['AdditionalDrivers']) {
    $result += @($Sample.AdditionalDrivers)
  }
  return @($result)
}

Assert-Administrator
$PackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
Write-Host '=== runtime preflight: package and guest guard passed ==='

$addedCertificates = [Collections.Generic.List[string]]::new()
try {
  foreach ($sample in $samples) {
    Write-Host "=== $($sample.Name): validating runtime artifacts ==="
    foreach ($artifact in Get-SampleApplicationFileNames $sample) {
      $path = Join-Path $PackageRoot $artifact
      if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required WFP runtime artifact was not found: $path"
      }
    }
    $driverSpecifications = @(Get-SampleDriverSpecifications $sample)
    foreach ($driverSpecification in $driverSpecifications) {
      foreach ($artifact in @(
          "$($driverSpecification.BaseName).sys",
          "$($driverSpecification.BaseName).inf",
          "$($driverSpecification.BaseName).cer")) {
        $path = Join-Path $PackageRoot $artifact
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
          throw "Required WFP runtime artifact was not found: $path"
        }
      }
    }
    if ($null -ne $sample.PSObject.Properties['RequiredFiles']) {
      foreach ($fileName in $sample.RequiredFiles) {
        $path = Join-Path $PackageRoot $fileName
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
          throw "Required runtime dependency was not found: $path"
        }
      }
    }
    if ($null -ne $sample.PSObject.Properties['RequiresMsQuicProvider'] -and
        $sample.RequiresMsQuicProvider -and
        -not (Test-Path -LiteralPath (
            Join-Path $env:SystemRoot 'System32\drivers\msquic.sys') `
            -PathType Leaf)) {
      throw (
          "$($sample.Name) requires the inbox MsQuic kernel " +
          'provider (msquic.sys); the suite never installs that provider.')
    }
    foreach ($driverSpecification in $driverSpecifications) {
      $certificate = Join-Path $PackageRoot (
          "$($driverSpecification.BaseName).cer")
      Write-Host (
          "=== $($sample.Name): validating certificate " +
          "$($driverSpecification.BaseName) ===")
      $certificateObject =
          [Security.Cryptography.X509Certificates.X509Certificate2]::new(
              $certificate)
      try {
        $thumbprint = $certificateObject.Thumbprint
      } finally {
        $certificateObject.Dispose()
      }

      foreach ($store in @('Root', 'TrustedPublisher')) {
        $storePath = "Cert:\LocalMachine\$store\$thumbprint"
        if (Test-Path -LiteralPath $storePath -PathType Leaf) {
          continue
        }
        Write-Host (
            "=== $($sample.Name): importing certificate into $store ===")
        & certutil.exe -f -addstore $store $certificate *> $null
        if ($LASTEXITCODE -ne 0) {
          throw (
            "$store certificate import failed for $($sample.Name).")
        }
        $addedCertificates.Add($storePath)
      }
    }
  }

  foreach ($sample in $samples) {
    Write-Host "=== $($sample.Name): preparing driver services ==="
    $application = Join-Path $PackageRoot (
        Get-SampleAcceptanceFileName $sample)
    $driverSpecifications = @(Get-SampleDriverSpecifications $sample)
    foreach ($driverSpecification in $driverSpecifications) {
      Remove-ServiceIfPresent $driverSpecification.Service
    }
    try {
      foreach ($driverSpecification in $driverSpecifications) {
        Install-DriverService $driverSpecification $PackageRoot $sample.Name
      }

      if ($sample.Name -eq 'async-inspection') {
        Write-Host '=== async-inspection: unload/reload race self-test ==='
        $raceController = Join-Path $PackageRoot (
            "$($sample.ControllerBaseName).exe")
        $raceIpcDirectory = Join-Path $PackageRoot (
            "$($sample.Name)-unload-race-ipc")
        Remove-Item -LiteralPath $raceIpcDirectory -Recurse -Force `
            -ErrorAction SilentlyContinue
        try {
          $race = Invoke-SampleApplication `
              -Path $application `
              -Arguments @(
                $raceController, $raceIpcDirectory,
                '--unload-race', '128', $sample.Service) `
              -TimeoutSeconds 180
          $race.Output | ForEach-Object { Write-Host $_ }
          if ($race.ExitCode -ne 0 -or
              -not $race.Text.Contains(
                  'NTL WFP async-inspection unload-race ok:')) {
            throw 'The async-inspection unload/reload race failed.'
          }
        } finally {
          Remove-Item -LiteralPath $raceIpcDirectory -Recurse -Force `
              -ErrorAction SilentlyContinue
        }
      }

      if ($sample.Name -eq 'stream-edit') {
        Write-Host '=== stream-edit: coroutine socket contract ==='
        $selfTest =
            Invoke-SampleApplication $application @('--coroutine-contract')
        $selfTest.Output | ForEach-Object { Write-Host $_ }
        if ($selfTest.ExitCode -ne 0 -or
            -not $selfTest.Text.Contains(
                'Kernel stream-edit coroutine contract PASS:')) {
          throw 'The stream-edit coroutine socket contract failed.'
        }
      }
      if ($sample.Name -eq 'flow-monitor') {
        Write-Host '=== flow-monitor: 20,000-flow load/latency acceptance ==='
        $loadTest = Invoke-SampleApplication `
            -Path $application `
            -Arguments @('--load-test', '10000', '64') `
            -TimeoutSeconds 600
        $loadTest.Output | ForEach-Object { Write-Host $_ }
        if ($loadTest.ExitCode -ne 0 -or
            -not $loadTest.Text.Contains(
                'Kernel flow-monitor acceptance PASS: flows=20000')) {
          throw 'The flow-monitor load/latency acceptance failed.'
        }
      }
      if ($null -ne $sample.PSObject.Properties['FailureMarker']) {
        Write-Host "=== $($sample.Name): fail-closed self-test ==="
        $selfTest =
            Invoke-SampleApplication $application @('--failure-self-test')
        $selfTest.Output | ForEach-Object { Write-Host $_ }
        $failureProofLines = @(
          $selfTest.Output | Where-Object {
            $_.Contains($sample.FailureMarker)
          })
        if ($selfTest.ExitCode -ne 0 -or $failureProofLines.Count -eq 0) {
          throw "The $($sample.Name) fail-closed self-test failed."
        }
        if ($null -ne
            $sample.PSObject.Properties['FailureRequiredMarkerTokens']) {
          foreach ($token in $sample.FailureRequiredMarkerTokens) {
            if (-not ($failureProofLines | Where-Object {
                  $_.Contains($token)
                })) {
              throw (
                "$($sample.Name) fail-closed self-test missed required " +
                "proof token '$token'.")
            }
          }
        }
      }
      for ($iteration = 1; $iteration -le $Iterations; ++$iteration) {
        Write-Host (
          "=== $($sample.Name): iteration $iteration/$Iterations ===")
        $arguments = if (
            $null -ne $sample.PSObject.Properties['Arguments']) {
          @($sample.Arguments)
        } else {
          @()
        }
        if ($sample.Name -eq 'specialized-observation') {
          $arguments += @(
            '--traffic-duration-ms',
            [string] $SpecializedObservationTrafficDurationMs)
          if ($SpecializedObservationRequireMac) {
            $arguments += @('--require-mac', 'true')
          }
          if ($SpecializedObservationRequireVSwitch) {
            $arguments += @('--require-vswitch', 'true')
          }
          if (-not [string]::IsNullOrWhiteSpace(
              $SpecializedObservationTrafficTarget)) {
            $arguments += @(
              '--traffic-target',
              $SpecializedObservationTrafficTarget)
          }
        }
        $acceptanceIpcDirectory = $null
        if ($null -ne
            $sample.PSObject.Properties['AcceptanceUsesControllerIpc'] -and
            $sample.AcceptanceUsesControllerIpc) {
          $controller = Join-Path $PackageRoot (
              "$($sample.ControllerBaseName).exe")
          $acceptanceIpcDirectory = Join-Path $PackageRoot (
              "$($sample.Name)-ipc-$iteration")
          Remove-Item -LiteralPath $acceptanceIpcDirectory -Recurse -Force `
              -ErrorAction SilentlyContinue
          $arguments = @($controller, $acceptanceIpcDirectory)
        }
        $timeoutSeconds = if (
            $null -ne $sample.PSObject.Properties['TimeoutSeconds']) {
          [int] $sample.TimeoutSeconds
        } else {
          120
        }
        try {
          $run = Invoke-SampleApplication `
              -Path $application -Arguments $arguments `
              -TimeoutSeconds $timeoutSeconds
        } finally {
          if ($acceptanceIpcDirectory) {
            Remove-Item -LiteralPath $acceptanceIpcDirectory -Recurse -Force `
                -ErrorAction SilentlyContinue
          }
        }
        $run.Output | ForEach-Object { Write-Host $_ }
        if ($run.ExitCode -ne 0) {
          throw (
            "$($sample.Name) iteration $iteration exited with " +
            "$($run.ExitCode).")
        }
        $proofLines = @(
          $run.Output | Where-Object { $_.Contains($sample.Marker) })
        if ($proofLines.Count -eq 0) {
          throw (
            "$($sample.Name) iteration $iteration missed its proof marker.")
        }
        if ($null -ne $sample.PSObject.Properties['RequiredMarkerTokens']) {
          foreach ($token in $sample.RequiredMarkerTokens) {
            if (-not ($proofLines | Where-Object { $_.Contains($token) })) {
              throw (
                "$($sample.Name) iteration $iteration missed required " +
                "proof token '$token'.")
            }
          }
        }
        if ($iteration -lt $Iterations -and
            $null -ne
                $sample.PSObject.Properties[
                    'ReloadDriversBetweenIterations'] -and
            $sample.ReloadDriversBetweenIterations) {
          Write-Host (
              "=== $($sample.Name): isolated lifetime reload ===")
          for ($index = $driverSpecifications.Count - 1;
               $index -ge 0; --$index) {
            Remove-ServiceIfPresent $driverSpecifications[$index].Service
          }
          foreach ($driverSpecification in $driverSpecifications) {
            Install-DriverService `
                $driverSpecification $PackageRoot $sample.Name
          }
        }
      }
    } finally {
      Write-Host "=== $($sample.Name): driver unload ==="
      for ($index = $driverSpecifications.Count - 1; $index -ge 0; --$index) {
        Remove-ServiceIfPresent $driverSpecifications[$index].Service
      }
    }

    foreach ($driverSpecification in $driverSpecifications) {
      & sc.exe query $driverSpecification.Service *> $null
      if ($LASTEXITCODE -eq 0) {
        throw (
          "$($driverSpecification.Service) remained installed after the suite.")
      }
    }
  }

  Write-Host (
    "Advanced WFP suite passed: $($samples.Count) samples, " +
    "$Iterations iterations each.")
} finally {
  for ($index = $addedCertificates.Count - 1; $index -ge 0; --$index) {
    $storePath = $addedCertificates[$index]
    if (Test-Path -LiteralPath $storePath -PathType Leaf) {
      Remove-Item -LiteralPath $storePath -Force
    }
  }
}
