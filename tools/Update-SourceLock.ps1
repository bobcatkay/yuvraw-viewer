[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$VcpkgRoot,
    [ValidateSet('x64-windows-static-md','x86-windows-static-md')][string]$Triplet = 'x64-windows-static-md'
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$VcpkgRoot = (Resolve-Path -LiteralPath $VcpkgRoot).Path
$manifest = Get-Content -Raw -Encoding UTF8 (Join-Path $repoRoot 'vcpkg.json') | ConvertFrom-Json
$lockPath = Join-Path $repoRoot 'third-party/sources.lock.json'
$oldLock = Get-Content -Raw -Encoding UTF8 $lockPath | ConvertFrom-Json
$lock = [ordered]@{schemaVersion=1; vcpkgBaseline=$manifest.'builtin-baseline'; ports=@(); vendored=@()}
$work = Join-Path $repoRoot ('artifacts/source-lock/' + [guid]::NewGuid().ToString('N'))
$cache = Join-Path $repoRoot 'artifacts/source-cache'
New-Item -ItemType Directory -Force -Path $work,$cache | Out-Null
foreach ($name in @('libraw','lcms','zlib','libjpeg-turbo','jasper','glm')) {
    $spdx = Get-Content -Raw -Encoding UTF8 (Join-Path $repoRoot "vcpkg_installed/$Triplet/share/$name/vcpkg.spdx.json") | ConvertFrom-Json
    $port = $spdx.packages | Where-Object SPDXID -EQ 'SPDXRef-port'
    $versionParts = $port.versionInfo -split '#'
    $portVersion = if ($versionParts.Count -gt 1) { [int]$versionParts[1] } else { 0 }
    $override = @($manifest.overrides | Where-Object name -EQ $name)
    $overrideRevision = if ($override.Count -eq 1 -and $override[0].PSObject.Properties['port-version']) { [int]$override[0].'port-version' } else { 0 }
    if ($override.Count -ne 1 -or $override[0].version -ne $versionParts[0] -or
        $overrideRevision -ne $portVersion) {
        throw "[SourceLock] Restore the manifest's $name version before updating its source lock."
    }
    $tree = ($port.downloadLocation -split '@')[-1]
    if ($tree -notmatch '^[a-f0-9]{40}$') { throw "[SourceLock] Invalid tree for $name" }
    $portWork = Join-Path $work $name
    New-Item -ItemType Directory -Force -Path $portWork | Out-Null
    $archive = Join-Path $work "$name.tar"
    & git -C $VcpkgRoot -c core.autocrlf=false -c core.eol=lf archive --format=tar "--output=$archive" $tree
    if ($LASTEXITCODE) { throw "[SourceLock] Cannot export $name recipe." }
    & tar -xf $archive -C $portWork
    if ($LASTEXITCODE) { throw "[SourceLock] Cannot extract $name recipe." }
    $files = @(foreach ($file in $spdx.files | Where-Object SPDXID -Like 'SPDXRef-port-file-*') {
        if (!$file.fileName.StartsWith('./')) { throw '[SourceLock] Invalid SPDX recipe path.' }
        $relative = $file.fileName.Substring(2)
        if (!$relative -or $relative -match '\\|[<>:"|?*\x00-\x1F]|^/|(^|/)\.\.?(/|$)|//|/$|[ .](/|$)') { throw '[SourceLock] Invalid recipe path.' }
        $hash = ($file.checksums | Where-Object algorithm -EQ SHA256).checksumValue
        if ((Get-FileHash -LiteralPath (Join-Path $portWork $relative) -Algorithm SHA256).Hash -ne $hash) { throw "[SourceLock] Recipe differs: $name/$relative" }
        [ordered]@{path=$relative;sha256=$hash}
    })
    $resources = @(foreach ($resource in $spdx.packages | Where-Object SPDXID -Like 'SPDXRef-resource-*') {
        if ($resource.downloadLocation -notmatch '^git\+https://github\.com/([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)@([A-Za-z0-9_.+-]+)$') { throw "[SourceLock] Review unsupported source $($resource.downloadLocation)" }
        $upstream = $Matches[1]; $ref = $Matches[2]
        $fileName = ($upstream -replace '/', '-') + '-' + $ref + '.tar.gz'
        $hash = ($resource.checksums | Where-Object algorithm -EQ SHA512).checksumValue
        $source = Join-Path $VcpkgRoot "downloads/$fileName"
        if ((Get-FileHash -LiteralPath $source -Algorithm SHA512).Hash -ne $hash) { throw "[SourceLock] Source differs: $fileName" }
        Copy-Item -LiteralPath $source -Destination (Join-Path $cache $fileName)
        [ordered]@{file=$fileName;url="https://github.com/$upstream/archive/$ref.tar.gz";sha512=$hash}
    })
    $lock.ports += [ordered]@{name=$name;version=$versionParts[0];portVersion=$portVersion;tree=$tree;files=$files;resources=$resources}
}
$glfw = $oldLock.vendored | Where-Object name -EQ 'glfw'
$glfwFiles = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'libs/GLFW/src'),(Join-Path $repoRoot 'libs/GLFW/include') -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{path=$_.FullName.Substring($repoRoot.Length + 1).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
$lock.vendored += [ordered]@{name='glfw';version='3.4';sourceFiles=$glfwFiles;resources=$glfw.resources}
$utf8 = New-Object Text.UTF8Encoding($false)
$stagedLock = Join-Path $work 'sources.lock.json'
[IO.File]::WriteAllText($stagedLock, ($lock | ConvertTo-Json -Depth 12) + [Environment]::NewLine, $utf8)
# Keep the old lock and all managed snapshots intact until validation is complete.
# The transaction checks local modifications and rolls back every earlier write
# if any replacement, including the final lock publication, fails.
& (Join-Path $PSScriptRoot 'Set-SourceSnapshot.ps1') -RepositoryRoot $repoRoot -StagedPortRoot $work `
    -StagedLockPath $stagedLock -WorkDirectory (Join-Path $work 'transaction')
Write-Host '[SourceLock] Updated six installed recipes, source archives and vendored GLFW source hashes.'
