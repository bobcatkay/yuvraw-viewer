# Build and run the end-to-end OpenGL format validation.
#
# The test uses a hidden GLFW window and requires an OpenGL 3.3 context.
# It first validates worker-thread texture creation in a second shared context,
# including fence/flush handoff and visibility from the main context.
# For every format it runs:
#   raw bytes -> FRawImageLoader -> FTextureData -> GLSL -> FBO -> glReadPixels
# and compares the GPU pixels against FImageSampler's CPU reference.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tests\run_gl_format_validation.ps1

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$srcDir = Join-Path $repoRoot "src"
$outDir = Join-Path $env:TEMP "YUVRawOpenGLValidation"
$vswherePath = Join-Path ${env:ProgramFiles(x86)} `
    "Microsoft Visual Studio\Installer\vswhere.exe"

if (-not (Test-Path -LiteralPath $vswherePath)) {
    throw "vswhere.exe was not found; Visual Studio is required"
}

$installationPath = & $vswherePath `
    -latest `
    -products '*' `
    -requires Microsoft.Component.MSBuild `
    -property installationPath |
    Select-Object -First 1

$msbuildPath = & $vswherePath `
    -latest `
    -products '*' `
    -requires Microsoft.Component.MSBuild `
    -find 'MSBuild\**\Bin\MSBuild.exe' |
    Select-Object -First 1

if (-not $installationPath -or
    -not $msbuildPath -or
    -not (Test-Path -LiteralPath $msbuildPath)) {
    throw "Visual Studio installation or MSBuild.exe was not found"
}

New-Item -ItemType Directory -Force -Path $outDir |
    Out-Null

# Some launchers inject both Path and PATH. MSBuild collects environment
# variables case-insensitively and then fails while starting CL.exe. Normalize
# the environment for this test process only.
$processPathValue = $env:PATH
[System.Environment]::SetEnvironmentVariable(
    "PATH",
    $null,
    "Process")
[System.Environment]::SetEnvironmentVariable(
    "Path",
    $null,
    "Process")
[System.Environment]::SetEnvironmentVariable(
    "Path",
    $processPathValue,
    "Process")

& $msbuildPath `
    (Join-Path $repoRoot "YUVRaw.sln") `
    "/t:Build" `
    "/p:Configuration=Debug" `
    "/p:Platform=x64" `
    "/m:1" `
    "/nodeReuse:false" `
    "/nologo" `
    "/v:minimal"

if ($LASTEXITCODE -ne 0) {
    throw "Debug x64 solution build failed"
}

$sources = @(
    (Join-Path $PSScriptRoot "TestOpenGLFormatPipeline.cpp")
    (Join-Path $srcDir "Image\FRawImageLoader.cpp")
    (Join-Path $srcDir "Image\FImageData.cpp")
    (Join-Path $srcDir "Image\FImageFormatDesc.cpp")
    (Join-Path $srcDir "Image\FImageSampler.cpp")
    (Join-Path $srcDir "gl\FTexture.cpp")
    (Join-Path $srcDir "gl\FTextureData.cpp")
    (Join-Path $srcDir "gl\FShader.cpp")
    (Join-Path $srcDir "gl\FShaderManager.cpp")
    (Join-Path $srcDir "Core\FLogger.cpp")
    (Join-Path $srcDir "Util.cpp")
)
$quotedSources =
    ($sources | ForEach-Object { "`"$_`"" }) -join " "
$exePath =
    Join-Path $outDir "TestOpenGLFormatPipeline.exe"
$gladInclude =
    Join-Path $repoRoot "libs\Glad\include"
$glfwInclude =
    Join-Path $repoRoot "libs\GLFW\include"
$libraryDirectory =
    Join-Path $repoRoot "libs\product\Debug\x64"
$vsdevPath =
    Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"

if (-not (Test-Path -LiteralPath $vsdevPath)) {
    throw "VsDevCmd.bat was not found"
}

$compileCommand =
    "`"$vsdevPath`" -arch=amd64 -host_arch=amd64 >nul 2>&1 && " +
    "cl /nologo /std:c++17 /EHsc /MDd /utf-8 " +
    "/I `"$srcDir`" /I `"$gladInclude`" /I `"$glfwInclude`" " +
    "/Fe:`"$exePath`" /Fo:`"$outDir\\`" $quotedSources " +
    "/link /LIBPATH:`"$libraryDirectory`" " +
    "Glad.lib GLFW.lib opengl32.lib user32.lib gdi32.lib shell32.lib"

cmd /c $compileCommand |
    Select-Object -Last 12

if ($LASTEXITCODE -ne 0) {
    throw "TestOpenGLFormatPipeline compilation failed"
}

Write-Host ""
& $exePath
$testExitCode = $LASTEXITCODE

if ($testExitCode -ne 0) {
    throw "OpenGL format validation failed with exit code $testExitCode"
}
