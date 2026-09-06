# 离线自检：编译并运行不依赖 OpenGL / ImGui 的纯逻辑测试。
#
#   powershell -ExecutionPolicy Bypass -File tests\run_tests.ps1
#   powershell -ExecutionPolicy Bypass -File tests\run_tests.ps1 `
#     -DngFixturePath C:\path\to\sample_1mp.dng
#
# 目前覆盖：
#   - 格式描述表（平面几何、帧大小、stride、名字查找）
#   - 命令行尺寸/stride/位深的严格有界整数解析
#   - 全格式字节序/有效位对齐策略、16bit 大端转换与 RAW_SENSOR 位深
#   - 默认程序化公共 DNG：尺寸、RGBA/alpha、渐变、损坏/超限与失败后恢复
#   - 额外真实 DNG 可由 -DngFixturePath 显式传入，CI 不需要私有原图
#   - WIC 超大图片头、截断、无效文件、安全内存边界与可选 codec 枚举
#   - 由文件大小反推分辨率（候选排序、stride 反演、跨格式提示）
#   - 色彩管线（PQ/HLG/sRGB 传输函数、原色矩阵、色调映射、直通恒等）
#   - 文件日志（线程安全写入、单文件大小限制、最多保留五个滚动文件）
#   - 用户设置（窗口边界、六项基础主题色、最近打开、直方图/对比偏好、全量清理与跨进程持久化）
#   - 旧版配置迁移（新文件优先、格式预设保留、失败重试、清除后不再导入）
#   - 窗口几何（1080p / 高 DPI / 副屏下为原生标题栏和边框预留空间）
#   - 单图/平铺对比的缩放平移字段同步（保留每张图自己的旋转与镜像）
#   - 图片配置 LRU 缓存（命中提升、容量淘汰、版本化持久化与损坏文件拒绝）
#   - 同目录同后缀上一张图属性继承（后缀隔离、自描述格式、文件大小不匹配时回退）
#   - 异步图片请求邮箱（代际递增、最新请求合并、取消与有界待处理槽）
#   - 无损 WebP 编码器的往返（编码后用系统 WIC 解码器读回来逐像素比对）
#   - 导出全流程（转 RGB -> 缩放 -> 编码 -> 读回比对）
#   - UTF-8 中文路径下的 RAW/WIC 文件读写
# 改动 FImageFormatDesc / 新增格式 / 动过 FColorTransform / FWebpEncoder / FImageExporter
# 后务必跑一遍。

