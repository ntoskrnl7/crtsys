param(
  [Parameter(Mandatory)]
  [string] $PackageRoot,

  [string] $X86AppRoot,

  [switch] $SkipWmi
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Administrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  if (-not $principal.IsInRole(
      [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'The KMDF runtime suite must run from an elevated PowerShell session.'
  }
}

function Find-DevCon {
  $command = Get-Command devcon.exe -ErrorAction SilentlyContinue
  if ($command) {
    return $command.Source
  }

  $tools = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Tools'
  $candidate = Get-ChildItem -Path $tools -Filter devcon.exe -Recurse |
    Where-Object { $_.FullName -match '\\x64\\devcon\.exe$' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
  if (-not $candidate) {
    throw 'devcon.exe was not found. Install the WDK Tools component.'
  }
  return $candidate.FullName
}

function Resolve-SampleFile(
  [string] $Sample,
  [string] $Name,
  [string] $Root = $PackageRoot
) {
  $path = Join-Path (Join-Path $Root $Sample) $Name
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Required runtime artifact was not found: $path"
  }
  return (Resolve-Path -LiteralPath $path).Path
}

function Invoke-Checked(
  [string] $FilePath,
  [string[]] $ArgumentList,
  [string] $ExpectedText = ''
) {
  $output = @(& $FilePath @ArgumentList 2>&1)
  $exitCode = $LASTEXITCODE
  $output | ForEach-Object { Write-Host $_ }
  if ($exitCode -ne 0) {
    throw "$FilePath exited with code $exitCode."
  }

  $joined = $output -join [Environment]::NewLine
  if ($ExpectedText -and -not $joined.Contains($ExpectedText)) {
    throw "$FilePath did not report the expected marker: $ExpectedText"
  }
}

function Invoke-AppPair(
  [string] $Sample,
  [string] $Application,
  [string] $ExpectedText,
  [string[]] $Arguments = @()
) {
  $x64App = Resolve-SampleFile $Sample $Application
  Invoke-Checked $x64App $Arguments $ExpectedText

  if ($X86AppRoot) {
    $x86App = Resolve-SampleFile $Sample $Application $X86AppRoot
    Invoke-Checked $x86App $Arguments $ExpectedText
  }
}

function Remove-PnpDevice([string] $HardwareId) {
  & $script:DevCon remove $HardwareId | ForEach-Object { Write-Host $_ }
  if ($LASTEXITCODE -notin @(0, 1)) {
    throw "devcon remove $HardwareId failed with exit code $LASTEXITCODE."
  }
}

function Install-PnpDevice(
  [string] $Sample,
  [string] $InfName,
  [string] $HardwareId
) {
  $inf = Resolve-SampleFile $Sample $InfName
  & $script:DevCon install $inf $HardwareId |
    ForEach-Object { Write-Host $_ }
  if ($LASTEXITCODE -ne 0) {
    throw "devcon install $HardwareId failed with exit code $LASTEXITCODE."
  }
}

function Add-DriverPackage(
  [string] $Sample,
  [string] $InfName
) {
  $inf = Resolve-SampleFile $Sample $InfName
  & pnputil.exe /add-driver $inf | ForEach-Object { Write-Host $_ }
  if ($LASTEXITCODE -ne 0) {
    throw "pnputil add-driver $inf failed with exit code $LASTEXITCODE."
  }
}

function Restart-PnpDevice([string] $HardwareId) {
  & $script:DevCon restart $HardwareId | ForEach-Object { Write-Host $_ }
  if ($LASTEXITCODE -ne 0) {
    throw "devcon restart $HardwareId failed with exit code $LASTEXITCODE."
  }
}

function Invoke-PnpCase(
  [string] $Sample,
  [string] $InfName,
  [string] $HardwareId,
  [string] $Application,
  [string] $ExpectedText,
  [string[]] $Arguments = @(),
  [bool] $Restart = $true
) {
  Write-Host "=== KMDF runtime: $Sample ==="
  Remove-PnpDevice $HardwareId
  try {
    Install-PnpDevice $Sample $InfName $HardwareId
    Invoke-AppPair $Sample $Application $ExpectedText $Arguments
    if ($Restart) {
      Restart-PnpDevice $HardwareId
      Invoke-AppPair $Sample $Application $ExpectedText $Arguments
    }
  } finally {
    Remove-PnpDevice $HardwareId
  }
}

function Remove-ControlService([string] $ServiceName) {
  & sc.exe query $ServiceName *> $null
  if ($LASTEXITCODE -eq 0) {
    & sc.exe stop $ServiceName *> $null
    & sc.exe delete $ServiceName *> $null
  }
}

function Invoke-BasicCase {
  $serviceName = 'CrtSysKmdfNtlSampleRuntime'
  $driver = Resolve-SampleFile 'basic' 'crtsys_kmdf_ntl_sample.sys'

  Write-Host '=== KMDF runtime: basic ==='
  Remove-ControlService $serviceName
  try {
    & sc.exe create $serviceName 'binPath=' $driver `
      'type=' 'kernel' 'start=' 'demand' | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
      throw "sc create failed with exit code $LASTEXITCODE."
    }

    & sc.exe start $serviceName | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
      throw "sc start failed with exit code $LASTEXITCODE."
    }

    Invoke-AppPair 'basic' 'crtsys_kmdf_ntl_sample_app.exe' `
      'NTL KMDF manual queue ok' @('36')
  } finally {
    Remove-ControlService $serviceName
  }
}

Assert-Administrator
$PackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
if ($X86AppRoot) {
  $X86AppRoot = (Resolve-Path -LiteralPath $X86AppRoot).Path
}
$script:DevCon = Find-DevCon

Invoke-BasicCase

Invoke-PnpCase `
  -Sample 'pnp' `
  -InfName 'crtsys_kmdf_pnp_ntl_sample.inf' `
  -HardwareId 'Root\CrtSysKmdfNtlPnpSample' `
  -Application 'crtsys_kmdf_pnp_ntl_sample_app.exe' `
  -ExpectedText 'NTL KMDF PnP ok' `
  -Arguments @('40')

Invoke-PnpCase `
  -Sample 'echo' `
  -InfName 'crtsys_kmdf_echo_ntl_sample.inf' `
  -HardwareId 'Root\CrtSysKmdfNtlEchoSample' `
  -Application 'crtsys_kmdf_echo_ntl_sample_app.exe' `
  -ExpectedText 'NTL KMDF echo ok'

Invoke-PnpCase `
  -Sample 'reference' `
  -InfName 'crtsys_kmdf_reference.inf' `
  -HardwareId 'Root\CrtSysKmdfReference' `
  -Application 'crtsys_kmdf_reference_app.exe' `
  -ExpectedText 'NTL KMDF reference ok'

# The bus creates a child PDO whose function driver must already be in the
# driver store before the child hardware ID is enumerated.
Add-DriverPackage 'bus' 'crtsys_kmdf_bus_ntl_function.inf'
Invoke-PnpCase `
  -Sample 'bus' `
  -InfName 'crtsys_kmdf_bus_ntl_sample.inf' `
  -HardwareId 'Root\CrtSysKmdfNtlBusSample' `
  -Application 'crtsys_kmdf_bus_ntl_sample_app.exe' `
  -ExpectedText 'NTL KMDF bus lifecycle and typed query interface: PASS' `
  -Restart $false

Invoke-PnpCase `
  -Sample 'filter-stack' `
  -InfName 'crtsys_kmdf_filter_stack_sample.inf' `
  -HardwareId 'Root\CrtSysKmdfNtlFilterStackSample' `
  -Application 'crtsys_kmdf_filter_stack_app.exe' `
  -ExpectedText 'NTL KMDF filter stack ok'

if (-not $SkipWmi) {
  Invoke-PnpCase `
    -Sample 'wmi' `
    -InfName 'crtsys_kmdf_wmi_ntl_sample.inf' `
    -HardwareId 'Root\CrtSysKmdfNtlWmiSample' `
    -Application 'crtsys_kmdf_wmi_ntl_sample_app.exe' `
    -ExpectedText 'NTL KMDF WMI ok'
}

Write-Host 'KMDF software-only runtime suite passed.'
