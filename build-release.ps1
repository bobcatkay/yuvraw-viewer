[CmdletBinding()]
param(
    [ValidateSet('x64','x86')][string]$Platform = 'x64',
    [switch]$DryRun,
    [string]$ExpectedVersion = '',
    [string]$OutputDirectory = '',
    [string]$SourceCacheDirectory = '',
    [switch]$OfflineSources
)
$ErrorActionPreference = 'Stop'
$versionHeader = Join-Path $PSScriptRoot 'src/Core/FAppVersion.h'
$headerBytes = [IO.File]::ReadAllBytes($versionHeader)
$header = [IO.File]::ReadAllText($versionHeader)
$versionParts = @(foreach ($name in @('MAJOR','MINOR','PATCH')) {
    $match = [regex]::Match($header, '(?m)^#define\s+YUVRAW_VERSION_' + $name + '\s+(\d+)')
    if (-not $match.Success) { throw "[Release] Cannot read $name version." }
    $match.Groups[1].Value
})
$version = $versionParts -join '.'
if ($ExpectedVersion -and $ExpectedVersion -ne $version) {
    throw "[Release] Expected $ExpectedVersion, but source declares $version."
}
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot "artifacts/releases/$version/$Platform" }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$binaryName = "YUVRaw-$version-windows-$Platform.zip"
$sourceName = "YUVRaw-$version-source.zip"
$checksumsName = "YUVRaw-$version-SHA256SUMS.txt"
if ($DryRun) {
    Write-Host "[Release] Fixed version $version | Release $Platform"
    Write-Host "[Release] $OutputDirectory | $binaryName | $sourceName | $checksumsName"
    return
}
foreach ($name in @($binaryName,$sourceName,$checksumsName)) {
    if (Test-Path -LiteralPath (Join-Path $OutputDirectory $name)) { throw "[Release] Output already exists: $name" }
}
$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw '[Release] Install Visual Studio Desktop development with C++ (v145).' }
# Some hosts expose both Path and PATH; MSBuild subprocess creation treats those as duplicate keys.
$processPathValue = $env:PATH
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $processPathValue, 'Process')
& $msbuild (Join-Path $PSScriptRoot 'YUVRaw.sln') /t:Rebuild /p:Configuration=Release "/p:Platform=$Platform" /m:1 /nodeReuse:false /nologo /v:minimal
if ($LASTEXITCODE -ne 0) { throw "[Release] Build failed: $LASTEXITCODE" }
$outputPlatform = if ($Platform -eq 'x86') { 'Win32' } else { $Platform }
$exe = Join-Path $PSScriptRoot "$outputPlatform/Release/YUVRaw.exe"
$binaryVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($exe).ProductVersion
if ($binaryVersion -ne $version) { throw "[Release] Executable version $binaryVersion differs from $version." }
if ([Convert]::ToBase64String($headerBytes) -ne [Convert]::ToBase64String([IO.File]::ReadAllBytes($versionHeader))) {
    throw '[Release] Build changed the version header.'
}
$triplet = if ($Platform -eq 'x86') { 'x86-windows-static-md' } else { 'x64-windows-static-md' }
# A unique staging directory prevents previous builds, logs or user state entering an archive.
$staging = Join-Path $PSScriptRoot ('artifacts/release-staging/' + [guid]::NewGuid().ToString('N'))
$binaryStaging = Join-Path $staging 'binary'
New-Item -ItemType Directory -Force -Path $binaryStaging | Out-Null
& (Join-Path $PSScriptRoot 'tools/Copy-ThirdPartyNotices.ps1') -InstallRoot (Join-Path $PSScriptRoot 'vcpkg_installed') -Triplet $triplet -OutputDirectory $binaryStaging
Copy-Item -LiteralPath $exe -Destination $binaryStaging
& (Join-Path $PSScriptRoot 'tools/Copy-ReleaseDocumentation.ps1') -OutputDirectory $binaryStaging
New-Item -ItemType Directory -Force -Path (Join-Path $binaryStaging 'third-party') | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'third-party/sbom.cdx.json') -Destination (Join-Path $binaryStaging 'third-party')
& (Join-Path $PSScriptRoot 'tools/Copy-RuntimeResources.ps1') -OutputDirectory $binaryStaging
$utf8 = New-Object Text.UTF8Encoding($false)
# Read translated text explicitly as UTF-8: Windows PowerShell 5.1 otherwise
# interprets non-ASCII literals in BOM-less scripts using the system code page.
$instructions = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'docs/SOURCE_CODE.txt'), $utf8).TrimEnd()
$instructions = $instructions.Replace('{{version}}', $version).Replace('{{sourceName}}', $sourceName).
    Replace('{{binaryName}}', $binaryName).Replace('{{checksumsName}}', $checksumsName).Replace('{{platform}}', $Platform)
[IO.File]::WriteAllText((Join-Path $binaryStaging 'SOURCE_CODE.txt'), $instructions, $utf8)
$sourcePath = Join-Path $staging $sourceName
& (Join-Path $PSScriptRoot 'tools/Export-SourceBundle.ps1') -DestinationPath $sourcePath -Triplet $triplet -SourceCacheDirectory $SourceCacheDirectory -Offline:$OfflineSources
& (Join-Path $PSScriptRoot 'tools/New-PortableZip.ps1') -SourceDirectory $binaryStaging -DestinationPath (Join-Path $staging $binaryName)
$hashes = @($binaryName,$sourceName) | ForEach-Object {
    (Get-FileHash -LiteralPath (Join-Path $staging $_) -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $_
}
[IO.File]::WriteAllLines((Join-Path $staging $checksumsName), [string[]]$hashes, $utf8)
& (Join-Path $PSScriptRoot 'tests/TestReleasePackage.ps1') -ReleaseDirectory $staging -Version $version -Platform $Platform
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
# Only verified artifacts leave staging; an existing release is never overwritten.
$createdOutputs = @()
try {
    foreach ($name in @($binaryName,$sourceName,$checksumsName)) {
        $destination = Join-Path $OutputDirectory $name
        # CreateNew reserves ownership before copying, so rollback cannot remove another release.
        $destinationStream = [IO.File]::Open($destination, [IO.FileMode]::CreateNew)
        $createdOutputs += $destination
        try {
            $sourceStream = [IO.File]::OpenRead((Join-Path $staging $name))
            try { $sourceStream.CopyTo($destinationStream) } finally { $sourceStream.Dispose() }
        } finally { $destinationStream.Dispose() }
    }
} catch {
    foreach ($path in $createdOutputs) { Remove-Item -LiteralPath $path }
    throw
}
Write-Host "[Release] Verified $version packages created in $OutputDirectory. Publish all three files together."
