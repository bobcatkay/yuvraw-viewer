#include "FRenderer.h"
#include "FHdrPresenter.h"
#include "FUiResources.h"
#include "UI/FUiScale.h"
#include "UI/FUiFont.h"
#include "UI/FUiLayout.h"
#include "UI/FUiTheme.h"
#include "Util.h"
// 必须在包含 ImGui OpenGL3 实现之前定义，以使用 GLAD 而不是默认加载器
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
// 先由 Windows SDK 定义 APIENTRY，避免 GLAD 与 glfw3native.h 重复定义。
#include <windows.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstdint>

namespace
{
    constexpr const char* kUiResourcesLogTag = "UiResources";
    constexpr float kUiFontSize = 18.0f;
    constexpr float kMinimumUiScale = 1.0f;
    constexpr float kMaximumUiScale = 4.0f;
    constexpr float kUiScaleChangeEpsilon = 0.01f;
}

FRenderer::FRenderer()
    : ImGuiContextPtr(nullptr)
    , NativeWindow(nullptr)
    , bIsInitialized(false)
    , CurrentUiScale(kMinimumUiScale)
{
}

FRenderer::~FRenderer()
{
    Shutdown();
}

bool FRenderer::Initialize(void* Window)
{
    if (bIsInitialized)
    {
        return true;
    }

    const auto initializationStart = std::chrono::steady_clock::now();
    NativeWindow = Window;

    // 加载OpenGL函数
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        LOGE("Initialize", "Failed to load OpenGL functions");
        NativeWindow = nullptr;
        return false;
    }

    // 初始化ImGui
    IMGUI_CHECKVERSION();
    ImGuiContextPtr = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // 必须早于首个 NewFrame 设置；否则 ImGui 会从启动目录读取个人布局。
    io.IniFilename = nullptr;
    const std::filesystem::path layoutPath = FUiResources::GetLayoutSettingsPath();
    std::error_code layoutError;
    if (!layoutPath.empty())
    {
        std::filesystem::create_directories(layoutPath.parent_path(), layoutError);
        if (!layoutError)
        {
            ImGuiIniPath = layoutPath.u8string();
            io.IniFilename = ImGuiIniPath.c_str();
        }
    }
    if (!io.IniFilename)
    {
        LOGW(kUiResourcesLogTag,
            "UI layout persistence is unavailable (user directory error=%d)", layoutError.value());
    }

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // 这是桌面图像工具，没有手柄交互。启用该标志会让 GLFW 在首帧同步初始化
    // Windows 游戏控制器后端；某些机器的失效/休眠 HID 设备会把 UI 线程卡住十几秒。
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    if (io.IniFilename)
    {
        ImGui::LoadIniSettingsFromDisk(io.IniFilename);
        const size_t migratedWindows = FUiLayout::MigrateLegacyWindowSettings();
        if (migratedWindows > 0)
        {
            LOGI(kUiResourcesLogTag, "Migrated %llu window layouts to stable language IDs",
                static_cast<unsigned long long>(migratedWindows));
        }
    }

    InitializeImGuiStyle();

    // 设置平台和渲染后端
    if (!ImGui_ImplGlfw_InitForOpenGL((GLFWwindow*)Window, true))
    {
        LOGE("Initialize", "Failed to initialize the ImGui GLFW backend");
        Shutdown();
        return false;
    }

    // 与 FWindow 请求的 3.3 core 上下文保持一致
    const char* glsl_version = "#version 330";
    if (!ImGui_ImplOpenGL3_Init(glsl_version))
    {
        LOGE("Initialize", "Failed to initialize the ImGui OpenGL backend");
        Shutdown();
        return false;
    }

    const auto fontStart = std::chrono::steady_clock::now();
    CurrentUiScale = QueryWindowContentScale();
    BaseImGuiStyle = ImGui::GetStyle();
    ApplyUiScale(CurrentUiScale);
    FUiTheme::ReloadPalette();

    if (!RebuildUiFont(CurrentUiScale))
    {
        LOGE("Initialize", "Failed to create the ImGui font texture");
        Shutdown();
        return false;
    }

    const auto fontMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - fontStart).count();

    // HDR 呈现层。初始化失败是常态（驱动不支持 interop、屏幕不是 HDR、
    // 甚至只是显卡不对），失败后一切照旧走 SDR，所以这里不检查返回值
    const auto hdrStart = std::chrono::steady_clock::now();
    HdrPresenter = std::make_unique<FHdrPresenter>();
    HdrPresenter->Initialize(glfwGetWin32Window((GLFWwindow*)Window));
    const auto hdrMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - hdrStart).count();

    bIsInitialized = true;

    const auto totalMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - initializationStart).count();
    LOGI(
        "Initialize",
        "Renderer initialized in %lld ms (UI scale: %.2f, font atlas: %lld ms, HDR: %lld ms)",
        static_cast<long long>(totalMilliseconds),
        CurrentUiScale,
        static_cast<long long>(fontMilliseconds),
        static_cast<long long>(hdrMilliseconds));

    return true;
}

