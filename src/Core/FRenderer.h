#pragma once

#include <memory>
#include <string>
#include <imgui.h>

struct ImGuiContext;
class FHdrPresenter;

/**
 * 渲染器类
 * 负责ImGui的初始化和渲染
 */
class FRenderer
{
public:
    FRenderer();
    ~FRenderer();

    /**
     * 初始化渲染器
     * @param Window GLFW窗口句柄
     * @return 是否初始化成功
     */
    bool Initialize(void* Window);

    /**
     * 开始新的一帧
     */
    void BeginFrame();

    /**
     * 结束当前帧并渲染
     *
     * @return 是否仍需调用方 SwapBuffers。HDR 通路生效时呈现由 D3D11 交换链接管，
     *         或最小化导致本帧没有可用帧缓冲时返回 false；此时不能再走 WGL 呈现
     */
    bool EndFrame();

    /**
     * 清理渲染器
     */
    void Shutdown();

    /**
     * 获取ImGui上下文
     */
    ImGuiContext* GetImGuiContext() const;

    /**
     * HDR 呈现层。初始化失败时也不为 nullptr —— 它自己知道要退回 SDR
     */
    FHdrPresenter* GetHdrPresenter() const { return HdrPresenter.get(); }

private:
    void InitializeImGuiStyle();
    void ApplyUiScale(float Scale);
    bool RebuildUiFont(float Scale);
    float QueryWindowContentScale() const;
    void UpdateUiScale();

    ImGuiContext* ImGuiContextPtr;
    void* NativeWindow;
    bool bIsInitialized;
    ImGuiStyle BaseImGuiStyle;
    float CurrentUiScale;
    // ImGuiIO 只借用指针，字符串必须活到 DestroyContext 之后。
    std::string ImGuiIniPath;

    std::unique_ptr<FHdrPresenter> HdrPresenter;
};
