#include "FApplication.h"
#include "FWindow.h"
#include "FHdrPresenter.h"
#include "FRenderer.h"
#include "FMainDockSpace.h"
#include "FCommandLine.h"
#include "FUserSettings.h"
#include "Image/FImageLoadParams.h"
#include "Image/FImageLoader.h"
#include "gl/FShaderManager.h"
#include "Util.h"
#include <chrono>
#include <string>
#include <vector>

namespace
{
    constexpr int32_t kDefaultWindowWidth = 1920;
    constexpr int32_t kDefaultWindowHeight = 1080;
    constexpr const char* kApplicationWindowTitle = "YUVRaw";

    /// 最小化期间只处理窗口消息，低频唤醒以兼顾响应速度与 CPU 占用。
    constexpr double kSuspendedEventWaitSeconds = 0.1;
}

FApplication::FApplication()
    : bIsInitialized(false)
{
}

FApplication::~FApplication()
{
    Shutdown();
}

bool FApplication::Initialize(const char* CommandLine)
{
    if (bIsInitialized)
    {
        return true;
    }

    const auto initializationStart = std::chrono::steady_clock::now();

    // 创建窗口
    Window = std::make_unique<FWindow>();

    FUserSettings::FWindowPlacement savedPlacement;
    FWindow::FPlacement initialPlacement;
    const bool bHasSavedPlacement =
        FUserSettings::TryGetWindowPlacement(savedPlacement);

    if (bHasSavedPlacement)
    {
        initialPlacement.X = savedPlacement.X;
        initialPlacement.Y = savedPlacement.Y;
        initialPlacement.Width = savedPlacement.Width;
        initialPlacement.Height = savedPlacement.Height;
        initialPlacement.bMaximized = savedPlacement.bMaximized;
    }

    if (!Window->Create(
            kDefaultWindowWidth,
            kDefaultWindowHeight,
            kApplicationWindowTitle,
            bHasSavedPlacement ? &initialPlacement : nullptr))
    {
        LOGE("Initialize", "Failed to create window");

        return false;
    }

    // 设置文件拖放回调
    Window->SetDropCallback([this](int Count, const char** Paths) {
        HandleFileDrop(Count, Paths);
    });

    // 初始化图像加载器
    FImageLoaderFactory::InitializeDefaultLoaders();

    // 创建渲染器
    Renderer = std::make_unique<FRenderer>();

    if (!Renderer->Initialize(Window->GetNativeWindow()))
    {
        LOGE("Initialize", "Failed to initialize renderer");

        return false;
    }

    // 创建主DockSpace
    MainDockSpace = std::make_unique<FMainDockSpace>();
    MainDockSpace->Initialize(Window->GetNativeWindow());

    bIsInitialized = true;

    const auto coreInitializationMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - initializationStart).count();
    LOGI(
        "Initialize",
        "Core UI initialized in %lld ms; image shaders will compile on first use",
        static_cast<long long>(coreInitializationMilliseconds));

    // 命令行指定了文件就直接打开：未显式给出的参数由文件名解析补齐
    const FCommandLineOptions options = FCommandLineOptions::Parse(CommandLine);

    if (options.bDisableHdr)
    {
        if (FHdrPresenter* presenter = Renderer->GetHdrPresenter())
        {
            presenter->SetUserEnabled(false);
        }
    }

    if (!options.PathToOpen.empty())
    {
        // 一个参数都没给（"用 YUVRaw 打开"就是这样）时走与拖放完全相同的路径，
        // 否则同一个文件"双击打开"和"拖进窗口"会得到不同的分辨率 ——
        // Open() 里那道"文件名分辨率对不上文件大小就换候选"的校验只在那条路径上。
        // 反之，用户既然显式写了 --width/--stride，就不该被二次猜测。
        const bool bHasExplicitParams = options.bHasFormat || options.bHasWidth
                                     || options.bHasHeight || options.bHasStride
                                     || options.bHasBitsPerPixel;

        if (!bHasExplicitParams)
        {
            LOGD("Initialize", "Opening from command line: %s (no explicit params)", options.PathToOpen.c_str());

            MainDockSpace->OpenPath(options.PathToOpen);

            if (!options.ComparePath.empty())
            {
                LOGD("Initialize", "Opening compare image: %s", options.ComparePath.c_str());

                MainDockSpace->OpenComparePath(options.ComparePath);
            }

            // 色彩解读要在图打开之后覆盖：Open() 里会把显示设置重置成默认的
            if (options.bHasDisplay)
            {
                MainDockSpace->ApplyDisplaySettings(options.Display);
            }

            return true;
        }

        FImageLoadParams params;

        int32_t parsedWidth = 0;
        int32_t parsedHeight = 0;
        const EImageFormat parsedFormat = ParseImageInfoFromFilename(options.PathToOpen, parsedWidth, parsedHeight);

        if (parsedWidth > 0 && parsedHeight > 0)
        {
            params.Width = parsedWidth;
            params.Height = parsedHeight;
        }

        if (parsedFormat != EImageFormat::Unknown)
        {
            params.SetDetectedFormat(parsedFormat);
        }

        // 命令行显式给出的项覆盖文件名解析的结果
        options.ApplyTo(params);

        LOGD("Initialize", "Opening from command line: %s (format=%d %dx%d stride=%d)",
             options.PathToOpen.c_str(), static_cast<int>(params.Format),
             params.Width, params.Height, params.Stride);

        MainDockSpace->OpenPathWithParams(options.PathToOpen, params);

        if (!options.ComparePath.empty())
        {
            LOGD("Initialize", "Opening compare image: %s", options.ComparePath.c_str());

            MainDockSpace->OpenComparePath(options.ComparePath);
        }

        if (options.bHasDisplay)
        {
            MainDockSpace->ApplyDisplaySettings(options.Display);
        }
    }

    return true;
}

