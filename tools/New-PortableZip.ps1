[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$SourceDirectory, [Parameter(Mandatory = $true)][string]$DestinationPath)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $SourceDirectory).Path.TrimEnd('\','/')
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$stream = [IO.File]::Open([IO.Path]::GetFullPath($DestinationPath), [IO.FileMode]::CreateNew)
$archive = $null
try {
    $archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create)
    foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File | Sort-Object FullName) {
        $relative = $file.FullName.Substring($root.Length + 1).Replace('\','/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $relative, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { if ($archive) { $archive.Dispose() }; $stream.Dispose() }
