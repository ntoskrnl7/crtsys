Set-StrictMode -Version Latest

function Resolve-CrtSysDumpbinExecutable {
  $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
  if ($command) {
    return $command.Source
  }

  $vswhere = Join-Path ${env:ProgramFiles(x86)} `
    'Microsoft Visual Studio\Installer\vswhere.exe'
  if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "vswhere.exe was not found: $vswhere"
  }

  $installationPaths = @(
    & $vswhere -all -products * `
      -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
      -property installationPath
  )
  foreach ($installationPath in $installationPaths) {
    $toolRoot = Join-Path $installationPath 'VC\Tools\MSVC'
    if (-not (Test-Path -LiteralPath $toolRoot)) {
      continue
    }

    $candidate = Get-ChildItem -LiteralPath $toolRoot -Directory |
      Sort-Object { [version]$_.Name } -Descending |
      ForEach-Object {
        Join-Path $_.FullName 'bin\Hostx64\x64\dumpbin.exe'
      } |
      Where-Object { Test-Path -LiteralPath $_ } |
      Select-Object -First 1
    if ($candidate) {
      return $candidate
    }
  }

  throw 'dumpbin.exe was not found in any Visual Studio installation.'
}

function Assert-CrtSysStaticCrtDirectives {
  param(
    [Parameter(Mandatory = $true)]
    [string] $LibraryPath,

    [Parameter(Mandatory = $true)]
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration,

    [string] $DumpbinPath
  )

  $LibraryPath = (Resolve-Path -LiteralPath $LibraryPath).Path
  if ([string]::IsNullOrWhiteSpace($DumpbinPath)) {
    $DumpbinPath = Resolve-CrtSysDumpbinExecutable
  }

  $directiveOutput = & $DumpbinPath /nologo /directives $LibraryPath 2>&1 |
    Out-String
  if ($LASTEXITCODE -ne 0) {
    throw "dumpbin failed for '$LibraryPath' with exit code $LASTEXITCODE."
  }

  if ($directiveOutput -match '(?im)/DEFAULTLIB:MSVCRTD?(?:\s|$)') {
    throw "Library embeds a dynamic MSVC CRT directive: $LibraryPath"
  }

  if ($directiveOutput -match '(?im)/DEFAULTLIB:LIBCMTD(?:\s|$)') {
    throw "Library embeds the user-mode debug CRT directive: $LibraryPath"
  }

  if ($directiveOutput -match '(?im)/DEFAULTLIB:LIBCMT(?:\s|$)') {
    Write-Host "$LibraryPath uses the static MSVC runtime (LIBCMT)."
  } else {
    Write-Host "$LibraryPath injects no user-mode MSVC CRT default library."
  }
}
