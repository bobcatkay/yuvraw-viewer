#include "FWindow.h"
#include "Util.h"

#include <cstdint>
#include <iostream>

FWindow* FWindow::Instance = nullptr;

FWindow::FWindow()
    : NativeWindow(nullptr)
    , WindowWidth(0)
    , WindowHeight(0)
    , bHasRestoredPlacement(false)
{
    Instance = this;
}

FWindow::~FWindow()
{
    Destroy();
}

bool FWindow::Create(
    int32_t Width,
    int32_t Height,
    const std::string& Title,
    const FPlacement* SavedPlacement)
{
    WindowTitle = Title;

    glfwSetErrorCallback(GLFWErrorCallback);


    if (!glfwInit())
    {
        LOGE("Create", "Failed to initialize GLFW");

        return false;
    }

    // 设置OpenGL版本
#if defined(IMGUI_IMPL_OPENGL_ES3)
    const char* glsl_version = "#version 300 es";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    // 请求 3.3 core 而不是 4.5：本工具用到的功能（texelFetch / mat3 uniform /
    // layout(location) / 归一化 16bit 纹理）3.3 全都有，而 4.5 会把老 Intel 核显挡在门外。
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif

    const bool bHasSavedPlacement =
        SavedPlacement && SavedPlacement->Width > 0 && SavedPlacement->Height > 0;
    // 创建窗口前还取不到真实边框尺寸，先做一次无边框的粗约束，避免损坏配置
    // 要求创建极大的客户区。隐藏窗口移动到目标显示器后再按当地 DPI 精确约束。
    const FPlacement coarsePlacement = bHasSavedPlacement
        ? ConstrainPlacementToVisibleWorkArea(
            *SavedPlacement,
            FWindowGeometry::FFrameSize{})
        : FPlacement{};
    const int32_t initialWidth = bHasSavedPlacement
        ? coarsePlacement.Width
        : Width;
    const int32_t initialHeight = bHasSavedPlacement
        ? coarsePlacement.Height
        : Height;

    WindowWidth = initialWidth;
    WindowHeight = initialHeight;

    // 先在不可见状态应用恢复边界，避免窗口以默认位置闪现一帧后再跳动。
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_FALSE);

    NativeWindow = glfwCreateWindow(
        initialWidth,
        initialHeight,
        Title.c_str(),
        nullptr,
        nullptr);

    // 窗口提示是进程级状态，创建完主窗口后恢复常规默认值，避免影响后续副视口。
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_FALSE);

    if (!NativeWindow)
    {
        LOGE("Create", "Failed to create GLFW window");

        glfwTerminate();
        return false;
    }

    if (bHasSavedPlacement)
    {
        // 先移到保存位置所在的显示器，使下面取得的标题栏/边框尺寸匹配目标 DPI。
        glfwSetWindowPos(
            NativeWindow,
            coarsePlacement.X,
            coarsePlacement.Y);
    }

    FPlacement requestedPlacement;

    if (bHasSavedPlacement)
    {
        requestedPlacement = *SavedPlacement;
    }
    else
    {
        glfwGetWindowPos(
            NativeWindow,
            &requestedPlacement.X,
            &requestedPlacement.Y);
        glfwGetWindowSize(
            NativeWindow,
            &requestedPlacement.Width,
            &requestedPlacement.Height);
    }

    FWindowGeometry::FFrameSize frame;
    FPlacement restoredPlacement = requestedPlacement;

    // 第一次按粗定位后的显示器 DPI 计算；应用位置后再取一次 frame，覆盖窗口
    // 恰好跨在不同 DPI 显示器边界、精确约束后目标显示器发生变化的情况。
    for (int32_t placementAttempt = 0; placementAttempt < 2; ++placementAttempt)
    {
        glfwGetWindowFrameSize(
            NativeWindow,
            &frame.Left,
            &frame.Top,
            &frame.Right,
            &frame.Bottom);
        restoredPlacement =
            ConstrainPlacementToVisibleWorkArea(requestedPlacement, frame);

        int32_t currentX = 0;
        int32_t currentY = 0;
        int32_t currentWidth = 0;
        int32_t currentHeight = 0;
        glfwGetWindowPos(NativeWindow, &currentX, &currentY);
        glfwGetWindowSize(NativeWindow, &currentWidth, &currentHeight);

        if (restoredPlacement.Width != currentWidth ||
            restoredPlacement.Height != currentHeight)
        {
            glfwSetWindowSize(
                NativeWindow,
                restoredPlacement.Width,
                restoredPlacement.Height);
        }

        if (restoredPlacement.X != currentX ||
            restoredPlacement.Y != currentY)
        {
            glfwSetWindowPos(
                NativeWindow,
                restoredPlacement.X,
                restoredPlacement.Y);
        }
    }

    RestoredPlacement = restoredPlacement;
    WindowWidth = restoredPlacement.Width;
    WindowHeight = restoredPlacement.Height;
    bHasRestoredPlacement = true;
    LOGI(
        "Create",
        "%s window placement: x=%d y=%d width=%d height=%d maximized=%d frame=%d,%d,%d,%d",
        bHasSavedPlacement ? "Restored" : "Initial",
        RestoredPlacement.X,
        RestoredPlacement.Y,
        RestoredPlacement.Width,
        RestoredPlacement.Height,
        RestoredPlacement.bMaximized ? 1 : 0,
        frame.Left,
        frame.Top,
        frame.Right,
        frame.Bottom);

    glfwSetWindowPosCallback(NativeWindow, GLFWWindowPositionCallback);
    glfwSetWindowSizeCallback(NativeWindow, GLFWWindowSizeCallback);
    glfwSetWindowMaximizeCallback(NativeWindow, GLFWWindowMaximizeCallback);

    glfwMakeContextCurrent(NativeWindow);
    glfwSwapInterval(1); // 启用垂直同步

    // 设置拖放回调
    glfwSetDropCallback(NativeWindow, GLFWDropCallback);

    if (bHasSavedPlacement && restoredPlacement.bMaximized)
    {
        // 先建立普通窗口恢复边界，再最大化；否则取消最大化时会退回系统默认尺寸。
        glfwMaximizeWindow(NativeWindow);
    }

    glfwShowWindow(NativeWindow);

    if (bHasSavedPlacement && restoredPlacement.bMaximized &&
        glfwGetWindowAttrib(NativeWindow, GLFW_MAXIMIZED) != GLFW_TRUE)
    {
        // 少数窗口管理器会忽略隐藏窗口的最大化请求，显示后补一次保证状态恢复。
        glfwMaximizeWindow(NativeWindow);
    }

    CaptureRestoredPlacement();

    return true;
}

