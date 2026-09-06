[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$utf8 = New-Object Text.UTF8Encoding($false, $true)
$copies = [ordered]@{
    'README.md' = 'docs/QUICK_START.md'
    'README.zh-CN.md' = 'docs/QUICK_START.zh-CN.md'
    'THIRD_PARTY_NOTICES.md' = 'THIRD_PARTY_NOTICES.md'
    'THIRD_PARTY_NOTICES.zh-CN.md' = 'THIRD_PARTY_NOTICES.zh-CN.md'
}
foreach ($source in $copies.Values) {
    if (!(Test-Path -LiteralPath (Join-Path $repoRoot $source) -PathType Leaf)) {
        throw "[ReleaseDocs] Required document is missing: $source"
    }
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
foreach ($target in $copies.Keys) {
    $text = [IO.File]::ReadAllText((Join-Path $repoRoot $copies[$target]), $utf8)
    if ($target -like 'README*') {
        # Quick-start pages become the portable package's README pair. Only the
        # language navigation changes; source-tree links otherwise stay intact.
        $text = $text.Replace('](QUICK_START.md)', '](README.md)').Replace('](QUICK_START.zh-CN.md)', '](README.zh-CN.md)')
    }
    [IO.File]::WriteAllText((Join-Path $OutputDirectory $target), $text, $utf8)
}
Write-Host '[ReleaseDocs] Copied English and Chinese release documentation.'
