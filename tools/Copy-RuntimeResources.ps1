[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$fontRoot = Join-Path $repoRoot 'resources/fonts'
$fontFile = 'NotoSansCJKsc-Regular.otf'
$expectedFontSha256 = '2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b'
foreach ($name in @($fontFile,'OFL-1.1.txt','README.md','README.zh-CN.md')) {
    if (-not (Test-Path -LiteralPath (Join-Path $fontRoot $name) -PathType Leaf)) { throw "[Resources] Missing $name" }
}
$fontStream = [IO.File]::OpenRead((Join-Path $fontRoot $fontFile))
$hasher = [Security.Cryptography.SHA256]::Create()
try { $fontHash = [BitConverter]::ToString($hasher.ComputeHash($fontStream)).Replace('-','') }
finally { $hasher.Dispose(); $fontStream.Dispose() }
if ($fontHash -ne $expectedFontSha256) {
    throw '[Resources] Bundled font differs from the reviewed upstream release.'
}
$destination = Join-Path $OutputDirectory 'resources/fonts'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
foreach ($name in @($fontFile,'OFL-1.1.txt','README.md','README.zh-CN.md')) { Copy-Item -LiteralPath (Join-Path $fontRoot $name) -Destination $destination -Force }
Write-Host '[Resources] Copied the complete Chinese fallback font and its license.'
