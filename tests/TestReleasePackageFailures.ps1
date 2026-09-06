[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path $repoRoot ('artifacts/release-verifier-failures/' + [guid]::NewGuid().ToString('N'))
$fixtureVersion = '1.2.3'
$mismatchedVersion = '1.2.4'
$triplet = 'x64-windows-static-md'
$utf8 = New-Object Text.UTF8Encoding($false)
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null

function Write-Utf8File {
    param([string]$Path, [string]$Text)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    [IO.File]::WriteAllText($Path, $Text, $utf8)
}

function Write-FixtureChecksums {
    param([string]$Directory, [string]$ReleaseVersion)
    $names = @("YUVRaw-$ReleaseVersion-windows-x64.zip", "YUVRaw-$ReleaseVersion-source.zip")
    $lines = @($names | ForEach-Object {
        (Get-FileHash -LiteralPath (Join-Path $Directory $_) -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $_
    })
    [IO.File]::WriteAllLines((Join-Path $Directory "YUVRaw-$ReleaseVersion-SHA256SUMS.txt"), [string[]]$lines, $utf8)
}

function Expect-Rejection {
    param([string]$Directory, [string]$ReleaseVersion, [string]$ExpectedReason)
    $message = ''
    try { & (Join-Path $PSScriptRoot 'TestReleasePackage.ps1') -ReleaseDirectory $Directory -Version $ReleaseVersion -Platform x64 }
    catch { $message = $_.Exception.Message }
    if (!$message.Contains($ExpectedReason)) {
        throw "[ReleaseVerifierFailures] Expected rejection '$ExpectedReason', got '$message'."
    }
    Write-Host "[ReleaseVerifierFailures] Correctly rejected: $ExpectedReason"
}

function Copy-Fixture {
    param([string]$CaseName)
    $directory = Join-Path $testRoot $CaseName
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath $baseline -File) {
        Copy-Item -LiteralPath $file.FullName -Destination $directory
    }
    return $directory
}

# A harmless native PE fixture supplies real Windows version resources without
# depending on a release build or executing code from a ZIP.
$cpp = Join-Path $testRoot 'fixture.cpp'
$rc = Join-Path $testRoot 'fixture.rc'
$res = Join-Path $testRoot 'fixture.res'
$fixtureExe = Join-Path $testRoot 'YUVRaw.exe'
Write-Utf8File $cpp '#include <Windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return 0; }
'
Write-Utf8File $rc '#include <winver.h>
VS_VERSION_INFO VERSIONINFO
 FILEVERSION 1,2,3,0
 PRODUCTVERSION 1,2,3,0
 FILEFLAGSMASK VS_FFI_FILEFLAGSMASK
 FILEOS VOS_NT_WINDOWS32
 FILETYPE VFT_APP
BEGIN
 BLOCK "StringFileInfo"
 BEGIN
  BLOCK "040904b0"
  BEGIN
   VALUE "FileVersion", "1.2.3\0"
   VALUE "ProductVersion", "1.2.3\0"
   VALUE "OriginalFilename", "YUVRaw.exe\0"
   VALUE "ProductName", "YUVRaw\0"
  END
 END
 BLOCK "VarFileInfo"
 BEGIN
  VALUE "Translation", 0x0409, 1200
 END
END
'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
if (!$installation) { throw 'Visual Studio C++ tools are required to create the PE fixture.' }
$vsdev = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
$compile = '"' + $vsdev + '" -arch=amd64 -host_arch=amd64 >nul 2>&1 && rc /nologo /fo "' + $res + '" "' + $rc +
    '" && cl /nologo /EHsc /MD /Fo:"' + $testRoot + '\\" "' + $cpp + '" "' + $res + '" /link /SUBSYSTEM:WINDOWS /OUT:"' + $fixtureExe + '"'
cmd /c $compile
if ($LASTEXITCODE -ne 0) { throw 'PE version fixture compilation failed.' }

$binaryStaging = Join-Path $testRoot 'binary'
$sourceStaging = Join-Path $testRoot 'source'
$sourceFiles = Join-Path $sourceStaging 'YUVRaw-source'
$baseline = Join-Path $testRoot 'baseline'
New-Item -ItemType Directory -Force -Path $binaryStaging,$sourceFiles,$baseline | Out-Null
& (Join-Path $repoRoot 'tools/Copy-ThirdPartyNotices.ps1') -InstallRoot (Join-Path $repoRoot 'vcpkg_installed') -Triplet $triplet -OutputDirectory $binaryStaging
& (Join-Path $repoRoot 'tools/Copy-RuntimeResources.ps1') -OutputDirectory $binaryStaging
Copy-Item -LiteralPath $fixtureExe -Destination $binaryStaging
& (Join-Path $repoRoot 'tools/Copy-ReleaseDocumentation.ps1') -OutputDirectory $binaryStaging
New-Item -ItemType Directory -Force -Path (Join-Path $binaryStaging 'third-party') | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'third-party/sbom.cdx.json') -Destination (Join-Path $binaryStaging 'third-party/sbom.cdx.json')
$instructionTemplate = [IO.File]::ReadAllText((Join-Path $repoRoot 'docs/SOURCE_CODE.txt'), $utf8)
$instructions = $instructionTemplate.TrimEnd().Replace('{{version}}', $fixtureVersion).
    Replace('{{sourceName}}', "YUVRaw-$fixtureVersion-source.zip").
    Replace('{{binaryName}}', "YUVRaw-$fixtureVersion-windows-x64.zip").
    Replace('{{checksumsName}}', "YUVRaw-$fixtureVersion-SHA256SUMS.txt").Replace('{{platform}}', 'x64')
