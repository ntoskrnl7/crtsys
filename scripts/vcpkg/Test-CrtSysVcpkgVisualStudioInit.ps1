param(
  [string] $WorkDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$initRoot = Join-Path $repoRoot 'vcpkg\ports\crtsys\tools'
$initScript = Join-Path $initRoot 'crtsys-vs-init.ps1'
$initCommand = Join-Path $initRoot 'crtsys-vs-init.cmd'

foreach ($requiredPath in @($initScript, $initCommand)) {
  if (-not (Test-Path -LiteralPath $requiredPath)) {
    throw "Required Visual Studio initializer was not found: $requiredPath"
  }
}

if ([string]::IsNullOrWhiteSpace($WorkDirectory)) {
  $WorkDirectory = Join-Path $repoRoot '.local\vcpkg-vs-init-test'
}
$WorkDirectory = [System.IO.Path]::GetFullPath($WorkDirectory)
$repoRootPrefix = $repoRoot.TrimEnd('\') + '\'
if (-not $WorkDirectory.StartsWith(
    $repoRootPrefix,
    [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "Test path must stay inside the repository: $WorkDirectory"
}

function Write-Utf8Text {
  param(
    [Parameter(Mandatory = $true)][string] $Path,
    [Parameter(Mandatory = $true)][string] $Content
  )

  [System.IO.File]::WriteAllText(
    $Path,
    $Content,
    [System.Text.UTF8Encoding]::new($false)
  )
}

function Assert-Contains {
  param(
    [Parameter(Mandatory = $true)][string] $Content,
    [Parameter(Mandatory = $true)][string] $Token
  )

  if (-not $Content.Contains($Token)) {
    throw "Expected content to contain '$Token'."
  }
}

function Assert-DoesNotContain {
  param(
    [Parameter(Mandatory = $true)][string] $Content,
    [Parameter(Mandatory = $true)][string] $Token
  )

  if ($Content.Contains($Token)) {
    throw "Expected content not to contain '$Token'."
  }
}

function Assert-SingleOccurrence {
  param(
    [Parameter(Mandatory = $true)][string] $Content,
    [Parameter(Mandatory = $true)][string] $Token
  )

  $count = ([regex]::Matches($Content, [regex]::Escape($Token))).Count
  if ($count -ne 1) {
    throw "Expected '$Token' once, got $count occurrences."
  }
}

function Assert-ValidXml {
  param([Parameter(Mandatory = $true)][string] $Path)

  $document = [System.Xml.XmlDocument]::new()
  $document.Load($Path)
  if ($document.DocumentElement.LocalName -ne 'Project') {
    throw "Expected an MSBuild Project root in '$Path'."
  }
}

Remove-Item -LiteralPath $WorkDirectory -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $WorkDirectory | Out-Null

try {
  $existingRoot = Join-Path $WorkDirectory 'existing-project'
  $nestedRoot = Join-Path $existingRoot 'src\driver'
  New-Item -ItemType Directory -Force -Path $nestedRoot | Out-Null
  Write-Utf8Text -Path (Join-Path $existingRoot 'vcpkg.json') -Content @'
{
  "name": "existing-project",
  "version-string": "0",
  "dependencies": ["crtsys"]
}
'@
  Write-Utf8Text -Path (Join-Path $existingRoot 'Directory.Build.props') -Content @'
<Project>
  <PropertyGroup Label="consumer settings">
    <ConsumerProperty>preserved-props</ConsumerProperty>
  </PropertyGroup>
</Project>
'@
  Write-Utf8Text -Path (Join-Path $existingRoot 'Directory.Build.targets') -Content @'
<Project>
  <Target Name="ConsumerTarget">
    <Message Text="preserved-targets" />
  </Target>
</Project>
'@

  & $initScript -ProjectRoot $nestedRoot -Triplet x64-windows-static

  $propsPath = Join-Path $existingRoot 'Directory.Build.props'
  $targetsPath = Join-Path $existingRoot 'Directory.Build.targets'
  $props = Get-Content -LiteralPath $propsPath -Raw
  $targets = Get-Content -LiteralPath $targetsPath -Raw
  Assert-Contains -Content $props -Token 'preserved-props'
  Assert-Contains -Content $props -Token '<VcpkgEnableManifest'
  Assert-Contains -Content $props -Token 'x64-windows-static'
  Assert-SingleOccurrence -Content $props -Token 'crtsys-vcpkg-init:props:begin'
  Assert-Contains -Content $targets -Token 'preserved-targets'
  Assert-Contains -Content $targets -Token 'crtsys-vcpkg.targets'
  Assert-SingleOccurrence -Content $targets -Token 'crtsys-vcpkg-init:targets:begin'
  Assert-ValidXml -Path $propsPath
  Assert-ValidXml -Path $targetsPath

  $propsHash = (Get-FileHash -LiteralPath $propsPath -Algorithm SHA256).Hash
  $targetsHash = (Get-FileHash -LiteralPath $targetsPath -Algorithm SHA256).Hash
  & $initScript -ProjectRoot $existingRoot -Triplet x64-windows-static
  if ((Get-FileHash -LiteralPath $propsPath -Algorithm SHA256).Hash -ne $propsHash -or
      (Get-FileHash -LiteralPath $targetsPath -Algorithm SHA256).Hash -ne $targetsHash) {
    throw 'Idempotent initializer retry changed the MSBuild files.'
  }

  & $initScript -ProjectRoot $existingRoot -Triplet x64-company-static
  $props = Get-Content -LiteralPath $propsPath -Raw
  Assert-Contains -Content $props -Token 'x64-company-static'
  Assert-DoesNotContain -Content $props -Token '>x64-windows-static<'
  Assert-SingleOccurrence -Content $props -Token 'crtsys-vcpkg-init:props:begin'

  & $initScript -ProjectRoot $existingRoot -Remove
  $props = Get-Content -LiteralPath $propsPath -Raw
  $targets = Get-Content -LiteralPath $targetsPath -Raw
  Assert-Contains -Content $props -Token 'preserved-props'
  Assert-DoesNotContain -Content $props -Token 'crtsys-vcpkg-init:'
  Assert-Contains -Content $targets -Token 'preserved-targets'
  Assert-DoesNotContain -Content $targets -Token 'crtsys-vcpkg-init:'
  Assert-ValidXml -Path $propsPath
  Assert-ValidXml -Path $targetsPath

  $generatedRoot = Join-Path $WorkDirectory 'generated-project'
  New-Item -ItemType Directory -Force -Path $generatedRoot | Out-Null
  Write-Utf8Text -Path (Join-Path $generatedRoot 'vcpkg.json') -Content @'
{
  "name": "generated-project",
  "version-string": "0",
  "dependencies": ["crtsys"]
}
'@

  & $initCommand -ProjectRoot $generatedRoot -Triplet x86-windows-static
  if ($LASTEXITCODE -ne 0) {
    throw "crtsys-vs-init.cmd failed with exit code $LASTEXITCODE."
  }
  foreach ($generatedPath in @(
    (Join-Path $generatedRoot 'Directory.Build.props'),
    (Join-Path $generatedRoot 'Directory.Build.targets')
  )) {
    if (-not (Test-Path -LiteralPath $generatedPath)) {
      throw "Initializer did not generate '$generatedPath'."
    }
    Assert-ValidXml -Path $generatedPath
  }

  & $initCommand -ProjectRoot $generatedRoot -Remove
  if ($LASTEXITCODE -ne 0) {
    throw "crtsys-vs-init.cmd -Remove failed with exit code $LASTEXITCODE."
  }
  foreach ($generatedPath in @(
    (Join-Path $generatedRoot 'Directory.Build.props'),
    (Join-Path $generatedRoot 'Directory.Build.targets')
  )) {
    if (Test-Path -LiteralPath $generatedPath) {
      throw "Initializer removal left its generated file behind: $generatedPath"
    }
  }

  Write-Host 'crtsys vcpkg Visual Studio initializer contract passed.'
} finally {
  Remove-Item -LiteralPath $WorkDirectory -Recurse -Force -ErrorAction SilentlyContinue
}
