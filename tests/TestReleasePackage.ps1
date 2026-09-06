[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ReleaseDirectory,
    [Parameter(Mandatory = $true)][ValidatePattern('^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$')][string]$Version,
    [ValidateSet('x64','x86')][string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$repoRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = 'YUVRaw-source/'
$binaryName = "YUVRaw-$Version-windows-$Platform.zip"
$sourceName = "YUVRaw-$Version-source.zip"
$checksumsName = "YUVRaw-$Version-SHA256SUMS.txt"
$expectedChecksumCount = 2
$dosMagic = 0x5A4D
$peSignature = 0x00004550
$peHeaderOffsetPosition = 0x3C
$minimumPeBytes = 64
$machineType = if ($Platform -eq 'x64') { 0x8664 } else { 0x014C }
$utf8 = New-Object Text.UTF8Encoding($false, $true)
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

function Assert-Release {
    param([bool]$Condition, [string]$Message)
    if (!$Condition) { throw "[ReleasePackageTest] $Message" }
}

function Get-EntryHash {
    param([IO.Compression.ZipArchiveEntry]$Entry)
    Assert-Release ($null -ne $Entry) 'Cannot hash a missing ZIP entry.'
    $stream = $Entry.Open()
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $hasher.Dispose(); $stream.Dispose() }
}

function Read-EntryText {
    param([IO.Compression.ZipArchive]$Archive, [string]$Name)
    $entry = $Archive.GetEntry($Name)
    Assert-Release ($null -ne $entry) "Missing source entry: $Name"
    $reader = New-Object IO.StreamReader($entry.Open(), $utf8, $true)
    try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
}

function Assert-CanonicalEntries {
    param([IO.Compression.ZipArchive]$Archive, [string]$Label)
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $Archive.Entries) {
        $name = $entry.FullName
        Assert-Release ($name -and $name -notmatch '\\|:|^/|(^|/)\.\.?(/|$)|//') "$Label has an unsafe or nonportable ZIP path: $name"
        Assert-Release ($seen.Add($name)) "$Label has a duplicate or case-colliding ZIP path: $name"
    }
}

$releasePath = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
$artifactNames = @($binaryName,$sourceName,$checksumsName)
foreach ($file in Get-ChildItem -LiteralPath $releasePath -File) {
    Assert-Release ($artifactNames -ccontains $file.Name) "Unexpected file beside release artifacts: $($file.Name)"
}
foreach ($name in $artifactNames) {
    Assert-Release (Test-Path -LiteralPath (Join-Path $releasePath $name) -PathType Leaf) "Missing release artifact: $name"
}
$checksumLines = @([IO.File]::ReadAllLines((Join-Path $releasePath $checksumsName), $utf8))
Assert-Release ($checksumLines.Count -eq $expectedChecksumCount) 'Checksum file must list exactly the binary ZIP and source ZIP.'
$checksums = @{}
foreach ($line in $checksumLines) {
    $match = [regex]::Match($line, '^([a-fA-F0-9]{64})  ([^/\\]+)$')
    Assert-Release $match.Success 'Malformed SHA256 checksum line.'
    $name = $match.Groups[2].Value
    Assert-Release (@($binaryName,$sourceName) -ccontains $name) "Unexpected checksum filename: $name"
    Assert-Release (!$checksums.ContainsKey($name)) "Duplicate checksum filename: $name"
    $checksums[$name] = $match.Groups[1].Value
    Assert-Release ((Get-FileHash -LiteralPath (Join-Path $releasePath $name) -Algorithm SHA256).Hash -eq $checksums[$name]) "SHA256 mismatch: $name"
}