Write-Utf8File (Join-Path $binaryStaging 'SOURCE_CODE.txt') $instructions

$sourceMap = [ordered]@{
    'LICENSE' = 'LICENSE'
    'docs/QUICK_START.md' = 'README.md'
    'docs/QUICK_START.zh-CN.md' = 'README.zh-CN.md'
    'THIRD_PARTY_NOTICES.md' = 'THIRD_PARTY_NOTICES.md'
    'THIRD_PARTY_NOTICES.zh-CN.md' = 'THIRD_PARTY_NOTICES.zh-CN.md'
    'third-party/sbom.cdx.json' = 'third-party/sbom.cdx.json'
    'docs/SOURCE_DISTRIBUTION.md' = 'SOURCE_DISTRIBUTION.md'
    'docs/SOURCE_DISTRIBUTION.zh-CN.md' = 'SOURCE_DISTRIBUTION.zh-CN.md'
    'licenses/LGPL-2.1.txt' = 'licenses/LGPL-2.1.txt'
    'third-party/provenance/libraw/copyright' = 'licenses/LibRaw.txt'
    'licenses/LibRaw-components.txt' = 'licenses/LibRaw-components.txt'
    'third-party/provenance/lcms/copyright' = 'licenses/Little-CMS.txt'
    'third-party/provenance/zlib/copyright' = 'licenses/zlib.txt'
    'third-party/provenance/libjpeg-turbo/copyright' = 'licenses/libjpeg-turbo.txt'
    'third-party/provenance/jasper/copyright' = 'licenses/JasPer.txt'
    'third-party/provenance/glm/copyright' = 'licenses/GLM.txt'
    'resources/fonts/NotoSansCJKsc-Regular.otf' = 'resources/fonts/NotoSansCJKsc-Regular.otf'
    'resources/fonts/OFL-1.1.txt' = 'resources/fonts/OFL-1.1.txt'
    'resources/fonts/README.md' = 'resources/fonts/README.md'
    'resources/fonts/README.zh-CN.md' = 'resources/fonts/README.zh-CN.md'
}
foreach ($name in $sourceMap.Keys) {
    $destination = Join-Path $sourceFiles $name
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath (Join-Path $binaryStaging $sourceMap[$name]) -Destination $destination
}
# Source pages retain their original names and navigation; the binary helper renames them.
foreach ($name in @('QUICK_START.md','QUICK_START.zh-CN.md')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "docs/$name") -Destination (Join-Path $sourceFiles "docs/$name")
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs/SOURCE_CODE.txt') -Destination (Join-Path $sourceFiles 'docs/SOURCE_CODE.txt')
Write-Utf8File (Join-Path $sourceFiles 'src/Core/FAppVersion.h') "#define YUVRAW_VERSION_MAJOR 1`n#define YUVRAW_VERSION_MINOR 2`n#define YUVRAW_VERSION_PATCH 3`n"
Copy-Item -LiteralPath (Join-Path $repoRoot 'src/Core/FThirdPartyNotices.h') -Destination (Join-Path $sourceFiles 'src/Core/FThirdPartyNotices.h')
& (Join-Path $repoRoot 'tools/New-PortableZip.ps1') -SourceDirectory $binaryStaging -DestinationPath (Join-Path $baseline "YUVRaw-$fixtureVersion-windows-x64.zip")
& (Join-Path $repoRoot 'tools/New-PortableZip.ps1') -SourceDirectory $sourceStaging -DestinationPath (Join-Path $baseline "YUVRaw-$fixtureVersion-source.zip")
Write-FixtureChecksums $baseline $fixtureVersion

