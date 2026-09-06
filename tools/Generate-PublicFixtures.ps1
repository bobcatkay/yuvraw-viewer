# Generates original, redistributable synthetic fixtures. Never reads external image inputs.
param([string]$OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'artifacts\public-fixtures'))
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
if (-not $installation) { throw 'MSVC x64 tools are required to generate fixtures.' }
$vsdev = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
$buildDirectory = Join-Path $repoRoot 'artifacts\fixture-generator'
New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null
$exe = Join-Path $buildDirectory 'GeneratePublicFixtures.exe'
$source = Join-Path $repoRoot 'tests\GeneratePublicFixtures.cpp'
$command = "`"$vsdev`" -arch=amd64 -host_arch=amd64 >nul 2>&1 && cl /nologo /std:c++17 /EHsc /utf-8 /W4 /WX /Fe:`"$exe`" /Fo:`"$buildDirectory\\`" `"$source`""
cmd /c $command
if ($LASTEXITCODE -ne 0) { throw 'Public fixture generator compilation failed.' }
& $exe $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'Public fixture generation failed.' }
$names = @('colorbars_64x48_RGBA8.raw', 'gradient_64x48_stride80_NV21.yuv', 'gradient_64x48_stride144_P010.yuv', 'gradient_64x48_Bayer12.raw', 'two_frames_64x48_stride80_NV21.yuv', 'synthetic_64x48.dng')
$files = foreach ($name in $names) {
    $path = Join-Path $OutputDirectory $name
    @{ name = $name; bytes = (Get-Item -LiteralPath $path).Length; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$manifest = @{ schemaVersion = 1; generator = 'tests/GeneratePublicFixtures.cpp'; license = 'GPL-3.0-only'; files = @($files) } | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText((Join-Path (Resolve-Path -LiteralPath $OutputDirectory).Path 'manifest.json'), $manifest, [System.Text.UTF8Encoding]::new($false))
Write-Host "Public fixtures: $OutputDirectory"
