[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$DestinationPath,

    [ValidateSet('x64-windows-static-md', 'x86-windows-static-md')]
    [string]$Triplet = 'x64-windows-static-md',

    [string]$SourceCacheDirectory = '',

    [switch]$Offline
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$repoPrefix = $repoRoot + [IO.Path]::DirectorySeparatorChar
$lockPath = Join-Path $repoRoot 'third-party\sources.lock.json'
$sourceLock = Get-Content -Raw -Encoding UTF8 $lockPath | ConvertFrom-Json
$manifest = Get-Content -Raw -Encoding UTF8 (Join-Path $repoRoot 'vcpkg.json') | ConvertFrom-Json
$downloadTimeoutSeconds = 120
$expectedLockSchema = 1
$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)

if ($sourceLock.schemaVersion -ne $expectedLockSchema -or
    $sourceLock.vcpkgBaseline -ne $manifest.'builtin-baseline') {
    throw '[SourceBundle] Source lock does not match the vcpkg manifest.'
}
$DestinationPath = [IO.Path]::GetFullPath($DestinationPath)
if (Test-Path -LiteralPath $DestinationPath) {
    throw "[SourceBundle] Output already exists: $DestinationPath"
}
if ([string]::IsNullOrWhiteSpace($SourceCacheDirectory)) {
    $SourceCacheDirectory = Join-Path $repoRoot 'artifacts\source-cache'
}
$SourceCacheDirectory = [IO.Path]::GetFullPath($SourceCacheDirectory)
New-Item -ItemType Directory -Force -Path $SourceCacheDirectory | Out-Null

function Assert-FileHash {
    param([string]$Path, [string]$Expected, [string]$Algorithm = 'SHA256')
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or
        (Get-FileHash -LiteralPath $Path -Algorithm $Algorithm).Hash -ne $Expected) {
        throw "[SourceBundle] Missing file or checksum mismatch: $Path"
    }
}

function Get-SourceArchive {
    param($Resource)
    if ([IO.Path]::GetFileName($Resource.file) -ne $Resource.file -or
        $Resource.url -notmatch '^https://github\.com/') {
        throw '[SourceBundle] Unsupported source archive location.'
    }
    $cachePath = Join-Path $SourceCacheDirectory $Resource.file
    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
        if ($Offline) { throw "[SourceBundle] Source cache is missing $($Resource.file)." }
        Write-Host "[SourceBundle] Downloading $($Resource.file)"
        $partialPath = $cachePath + '.' + [guid]::NewGuid().ToString('N') + '.partial'
        try {
            $request = @{
                Uri = $Resource.url
                OutFile = $partialPath
                UseBasicParsing = $true
                TimeoutSec = $downloadTimeoutSeconds
            }
            if ($env:HTTPS_PROXY) { $request.Proxy = $env:HTTPS_PROXY }
            Invoke-WebRequest @request
            Assert-FileHash $partialPath $Resource.sha512 'SHA512'
            Move-Item -LiteralPath $partialPath -Destination $cachePath
        }
        finally {
            if (Test-Path -LiteralPath $partialPath) { Remove-Item -LiteralPath $partialPath }
        }
    }
    Assert-FileHash $cachePath $Resource.sha512 'SHA512'
    return $cachePath
}

# The installed SPDX records describe the inputs used to build the linked
# library, not merely the newest recipe present in a developer's vcpkg checkout.
foreach ($port in $sourceLock.ports) {
    $override = @($manifest.overrides | Where-Object name -EQ $port.name)
    $spdxPath = Join-Path $repoRoot "vcpkg_installed\$Triplet\share\$($port.name)\vcpkg.spdx.json"
    $spdx = Get-Content -Raw -Encoding UTF8 $spdxPath | ConvertFrom-Json
    $installedPort = $spdx.packages | Where-Object SPDXID -EQ 'SPDXRef-port'
    $portVersion = if ($port.portVersion) { [int]$port.portVersion } else { 0 }
    $overrideRevision = if ($override.Count -eq 1 -and $override[0].PSObject.Properties['port-version']) { [int]$override[0].'port-version' } else { 0 }
    $installedVersion = $port.version + $(if ($portVersion) { '#' + $portVersion } else { '' })
    if ($override.Count -ne 1 -or $override[0].version -ne $port.version -or
        $overrideRevision -ne $portVersion -or
        $installedPort.versionInfo -ne $installedVersion -or
        $installedPort.downloadLocation -notlike "*@$($port.tree)") {
        throw "[SourceBundle] Installed $($port.name) does not match the source lock."
    }
    foreach ($file in $port.files) {
        $record = $spdx.files | Where-Object fileName -EQ ('./' + $file.path)
        $recordHash = ($record.checksums | Where-Object algorithm -EQ 'SHA256').checksumValue
        if ($recordHash -ne $file.sha256) {
            throw "[SourceBundle] Installed recipe differs: $($port.name)/$($file.path)"
        }
        Assert-FileHash (Join-Path $repoRoot "third-party\vcpkg-ports\$($port.name)\$($file.path)") $file.sha256
    }
}
foreach ($vendor in $sourceLock.vendored) {
    foreach ($file in $vendor.sourceFiles) {
        Assert-FileHash (Join-Path $repoRoot $file.path) $file.sha256
    }
}