# The baseline must pass every release-specific check before reaching the
# deliberately incomplete source fixture. This guards against a verifier that
# rejects all inputs or against an unrelated early error masking the cases.
Expect-Rejection $baseline $fixtureVersion '[SourceDistributionTest] Archive is missing'

$missingLicense = Copy-Fixture 'missing-license'
$zip = [IO.Compression.ZipFile]::Open((Join-Path $missingLicense "YUVRaw-$fixtureVersion-windows-x64.zip"), [IO.Compression.ZipArchiveMode]::Update)
try { $zip.GetEntry('licenses/LGPL-2.1.txt').Delete() } finally { $zip.Dispose() }
Write-FixtureChecksums $missingLicense $fixtureVersion
Expect-Rejection $missingLicense $fixtureVersion 'Missing binary entry: licenses/LGPL-2.1.txt'

$personalConfig = Copy-Fixture 'personal-config'
$zip = [IO.Compression.ZipFile]::Open((Join-Path $personalConfig "YUVRaw-$fixtureVersion-windows-x64.zip"), [IO.Compression.ZipArchiveMode]::Update)
try {
    $writer = New-Object IO.StreamWriter($zip.CreateEntry('imgui.ini').Open(), $utf8)
    try { $writer.Write('[Window][private-layout]') } finally { $writer.Dispose() }
} finally { $zip.Dispose() }
Write-FixtureChecksums $personalConfig $fixtureVersion
Expect-Rejection $personalConfig $fixtureVersion 'Unexpected binary entry: imgui.ini'

$wrongVersion = Join-Path $testRoot 'wrong-version'
New-Item -ItemType Directory -Force -Path $wrongVersion | Out-Null
foreach ($suffix in @('windows-x64.zip','source.zip')) {
    Copy-Item -LiteralPath (Join-Path $baseline "YUVRaw-$fixtureVersion-$suffix") -Destination (Join-Path $wrongVersion "YUVRaw-$mismatchedVersion-$suffix")
}
Write-FixtureChecksums $wrongVersion $mismatchedVersion
Expect-Rejection $wrongVersion $mismatchedVersion "EXE VERSIONINFO mismatch: expected $mismatchedVersion"

$wrongSourceVersion = Copy-Fixture 'wrong-source-version'
$zip = [IO.Compression.ZipFile]::Open((Join-Path $wrongSourceVersion "YUVRaw-$fixtureVersion-source.zip"), [IO.Compression.ZipArchiveMode]::Update)
try {
    $zip.GetEntry('YUVRaw-source/src/Core/FAppVersion.h').Delete()
    $writer = New-Object IO.StreamWriter($zip.CreateEntry('YUVRaw-source/src/Core/FAppVersion.h').Open(), $utf8)
    try { $writer.Write("#define YUVRAW_VERSION_MAJOR 1`n#define YUVRAW_VERSION_MINOR 2`n#define YUVRAW_VERSION_PATCH 4`n") }
    finally { $writer.Dispose() }
} finally { $zip.Dispose() }
Write-FixtureChecksums $wrongSourceVersion $fixtureVersion
Expect-Rejection $wrongSourceVersion $fixtureVersion 'Source version header differs from the release version.'
$missingTranslation = Copy-Fixture 'missing-translation'
$zip = [IO.Compression.ZipFile]::Open((Join-Path $missingTranslation "YUVRaw-$fixtureVersion-windows-x64.zip"), [IO.Compression.ZipArchiveMode]::Update)
try { $zip.GetEntry('README.zh-CN.md').Delete() } finally { $zip.Dispose() }
Write-FixtureChecksums $missingTranslation $fixtureVersion
Expect-Rejection $missingTranslation $fixtureVersion 'Missing binary entry: README.zh-CN.md'
Write-Host '[ReleaseVerifierFailures] Real PE baseline and five targeted package corruption cases passed.'