void FWindow::Destroy()
{
    if (NativeWindow)
    {
        glfwDestroyWindow(NativeWindow);
        NativeWindow = nullptr;
    }
    bHasRestoredPlacement = false;
    glfwTerminate();
}

bool FWindow::ShouldClose() const
{
    return NativeWindow ? glfwWindowShouldClose(NativeWindow) : true;
}

void FWindow::SwapBuffers()
{
    if (NativeWindow)
    {
        glfwSwapBuffers(NativeWindow);
    }
}

void FWindow::PollEvents()
{
    glfwPollEvents();
}

bool FWindow::CanRender() const
{
    if (!NativeWindow ||
        glfwGetWindowAttrib(NativeWindow, GLFW_ICONIFIED) == GLFW_TRUE)
    {
        return false;
    }

    int32_t framebufferWidth = 0;
    int32_t framebufferHeight = 0;
    glfwGetFramebufferSize(
        NativeWindow,
        &framebufferWidth,
        &framebufferHeight);

    return framebufferWidth > 0 && framebufferHeight > 0;
}

void FWindow::WaitEvents(double TimeoutSeconds)
{
    glfwWaitEventsTimeout(TimeoutSeconds);
}

void FWindow::GetFramebufferSize(int32_t& OutWidth, int32_t& OutHeight) const
{

    if (NativeWindow)
    {
        glfwGetFramebufferSize(NativeWindow, &OutWidth, &OutHeight);
    }
    else
    {
        OutWidth = 0;
        OutHeight = 0;
    }
}

bool FWindow::GetPlacement(FPlacement& OutPlacement)
{
    if (!NativeWindow)
    {
        return false;
    }

    CaptureRestoredPlacement();

    if (!bHasRestoredPlacement)
    {
        return false;
    }

    OutPlacement = RestoredPlacement;
    OutPlacement.bMaximized =
        glfwGetWindowAttrib(NativeWindow, GLFW_MAXIMIZED) == GLFW_TRUE;
    return true;
}