# Vendored ImGui/GLAD are compared with their reviewed text provenance while
# allowing normal Windows checkout line endings. No upstream branch is inferred.
$vendored = Get-Content -Raw -Encoding UTF8 (Join-Path $repoRoot 'third-party/vendored-provenance.json') | ConvertFrom-Json
foreach ($component in @($vendored.imgui, $vendored.glad)) {
    foreach ($file in $component.files) {
        $contents = [IO.File]::ReadAllText((Join-Path $repoRoot $file.path)).Replace("`r`n", "`n")
        $hasher = [Security.Cryptography.SHA256]::Create()
        try { $hash = [BitConverter]::ToString($hasher.ComputeHash($utf8WithoutBom.GetBytes($contents))).Replace('-','') }
        finally { $hasher.Dispose() }
        if ($hash -ne $file.sha256NormalizedLf) { throw "[SourceBundle] Vendored source differs: $($file.path)" }
    }
}

$stagingParent = Join-Path $repoRoot ('artifacts\source-staging\' + [guid]::NewGuid().ToString('N'))
$stagingRoot = Join-Path $stagingParent 'YUVRaw-source'
New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null
$copiedFiles = @{}

function Add-SourceFile {
    param([string]$RelativePath)
    $fullPath = [IO.Path]::GetFullPath((Join-Path $repoRoot $RelativePath))
    if (-not $fullPath.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw '[SourceBundle] Source paths must stay inside the project.'
    }
    $relative = $fullPath.Substring($repoPrefix.Length).Replace('\', '/')
    # A project-file Include is not permission to publish private inputs or a
    # build output. Keep this boundary even if a future project adds such a file.
    if ($relative -match '(^|/)(\.git|\.continue|private-fixtures|artifacts|design-previews|vcpkg_installed)(/|$)' -or
        $relative -match '(^|/)imgui\.ini$|\.(raw|yuv|dng|cr2|cr3|nef|nrw|arw|raf|rw2|orf|pef|srw|heic|heif|tif|tiff|pdb|obj|lib|dll|exe|log)$') {
        throw "[SourceBundle] Refusing private input or generated file: $relative"
    }
    if ($relative -match '\.(png|jpg|jpeg)$' -and $relative -notmatch '^(assets/(app-icon|ui)/|docs/screenshots/)') {
        throw "[SourceBundle] Image is outside reviewed public resource directories: $relative"
    }
    if ($copiedFiles.ContainsKey($relative)) { return }
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "[SourceBundle] Required source file is missing: $relative"
    }
    $destination = Join-Path $stagingRoot $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $fullPath -Destination $destination
    $copiedFiles[$relative] = $true
}

# Only compilation inputs and explicitly selected public files are included.
# Walking the entire workspace would collect private fixtures and user state.
$projects = @('YUVRaw.vcxproj','libs/Glad/Glad.vcxproj','libs/GLFW/GLFW.vcxproj','libs/ImGui/ImGui.vcxproj')
$inputKinds = @('ClCompile','ClInclude','Library','ResourceCompile','Image','ProjectReference')
foreach ($project in $projects) {
    Add-SourceFile $project
    Add-SourceFile ($project + '.filters')
    [xml]$projectXml = Get-Content -Raw -Encoding UTF8 (Join-Path $repoRoot $project)
    $projectDirectory = Split-Path -Parent $project
    if (-not $projectDirectory) { $projectDirectory = '.' }
    foreach ($inputNode in $projectXml.SelectNodes('//*[@Include]')) {
        if ($inputKinds -notcontains $inputNode.LocalName) { continue }
        Add-SourceFile (Join-Path $projectDirectory $inputNode.Include)
    }
}
foreach ($file in @(
    'Application.cpp','YUVRaw.sln','vcpkg.json','build-release.ps1','Directory.Build.props','LICENSE','.gitattributes','.gitignore',
    'README.md','README.zh-CN.md','licenses/LGPL-2.1.txt','licenses/LibRaw-components.txt','libs/ImGui/LICENSE.txt',
    'tools/Copy-ThirdPartyNotices.ps1','tools/Export-SourceBundle.ps1','tools/Copy-RuntimeResources.ps1','tools/Copy-ReleaseDocumentation.ps1',
    'tools/New-PortableZip.ps1','tools/Update-SourceLock.ps1','tools/Set-SourceSnapshot.ps1','tools/Set-Version.ps1',
    'tools/Generate-PublicFixtures.ps1','third-party/imgui-local.patch',
    'third-party/sbom.cdx.json','third-party/vendored-provenance.json',
    'libs/GLFW/LICENSE.md','libs/GLFW/src/CMakeLists.txt',
    'ARCHITECTURE.md','CONTRIBUTING.md','SECURITY.md','THIRD_PARTY_NOTICES.md',
    'ARCHITECTURE.zh-CN.md','CONTRIBUTING.zh-CN.md','SECURITY.zh-CN.md','THIRD_PARTY_NOTICES.zh-CN.md',
    'docs/SOURCE_DISTRIBUTION.md','docs/SOURCE_CODE.txt',
    'docs/screenshots/preview_compare_en.jpg','docs/screenshots/preview_compare_zh.jpg',
    'third-party/sources.lock.json','third-party/vcpkg-ports/LICENSE.txt'
)) { Add-SourceFile $file }
foreach ($directory in @('tests','vcpkg-triplets','assets/app-icon','assets/ui','resources/fonts')) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $repoRoot $directory) -File) {
        $allowed = if ($directory.StartsWith('assets/')) { @('.png') } elseif ($directory -eq 'resources/fonts') { @('.otf','.txt','.md') } else { @('.cpp','.h','.ps1','.cmake') }
        if ($allowed -contains $file.Extension) { Add-SourceFile ($directory + '/' + $file.Name) }
    }
}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $repoRoot 'docs') -File -Filter *.md) {
    Add-SourceFile ('docs/' + $file.Name)
}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $repoRoot '.github') -Recurse -File) {
    if (@('.yml','.yaml','.md') -contains $file.Extension) { Add-SourceFile $file.FullName.Substring($repoRoot.Length + 1) }
}
# The lock records the complete vendored GLFW source; ship the same reviewed files.
foreach ($vendor in $sourceLock.vendored) { foreach ($file in $vendor.sourceFiles) { Add-SourceFile $file.path } }