void FRenderer::BeginFrame()
{
    if (!bIsInitialized)
    {
        return;
    }

    // GLFW 在 Windows 上提供主窗口当前显示器的内容缩放。跨屏或系统缩放改变后，
    // 在 NewFrame 之前重建字体图集，避免本帧仍引用已经释放的字体或纹理。
    UpdateUiScale();
    // ApplyUiScale 会从未缩放基准样式重置颜色；主题必须随后重新覆盖，且这里不做磁盘 I/O。
    FUiTheme::ApplyToImGuiStyle();

    // 窗口可能被拖到了另一块屏上，HDR 状态与 SDR 白电平都要跟着变。
    // 放在出帧最前面：本帧的色彩管线要用到刚查到的值
    if (HdrPresenter)
    {
        HdrPresenter->Update();
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

bool FRenderer::EndFrame()
{
    if (!bIsInitialized)
    {
        return true;
    }

    ImGui::Render();

    int32_t displayWidth, displayHeight;
    glfwGetFramebufferSize((GLFWwindow*)NativeWindow, &displayWidth, &displayHeight);

    // 最小化可能发生在 BeginFrame 与这里之间。0x0 是窗口生命周期中的临时状态，
    // 不能交给 HDR 资源重建，也不能在 DXGI 已接管 HWND 后退回 WGL SwapBuffers。
    if (displayWidth <= 0 || displayHeight <= 0)
    {
        return false;
    }

    // HDR 生效时整帧画进 fp16 帧缓冲；否则照旧画默认帧缓冲
    const bool bHdrFrame = HdrPresenter && HdrPresenter->BeginFrame(displayWidth, displayHeight);

    if (!bHdrFrame)
    {
        glViewport(0, 0, displayWidth, displayHeight);
    }

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        // 副视口有各自的 GL 上下文与窗口，仍然走 SDR —— 帧缓冲绑定是按上下文分开的，
        // 主上下文这边的 fp16 FBO 不受影响
        GLFWwindow* backup_current_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();

        // 没有副视口时上面两步不会切换 Context，这里的恢复就是多余的。但 GLFW 的 WGL
        // 后端不做“已经是当前”短路，照样再执行一次 wglMakeCurrent；与后台上传 Context
        // 同处一个 share group 时，Intel 驱动有机会在 ICD 内自旋不返回，UI 线程彻底停摆。
        if (glfwGetCurrentContext() != backup_current_context)
        {
            glfwMakeContextCurrent(backup_current_context);
        }
    }

    if (bHdrFrame && HdrPresenter->EndFrame())
    {
        // 呈现已由 D3D11 交换链完成，不能再 SwapBuffers
        return false;
    }

    return true;
}

void FRenderer::Shutdown()
{
    // 初始化可能在后端或字体阶段失败，不能仅凭最终成功标志决定是否释放资源。
    // 呈现层持有 GL 资源，必须在 GL 上下文还活着时先放掉。
    HdrPresenter.reset();

    if (ImGuiContextPtr)
    {
        ImGui::SetCurrentContext(ImGuiContextPtr);
        ImGuiIO& io = ImGui::GetIO();

        // 后端以各自的 UserData 表示已初始化；对尚未建立的后端调用 Shutdown 会断言。
        if (io.BackendRendererUserData)
        {
            ImGui_ImplOpenGL3_Shutdown();
        }
        if (io.BackendPlatformUserData)
        {
            ImGui_ImplGlfw_Shutdown();
        }

        ImGui::DestroyContext(ImGuiContextPtr);
        ImGuiContextPtr = nullptr;
    }

    // io.IniFilename 借用此字符串，必须等 ImGui 上下文销毁后才能清空。
    ImGuiIniPath.clear();
    NativeWindow = nullptr;
    FUiScale::Set(kMinimumUiScale);
    CurrentUiScale = kMinimumUiScale;
    bIsInitialized = false;
}

ImGuiContext* FRenderer::GetImGuiContext() const
{
    return ImGuiContextPtr;
}

void FRenderer::InitializeImGuiStyle()
{
    ImGui::StyleColorsLight();

    ImGuiStyle& style = ImGui::GetStyle();

    // 这里只构造 DPI 缩放所需的默认基准样式；可编辑颜色随后由 FUiTheme 覆盖。
    ImGuiIO& io = ImGui::GetIO();

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        // === 浅色主题色彩调优（Tailwind Slate 风格）=========================
        ImVec4* c = style.Colors;

        c[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);

        // 按钮三态
        c[ImGuiCol_Button]        = ImVec4(0.165f, 0.631f, 0.596f, 1.00f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.118f, 0.490f, 0.467f, 1.00f);
        c[ImGuiCol_ButtonActive]  = ImVec4(0.078f, 0.349f, 0.337f, 1.00f);

        // 输入框 / 下拉 / 复选框底：深于面板，便于与 WindowBg 区分
        c[ImGuiCol_FrameBg]        = ImVec4(0.848f, 0.868f, 0.895f, 1.00f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.780f, 0.812f, 0.855f, 1.00f);
        c[ImGuiCol_FrameBgActive]  = ImVec4(0.695f, 0.745f, 0.815f, 1.00f);

        c[ImGuiCol_Header]        = ImVec4(0.886f, 0.910f, 0.949f, 1.00f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.800f, 0.847f, 0.918f, 1.00f);
        c[ImGuiCol_HeaderActive]  = ImVec4(0.282f, 0.388f, 0.553f, 1.00f);

        // === Tab 颜色统一为按钮主题色 (深青)，避免失焦时背景太淡 ==========
        // 颜色梯度: 主色 #1E8C94 (选中) <- 副色 #18717D (悬停/未选中/失焦) <- 暗色 #105764
        const ImVec4 TealPrimary  = ImVec4(0.118f, 0.553f, 0.580f, 1.00f); // #1E8C94
        const ImVec4 TealHover    = ImVec4(0.094f, 0.443f, 0.490f, 1.00f); // #18717D
        const ImVec4 TealDeep     = ImVec4(0.063f, 0.341f, 0.392f, 1.00f); // #105764

        c[ImGuiCol_Tab]                = TealHover;    // tab-bar 聚焦时, 未被选中的 tab
        c[ImGuiCol_TabHovered]         = TealHover;    // 鼠标悬停
        c[ImGuiCol_TabActive]          = TealPrimary;  // tab-bar 聚焦时, 被选中的 tab (= TabSelected)
        c[ImGuiCol_TabUnfocused]       = TealHover;    // tab-bar 失焦时, 未被选中的 tab (= TabDimmed)
        c[ImGuiCol_TabUnfocusedActive] = TealPrimary;  // tab-bar 失焦时, 被选中的 tab (= TabDimmedSelected)

        // 边框收一点饱和度，跟按钮的蓝灰呼应
        c[ImGuiCol_Border]       = ImVec4(0.820f, 0.847f, 0.886f, 1.00f);
        c[ImGuiCol_BorderShadow] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);

        c[ImGuiCol_CheckMark]        = TealPrimary;
        c[ImGuiCol_SliderGrab]       = TealPrimary;
        c[ImGuiCol_SliderGrabActive] = TealDeep;
        c[ImGuiCol_HeaderHovered]    = ImVec4(0.094f, 0.443f, 0.490f, 0.45f);
        c[ImGuiCol_HeaderActive]     = ImVec4(0.118f, 0.553f, 0.580f, 0.65f);

        // 圆角更柔和
        style.FrameRounding  = 4.0f;
        style.GrabRounding   = 4.0f;
        style.WindowRounding = 6.0f;
        style.PopupRounding  = 4.0f;
    }
}

