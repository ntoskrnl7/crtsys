function Get-CrtSysMsQuicHeaderContract {
  [CmdletBinding()]
  param()

  return [pscustomobject]@{
    Revision = 'b3945bb0c9e44463c93dac13e40975a7c3a526ca'
    Files = @(
      [pscustomobject]@{
        Name = 'msquic.h'
        Sha256 = 'C9ABFDD02C45910649DD335D6BD82718E4DDD2FDB35FE550567C78F032551E0C'
      }
      [pscustomobject]@{
        Name = 'msquic_winuser.h'
        Sha256 = '7C54AEA27C784BD9F2F609668F7141E299F0E35209015BD559A5BA12AA136D08'
      }
      [pscustomobject]@{
        Name = 'msquic_winkernel.h'
        Sha256 = '153A3B639E6494DC1E978A4C921CB2A62B6D10BF5FDB6039A37234C147C3CC65'
      }
    )
  }
}

function Test-CrtSysMsQuicHeaderFile {
  [CmdletBinding()]
  param(
    [Parameter(Mandatory = $true)]
    [string] $Path,

    [Parameter(Mandatory = $true)]
    [string] $ExpectedSha256
  )

  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    return $false
  }

  $actualSha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
  return $actualSha256 -eq $ExpectedSha256
}

function Assert-CrtSysMsQuicHeaderSet {
  [CmdletBinding()]
  param(
    [Parameter(Mandatory = $true)]
    [string] $IncludeDirectory,

    [string] $Description = 'MsQuic public header set'
  )

  $contract = Get-CrtSysMsQuicHeaderContract
  foreach ($file in $contract.Files) {
    $path = Join-Path $IncludeDirectory $file.Name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
      throw "$Description is missing $($file.Name): $path"
    }
    $actualSha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actualSha256 -ne $file.Sha256) {
      throw "$Description has an invalid $($file.Name) SHA256: expected $($file.Sha256), got $actualSha256."
    }
  }
}