void FWindow::GetCursorPos(double& OutX, double& OutY) const
{
    OutX = 0.0;
    OutY = 0.0;

    if (NativeWindow)
    {
        glfwGetCursorPos(NativeWindow, &OutX, &OutY);
    }
}

void FWindow::SetDropCallback(DropCallback Callback)
{
    DropCallbackFunc = Callback;
}

FWindow* FWindow::Get()
{
    return Instance;
}

void FWindow::GLFWErrorCallback(int Error, const char* Description)
{
    LOGE("GLFWErrorCallback", "GLFW Error, Error: %d, Description: %s", Error, Description);
}

void FWindow::GLFWDropCallback(GLFWwindow* Window, int Count, const char** Paths)
{

    if (Instance && Instance->DropCallbackFunc)
    {
        Instance->DropCallbackFunc(Count, Paths);
    }
}

void FWindow::GLFWWindowPositionCallback(GLFWwindow* Window, int X, int Y)
{
    (void)X;
    (void)Y;

    if (Instance && Instance->NativeWindow == Window)
    {
        Instance->CaptureRestoredPlacement();
    }
}

void FWindow::GLFWWindowSizeCallback(GLFWwindow* Window, int Width, int Height)
{
    (void)Width;
    (void)Height;

    if (Instance && Instance->NativeWindow == Window)
    {
        Instance->CaptureRestoredPlacement();
    }
}

void FWindow::GLFWWindowMaximizeCallback(GLFWwindow* Window, int Maximized)
{
    if (Instance && Instance->NativeWindow == Window && Maximized == GLFW_FALSE)
    {
        // 从最大化恢复后，系统已经重新应用普通窗口边界，此时再刷新快照。
        Instance->CaptureRestoredPlacement();
    }
}

void FWindow::CaptureRestoredPlacement()
{
    if (!NativeWindow ||
        glfwGetWindowAttrib(NativeWindow, GLFW_ICONIFIED) == GLFW_TRUE ||
        glfwGetWindowAttrib(NativeWindow, GLFW_MAXIMIZED) == GLFW_TRUE)
    {
        return;
    }

    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    glfwGetWindowPos(NativeWindow, &x, &y);
    glfwGetWindowSize(NativeWindow, &width, &height);

    if (width <= 0 || height <= 0)
    {
        return;
    }

    RestoredPlacement.X = x;
    RestoredPlacement.Y = y;
    RestoredPlacement.Width = width;
    RestoredPlacement.Height = height;
    RestoredPlacement.bMaximized = false;
    WindowWidth = width;
    WindowHeight = height;
    bHasRestoredPlacement = true;
}

FWindow::FPlacement FWindow::ConstrainPlacementToVisibleWorkArea(
    const FPlacement& Placement,
    const FWindowGeometry::FFrameSize& Frame)
{
    int32_t monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);

    if (!monitors || monitorCount <= 0)
    {
        return FWindowGeometry::ConstrainToWorkArea(
            Placement,
            Frame,
            FWindowGeometry::FWorkArea{});
    }

    GLFWmonitor* selectedMonitor = glfwGetPrimaryMonitor();
    int64_t largestIntersectionArea = 0;

    for (int32_t monitorIndex = 0;
         monitorIndex < monitorCount;
         ++monitorIndex)
    {
        int32_t workX = 0;
        int32_t workY = 0;
        int32_t workWidth = 0;
        int32_t workHeight = 0;
        glfwGetMonitorWorkarea(
            monitors[monitorIndex],
            &workX,
            &workY,
            &workWidth,
            &workHeight);

        if (workWidth <= 0 || workHeight <= 0)
        {
            continue;
        }

        const int64_t intersectionArea =
            FWindowGeometry::CalculateIntersectionArea(
                Placement,
                Frame,
                {workX, workY, workWidth, workHeight});

        if (intersectionArea > largestIntersectionArea)
        {
            largestIntersectionArea = intersectionArea;
            selectedMonitor = monitors[monitorIndex];
        }
    }

    if (!selectedMonitor)
    {
        selectedMonitor = monitors[0];
    }

    int32_t workX = 0;
    int32_t workY = 0;
    int32_t workWidth = 0;
    int32_t workHeight = 0;
    glfwGetMonitorWorkarea(
        selectedMonitor,
        &workX,
        &workY,
        &workWidth,
        &workHeight);

    return FWindowGeometry::ConstrainToWorkArea(
        Placement,
        Frame,
        {workX, workY, workWidth, workHeight});
}
