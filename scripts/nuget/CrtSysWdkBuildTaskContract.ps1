Set-StrictMode -Version Latest

function Resolve-CrtSysWdkBuildTaskVisualStudioVersion {
  param(
    [Parameter(Mandatory = $true)]
    [string] $WindowsKitsRoot,

    [Parameter(Mandatory = $true)]
    [string] $WdkVersion,

    [Parameter(Mandatory = $true)]
    [string] $MsBuildPath
  )

  $msbuildVersion = [version](Get-Item -LiteralPath $MsBuildPath).VersionInfo.FileVersion
  $requestedMajor = $msbuildVersion.Major
  $taskDirectory = Join-Path $WindowsKitsRoot "build\$WdkVersion\bin"
  if (-not (Test-Path -LiteralPath $taskDirectory)) {
    throw "WDK build-task directory was not found: $taskDirectory"
  }

  $availableVersions = @(
    Get-ChildItem -LiteralPath $taskDirectory `
        -Filter 'Microsoft.DriverKit.Build.Tasks.*.dll' -File |
      ForEach-Object {
        if ($_.Name -match '^Microsoft\.DriverKit\.Build\.Tasks\.(\d+\.\d+)\.dll$') {
          [version]$Matches[1]
        }
      } |
      Where-Object { $_.Major -le $requestedMajor } |
      Sort-Object -Descending -Unique
  )
  if ($availableVersions.Count -eq 0) {
    throw "No WDK build-task DLL compatible with MSBuild $requestedMajor was found under $taskDirectory."
  }

  $selected = $availableVersions[0]
  $selectedText = "$($selected.Major).$($selected.Minor)"
  if ($selected.Major -ne $requestedMajor) {
    Write-Host "WDK $WdkVersion provides build tasks $selectedText for MSBuild $requestedMajor; using the installed task ABI."
  }
  return $selectedText
}