param(
    [string]$DngFixturePath = "",
    [string[]]$SuiteNames = @(),
    [switch]$Analyze,
    [switch]$EnableAddressSanitizer
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$srcDir   = Join-Path $repoRoot "src"
$mode = if ($EnableAddressSanitizer) { 'asan' } elseif ($Analyze) { 'analyze' } else { 'normal' }
$outDir   = Join-Path $repoRoot "artifacts\cpu-tests\$mode"

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$vswherePath = Join-Path ${env:ProgramFiles(x86)} `
    "Microsoft Visual Studio\Installer\vswhere.exe"

if (-not (Test-Path -LiteralPath $vswherePath)) {
    throw "找不到 vswhere.exe，请确认 Visual Studio 已安装"
}

$installationPath = & $vswherePath `
    -latest `
    -products '*' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath |
    Select-Object -First 1

if (-not $installationPath) {
    throw "找不到包含 MSVC x64 工具链的 Visual Studio 安装"
}

$vsdev = Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"

if (-not (Test-Path -LiteralPath $vsdev)) {
    throw "找不到 VsDevCmd.bat"
}

# 每个条目：可执行文件名 / 源文件 / 附加链接库
$suites = @(
    @{
        Name = "TestWicImageLoader"
        Sources = @(
            (Join-Path $PSScriptRoot "TestWicImageLoader.cpp")
            (Join-Path $srcDir "Image\FWicImageLoader.cpp")
            (Join-Path $srcDir "Image\FImageData.cpp")
            (Join-Path $srcDir "Image\FImageFormatDesc.cpp")
            (Join-Path $srcDir "Core\FLogger.cpp")
            (Join-Path $srcDir "Util.cpp")
        )
        Libs = "windowscodecs.lib ole32.lib"
    },
    @{
        Name    = "TestFormatDesc"
        Sources = @(
            (Join-Path $PSScriptRoot "TestFormatDesc.cpp")
            (Join-Path $srcDir "Image\FImageFormatDesc.cpp")
        )
        Libs    = ""
    },
    @{
        Name    = "TestResolutionGuess"
        Sources = @(
            (Join-Path $PSScriptRoot "TestResolutionGuess.cpp")
            (Join-Path $srcDir "Image\FResolutionGuess.cpp")
            (Join-Path $srcDir "Image\FImageFormatDesc.cpp")
        )
        Libs    = ""
    },
    @{
        Name    = "TestCommandLine"
        Sources = @(
            (Join-Path $PSScriptRoot "TestCommandLine.cpp")
            (Join-Path $srcDir "Core\FCommandLine.cpp")
            (Join-Path $srcDir "Image\FImageFormatDesc.cpp")
            (Join-Path $srcDir "Core\FLogger.cpp")
            (Join-Path $srcDir "Util.cpp")
        )
        Libs    = ""
    },
    @{
        Name    = "TestFilenameParser"
        Sources = @(
            (Join-Path $PSScriptRoot "TestFilenameParser.cpp")
            (Join-Path $srcDir "Util.cpp")
            (Join-Path $srcDir "Image\FImageFormatDesc.cpp")
            (Join-Path $srcDir "Core\FLogger.cpp")
        )
        Libs    = ""
    },
    @{
        Name    = "TestRawImageLoader"
        Sources = @(
            (Join-Path $PSScriptRoot "TestRawImageLoader.cpp")
            (Join-Path $srcDir "Image\FRawImageLoader.cpp")
            (Join-Path $srcDir "Image\FImageData.cpp")
            (Join-Path $srcDir "Image\FImageFormatDesc.cpp")
            (Join-Path $srcDir "Core\FLogger.cpp")
            (Join-Path $srcDir "Util.cpp")
        )
        Libs    = ""
    },
    @{
        Name    = "TestColorPipeline"
        Sources = @(
            (Join-Path $PSScriptRoot "TestColorPipeline.cpp")
            (Join-Path $srcDir "Image\FImageFormatDesc.cpp")
        )
        Libs    = ""
    },
    @{
        Name    = "TestImageSampler"
        Sources = @(
            (Join-Path $PSScriptRoot "TestImageSampler.cpp")
            (Join-Path $srcDir "Image\FImageSampler.cpp")
            (Join-Path $srcDir "Image\FImageData.cpp")
            (Join-Path $srcDir "Image\FImageFormatDesc.cpp")
        )
        Libs    = ""
    },
    @{
        Name    = "TestLogger"
        Sources = @(
            (Join-Path $PSScriptRoot "TestLogger.cpp")
            (Join-Path $srcDir "Core\FLogger.cpp")
        )
        Libs    = ""
    },
    @{
        Name    = "TestUserSettings"
        Sources = @(
            (Join-Path $PSScriptRoot "TestUserSettings.cpp")
            (Join-Path $srcDir "Core\FUserSettings.cpp")
            (Join-Path $srcDir "Image\FImageFormatDesc.cpp")
            (Join-Path $srcDir "Core\FLogger.cpp")
        )
        Libs    = ""
    },
    @{
        Name    = "TestUserDataMigration"
        Sources = @(
            (Join-Path $PSScriptRoot "TestUserDataMigration.cpp")
            (Join-Path $srcDir "Core\FUserSettings.cpp")
            (Join-Path $srcDir "Image\FImageFormatDesc.cpp")
            (Join-Path $srcDir "Core\FLogger.cpp")
        )
        Libs    = ""
    },
    @{
        Name    = "TestWindowPlacement"
        Sources = @(
            (Join-Path $PSScriptRoot "TestWindowPlacement.cpp")
        )
        Libs    = ""
    },
    @{
        Name    = "TestImageViewSettings"
        Sources = @(
            (Join-Path $PSScriptRoot "TestImageViewSettings.cpp")
        )
        Libs    = ""
    },
    @{
        Name    = "TestImageConfigCache"
        Sources = @(
            (Join-Path $PSScriptRoot "TestImageConfigCache.cpp")
            (Join-Path $srcDir "Core\FImageConfigCache.cpp")
        )
        Libs    = ""
    },
    @{
        Name    = "TestDirectoryImagePropertyHistory"
        Sources = @(
            (Join-Path $PSScriptRoot "TestDirectoryImagePropertyHistory.cpp")
            (Join-Path $srcDir "Core\FDirectoryImagePropertyHistory.cpp")
            (Join-Path $srcDir "Image\FResolutionGuess.cpp")
            (Join-Path $srcDir "Image\FImageFormatDesc.cpp")
        )
        Libs    = ""
    },
    @{
        Name    = "TestAsyncImageLoadMailbox"
        Sources = @(
            (Join-Path $PSScriptRoot "TestAsyncImageLoadMailbox.cpp")
        )
        Libs    = ""
    },
    @{
        Name    = "TestWebpEncoder"
        Sources = @(
            (Join-Path $PSScriptRoot "TestWebpEncoder.cpp")
            (Join-Path $srcDir "Image\FWebpEncoder.cpp")
            (Join-Path $srcDir "Image\FWicImageLoader.cpp")
            (Join-Path $srcDir "Image\FImageData.cpp")
            (Join-Path $srcDir "Image\FImageFormatDesc.cpp")
            (Join-Path $srcDir "Core\FLogger.cpp")
            (Join-Path $srcDir "Util.cpp")
        )
        Libs    = "windowscodecs.lib ole32.lib"
    },
    @{
        Name    = "TestImageExporter"
        Sources = @(
            (Join-Path $PSScriptRoot "TestImageExporter.cpp")
            (Join-Path $srcDir "Image\FImageExporter.cpp")
            (Join-Path $srcDir "Image\FImageSampler.cpp")
            (Join-Path $srcDir "Image\FImageData.cpp")
            (Join-Path $srcDir "Image\FImageFormatDesc.cpp")
            (Join-Path $srcDir "Image\FWicImageLoader.cpp")
            (Join-Path $srcDir "Image\FWebpEncoder.cpp")
            (Join-Path $srcDir "Core\FLogger.cpp")
            (Join-Path $srcDir "Util.cpp")
        )
        Libs    = "windowscodecs.lib ole32.lib"
    }
)

$testExit = 0
$skippedSuites = @()
$passedSuites = @()
$skipExitCode = 77
$extraFlags = if ($EnableAddressSanitizer) { '/fsanitize=address /Zi' } else { '' }
if ($Analyze) { $extraFlags += ' /analyze /analyze:external- /WX' }
if ($SuiteNames.Count) {
    foreach ($name in $SuiteNames) {
        if ($name -notin $suites.Name -and $name -ne 'TestDngImageLoader') { throw "Unknown test suite: $name" }
    }
    $suites = @($suites | Where-Object { $_.Name -in $SuiteNames })
}

foreach ($suite in $suites) {
    $exe     = Join-Path $outDir ($suite.Name + ".exe")
    $sources = ($suite.Sources | ForEach-Object { "`"$_`"" }) -join " "
    $link    = if ($suite.Libs) { "/link " + $suite.Libs } else { "" }

    $compile = "`"$vsdev`" -arch=amd64 -host_arch=amd64 >nul 2>&1 && cl /nologo /std:c++17 /EHsc /MD /utf-8 $extraFlags /I `"$srcDir`" /Fe:`"$exe`" /Fo:`"$outDir\\`" /Fd:`"$outDir\$($suite.Name).pdb`" $sources $link"

    cmd /c $compile

    if ($LASTEXITCODE -ne 0) {
        throw ($suite.Name + " 编译失败")
    }

    Write-Host ""
    # ASan runtime DLLs live beside cl.exe; execute in the same developer environment.
    cmd /c "`"$vsdev`" -arch=amd64 -host_arch=amd64 >nul 2>&1 && `"$exe`""

    if ($LASTEXITCODE -eq $skipExitCode) {
        $skippedSuites += $suite.Name
    } elseif ($LASTEXITCODE -ne 0) {
        $testExit = $LASTEXITCODE
    } else {
        $passedSuites += $suite.Name
    }
}

