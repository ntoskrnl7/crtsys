param(
  [Parameter(Mandatory = $true)]
  [string] $OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'CrtSysMsQuicHeaderContract.ps1')
$contract = Get-CrtSysMsQuicHeaderContract

$includeDirectory = Join-Path $OutputDirectory 'include'
New-Item -ItemType Directory -Force -Path $includeDirectory | Out-Null

foreach ($file in $contract.Files) {
  $headerPath = Join-Path $includeDirectory $file.Name
  $temporaryPath = Join-Path $includeDirectory "$($file.Name).download.$PID"
  if (-not (Test-CrtSysMsQuicHeaderFile -Path $headerPath `
        -ExpectedSha256 $file.Sha256)) {
    Remove-Item -LiteralPath $temporaryPath -Force -ErrorAction SilentlyContinue
    $lastErrorMessage = ''
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
      try {
        $headerUri =
          "https://raw.githubusercontent.com/microsoft/msquic/$($contract.Revision)/src/inc/$($file.Name)"
        Write-Host "Downloading pinned $($file.Name) (attempt $attempt of 3)."
        Invoke-WebRequest -Uri $headerUri -OutFile $temporaryPath `
          -UseBasicParsing -TimeoutSec 180
        if (-not (Test-CrtSysMsQuicHeaderFile -Path $temporaryPath `
              -ExpectedSha256 $file.Sha256)) {
          $actualSha256 =
            (Get-FileHash -LiteralPath $temporaryPath -Algorithm SHA256).Hash
          throw "$($file.Name) SHA256 mismatch: expected $($file.Sha256), got $actualSha256."
        }
        Move-Item -LiteralPath $temporaryPath -Destination $headerPath -Force
        break
      } catch {
        $lastErrorMessage = $_.Exception.Message
        Remove-Item -LiteralPath $temporaryPath -Force -ErrorAction SilentlyContinue
        if ($attempt -lt 3) {
          Start-Sleep -Seconds $attempt
        }
      }
    }

    if (-not (Test-CrtSysMsQuicHeaderFile -Path $headerPath `
          -ExpectedSha256 $file.Sha256)) {
      throw "Could not stage pinned $($file.Name): $lastErrorMessage"
    }
  }
}

Assert-CrtSysMsQuicHeaderSet -IncludeDirectory $includeDirectory `
  -Description 'Staged MsQuic public header set'

$revisionPath = Join-Path $OutputDirectory 'REVISION.txt'
Set-Content -LiteralPath $revisionPath -Value $contract.Revision -Encoding ASCII

Write-Host "Staged MsQuic $($contract.Revision) public headers at $includeDirectory."
