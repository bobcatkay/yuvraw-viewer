[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$RepositoryRoot,
    [Parameter(Mandatory = $true)][string]$StagedPortRoot,
    [Parameter(Mandatory = $true)][string]$StagedLockPath,
    [Parameter(Mandatory = $true)][string]$WorkDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$RepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\','/')
$StagedPortRoot = [IO.Path]::GetFullPath($StagedPortRoot).TrimEnd('\','/')
$WorkDirectory = [IO.Path]::GetFullPath($WorkDirectory).TrimEnd('\','/')
$repoPrefix = $RepositoryRoot + [IO.Path]::DirectorySeparatorChar
$lockRelativePath = 'third-party/sources.lock.json'
$lockPath = Join-Path $RepositoryRoot $lockRelativePath

function Assert-Snapshot {
    param([bool]$Condition, [string]$Message)
    if (!$Condition) { throw "[SourceSnapshot] $Message" }
}

function Resolve-ContainedPath {
    param([string]$Root, [string]$Relative)
    Assert-Snapshot ($Relative -and $Relative -notmatch '\\|[<>:"|?*\x00-\x1F]|^/|(^|/)\.\.?(/|$)|//|/$|[ .](/|$)') "Invalid managed path: $Relative"
    $path = [IO.Path]::GetFullPath((Join-Path $Root $Relative))
    Assert-Snapshot ($path.StartsWith($Root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) "Managed path escapes its root: $Relative"
    # A lexical prefix alone cannot contain a junction or symbolic link. Reject
    # reparse points at every existing component before reading, moving or deleting.
    $component = $Root
    foreach ($part in $Relative.Split('/')) {
        $component = Join-Path $component $part
        if (Test-Path -LiteralPath $component) {
            $item = Get-Item -LiteralPath $component -Force
            Assert-Snapshot (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) "Managed paths cannot use reparse points: $Relative"
        }
    }
    return $path
}

function Get-ManagedFiles {
    param($Lock)
    $files = @{}
    foreach ($port in $Lock.ports) {
        Assert-Snapshot ($port.name -cmatch '^[a-z][a-z0-9-]*$') 'Invalid managed port name.'
        foreach ($file in $port.files) {
            $relative = 'third-party/vcpkg-ports/' + $port.name + '/' + $file.path
            Resolve-ContainedPath $RepositoryRoot $relative | Out-Null
            Assert-Snapshot (!$files.ContainsKey($relative)) "Duplicate managed path: $relative"
            Assert-Snapshot ($file.sha256 -match '^[a-fA-F0-9]{64}$') "Invalid managed hash: $relative"
            $files[$relative] = [string]$file.sha256
        }
    }
    return $files
}

function Assert-ExistingHash {
    param([string]$Path, [string]$Hash)
    Assert-Snapshot (Test-Path -LiteralPath $Path -PathType Leaf) "Missing managed file: $Path"
    Assert-Snapshot ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Hash) "Preserve locally modified file: $Path"
}

Assert-Snapshot ($WorkDirectory.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) 'Transaction work directory must stay inside the repository.'
Assert-Snapshot (!(Test-Path -LiteralPath $WorkDirectory)) 'Transaction work directory must be new.'
Resolve-ContainedPath $RepositoryRoot $lockRelativePath | Out-Null
$oldLockHash = (Get-FileHash -LiteralPath $lockPath -Algorithm SHA256).Hash
$stagedLockHash = (Get-FileHash -LiteralPath $StagedLockPath -Algorithm SHA256).Hash
$oldLock = Get-Content -Raw -Encoding UTF8 -LiteralPath $lockPath | ConvertFrom-Json
$newLock = Get-Content -Raw -Encoding UTF8 -LiteralPath $StagedLockPath | ConvertFrom-Json
$oldFiles = Get-ManagedFiles $oldLock
$newFiles = Get-ManagedFiles $newLock

# Preflight all existing files, including unchanged names, before any managed
# mutation. Only lock-registered files may be replaced or removed.
foreach ($relative in $oldFiles.Keys) {
    Assert-ExistingHash (Resolve-ContainedPath $RepositoryRoot $relative) $oldFiles[$relative]
}
foreach ($relative in $newFiles.Keys) {
    $target = Resolve-ContainedPath $RepositoryRoot $relative
    Assert-Snapshot ($oldFiles.ContainsKey($relative) -or !(Test-Path -LiteralPath $target)) "Preserve unregistered file: $relative"
    $stagedRelative = $relative.Substring('third-party/vcpkg-ports/'.Length)
    Assert-ExistingHash (Resolve-ContainedPath $StagedPortRoot $stagedRelative) $newFiles[$relative]
}

New-Item -ItemType Directory -Force -Path $WorkDirectory | Out-Null
$preparedRoot = Join-Path $WorkDirectory 'prepared'
$backupRoot = Join-Path $WorkDirectory 'backup'
New-Item -ItemType Directory -Force -Path $preparedRoot,$backupRoot | Out-Null
$operations = New-Object 'System.Collections.Generic.List[object]'
foreach ($relative in @($oldFiles.Keys + $newFiles.Keys | Sort-Object -Unique)) {
    $oldHash = if ($oldFiles.ContainsKey($relative)) { $oldFiles[$relative] } else { $null }
    $newHash = if ($newFiles.ContainsKey($relative)) { $newFiles[$relative] } else { $null }
    if ($oldHash -and $newHash -and $oldHash -eq $newHash) { continue }
    $target = Resolve-ContainedPath $RepositoryRoot $relative
    $backup = Resolve-ContainedPath $backupRoot $relative
    $prepared = Resolve-ContainedPath $preparedRoot $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target),(Split-Path -Parent $backup),(Split-Path -Parent $prepared) | Out-Null
    if ($newHash) {
        $stagedRelative = $relative.Substring('third-party/vcpkg-ports/'.Length)
        Copy-Item -LiteralPath (Resolve-ContainedPath $StagedPortRoot $stagedRelative) -Destination $prepared
        Assert-ExistingHash $prepared $newHash
    }
    $kind = if (!$newHash) { 'Remove' } elseif ($oldHash) { 'Replace' } else { 'Create' }
    $operations.Add([pscustomobject]@{Kind=$kind;Target=$target;Prepared=$prepared;Backup=$backup;OldHash=$oldHash;NewHash=$newHash})
}
$preparedLock = Join-Path $preparedRoot 'sources.lock.json'
Copy-Item -LiteralPath $StagedLockPath -Destination $preparedLock
$newLockHash = (Get-FileHash -LiteralPath $preparedLock -Algorithm SHA256).Hash
Assert-Snapshot ($newLockHash -eq $stagedLockHash) 'Staged lock changed while preparing the transaction.'
# The lock is committed last. A failed lock replacement must restore every
# preceding source change instead of leaving mixed versions behind.
$operations.Add([pscustomobject]@{Kind='Replace';Target=$lockPath;Prepared=$preparedLock;Backup=(Join-Path $backupRoot 'sources.lock.json');OldHash=$oldLockHash;NewHash=$newLockHash})
$committed = New-Object 'System.Collections.Generic.List[object]'
$activeTarget = $lockPath
try {
    Assert-ExistingHash $lockPath $oldLockHash
    foreach ($operation in $operations) {
        $activeTarget = $operation.Target
        if ($operation.Target -eq $lockPath) {
            # Recheck the complete result, including unchanged files, before the
            # lock claims this snapshot. Concurrent local edits must be detected.
            foreach ($relative in $newFiles.Keys) {
                Assert-ExistingHash (Resolve-ContainedPath $RepositoryRoot $relative) $newFiles[$relative]
            }
            foreach ($relative in $oldFiles.Keys) {
                if (!$newFiles.ContainsKey($relative)) {
                    Assert-Snapshot (!(Test-Path -LiteralPath (Resolve-ContainedPath $RepositoryRoot $relative))) "Obsolete managed path reappeared during the transaction: $relative"
                }
            }
        }
        if ($operation.OldHash) { Assert-ExistingHash $operation.Target $operation.OldHash }
        else { Assert-Snapshot (!(Test-Path -LiteralPath $operation.Target)) "Preserve unregistered file: $($operation.Target)" }
        if ($operation.Kind -eq 'Remove') {
            [IO.File]::Move($operation.Target, $operation.Backup)
        } elseif ($operation.Kind -eq 'Replace') {
            [IO.File]::Replace($operation.Prepared, $operation.Target, $operation.Backup, $true)
        } else {
            [IO.File]::Move($operation.Prepared, $operation.Target)
        }
        $committed.Add($operation)
    }
} catch {
    $originalError = $_.Exception.Message
    $failedOperation = $activeTarget
    $rollbackErrors = New-Object 'System.Collections.Generic.List[string]'
    for ($index = $committed.Count - 1; $index -ge 0; --$index) {
        $operation = $committed[$index]
        try {
            if ($operation.Kind -eq 'Remove') {
                # Move refuses to replace an independently created file.
                [IO.File]::Move($operation.Backup, $operation.Target)
            } else {
                Assert-ExistingHash $operation.Target $operation.NewHash
                if ($operation.Kind -eq 'Create') {
                    Remove-Item -LiteralPath $operation.Target
                } else {
                    # Windows PowerShell converts $null for a string argument
                    # into an empty path; retain the replaced new file instead.
                    $discardedNewFile = $operation.Backup + '.replaced-new'
                    [IO.File]::Replace($operation.Backup, $operation.Target, $discardedNewFile, $true)
                }
            }
        } catch { $rollbackErrors.Add($_.Exception.Message) }
    }
    if ($rollbackErrors.Count) {
        throw "[SourceSnapshot] Commit failed at $failedOperation`: $originalError Rollback needs attention; preserved backups: $backupRoot. $($rollbackErrors -join ' | ')"
    }
    throw "[SourceSnapshot] Commit failed and was rolled back at $failedOperation`: $originalError"
}
Write-Host '[SourceSnapshot] Committed validated managed source files and lock; unregistered files were preserved.'