if (-not $SuiteNames.Count -or 'TestDngImageLoader' -in $SuiteNames) {
    $fixtureDirectory = Join-Path $repoRoot 'artifacts\public-fixtures'
    & (Join-Path $repoRoot 'tools\Generate-PublicFixtures.ps1') -OutputDirectory $fixtureDirectory
    $publicDngFixture = Join-Path $fixtureDirectory 'synthetic_64x48.dng'
    $tripletRoot = Join-Path $repoRoot "vcpkg_installed\x64-windows-static-md"
    $libRawHeader = Join-Path $tripletRoot "include\libraw\libraw.h"
    $libRawLibrary = Join-Path $tripletRoot "lib\raw_r.lib"

    if (-not (Test-Path -LiteralPath $libRawHeader) -or
        -not (Test-Path -LiteralPath $libRawLibrary)) {
        throw "DNG 测试依赖不存在，请先构建 YUVRaw.sln 的 x64 配置以自动恢复依赖"
    }

    $dngOutDir = Join-Path $outDir 'dng'
    New-Item -ItemType Directory -Force -Path $dngOutDir | Out-Null

    $dngExe = Join-Path $dngOutDir "TestDngImageLoader.exe"
    $dngSources = @(
        (Join-Path $PSScriptRoot "TestDngImageLoader.cpp")
        (Join-Path $srcDir "Image\FDngImageLoader.cpp")
        (Join-Path $srcDir "Image\FImageLoader.cpp")
        (Join-Path $srcDir "Image\FRawImageLoader.cpp")
        (Join-Path $srcDir "Image\FWicImageLoader.cpp")
        (Join-Path $srcDir "Image\FImageData.cpp")
        (Join-Path $srcDir "Image\FImageFormatDesc.cpp")
        (Join-Path $srcDir "Core\FLogger.cpp")
        (Join-Path $srcDir "Util.cpp")
    )
    $quotedDngSources = ($dngSources | ForEach-Object { "`"$_`"" }) -join " "
    $dngLibraries = "raw_r.lib lcms2.lib zs.lib jpeg.lib jasper.lib ws2_32.lib windowscodecs.lib ole32.lib"
    # 固定的 vcpkg LibRaw 未启用 STL ASan annotations，ODR 设置必须一致。
    # 只在 DNG 混合链接时关闭容器 annotations，保留本项目的地址插桩；RAW/WIC 保持完整 annotations。
    $dngExtraFlags = $extraFlags
    if ($EnableAddressSanitizer) { $dngExtraFlags += ' /D_DISABLE_STL_ANNOTATION' }
    $dngCompile =
        "`"$vsdev`" -arch=amd64 -host_arch=amd64 >nul 2>&1 && " +
        "cl /nologo /std:c++17 /EHsc /MD /utf-8 $dngExtraFlags /DLIBRAW_NODLL " +
        "/I `"$srcDir`" /I `"$tripletRoot\include`" " +
        "/Fe:`"$dngExe`" /Fo:`"$dngOutDir\\`" /Fd:`"$dngOutDir\TestDngImageLoader.pdb`" $quotedDngSources " +
        "/link /LIBPATH:`"$tripletRoot\lib`" $dngLibraries"

    cmd /c $dngCompile

    if ($LASTEXITCODE -ne 0) {
        throw "TestDngImageLoader 编译失败"
    }

    Write-Host ""
    cmd /c "`"$vsdev`" -arch=amd64 -host_arch=amd64 >nul 2>&1 && `"$dngExe`" `"$publicDngFixture`" --synthetic"

    if ($LASTEXITCODE -ne 0) {
        $testExit = $LASTEXITCODE
    } else {
        $passedSuites += 'TestDngImageLoader'
    }
    if (-not [string]::IsNullOrWhiteSpace($DngFixturePath)) {
        $resolvedDngFixture = Resolve-Path -LiteralPath $DngFixturePath -ErrorAction Stop
        cmd /c "`"$vsdev`" -arch=amd64 -host_arch=amd64 >nul 2>&1 && `"$dngExe`" `"$($resolvedDngFixture.Path)`""
        if ($LASTEXITCODE -ne 0) { $testExit = $LASTEXITCODE }
    }
}

Write-Host ""

if ($testExit -eq 0) {
    Write-Host "PASS: $($passedSuites.Count) suites; SKIP: $($skippedSuites.Count) suites ($($skippedSuites -join ', '))" -ForegroundColor Green
} else {
    Write-Host "测试失败" -ForegroundColor Red
}

exit $testExit
