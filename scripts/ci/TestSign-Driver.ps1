param(
  [Parameter(Mandatory = $true)]
  [string] $DriverPath,

  [string] $CertificateSubject = 'CN=crtsys CI Test Signing',

  [string] $WorkDir = (Join-Path $PWD 'artifacts\test-signing')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Find-SignTool {
  $kitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
  if (Test-Path $kitsRoot) {
    $candidate = Get-ChildItem -Path $kitsRoot -Recurse -Filter signtool.exe |
      Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
      Sort-Object FullName -Descending |
      Select-Object -First 1

    if ($candidate) {
      return $candidate.FullName
    }
  }

  $fromPath = Get-Command signtool.exe -ErrorAction SilentlyContinue
  if ($fromPath) {
    return $fromPath.Source
  }

  throw 'signtool.exe was not found. Install the Windows Driver Kit on this machine.'
}

$resolvedDriverPath = (Resolve-Path $DriverPath).Path
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

$passwordText = ([guid]::NewGuid().ToString('N') + [guid]::NewGuid().ToString('N'))
$password = ConvertTo-SecureString -String $passwordText -Force -AsPlainText

$cert = New-SelfSignedCertificate `
  -Type CodeSigningCert `
  -Subject $CertificateSubject `
  -CertStoreLocation Cert:\CurrentUser\My `
  -KeyExportPolicy Exportable `
  -KeyUsage DigitalSignature `
  -HashAlgorithm SHA256 `
  -NotAfter (Get-Date).AddDays(7)

try {
  $pfxPath = Join-Path $WorkDir 'crtsys-test-signing.pfx'
  $cerPath = Join-Path $WorkDir 'crtsys-test-signing.cer'

  Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $password | Out-Null
  Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null

  $signTool = Find-SignTool

  Write-Host "Test-signing $resolvedDriverPath"
  & $signTool sign /v /fd SHA256 /f $pfxPath /p $passwordText $resolvedDriverPath
  if ($LASTEXITCODE -ne 0) {
    throw "signtool failed with exit code $LASTEXITCODE."
  }

  $signature = Get-AuthenticodeSignature $resolvedDriverPath
  if (-not $signature.SignerCertificate) {
    throw "The driver does not contain an Authenticode signature."
  }

  Write-Host "Signed by $($signature.SignerCertificate.Subject)"
} finally {
  if ($cert) {
    Remove-Item -Path "Cert:\CurrentUser\My\$($cert.Thumbprint)" -ErrorAction SilentlyContinue
  }
}
