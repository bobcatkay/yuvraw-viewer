[CmdletBinding()]
param([string]$RepositoryRoot = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
if (!$RepositoryRoot) { $RepositoryRoot = Split-Path -Parent $PSScriptRoot }
$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$utf8 = New-Object Text.UTF8Encoding($false, $true)
$documents = @('README.md','ARCHITECTURE.md','CONTRIBUTING.md','SECURITY.md','THIRD_PARTY_NOTICES.md',
    'resources/fonts/README.md','.github/pull_request_template.md')
$documents += @('BUILDING','DEPENDENCIES','PUBLIC_FIXTURES','QUICK_START','SOURCE_DISTRIBUTION') | ForEach-Object { "docs/$_.md" }
# Include future pages too, so adding an English-only document fails immediately.
$documents += Get-ChildItem -LiteralPath (Join-Path $root 'docs') -Filter *.md -File |
    Where-Object Name -NotLike '*.zh-CN.md' | ForEach-Object { 'docs/' + $_.Name }
$documents = @($documents | Sort-Object -Unique)

function Read-Document([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "[DocumentationTest] Missing document: $Path" }
    return [IO.File]::ReadAllText($Path, $utf8)
}

function Get-MarkdownBody([string]$Text) {
    # Examples contain paths and captured output, not navigation links.
    return [regex]::Replace($Text, '(?ms)^ {0,3}(`{3,}|~{3,})[^\r\n]*\r?\n.*?^ {0,3}\1[ \t]*(?:\r?\n|$)', '')
}

foreach ($english in $documents) {
    $chinese = $english -replace '\.md$', '.zh-CN.md'
    foreach ($relative in @($english,$chinese)) {
        $path = Join-Path $root $relative
        $text = Read-Document $path
        $other = if ($relative -eq $english) { $chinese } else { $english }
        $navigation = '](' + [IO.Path]::GetFileName($other) + ')'
        # A PR body resolves relative links at repository root, not .github/.
        if ($relative -like '.github/*') {
            $navigation = '](https://github.com/bobcatkay/yuvraw-viewer/blob/main/' + $other + ')'
        }
        if (!(($text -split '\r?\n', 2)[0].Contains($navigation))) {
            throw "[DocumentationTest] Missing top language link: $relative"
        }
        $body = Get-MarkdownBody $text
        foreach ($match in [regex]::Matches($body, '\]\(([^)\s]+)\)')) {
            $href = $match.Groups[1].Value
            if ($href -match '^[a-zA-Z][a-zA-Z0-9+.-]*:|^//') { continue }
            $parts = $href -split '#', 2
            $target = if ($parts[0]) { Join-Path (Split-Path -Parent $path) ([Uri]::UnescapeDataString($parts[0])) } else { $path }
            if (!(Test-Path -LiteralPath $target)) { throw "[DocumentationTest] Broken link in ${relative}: $href" }
            if ($parts.Count -eq 2 -and $parts[1] -and $target -like '*.md') {
                $targetBody = Get-MarkdownBody (Read-Document $target)
                $anchors = @([regex]::Matches($targetBody, '(?m)^#{1,6}\s+(.+?)\s*$') | ForEach-Object {
                    $heading = $_.Groups[1].Value.ToLowerInvariant()
                    ([regex]::Replace($heading, '[^\p{L}\p{Nd}_\- ]', '') -replace ' ', '-')
                })
                if ($anchors -cnotcontains [Uri]::UnescapeDataString($parts[1])) {
                    throw "[DocumentationTest] Broken heading link in ${relative}: $href"
                }
            }
        }
    }
}
foreach ($name in @('bug_report','feature_request')) {
    foreach ($suffix in @('','.zh-CN')) {
        $null = Read-Document (Join-Path $root ".github/ISSUE_TEMPLATE/$name$suffix.yml")
    }
}
$instructions = Read-Document (Join-Path $root 'docs/SOURCE_CODE.txt')
foreach ($required in @('SOURCE_DISTRIBUTION.md','SOURCE_DISTRIBUTION.zh-CN.md',
    '{{version}}','{{binaryName}}','{{sourceName}}','{{checksumsName}}','{{platform}}')) {
    if (!$instructions.Contains($required)) { throw "[DocumentationTest] Incomplete bilingual source instructions: $required" }
}
Write-Host "[DocumentationTest] Verified $($documents.Count) bilingual document pairs, language navigation, relative links and heading targets."
