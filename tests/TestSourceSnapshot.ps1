[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$repoRoot = Split-Path -Parent $PSScriptRoot
$helper = Join-Path $repoRoot 'tools/Set-SourceSnapshot.ps1'
$testRoot = Join-Path $repoRoot ('artifacts/source-snapshot-tests/' + [guid]::NewGuid().ToString('N'))
$utf8 = New-Object Text.UTF8Encoding($false)
$expectedCreatedText = 'new managed source'
$personalText = 'unregistered local notes must survive'
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null

function Assert-Test {
    param([bool]$Condition, [string]$Message)
    if (!$Condition) { throw "[SourceSnapshotTest] $Message" }
}

function Write-Text {
    param([string]$Path, [string]$Text)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    [IO.File]::WriteAllText($Path, $Text, $utf8)
}

function New-Fixture {
    param([string]$Name)
    $root = Join-Path $testRoot $Name
    $managed = Join-Path $root 'third-party/vcpkg-ports/sample'
    $stage = Join-Path $root 'staged/sample'
    $oldFiles = @()
    $newFiles = @()
    foreach ($name in @('a.txt','b.txt','obsolete.patch')) {
        $path = Join-Path $managed $name
        Write-Text $path ('original ' + $name)
        $oldFiles += @{path=$name;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
    }
    foreach ($name in @('a.txt','b.txt','new-dir/added.patch')) {
        $path = Join-Path $stage $name
        Write-Text $path $expectedCreatedText
        $newFiles += @{path=$name;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
    }
    Write-Text (Join-Path $managed 'personal.patch') $personalText
    $oldLock = @{schemaVersion=1;vcpkgBaseline='previous';ports=@(@{name='sample';version='1';files=$oldFiles});vendored=@()}
    $newLock = @{schemaVersion=1;vcpkgBaseline='next';ports=@(@{name='sample';version='2';files=$newFiles});vendored=@()}
    Write-Text (Join-Path $root 'third-party/sources.lock.json') ($oldLock | ConvertTo-Json -Depth 8)
    Write-Text (Join-Path $root 'new.lock.json') ($newLock | ConvertTo-Json -Depth 8)
    return $root
}

function Read-State {
    param([string]$Root)
    $state = @(Get-ChildItem -LiteralPath (Join-Path $Root 'third-party') -Recurse -File | Sort-Object FullName | ForEach-Object {
        $_.FullName.Substring($Root.Length) + ' ' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    })
    return $state -join "`n"
}

function Invoke-Transaction {
    param([string]$Root)
    & $helper -RepositoryRoot $Root -StagedPortRoot (Join-Path $Root 'staged') `
        -StagedLockPath (Join-Path $Root 'new.lock.json') -WorkDirectory (Join-Path $Root 'transaction')
}

function Assert-RejectedWithoutChanges {
    param([string]$Root, [string]$Reason)
    $before = Read-State $Root
    $errorText = ''
    try { Invoke-Transaction $Root } catch { $errorText = $_.Exception.Message }
    Assert-Test ($errorText.Contains($Reason)) "Wrong rejection for $Reason`: $errorText"
    Assert-Test ((Read-State $Root) -ceq $before) 'Rejected transaction changed managed or unregistered files.'
    Write-Host "[SourceSnapshotTest] Preserved state on $Reason"
    return $errorText
}

$modified = New-Fixture 'modified-same-name'
Write-Text (Join-Path $modified 'third-party/vcpkg-ports/sample/a.txt') 'local modification to a same-name managed file'
Assert-RejectedWithoutChanges $modified 'Preserve locally modified file:' | Out-Null

$obsolete = New-Fixture 'modified-obsolete'
Write-Text (Join-Path $obsolete 'third-party/vcpkg-ports/sample/obsolete.patch') 'local modification to an obsolete managed file'
Assert-RejectedWithoutChanges $obsolete 'Preserve locally modified file:' | Out-Null

$collision = New-Fixture 'unregistered-collision'
Write-Text (Join-Path $collision 'third-party/vcpkg-ports/sample/new-dir/added.patch') $personalText
Assert-RejectedWithoutChanges $collision 'Preserve unregistered file:' | Out-Null

$escape = New-Fixture 'invalid-path'
$lockPath = Join-Path $escape 'third-party/sources.lock.json'
$lock = Get-Content -Raw -Encoding UTF8 -LiteralPath $lockPath | ConvertFrom-Json
$lock.ports[0].files[0].path = '../outside.txt'
Write-Text $lockPath ($lock | ConvertTo-Json -Depth 8)
Assert-RejectedWithoutChanges $escape 'Invalid managed path:' | Out-Null

$windowsAlias = New-Fixture 'windows-path-alias'
$lockPath = Join-Path $windowsAlias 'third-party/sources.lock.json'
$lock = Get-Content -Raw -Encoding UTF8 -LiteralPath $lockPath | ConvertFrom-Json
$lock.ports[0].files[0].path = 'nested/.. /outside.txt'
Write-Text $lockPath ($lock | ConvertTo-Json -Depth 8)
Assert-RejectedWithoutChanges $windowsAlias 'Invalid managed path:' | Out-Null

$junction = New-Fixture 'junction-path'
$outsideFile = Join-Path $testRoot 'junction-target/outside.txt'
Write-Text $outsideFile $personalText
New-Item -ItemType Junction -Path (Join-Path $junction 'third-party/vcpkg-ports/sample/junction') -Target (Split-Path -Parent $outsideFile) | Out-Null
$lockPath = Join-Path $junction 'third-party/sources.lock.json'
$lock = Get-Content -Raw -Encoding UTF8 -LiteralPath $lockPath | ConvertFrom-Json
$lock.ports[0].files[0].path = 'junction/outside.txt'
$lock.ports[0].files[0].sha256 = (Get-FileHash -LiteralPath $outsideFile -Algorithm SHA256).Hash
Write-Text $lockPath ($lock | ConvertTo-Json -Depth 8)
Assert-RejectedWithoutChanges $junction 'Managed paths cannot use reparse points:' | Out-Null
Assert-Test ([IO.File]::ReadAllText($outsideFile) -ceq $personalText) 'Junction target outside the managed root changed.'

# Deny delete/replace on the final lock only. The preceding replacements,
# removal and creation must all be undone when that real filesystem operation
# fails; no test-only failure switch is present in the production helper.
$rollback = New-Fixture 'late-lock-failure'
$lockPath = Join-Path $rollback 'third-party/sources.lock.json'
$handle = [IO.File]::Open($lockPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
try {
    $errorText = Assert-RejectedWithoutChanges $rollback 'Commit failed and was rolled back at'
    Assert-Test ($errorText.Contains($lockPath)) 'Failure must occur at the final lock publication.'
} finally { $handle.Dispose() }

$success = New-Fixture 'success'
Invoke-Transaction $success
$managed = Join-Path $success 'third-party/vcpkg-ports/sample'
foreach ($name in @('a.txt','b.txt','new-dir/added.patch')) {
    Assert-Test ([IO.File]::ReadAllText((Join-Path $managed $name)) -ceq $expectedCreatedText) "New content was not committed: $name"
}
Assert-Test (!(Test-Path -LiteralPath (Join-Path $managed 'obsolete.patch'))) 'Obsolete registered file survived a successful update.'
Assert-Test ([IO.File]::ReadAllText((Join-Path $managed 'personal.patch')) -ceq $personalText) 'Unregistered personal file changed.'
Assert-Test ((Get-FileHash -LiteralPath (Join-Path $success 'third-party/sources.lock.json') -Algorithm SHA256).Hash -eq
    (Get-FileHash -LiteralPath (Join-Path $success 'new.lock.json') -Algorithm SHA256).Hash) 'Final lock does not describe the committed snapshot.'
Write-Host '[SourceSnapshotTest] Local edits, unregistered files, path containment, real late rollback and successful commit passed.'
