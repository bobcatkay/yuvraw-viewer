#pragma once

#include "FWindowPlacement.h"

#include <GLFW/glfw3.h>
#include <string>
#include <functional>
#include <cstdint>

struct GLFWwindow;

/**
 * 窗口管理类
 * 负责窗口的创建、事件处理和生命周期管理
 */
class FWindow
{
public:
    using DropCallback = std::function<void(int, const char**)>;
    using FPlacement = FWindowGeometry::FPlacement;

    FWindow();
    ~FWindow();

    /**
     * 创建窗口
     * @param Width 窗口宽度
     * @param Height 窗口高度
     * @param Title 窗口标题
     * @return 是否创建成功
     */
    bool Create(
        int32_t Width,
        int32_t Height,
        const std::string& Title,
        const FPlacement* SavedPlacement = nullptr);

    /**
     * 销毁窗口
     */
    void Destroy();

    /**
     * 检查窗口是否应该关闭
     */
    bool ShouldClose() const;

    /**
     * 交换缓冲区
     */
    void SwapBuffers();

    /**
     * 轮询事件
     */
    void PollEvents();

    /**
     * 最小化或帧缓冲暂时为 0x0 时不可安全渲染。
     */
    bool CanRender() const;

    /**
     * 最小化期间等待窗口消息，避免无意义地高速空转。
     */
    void WaitEvents(double TimeoutSeconds);

    /**
     * 获取GLFW窗口句柄
     */
    GLFWwindow* GetNativeWindow() const { return NativeWindow; }

    /**
     * 获取窗口大小
     */
    void GetFramebufferSize(int32_t& OutWidth, int32_t& OutHeight) const;

    /**
     * 获取可供下次启动恢复的普通窗口边界和当前最大化状态。
     * 最小化退出时仍返回最小化之前的有效边界。
     */
    bool GetPlacement(FPlacement& OutPlacement);

    /**
     * 获取光标位置（相对窗口客户区左上角的逻辑像素）
     *
     * GLFW 的拖放回调本身不带坐标，但它在派发回调前刚把光标位置更新成松手处的位置，
     * 所以在回调里读到的就是"拖到哪里松的手"。
     */
    void GetCursorPos(double& OutX, double& OutY) const;

    /**
     * 设置文件拖放回调
     */
    void SetDropCallback(DropCallback Callback);

    /**
     * 获取窗口实例
     */
    static FWindow* Get();

private:
    static void GLFWErrorCallback(int Error, const char* Description);
    static void GLFWDropCallback(GLFWwindow* Window, int Count, const char** Paths);
    static void GLFWWindowPositionCallback(GLFWwindow* Window, int X, int Y);
    static void GLFWWindowSizeCallback(GLFWwindow* Window, int Width, int Height);
    static void GLFWWindowMaximizeCallback(GLFWwindow* Window, int Maximized);

    void CaptureRestoredPlacement();
    static FPlacement ConstrainPlacementToVisibleWorkArea(
        const FPlacement& Placement,
        const FWindowGeometry::FFrameSize& Frame);

    GLFWwindow* NativeWindow;
    DropCallback DropCallbackFunc;
    int32_t WindowWidth;
    int32_t WindowHeight;
    std::string WindowTitle;
    FPlacement RestoredPlacement;
    bool bHasRestoredPlacement;

    static FWindow* Instance;
};