# This is the public ZIP contract, deliberately independent of the packaging
# implementation. Additional files require an explicit review of this list.
$binaryFiles = @(
    'YUVRaw.exe','LICENSE','README.md','SOURCE_DISTRIBUTION.md','SOURCE_CODE.txt',
    'README.zh-CN.md','SOURCE_DISTRIBUTION.zh-CN.md','THIRD_PARTY_NOTICES.zh-CN.md','resources/fonts/README.zh-CN.md',
    'THIRD_PARTY_NOTICES.md','third-party/sbom.cdx.json',
    'licenses/LGPL-2.1.txt','licenses/ThirdPartyNotices.txt','licenses/LibRaw.txt','licenses/LibRaw-components.txt',
    'licenses/Little-CMS.txt','licenses/zlib.txt','licenses/libjpeg-turbo.txt',
    'licenses/JasPer.txt','licenses/GLM.txt',
    'resources/fonts/NotoSansCJKsc-Regular.otf','resources/fonts/OFL-1.1.txt','resources/fonts/README.md'
)
$sourceCopies = [ordered]@{
    'LICENSE' = 'LICENSE'
    'README.md' = 'docs/QUICK_START.md'
    'README.zh-CN.md' = 'docs/QUICK_START.zh-CN.md'
    'THIRD_PARTY_NOTICES.md' = 'THIRD_PARTY_NOTICES.md'
    'THIRD_PARTY_NOTICES.zh-CN.md' = 'THIRD_PARTY_NOTICES.zh-CN.md'
    'third-party/sbom.cdx.json' = 'third-party/sbom.cdx.json'
    'SOURCE_DISTRIBUTION.md' = 'docs/SOURCE_DISTRIBUTION.md'
    'SOURCE_DISTRIBUTION.zh-CN.md' = 'docs/SOURCE_DISTRIBUTION.zh-CN.md'
    'licenses/LGPL-2.1.txt' = 'licenses/LGPL-2.1.txt'
    'licenses/LibRaw.txt' = 'third-party/provenance/libraw/copyright'
    'licenses/LibRaw-components.txt' = 'licenses/LibRaw-components.txt'
    'licenses/Little-CMS.txt' = 'third-party/provenance/lcms/copyright'
    'licenses/zlib.txt' = 'third-party/provenance/zlib/copyright'
    'licenses/libjpeg-turbo.txt' = 'third-party/provenance/libjpeg-turbo/copyright'
    'licenses/JasPer.txt' = 'third-party/provenance/jasper/copyright'
    'licenses/GLM.txt' = 'third-party/provenance/glm/copyright'
    'resources/fonts/NotoSansCJKsc-Regular.otf' = 'resources/fonts/NotoSansCJKsc-Regular.otf'
    'resources/fonts/OFL-1.1.txt' = 'resources/fonts/OFL-1.1.txt'
    'resources/fonts/README.md' = 'resources/fonts/README.md'
    'resources/fonts/README.zh-CN.md' = 'resources/fonts/README.zh-CN.md'
}
$binaryZip = [IO.Compression.ZipFile]::OpenRead((Join-Path $releasePath $binaryName))
$sourceZip = $null
try {
    Assert-CanonicalEntries $binaryZip 'Binary archive'
    foreach ($entry in $binaryZip.Entries) {
        Assert-Release ($binaryFiles -ccontains $entry.FullName) "Unexpected binary entry: $($entry.FullName)"
        Assert-Release ($entry.Length -gt 0) "Empty binary entry: $($entry.FullName)"
    }
    foreach ($name in $binaryFiles) {
        Assert-Release ($null -ne $binaryZip.GetEntry($name)) "Missing binary entry: $name"
    }
    Assert-Release ($binaryZip.Entries.Count -eq $binaryFiles.Count) 'Binary archive differs from the reviewed file list.'

    # Extract only the checked executable under a unique workspace test folder.
    # Never execute a program from the candidate ZIP during package inspection.
    $testDirectory = Join-Path $repoRoot ('artifacts/release-package-tests/' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $testDirectory | Out-Null
    $exePath = Join-Path $testDirectory 'YUVRaw.exe'
    [IO.Compression.ZipFileExtensions]::ExtractToFile($binaryZip.GetEntry('YUVRaw.exe'), $exePath, $false)
    $fileVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($exePath)
    Assert-Release ($fileVersion.FileVersion -ceq $Version -and $fileVersion.ProductVersion -ceq $Version) "EXE VERSIONINFO mismatch: expected $Version; file='$($fileVersion.FileVersion)' product='$($fileVersion.ProductVersion)'."
    $parts = @($Version.Split('.') | ForEach-Object { [int]$_ })
    $expectedNumbers = @($parts[0],$parts[1],$parts[2],0) -join '.'
    $fileNumbers = @($fileVersion.FileMajorPart,$fileVersion.FileMinorPart,$fileVersion.FileBuildPart,$fileVersion.FilePrivatePart) -join '.'
    $productNumbers = @($fileVersion.ProductMajorPart,$fileVersion.ProductMinorPart,$fileVersion.ProductBuildPart,$fileVersion.ProductPrivatePart) -join '.'
    Assert-Release ($fileNumbers -eq $expectedNumbers -and $productNumbers -eq $expectedNumbers) 'EXE numeric version fields disagree with the release version.'
    Assert-Release ($fileVersion.ProductName -ceq 'YUVRaw' -and $fileVersion.OriginalFilename -ceq 'YUVRaw.exe') 'EXE VERSIONINFO product identity is incorrect.'

    $reader = New-Object IO.BinaryReader([IO.File]::OpenRead($exePath))
    try {
        Assert-Release ($reader.BaseStream.Length -ge $minimumPeBytes -and $reader.ReadUInt16() -eq $dosMagic) 'EXE has no valid DOS/PE header.'
        $reader.BaseStream.Position = $peHeaderOffsetPosition
        $peOffset = $reader.ReadInt32()
        Assert-Release ($peOffset -ge $minimumPeBytes -and $peOffset -le $reader.BaseStream.Length - $minimumPeBytes) 'EXE PE header offset is out of range.'
        $reader.BaseStream.Position = $peOffset
        Assert-Release ($reader.ReadUInt32() -eq $peSignature -and $reader.ReadUInt16() -eq $machineType) "EXE architecture does not match $Platform."
    } finally { $reader.Dispose() }

    $sourceZip = [IO.Compression.ZipFile]::OpenRead((Join-Path $releasePath $sourceName))
    Assert-CanonicalEntries $sourceZip 'Source archive'
    $header = Read-EntryText $sourceZip ($sourceRoot + 'src/Core/FAppVersion.h')
    $sourceVersionParts = @(foreach ($name in @('MAJOR','MINOR','PATCH')) {
        $matches = [regex]::Matches($header, '(?m)^#define\s+YUVRAW_VERSION_' + $name + '\s+(\d+)\s*$')
        Assert-Release ($matches.Count -eq 1) "Source version header has an invalid $name definition."
        $matches[0].Groups[1].Value
    })
    Assert-Release (($sourceVersionParts -join '.') -ceq $Version) 'Source version header differs from the release version.'

    foreach ($name in $sourceCopies.Keys) {
        $sourceEntry = $sourceZip.GetEntry($sourceRoot + $sourceCopies[$name])
        Assert-Release ($null -ne $sourceEntry) "Missing corresponding source material: $($sourceCopies[$name])"
        if ($name -in @('README.md','README.zh-CN.md')) {
            # Portable quick starts have different filenames; only language links may differ.
            $expectedText = (Read-EntryText $sourceZip ($sourceRoot + $sourceCopies[$name])).Replace(
                '](QUICK_START.md)', '](README.md)').Replace('](QUICK_START.zh-CN.md)', '](README.zh-CN.md)')
            Assert-Release ((Read-EntryText $binaryZip $name) -ceq $expectedText) "Binary/source content mismatch: $name"
        } else {
            Assert-Release ((Get-EntryHash $binaryZip.GetEntry($name)) -eq (Get-EntryHash $sourceEntry)) "Binary/source content mismatch: $name"
        }
    }
    # Copy-ThirdPartyNotices exports this raw C++ literal; compare against the
    # source shipped in this release, rather than trusting current build output.
    $noticeHeader = Read-EntryText $sourceZip ($sourceRoot + 'src/Core/FThirdPartyNotices.h')
    $notice = [regex]::Match($noticeHeader, '(?s)R"NOTICE\((.*?)\)NOTICE"')
    Assert-Release $notice.Success 'Source has no complete third-party notice literal.'
    $expectedNotice = $notice.Groups[1].Value.Trim() + "`n"
    Assert-Release ((Read-EntryText $binaryZip 'licenses/ThirdPartyNotices.txt') -ceq $expectedNotice) 'Third-party notices differ from the corresponding source.'

    $instructions = Read-EntryText $binaryZip 'SOURCE_CODE.txt'
    $instructionTemplate = Read-EntryText $sourceZip ($sourceRoot + 'docs/SOURCE_CODE.txt')
    $expectedInstructions = $instructionTemplate.TrimEnd().Replace('{{version}}', $Version).
        Replace('{{sourceName}}', $sourceName).Replace('{{binaryName}}', $binaryName).
        Replace('{{checksumsName}}', $checksumsName).Replace('{{platform}}', $Platform)
    Assert-Release ($instructions -ceq $expectedInstructions) 'Source instructions differ from the corresponding bilingual template.'
    foreach ($name in @($binaryName,$sourceName,$checksumsName)) {
        Assert-Release ($instructions.Contains($name)) "SOURCE_CODE.txt does not identify $name."
    }
    Assert-Release ($instructions.Contains('SOURCE_DISTRIBUTION.md') -and $instructions.Contains('Microsoft Visual C++')) 'SOURCE_CODE.txt is missing build/relink or Windows runtime instructions.'
    Assert-Release ($instructions.Contains('SOURCE_DISTRIBUTION.zh-CN.md')) 'SOURCE_CODE.txt is missing Chinese source instructions.'
    foreach ($name in @('README.md','README.zh-CN.md','SOURCE_DISTRIBUTION.md','SOURCE_DISTRIBUTION.zh-CN.md',
        'THIRD_PARTY_NOTICES.md','THIRD_PARTY_NOTICES.zh-CN.md','resources/fonts/README.md','resources/fonts/README.zh-CN.md')) {
        $other = if ($name -like '*.zh-CN.md') { $name.Replace('.zh-CN.md','.md') } else { $name.Replace('.md','.zh-CN.md') }
        $link = '](' + [IO.Path]::GetFileName($other) + ')'
        Assert-Release (((Read-EntryText $binaryZip $name) -split '\r?\n', 2)[0].Contains($link)) "Missing portable language link: $name"
    }
} finally {
    if ($sourceZip) { $sourceZip.Dispose() }
    $binaryZip.Dispose()
}

# The source verifier owns source inventory, dependency provenance and source
# completeness checks, including its real corrupted-source negative cases.
$triplet = if ($Platform -eq 'x86') { 'x86-windows-static-md' } else { 'x64-windows-static-md' }
& (Join-Path $PSScriptRoot 'TestSourceDistribution.ps1') -SourceArchivePath (Join-Path $releasePath $sourceName) -Triplet $triplet
Write-Host "[ReleasePackageTest] Verified $Version Windows ${Platform}: checksums, exact file list, licenses/resources, binary/source versions and corresponding source."