void FRenderer::ApplyUiScale(float Scale)
{
    // 每次都从未缩放的基准样式重算，避免窗口在不同 DPI 显示器之间往返时累积浮点误差。
    ImGuiStyle& style = ImGui::GetStyle();
    style = BaseImGuiStyle;
    style.ScaleAllSizes(Scale);
    FUiScale::Set(Scale);
}

bool FRenderer::RebuildUiFont(float Scale)
{
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.FontDefault = nullptr;
    io.Fonts->Clear();

    // 完整中文字形集会同步栅格化两万多个字形。常用集覆盖 UI 与绝大多数文件名，
    // 再按当前 DPI 直接栅格化目标字号，既保证清晰度，也控制跨屏重建耗时。
    ImVector<ImWchar> uiGlyphRanges;
    FUiFont::BuildGlyphRanges(*io.Fonts, uiGlyphRanges);

    const float fontPixelSize = std::round(kUiFontSize * Scale);

    const std::filesystem::path fontCandidates[] = {
        FUiResources::GetSystemChineseFontPath(),
        FUiResources::GetBundledFontPath(FUiResources::GetExecutableDirectory())
    };
    for (const std::filesystem::path& fontPath : fontCandidates)
    {
        std::error_code error;
        if (fontPath.empty() || !std::filesystem::is_regular_file(fontPath, error))
        {
            continue;
        }
        io.FontDefault = io.Fonts->AddFontFromFileTTF(
            fontPath.u8string().c_str(),
            fontPixelSize,
            nullptr,
            uiGlyphRanges.Data);
        if (io.FontDefault)
        {
            LOGI(kUiResourcesLogTag, "UI font selected: %s",
                fontPath.filename().u8string().c_str());
            break;
        }
    }

    if (!io.FontDefault)
    {
        ImFontConfig fallbackConfig;
        fallbackConfig.SizePixels = fontPixelSize;
        io.FontDefault = io.Fonts->AddFontDefault(&fallbackConfig);
        LOGE(kUiResourcesLogTag,
            "System and bundled Chinese fonts are unavailable; verify resources/fonts in the ZIP");
    }

    return io.FontDefault && ImGui_ImplOpenGL3_CreateFontsTexture();
}