$upstreamDirectory = Join-Path $stagingRoot 'third-party\upstream'
New-Item -ItemType Directory -Force -Path $upstreamDirectory | Out-Null
foreach ($package in @($sourceLock.ports) + @($sourceLock.vendored)) {
    if ($package.PSObject.Properties['files']) {
        foreach ($file in $package.files) {
            Add-SourceFile "third-party/vcpkg-ports/$($package.name)/$($file.path)"
        }
    }
    foreach ($resource in $package.resources) {
        $archive = Get-SourceArchive $resource
        Copy-Item -LiteralPath $archive -Destination (Join-Path $upstreamDirectory $resource.file)
    }
}

# Preserve the installed license and source provenance records, but never the
# installed binaries, build logs or local paths from the dependency build tree.
foreach ($port in $sourceLock.ports) {
    $provenanceDirectory = Join-Path $stagingRoot "third-party\provenance\$($port.name)"
    New-Item -ItemType Directory -Force -Path $provenanceDirectory | Out-Null
    foreach ($file in @('copyright','vcpkg.spdx.json','vcpkg-spdx-resources.json')) {
        Copy-Item -LiteralPath (Join-Path $repoRoot "vcpkg_installed\$Triplet\share\$($port.name)\$file") -Destination $provenanceDirectory
    }
}
$inventory = @(Get-ChildItem -LiteralPath $stagingRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
    $relative = $_.FullName.Substring($stagingRoot.Length + 1).Replace('\','/')
    (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $relative
})
[IO.File]::WriteAllLines((Join-Path $stagingRoot 'SHA256SUMS'), $inventory, $utf8WithoutBom)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $DestinationPath) | Out-Null
# Windows PowerShell's Compress-Archive writes backslashes into entry names.
# Use portable ZIP paths so the source and its inventory also work elsewhere.
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archiveStream = [IO.File]::Open($DestinationPath, [IO.FileMode]::CreateNew)
$archive = $null
$archiveSucceeded = $false
try {
    $archive = New-Object IO.Compression.ZipArchive($archiveStream, [IO.Compression.ZipArchiveMode]::Create)
    foreach ($file in Get-ChildItem -LiteralPath $stagingRoot -Recurse -File | Sort-Object FullName) {
        $entryName = 'YUVRaw-source/' + $file.FullName.Substring($stagingRoot.Length + 1).Replace('\','/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $file.FullName, $entryName, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
    $archive.Dispose()
    $archive = $null
    $archiveSucceeded = $true
}
finally {
    if ($archive) { $archive.Dispose() }
    $archiveStream.Dispose()
    if (-not $archiveSucceeded) { Remove-Item -LiteralPath $DestinationPath }
}
Write-Host "[SourceBundle] Created $DestinationPath"