int FApplication::Run()
{

    if (!bIsInitialized)
    {

        if (!Initialize())
        {
            return 1;
        }
    }

    bool bRenderingSuspended = false;
    bool bFirstRenderableFrame = true;
    std::chrono::steady_clock::time_point firstRenderableFrameStart;

    // 主循环
    while (!Window->ShouldClose() && !MainDockSpace->WantsToClose())
    {
        if (bFirstRenderableFrame)
        {
            firstRenderableFrameStart = std::chrono::steady_clock::now();
        }

        Window->PollEvents();

        // Windows 最小化窗口时帧缓冲会暂时变成 0x0。此时继续创建 HDR 资源或
        // SwapBuffers 会让同一 HWND 上的 DXGI/WGL 呈现状态失配，恢复后可能卡住。
        // 暂停渲染但持续处理消息，弹窗与窗口状态会在恢复后的第一帧自然续上。
        if (!Window->CanRender())
        {
            if (!bRenderingSuspended)
            {
                LOGD("Run", "Rendering suspended while the main window is minimized");
                bRenderingSuspended = true;
            }

            Window->WaitEvents(kSuspendedEventWaitSeconds);
            continue;
        }

        if (bRenderingSuspended)
        {
            LOGD("Run", "Rendering resumed after the main window was restored");
            bRenderingSuspended = false;
        }

        Renderer->BeginFrame();
        MainDockSpace->Render();

        // HDR 通路生效时呈现由 D3D11 交换链接管，这里就不能再 SwapBuffers
        const bool bNeedsSwapBuffers = Renderer->EndFrame();

        if (bNeedsSwapBuffers)
        {
            Window->SwapBuffers();
        }

        if (bFirstRenderableFrame)
        {
            const auto frameMilliseconds =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - firstRenderableFrameStart).count();
            LOGI(
                "Run",
                "First renderable frame presented in %lld ms",
                static_cast<long long>(frameMilliseconds));
            bFirstRenderableFrame = false;
        }
    }

    return 0;
}

void FApplication::Shutdown()
{
    const bool bHasResources = MainDockSpace || Renderer || Window;

    if (!bHasResources)
    {
        bIsInitialized = false;
        return;
    }

    const auto shutdownStart = std::chrono::steady_clock::now();
    auto logStage = [&shutdownStart](const char* Stage) {
        const auto elapsedMilliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - shutdownStart).count();
        LOGI(
            "Shutdown",
            "%s completed at %lld ms",
            Stage,
            static_cast<long long>(elapsedMilliseconds));
    };

    LOGI("Shutdown", "Application shutdown started");

    if (Window && !FUserSettings::WasAllDataClearedThisSession())
    {
        FWindow::FPlacement placement;

        if (Window->GetPlacement(placement))
        {
            FUserSettings::FWindowPlacement savedPlacement;
            savedPlacement.X = placement.X;
            savedPlacement.Y = placement.Y;
            savedPlacement.Width = placement.Width;
            savedPlacement.Height = placement.Height;
            savedPlacement.bMaximized = placement.bMaximized;
            FUserSettings::SetWindowPlacement(savedPlacement);
            LOGI(
                "Shutdown",
                "Window placement saved: x=%d y=%d width=%d height=%d maximized=%d",
                savedPlacement.X,
                savedPlacement.Y,
                savedPlacement.Width,
                savedPlacement.Height,
                savedPlacement.bMaximized ? 1 : 0);
        }
    }
    else if (Window)
    {
        // “清除全部数据”后不在同一次退出中重新生成 settings.ini。
        LOGI("Shutdown", "Window placement persistence skipped after clearing all data");
    }

    MainDockSpace.reset();
    logStage("Dock space and documents");

    // 全局着色器缓存持有 GL 对象；必须在渲染器和窗口销毁 OpenGL 上下文前清空。
    FShaderManager::Get().Shutdown();
    logStage("Image shaders");

    // 文档归档会查询加载器判断文件是否自描述，因此只能在 DockSpace 销毁后清空。
    FImageLoaderFactory::Shutdown();
    logStage("Image loaders");

    if (Renderer)
    {
        Renderer->Shutdown();
        Renderer.reset();
    }
    logStage("Renderer");

    if (Window)
    {
        Window->Destroy();
        Window.reset();
    }
    logStage("Window");

    bIsInitialized = false;
}

FApplication& FApplication::Get()
{
    // 函数内静态对象：程序退出时自动析构，不泄漏。
    static FApplication ApplicationInstance;

    return ApplicationInstance;
}

void FApplication::HandleFileDrop(int Count, const char** Paths)
{
    if (Count <= 0 || !Paths || !MainDockSpace || !Window)
    {
        LOGE("HandleFileDrop", "Invalid parameters, Count: %d", Count);

        return;
    }

    std::vector<std::string> paths;
    paths.reserve(static_cast<size_t>(Count));

    for (int32_t i = 0; i < Count; ++i)
    {
        if (Paths[i])
        {
            paths.emplace_back(Paths[i]);
        }
    }

    // 松手的位置决定拖到了哪个面板上，交给 DockSpace 去派发
    double cursorX = 0.0;
    double cursorY = 0.0;
    Window->GetCursorPos(cursorX, cursorY);

    MainDockSpace->HandleDroppedPaths(paths, static_cast<float>(cursorX), static_cast<float>(cursorY));
}