float FRenderer::QueryWindowContentScale() const
{
    float xScale = kMinimumUiScale;
    float yScale = kMinimumUiScale;

    if (NativeWindow)
    {
        glfwGetWindowContentScale(
            static_cast<GLFWwindow*>(NativeWindow),
            &xScale,
            &yScale);
    }

    if (!std::isfinite(xScale) || xScale <= 0.0f)
    {
        xScale = kMinimumUiScale;
    }

    if (!std::isfinite(yScale) || yScale <= 0.0f)
    {
        yScale = kMinimumUiScale;
    }

    return std::clamp(
        (std::max)(xScale, yScale),
        kMinimumUiScale,
        kMaximumUiScale);
}

void FRenderer::UpdateUiScale()
{
    const float requestedScale = QueryWindowContentScale();

    if (std::fabs(requestedScale - CurrentUiScale) < kUiScaleChangeEpsilon)
    {
        return;
    }

    const auto rebuildStart = std::chrono::steady_clock::now();
    const float previousScale = CurrentUiScale;

    if (!RebuildUiFont(requestedScale))
    {
        LOGE(
            "DPI",
            "Failed to rebuild UI font atlas for scale %.2f",
            requestedScale);
        return;
    }

    ApplyUiScale(requestedScale);
    CurrentUiScale = requestedScale;

    const auto rebuildMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - rebuildStart).count();
    LOGI(
        "DPI",
        "UI scale changed from %.2f to %.2f; font atlas rebuilt in %lld ms",
        previousScale,
        CurrentUiScale,
        static_cast<long long>(rebuildMilliseconds));
}
