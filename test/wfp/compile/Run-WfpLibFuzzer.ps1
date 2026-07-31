[CmdletBinding()]
param(
  [ValidateRange(1, 86400)]
  [int] $Seconds = 300,

  [string] $BuildDirectory = '',

  [string] $Generator = 'Visual Studio 17 2022',

  [ValidateSet('x64')]
  [string] $Architecture = 'x64'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$source = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
  $BuildDirectory = Join-Path $source 'build_libfuzzer_clang'
}
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)

& cmake -S $source -B $BuildDirectory `
    -G $Generator -A $Architecture -T ClangCL `
    -DCRTSYS_WFP_ENABLE_LIBFUZZER=ON
if ($LASTEXITCODE -ne 0) {
  throw "Configuring the WFP libFuzzer target failed: $LASTEXITCODE"
}
& cmake --build $BuildDirectory --config Release `
    --target crtsys_wfp_protocol_libfuzzer --parallel
if ($LASTEXITCODE -ne 0) {
  throw "Building the WFP libFuzzer target failed: $LASTEXITCODE"
}

$corpus = Join-Path $BuildDirectory 'corpus'
$artifacts = Join-Path $BuildDirectory 'artifacts'
New-Item -ItemType Directory -Path $corpus -Force | Out-Null
New-Item -ItemType Directory -Path $artifacts -Force | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllBytes(
    (Join-Path $corpus 'http1-request'),
    $utf8.GetBytes(
        "GET / HTTP/1.1`r`nHost: example.test`r`n" +
        "Content-Length: 4`r`n`r`ntest"))
[IO.File]::WriteAllBytes(
    (Join-Path $corpus 'http1-chunked'),
    $utf8.GetBytes(
        "HTTP/1.1 200 OK`r`nTransfer-Encoding: chunked`r`n`r`n" +
        "4`r`ntest`r`n0`r`n`r`n"))
[IO.File]::WriteAllBytes(
    (Join-Path $corpus 'tls-record-prefix'),
    [byte[]] @(0x16, 0x03, 0x03, 0x00, 0x00))

$executable = Join-Path $BuildDirectory `
    'Release\crtsys_wfp_protocol_libfuzzer.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
  throw "The WFP libFuzzer executable was not produced: $executable"
}
$dictionary = Join-Path $source 'wfp-fuzz.dict'
$env:ASAN_OPTIONS =
    'abort_on_error=1:allocator_may_return_null=1:detect_leaks=0'
& $executable $corpus `
    "-dict=$dictionary" `
    '-max_len=2048' `
    '-timeout=10' `
    "-max_total_time=$Seconds" `
    '-print_final_stats=1' `
    "-artifact_prefix=$artifacts\"
if ($LASTEXITCODE -ne 0) {
  throw "The WFP libFuzzer run failed: $LASTEXITCODE"
}
