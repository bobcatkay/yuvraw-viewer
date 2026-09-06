[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceArchivePath,
    [ValidateSet('x64-windows-static-md','x86-windows-static-md')]
    [string]$Triplet = 'x64-windows-static-md'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$repoRoot = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path $repoRoot ('artifacts\source-tests\' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null
$sourcePrefix = 'YUVRaw-source/'
$utf8 = New-Object Text.UTF8Encoding($false, $true)
$expectedLockSchema = 1
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

function Require {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "[SourceDistributionTest] $Message" }
}

function Assert-SourcePath {
    param([string]$Path)
    # Reject aliases that can overwrite another inventory entry after Windows
    # extraction, as well as paths that are unsafe on other platforms.
    Require ($Path -and $Path -notmatch '^/|[\\:<>"|?*\x00-\x1f]|(^|/)\.\.?(/|$)|//|/$|(^|/)[^/]*[. ](/|$)') "Noncanonical source path: $Path"
    Require ($Path -notmatch '(^|/)(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.[^/]*)?(/|$)') "Reserved Windows source path: $Path"
}

function Get-SourceHash {
    param([IO.Compression.ZipArchiveEntry]$Entry, [string]$Algorithm = 'SHA256')
    Require ($null -ne $Entry) 'Cannot hash a missing source entry.'
    $stream = $Entry.Open()
    $hasher = [Security.Cryptography.HashAlgorithm]::Create($Algorithm)
    try { return [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $hasher.Dispose(); $stream.Dispose() }
}

function Read-SourceText {
    param([IO.Compression.ZipArchiveEntry]$Entry)
    Require ($null -ne $Entry) 'Cannot read a missing source entry.'
    $reader = New-Object IO.StreamReader($Entry.Open(), $utf8, $true)
    try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
}

function Resolve-ProjectInput {
    param([string]$Project, [string]$InputPath)
    $inputPathNormalized = $InputPath.Replace('\','/')
    Require ($inputPathNormalized -and $inputPathNormalized -notmatch '^/|:|\$\(|%\(|@\(|[?*\x00-\x1f]') "Unsupported project input: $Project -> $InputPath"
    $segments = New-Object 'System.Collections.Generic.List[string]'
    $projectDirectory = $Project.Substring(0, [Math]::Max(0, $Project.LastIndexOf('/') + 1))
    foreach ($segment in ($projectDirectory + $inputPathNormalized).Split('/')) {
        if ($segment -eq '.' -or $segment -eq '') { continue }
        if ($segment -eq '..') {
            Require ($segments.Count -gt 0) "Project input escapes the source root: $Project -> $InputPath"
            $segments.RemoveAt($segments.Count - 1)
        } else { $segments.Add($segment) }
    }
    $result = $segments -join '/'
    Assert-SourcePath $result
    return $result
}

function Test-SourceArchive {
    param([IO.Compression.ZipArchive]$Archive)
    $entries = New-Object 'System.Collections.Generic.Dictionary[string,System.IO.Compression.ZipArchiveEntry]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $Archive.Entries) {
        Assert-SourcePath $entry.FullName
        Require ($entry.FullName.StartsWith($sourcePrefix, [StringComparison]::Ordinal)) "Entry is outside the source root: $($entry.FullName)"
        $relative = $entry.FullName.Substring($sourcePrefix.Length)
        Require (!$entries.ContainsKey($relative)) "Duplicate or case-colliding source entry: $relative"
        $entries.Add($relative, $entry)
    }
    foreach ($relative in $entries.Keys) {
        $parentPath = $relative
        while ($parentPath.Contains('/')) {
            $parentPath = $parentPath.Substring(0, $parentPath.LastIndexOf('/'))
            Require (!$entries.ContainsKey($parentPath)) "Source file conflicts with a directory: $parentPath"
        }
    }

    $projects = @('YUVRaw.vcxproj','libs/Glad/Glad.vcxproj','libs/GLFW/GLFW.vcxproj','libs/ImGui/ImGui.vcxproj')
    # Independent public contract: scripts and CI are needed to repeat checks.
    $requiredFiles = @(
        'LICENSE','.gitattributes','.gitignore','Application.cpp','YUVRaw.sln','Directory.Build.props','vcpkg.json',
        'README.md','ARCHITECTURE.md','CONTRIBUTING.md','SECURITY.md','THIRD_PARTY_NOTICES.md',
        'build-release.ps1','licenses/LGPL-2.1.txt','licenses/LibRaw-components.txt',
        'resources/AppIcon.ico','resources/fonts/NotoSansCJKsc-Regular.otf','resources/fonts/OFL-1.1.txt','resources/fonts/README.md',
        'src/Core/FUiResources.h','src/Core/FAppVersion.h','src/Core/FThirdPartyNotices.h','src/Image/FDngImageLoader.cpp',
        'tools/Copy-ThirdPartyNotices.ps1','tools/Copy-RuntimeResources.ps1','tools/Export-SourceBundle.ps1','tools/Copy-ReleaseDocumentation.ps1',
        'tools/New-PortableZip.ps1','tools/Update-SourceLock.ps1','tools/Set-SourceSnapshot.ps1','tools/Set-Version.ps1','tools/Generate-PublicFixtures.ps1',
        'tests/run_tests.ps1','tests/run_gl_format_validation.ps1','tests/run_ui_resource_tests.ps1',
        'tests/GeneratePublicFixtures.cpp','tests/PublicFixtures.h','tests/TestSourceDistribution.ps1',
        'tests/TestReleasePackage.ps1','tests/TestReleasePackageFailures.ps1','tests/TestSourceSnapshot.ps1',
        'vcpkg-triplets/x64-windows-static-md.cmake','vcpkg-triplets/x86-windows-static-md.cmake',
        'third-party/sources.lock.json','third-party/vendored-provenance.json','third-party/sbom.cdx.json',
        'third-party/vcpkg-ports/libraw/portfile.cmake','third-party/vcpkg-ports/libraw/dependencies.patch','third-party/vcpkg-ports/libraw/fix-install.patch',
        'third-party/imgui-local.patch','third-party/vcpkg-ports/LICENSE.txt','libs/ImGui/LICENSE.txt','libs/GLFW/LICENSE.md','libs/GLFW/src/CMakeLists.txt',
        'docs/SOURCE_DISTRIBUTION.md','docs/SOURCE_CODE.txt','docs/BUILDING.md','docs/QUICK_START.md','docs/PUBLIC_FIXTURES.md',
        '.github/workflows/windows.yml','.github/pull_request_template.md',
        '.github/ISSUE_TEMPLATE/bug_report.yml','.github/ISSUE_TEMPLATE/feature_request.yml','.github/ISSUE_TEMPLATE/config.yml',
        '.github/ISSUE_TEMPLATE/bug_report.zh-CN.yml','.github/ISSUE_TEMPLATE/feature_request.zh-CN.yml','tests/TestDocumentation.ps1',
        'SHA256SUMS'
    ) + $projects + @($projects | ForEach-Object { $_ + '.filters' })
    $publicDocuments = @('README','ARCHITECTURE','CONTRIBUTING','SECURITY','THIRD_PARTY_NOTICES',
        'resources/fonts/README','.github/pull_request_template')
    $publicDocuments += @('BUILDING','DEPENDENCIES','PUBLIC_FIXTURES','QUICK_START','SOURCE_DISTRIBUTION') | ForEach-Object { "docs/$_" }
    foreach ($document in $publicDocuments) { $requiredFiles += @("$document.md", "$document.zh-CN.md") }
    foreach ($required in $requiredFiles) { Require ($entries.ContainsKey($required)) "Archive is missing $required" }

    # Photographs can be JPEG/PNG too. Only reviewed artwork, screenshots and fonts are
    # public binary resources; generated test fixtures are deliberately absent.
    $publicResources = @(
        'resources/AppIcon.ico','resources/fonts/NotoSansCJKsc-Regular.otf',
        'assets/app-icon/pixscope-tech-gray.png','assets/app-icon/pixscope-tech-gray-source.png',
        'assets/app-icon/pixscope-tech-gray-chroma.png','assets/app-icon/pixscope-tech-gray-1024.png',
        'assets/ui/viewer-icons.png','assets/ui/viewer-icons-imagegen-source.png',
        'docs/screenshots/preview_compare_en.jpg','docs/screenshots/preview_compare_zh.jpg'
    )
    foreach ($relative in $entries.Keys) {
        Require ($relative -notmatch '(^|/)(\.git|\.continue|\.agents|\.codex|\.vs|private-fixtures|vcpkg_installed|artifacts|design-previews|intermediate|product)(/|$)|(^|/)imgui\.ini$|\.(exe|dll|lib|obj|pdb|log|binlog|user|yuv|raw|dng|cr2|cr3|nef|nrw|arw|srf|sr2|orf|rw2|raf|pef|srw|rwl|3fr|iiq|kdc|dcr|mos|mrw|x3f)$') "Private or generated source entry: $relative"
        if ($relative -match '\.(png|jpe?g|tiff?|bmp|gif|webp|heif|heic|avif|ico|otf|ttf|ttc)$') {
            Require ($publicResources -ccontains $relative) "Unreviewed image or font source entry: $relative"
        }
    }

    $inventoryText = Read-SourceText $entries['SHA256SUMS']
    $inventoryLines = @($inventoryText -split '\r?\n')
    if ($inventoryLines.Count -gt 0 -and $inventoryLines[-1] -eq '') { $inventoryLines = @($inventoryLines | Select-Object -SkipLast 1) }
    $inventory = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($line in $inventoryLines) {
        $match = [regex]::Match($line, '\A([a-fA-F0-9]{64})  ([^\r\n]+)\z')
        Require $match.Success 'Malformed SHA256 inventory line.'
        $relative = $match.Groups[2].Value
        Assert-SourcePath $relative
        Require ($relative -cne 'SHA256SUMS') 'SHA256 inventory must not contain itself.'
        Require ($inventory.Add($relative)) "Duplicate or case-colliding inventory path: $relative"
        Require ($entries.ContainsKey($relative)) "Inventory entry is missing: $relative"
        Require ($entries[$relative].FullName -ceq ($sourcePrefix + $relative)) "Inventory path case differs from ZIP: $relative"
        Require ((Get-SourceHash $entries[$relative]) -eq $match.Groups[1].Value) "Archive bytes differ from the inventory: $relative"
    }
    foreach ($relative in $entries.Keys) {
        if ($relative -ceq 'SHA256SUMS') { continue }
        Require ($inventory.Contains($relative)) "Source entry is absent from the inventory: $relative"
    }
    Require ($inventory.Count -eq $entries.Count - 1) 'SHA256 inventory does not exactly match the source archive.'

    # Read projects inside the ZIP so a removed source + inventory line fails.
    $pendingProjects = New-Object 'System.Collections.Generic.Queue[string]'
    $visitedProjects = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($project in $projects) { $pendingProjects.Enqueue($project) }
    $solution = Read-SourceText $entries['YUVRaw.sln']
    foreach ($match in [regex]::Matches($solution, '(?m)^Project\([^\r\n]+?=\s*"[^"]+",\s*"([^"]+\.vcxproj)"')) {
        $pendingProjects.Enqueue((Resolve-ProjectInput 'YUVRaw.sln' $match.Groups[1].Value))
    }
    $inputKinds = @('ClCompile','ClInclude','Library','ResourceCompile','Image','ProjectReference')
    while ($pendingProjects.Count -gt 0) {
        $project = $pendingProjects.Dequeue()
        if (!$visitedProjects.Add($project)) { continue }
        Require ($entries.ContainsKey($project)) "Missing solution/project input: $project"
        $settings = New-Object Xml.XmlReaderSettings
        $settings.DtdProcessing = [Xml.DtdProcessing]::Prohibit
        $settings.XmlResolver = $null
        $textReader = New-Object IO.StringReader((Read-SourceText $entries[$project]))
        $xmlReader = [Xml.XmlReader]::Create($textReader, $settings)
        try {
            $projectXml = New-Object Xml.XmlDocument
            $projectXml.XmlResolver = $null
            $projectXml.Load($xmlReader)
        } finally { $xmlReader.Dispose(); $textReader.Dispose() }
        foreach ($node in $projectXml.SelectNodes('//*[@Include]')) {
            if ($inputKinds -notcontains $node.LocalName) { continue }
            $projectInput = Resolve-ProjectInput $project $node.GetAttribute('Include')
            Require ($entries.ContainsKey($projectInput)) "Missing project compilation/resource input: $project -> $projectInput"
            if ($node.LocalName -eq 'ProjectReference') { $pendingProjects.Enqueue($projectInput) }
        }
    }

    $sourceLock = Read-SourceText $entries['third-party/sources.lock.json'] | ConvertFrom-Json
    $manifest = Read-SourceText $entries['vcpkg.json'] | ConvertFrom-Json
    Require ($sourceLock.schemaVersion -eq $expectedLockSchema -and $sourceLock.vcpkgBaseline -eq $manifest.'builtin-baseline') 'Archived source lock does not match the manifest.'
    # Do not let deleting a complete lock record also delete its obligation to
    # ship source. These are the libraries linked by the reviewed solution.
    $requiredPorts = @('libraw','lcms','zlib','libjpeg-turbo','jasper','glm')
    Require (@($sourceLock.ports).Count -eq $requiredPorts.Count) 'Archived source lock has an incomplete or unreviewed dependency set.'
    foreach ($name in $requiredPorts) {
        Require (@($sourceLock.ports | Where-Object name -CEQ $name).Count -eq 1) "Archived source lock must contain exactly one $name record."
    }
    Require (@($manifest.overrides).Count -eq $requiredPorts.Count) 'Manifest overrides and locked dependency sets differ.'
    foreach ($dependency in $manifest.dependencies) {
        if ($dependency -is [string]) { $dependencyName = $dependency }
        else {
            if ($dependency.PSObject.Properties['host'] -and $dependency.host) { continue }
            $dependencyName = $dependency.name
        }
        Require ($requiredPorts -ccontains $dependencyName) "Manifest runtime dependency has no corresponding source lock: $dependencyName"
    }
    Require (@($sourceLock.vendored).Count -eq 1 -and $sourceLock.vendored[0].name -ceq 'glfw') 'Archived source lock must identify the reviewed GLFW sources.'
    foreach ($port in $sourceLock.ports) {
        $override = @($manifest.overrides | Where-Object name -EQ $port.name)
        $revision = if ($port.PSObject.Properties['portVersion']) { [int]$port.portVersion } else { 0 }
        $overrideRevision = if ($override.Count -eq 1 -and $override[0].PSObject.Properties['port-version']) { [int]$override[0].'port-version' } else { 0 }
        Require ($override.Count -eq 1 -and $override[0].version -eq $port.version -and $overrideRevision -eq $revision) "Archived dependency differs from manifest: $($port.name)"
        foreach ($file in $port.files) {
            $path = "third-party/vcpkg-ports/$($port.name)/$($file.path)"
            Require ($entries.ContainsKey($path)) "Missing locked recipe: $path"
            Require ((Get-SourceHash $entries[$path]) -eq $file.sha256) "Archived recipe differs from source lock: $path"
        }
        foreach ($name in @('copyright','vcpkg.spdx.json','vcpkg-spdx-resources.json')) {
            $path = "third-party/provenance/$($port.name)/$name"
            Require ($entries.ContainsKey($path)) "Missing dependency provenance: $path"
        }
        $spdx = Read-SourceText $entries["third-party/provenance/$($port.name)/vcpkg.spdx.json"] | ConvertFrom-Json
        $installed = @($spdx.packages | Where-Object SPDXID -EQ 'SPDXRef-port')
        $version = $port.version + $(if ($revision) { '#' + $revision } else { '' })
        Require ($installed.Count -eq 1 -and $installed[0].versionInfo -eq $version -and $installed[0].downloadLocation -like "*@$($port.tree)") "Archived dependency provenance differs from lock: $($port.name)"
    }
    foreach ($package in @($sourceLock.ports) + @($sourceLock.vendored)) {
        foreach ($resource in $package.resources) {
            Assert-SourcePath $resource.file
            Require ($resource.file -notmatch '/') "Upstream filename must be a basename: $($resource.file)"
            $path = 'third-party/upstream/' + $resource.file
            Require ($entries.ContainsKey($path)) "Missing locked upstream source: $path"
            Require ((Get-SourceHash $entries[$path] 'SHA512') -eq $resource.sha512) "Archived upstream differs from source lock: $path"
        }
    }
    foreach ($vendor in $sourceLock.vendored) {
        foreach ($file in $vendor.sourceFiles) {
            Require ($entries.ContainsKey($file.path)) "Missing locked vendored source: $($file.path)"
            Require ((Get-SourceHash $entries[$file.path]) -eq $file.sha256) "Vendored source differs from lock: $($file.path)"
        }
    }
    $vendored = Read-SourceText $entries['third-party/vendored-provenance.json'] | ConvertFrom-Json
    foreach ($component in @($vendored.imgui, $vendored.glad)) {
        foreach ($file in $component.files) {
            Require ($entries.ContainsKey($file.path)) "Missing reviewed vendored source: $($file.path)"
            $text = (Read-SourceText $entries[$file.path]).Replace("`r`n", "`n")
            $hasher = [Security.Cryptography.SHA256]::Create()
            try { $hash = [BitConverter]::ToString($hasher.ComputeHash($utf8.GetBytes($text))).Replace('-','') }
            finally { $hasher.Dispose() }
            Require ($hash -eq $file.sha256NormalizedLf) "Vendored source differs from reviewed provenance: $($file.path)"
        }
    }
    Write-Host "[SourceDistributionTest] Verified $($inventory.Count) archived files, project inputs, locked dependencies and privacy exclusions."
}

$resolvedArchive = (Resolve-Path -LiteralPath $SourceArchivePath).Path
$zip = [IO.Compression.ZipFile]::OpenRead($resolvedArchive)
try { Test-SourceArchive $zip } finally { $zip.Dispose() }

# Corrupt copies live only in memory. No private inputs or workspace sources
# are opened or modified to manufacture these failure cases.
function Set-MemoryEntryText {
    param([IO.Compression.ZipArchive]$Archive, [string]$Name, [string]$Text)
    $entry = $Archive.GetEntry($Name)
    if ($entry) { $entry.Delete() }
    $writer = New-Object IO.StreamWriter($Archive.CreateEntry($Name).Open(), $utf8)
    try { $writer.Write($Text) } finally { $writer.Dispose() }
}

function Test-SourceRejection {
    param([string]$Label, [string]$ExpectedReason, [scriptblock]$Mutation)
    $memory = New-Object IO.MemoryStream
    $fileStream = [IO.File]::OpenRead($resolvedArchive)
    try { $fileStream.CopyTo($memory) } finally { $fileStream.Dispose() }
    $memory.Position = 0
    $candidate = New-Object IO.Compression.ZipArchive($memory, [IO.Compression.ZipArchiveMode]::Update, $true)
    try {
        & $Mutation $candidate
        $message = ''
        try { Test-SourceArchive $candidate } catch { $message = $_.Exception.Message }
        Require ($message.Contains($ExpectedReason)) "Negative case '$Label' expected '$ExpectedReason', got '$message'."
        Write-Host "[SourceDistributionTest] Correctly rejected: $Label"
    } finally { $candidate.Dispose(); $memory.Dispose() }
}

Test-SourceRejection 'removed source and its inventory line' 'Missing project compilation/resource input:' {
    param($candidate)
    $candidate.GetEntry($sourcePrefix + 'src/Core/FApplication.cpp').Delete()
    $inventory = Read-SourceText $candidate.GetEntry($sourcePrefix + 'SHA256SUMS')
    $inventory = [regex]::Replace($inventory, '(?m)^[^\r\n]+  src/Core/FApplication\.cpp\r?\n?', '')
    Set-MemoryEntryText $candidate ($sourcePrefix + 'SHA256SUMS') $inventory
}
Test-SourceRejection 'unlisted source entry' 'Source entry is absent from the inventory:' {
    param($candidate)
    Set-MemoryEntryText $candidate ($sourcePrefix + 'tests/UnlistedSynthetic.cpp') '// synthetic negative-test marker'
}
Test-SourceRejection 'unreviewed PNG entry' 'Unreviewed image or font source entry:' {
    param($candidate)
    Set-MemoryEntryText $candidate ($sourcePrefix + 'assets/app-icon/unreviewed.png') 'synthetic marker; not a photograph'
}
Test-SourceRejection 'private JPEG path' 'Private or generated source entry:' {
    param($candidate)
    Set-MemoryEntryText $candidate ($sourcePrefix + 'private-fixtures/camera.jpg') 'synthetic marker; no private input was read'
}
Test-SourceRejection 'case-colliding ZIP entry' 'Duplicate or case-colliding source entry:' {
    param($candidate)
    $writer = New-Object IO.StreamWriter($candidate.CreateEntry($sourcePrefix + 'application.CPP').Open(), $utf8)
    try { $writer.Write('synthetic duplicate marker') } finally { $writer.Dispose() }
}
Test-SourceRejection 'duplicate ZIP entry' 'Duplicate or case-colliding source entry:' {
    param($candidate)
    $writer = New-Object IO.StreamWriter($candidate.CreateEntry($sourcePrefix + 'Application.cpp').Open(), $utf8)
    try { $writer.Write('synthetic duplicate marker') } finally { $writer.Dispose() }
}
Test-SourceRejection 'file/directory collision' 'Source file conflicts with a directory:' {
    param($candidate)
    Set-MemoryEntryText $candidate ($sourcePrefix + 'src') 'synthetic collision marker'
}
Test-SourceRejection 'ZIP path traversal' 'Noncanonical source path:' {
    param($candidate)
    Set-MemoryEntryText $candidate ($sourcePrefix + '../escaped.cpp') 'synthetic traversal marker'
}
Test-SourceRejection 'duplicate inventory line' 'Duplicate or case-colliding inventory path:' {
    param($candidate)
    $inventory = Read-SourceText $candidate.GetEntry($sourcePrefix + 'SHA256SUMS')
    $firstLine = ($inventory -split '\r?\n')[0]
    Set-MemoryEntryText $candidate ($sourcePrefix + 'SHA256SUMS') ($inventory.TrimEnd() + "`n" + $firstLine + "`n")
}
Test-SourceRejection 'malformed inventory hash' 'Malformed SHA256 inventory line.' {
    param($candidate)
    $inventory = Read-SourceText $candidate.GetEntry($sourcePrefix + 'SHA256SUMS')
    Set-MemoryEntryText $candidate ($sourcePrefix + 'SHA256SUMS') ('not-a-sha256' + $inventory.Substring($inventory.IndexOf('  ')))
}
Test-SourceRejection 'modified source bytes' 'Archive bytes differ from the inventory:' {
    param($candidate)
    Set-MemoryEntryText $candidate ($sourcePrefix + 'src/Core/FApplication.cpp') '// synthetic changed source marker'
}
Test-SourceRejection 'unlocked runtime dependency' 'Manifest runtime dependency has no corresponding source lock:' {
    param($candidate)
    $manifestPath = $sourcePrefix + 'vcpkg.json'
    $manifest = Read-SourceText $candidate.GetEntry($manifestPath) | ConvertFrom-Json
    $manifest.dependencies = @($manifest.dependencies) + @('synthetic-unlocked-library')
    Set-MemoryEntryText $candidate $manifestPath ($manifest | ConvertTo-Json -Depth 10)
    # Keep the inventory valid to reach the dependency-completeness check.
    $hash = Get-SourceHash $candidate.GetEntry($manifestPath)
    $inventory = Read-SourceText $candidate.GetEntry($sourcePrefix + 'SHA256SUMS')
    $inventory = [regex]::Replace($inventory, '(?m)^[a-fA-F0-9]{64}(?=  vcpkg\.json\r?$)', $hash)
    Set-MemoryEntryText $candidate ($sourcePrefix + 'SHA256SUMS') $inventory
}

# Exercise real failure paths: neither a tampered upstream archive nor a missing
# LGPL text may silently produce a distributable package.
$badCache = Join-Path $testRoot 'bad-cache'
New-Item -ItemType Directory -Force -Path $badCache | Out-Null
$currentLock = Get-Content -Raw -Encoding UTF8 (Join-Path $repoRoot 'third-party/sources.lock.json') | ConvertFrom-Json
$libRawPort = @($currentLock.ports | Where-Object name -EQ 'libraw')[0]
$libRawArchive = $libRawPort.resources[0].file
Require ([IO.Path]::GetFileName($libRawArchive) -ceq $libRawArchive) 'Invalid LibRaw cache filename.'
[IO.File]::WriteAllText((Join-Path $badCache $libRawArchive), 'tampered source')
$rejectedArchive = Join-Path $testRoot 'must-not-exist.zip'
$hashRejected = $false
$sourceFailure = ''
try {
    & (Join-Path $repoRoot 'tools\Export-SourceBundle.ps1') -DestinationPath $rejectedArchive -SourceCacheDirectory $badCache -Triplet $Triplet -Offline
}
catch { $sourceFailure = $_.Exception.Message; $hashRejected = $sourceFailure -like '*checksum mismatch*' }
Require $hashRejected "A corrupted source archive was not rejected by its hash. Actual error: $sourceFailure"
Require (-not (Test-Path -LiteralPath $rejectedArchive)) 'Failed source validation produced an archive.'

$fixtureRoot = Join-Path $testRoot 'missing-license'
New-Item -ItemType Directory -Force -Path (Join-Path $fixtureRoot 'tools') | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'tools\Copy-ThirdPartyNotices.ps1') -Destination (Join-Path $fixtureRoot 'tools')
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination $fixtureRoot
$noticeOutput = Join-Path $fixtureRoot 'output'
$licenseRejected = $false
try {
    & (Join-Path $fixtureRoot 'tools\Copy-ThirdPartyNotices.ps1') -InstallRoot (Join-Path $repoRoot 'vcpkg_installed') -Triplet $Triplet -OutputDirectory $noticeOutput
}
catch { $licenseRejected = $_.Exception.Message -like '*LGPL-2.1.txt*' }
Require $licenseRejected 'A missing LGPL text was not rejected.'
Require (-not (Test-Path -LiteralPath $noticeOutput)) 'Missing-license validation left a partial output directory.'
Write-Host '[SourceDistributionTest] Tampered-source and missing-license checks passed.'
