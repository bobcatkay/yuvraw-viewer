[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallRoot,

    [Parameter(Mandatory = $true)]
    [ValidateSet('x64-windows-static-md', 'x86-windows-static-md')]
    [string]$Triplet,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$licenseDirectory = Join-Path $OutputDirectory 'licenses'
$packageNotices = [ordered]@{
    'libraw' = 'LibRaw.txt'
    'lcms' = 'Little-CMS.txt'
    'zlib' = 'zlib.txt'
    'libjpeg-turbo' = 'libjpeg-turbo.txt'
    'jasper' = 'JasPer.txt'
    'glm' = 'GLM.txt'
}

# Validate all inputs before copying; a missing early license must not be hidden
# by a successful final copy in an MSBuild command sequence.
$copies = @(
    @{ Source = (Join-Path $repoRoot 'LICENSE'); Target = (Join-Path $OutputDirectory 'LICENSE') },
    @{ Source = (Join-Path $repoRoot 'licenses\LGPL-2.1.txt'); Target = (Join-Path $licenseDirectory 'LGPL-2.1.txt') },
    @{ Source = (Join-Path $repoRoot 'licenses\LibRaw-components.txt'); Target = (Join-Path $licenseDirectory 'LibRaw-components.txt') },
    @{ Source = (Join-Path $repoRoot 'docs\SOURCE_DISTRIBUTION.md'); Target = (Join-Path $OutputDirectory 'SOURCE_DISTRIBUTION.md') },
    @{ Source = (Join-Path $repoRoot 'docs\SOURCE_DISTRIBUTION.zh-CN.md'); Target = (Join-Path $OutputDirectory 'SOURCE_DISTRIBUTION.zh-CN.md') }
)
foreach ($package in $packageNotices.Keys) {
    $copies += @{
        Source = (Join-Path $InstallRoot "$Triplet\share\$package\copyright")
        Target = (Join-Path $licenseDirectory $packageNotices[$package])
    }
}
foreach ($copy in $copies) {
    if (-not (Test-Path -LiteralPath $copy.Source -PathType Leaf)) {
        throw "[Licenses] Required file is missing: $($copy.Source)"
    }
}
$noticeHeader = Get-Content -Raw -Encoding UTF8 (Join-Path $repoRoot 'src\Core\FThirdPartyNotices.h')
$noticeMatch = [regex]::Match($noticeHeader, '(?s)R"NOTICE\((.*?)\)NOTICE"')
if (-not $noticeMatch.Success) {
    throw '[Licenses] Cannot read the application third-party notices.'
}

New-Item -ItemType Directory -Force -Path $licenseDirectory | Out-Null
foreach ($copy in $copies) {
    Copy-Item -LiteralPath $copy.Source -Destination $copy.Target -Force
}
$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText(
    (Join-Path $licenseDirectory 'ThirdPartyNotices.txt'),
    $noticeMatch.Groups[1].Value.Trim() + "`n",
    $utf8WithoutBom)
Write-Host '[Licenses] Copied GPL, LGPL and all application dependency notices.'
