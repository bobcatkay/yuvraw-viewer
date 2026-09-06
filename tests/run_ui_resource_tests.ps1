param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outDirectory = Join-Path $repoRoot ('artifacts/ui-resource-tests/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $outDirectory | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
if (!$installation) { throw 'Visual Studio C++ x64 tools are required' }
$vsdev = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
$executable = Join-Path $outDirectory 'TestUiResources.exe'
$sources = @(
    (Join-Path $PSScriptRoot 'TestUiResources.cpp')
    (Join-Path $repoRoot 'libs/ImGui/imgui.cpp')
    (Join-Path $repoRoot 'libs/ImGui/imgui_draw.cpp')
    (Join-Path $repoRoot 'libs/ImGui/imgui_tables.cpp')
    (Join-Path $repoRoot 'libs/ImGui/imgui_widgets.cpp')
)
$quotedSources = ($sources | ForEach-Object { '"' + $_ + '"' }) -join ' '
$compile = '"' + $vsdev + '" -arch=amd64 -host_arch=amd64 >nul 2>&1 && cl /nologo /std:c++17 /EHsc /utf-8 /W4 /I "' +
    (Join-Path $repoRoot 'src') + '" /I "' + (Join-Path $repoRoot 'libs/ImGui') + '" /Fe:"' +
    $executable + '" /Fo:"' + $outDirectory + '\\" ' + $quotedSources + ' /link shell32.lib ole32.lib imm32.lib'
cmd /c $compile
if ($LASTEXITCODE -ne 0) { throw 'UI resource test compilation failed' }

& $executable $repoRoot $outDirectory
if ($LASTEXITCODE -ne 0) { throw 'UI resource tests failed' }
