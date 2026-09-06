#pragma once

#include "FHdrDisplay.h"
#include "Image/FColorTransform.h"

#include <cstdint>
#include <memory>
#include <string>

/**
 * HDR 呈现层
 *
 * OpenGL 在 Windows 上没有任何标准途径拿到 HDR 后台缓冲：WGL_EXT_colorspace 只有
 * sRGB / linear-sRGB，GLFW 也没有暴露相关 hint。默认窗口拿到的是 8bit UNORM、
 * 被 DWM 当 SDR 内容处理的帧缓冲，**峰值物理上就是 SDR 白**，
 * 在着色器里算出再漂亮的 1000nit 值也出不去。
 *
 * 所以这里把呈现（present）从 OpenGL 手里接管过来：
 *
 *   1. ImGui **整帧**渲染到一张 fp16 的 GL 帧缓冲（FBO-A）
 *   2. 一个 GL 全屏 pass 把 FBO-A 合成到一张与 D3D11 **共享**的 fp16 纹理（FBO-B）
 *   3. D3D11 把共享纹理 CopyResource 到 flip-model 交换链的后台缓冲，Present
 *
 * 共享靠 WGL_NV_DX_interop2（名字带 NV，AMD 也支持；Intel 是弱环节）。
 * 任何一步失败都回退到原来的"直接画到默认帧缓冲 + glfwSwapBuffers"，
 * 也就是本次改造之前的行为 —— **HDR 通路是纯增量，坏掉不影响 SDR**。
 *
 * ## fp16 帧缓冲的编码约定
 *
 * FBO-A 里存的是**扩展 sRGB 编码值，1.0 = 显示器 SDR 白电平**（可超过 1，也可为负）。
 *
 * 这个约定是整套设计的关键，因为它让 **ImGui 一个字节都不用改**：
 * 界面本来就往帧缓冲里写 0-1 的 sRGB 值，在这个约定下含义完全正确。
 * 只有图像内容需要写出 >1 的值，那是着色器里 uOutputMode=1 分支负责的
 * （见 gl/FShaders.h 的 ApplyColorPipeline）。
 *
 * 换成"FBO 直接存 scRGB 线性"就必须改 imgui_impl_opengl3 的着色器
 * —— 那是 vendored 代码，改了以后每次更新 ImGui 都要重新打补丁；
 * 而且 ImGui 的顶点色是 8bit ImU32，根本表达不了 >1 的值。
 *
 * 合成 pass 做的就是这个约定到 scRGB 的换算：
 *
 *   scRGB = ExtendedSrgbDecode(fbo) * (SdrWhiteNits / 80)
 *
 * （scRGB 的定义是线性 BT.709，1.0 = 80 nit。选它而不是 HDR10/PQ 的理由有两个：
 *   广色域可以用负分量表示，不需要 gamut clip；以及 PQ 下界面的 1.0 = 10000nit 会闪瞎眼。）
 */
class FHdrPresenter
{
public:
    FHdrPresenter();
    ~FHdrPresenter();

    FHdrPresenter(const FHdrPresenter&) = delete;
    FHdrPresenter& operator=(const FHdrPresenter&) = delete;

    /**
     * 初始化 HDR 通路
     *
     * 失败不是错误，只是这台机器/这块屏走不了 HDR。调用方照常走 SDR 路径。
     *
     * @param Hwnd 窗口句柄（HWND）。GL 上下文必须已经 current
     * @return 是否成功建立 HDR 通路
     */
    bool Initialize(void* Hwnd);

    void Shutdown();

    /**
     * 每帧开头调一次，刷新显示器状态（内部有节流，不会每帧都去查 DXGI）
     *
     * 窗口被拖到另一块屏、或用户在系统设置里开关 HDR，都靠这里发现。
     */
    void Update();

    /**
     * 图像内容是否按 HDR 输出
     *
     * 需要同时满足：呈现层初始化成功、用户没关掉、当前显示器确实处于 HDR 模式。
     * 这个状态只决定色彩管线，不决定由 DXGI 还是 WGL 呈现窗口。
     */
    bool IsHdrActive() const;

    /**
     * HDR 输出是否**可选**（与用户开关无关）
     *
     * 通路就绪 + 当前显示器处于 HDR 模式。为 false 时"HDR 输出"这个开关点了也没有
     * 任何效果，界面应当把它置灰并说明原因，而不是让用户以为勾上就会生效。
     */
    bool IsHdrAvailable() const;

    /**
     * 当前主窗口是否必须继续由 DXGI 交换链呈现
     *
     * 一旦 HDR 显示器上的 flip-model 交换链开始接管同一个 HWND，就不能再切回
     * WGL SwapBuffers；两套呈现后端在同一窗口间来回切换会造成闪屏，并可能阻塞 UI 线程。
     *
     * 所以这里有一个**单向门闩**（bPresentationLatched）：只要成功 Present 过一次，
     * 此后无论用户关掉“HDR 输出”、在系统设置里关掉 HDR，还是把窗口拖到 SDR 副屏上，
     * 都继续由 DXGI 呈现，只让色彩管线退回 SDR（见 IsHdrActive）。
     * 唯一解除门闩的途径是 DisableAfterFailure —— 那时 HDR 通路本身已经坏了，
     * 除了退回 WGL 别无选择。
     */
    bool IsPresentationActive() const;

    /**
     * 开始渲染到 fp16 帧缓冲
     *
     * @return false 表示本帧走 SDR，调用方应照常渲染到默认帧缓冲
     */
    bool BeginFrame(int32_t Width, int32_t Height);

    /**
     * 合成并呈现
     *
     * @return false 表示没有接管呈现，调用方需要自己 SwapBuffers
     */
    bool EndFrame();

    /// 供色彩管线使用的输出目标描述
    FColorTransform::FDisplayOutput GetDisplayOutput() const;

    /// 显示器探测结果，供界面展示
    const FHdrDisplayInfo& GetDisplayInfo() const { return DisplayInfo; }

    /// 一行可直接显示的状态说明（含失败原因）
    const std::string& GetStatusText() const { return StatusText; }

    /// 用户开关。关掉后仍由 DXGI 呈现，但图像色彩管线改走 SDR，便于对比"HDR 前后"
    void SetUserEnabled(bool bEnabled);
    bool IsUserEnabled() const { return bUserEnabled; }

    /**
     * 全局访问
     *
     * 图像查看器在 ImGui 的绘制回调里要知道往哪种帧缓冲输出，
     * 而那条路径上没有渲染器的引用可用。
     */
    static FHdrPresenter* Get();

    /**
     * 当前输出目标。没有呈现层或未启用 HDR 时返回默认值（SDR），
     * 于是色彩管线自然退回 SDR 分支。
     */
    static FColorTransform::FDisplayOutput GetActiveDisplayOutput();

private:
    struct FImpl;

    /// 按当前尺寸建/重建 fp16 帧缓冲、共享纹理与交换链缓冲
    bool EnsureResources(int32_t Width, int32_t Height);

    void ReleaseSizedResources();

    /// 编译合成 pass 的着色器
    bool CreateCompositeProgram();

    /**
     * 运行期失败后整体退回 SDR
     *
     * 不做重试：HDR 通路失败通常是设备丢失或驱动状态出了问题，
     * 每帧重试只会把日志刷爆，画面也一样不动。
     */
    void DisableAfterFailure(const char* Status, const char* Where, long Code);

    std::unique_ptr<FImpl> Impl;

    FHdrDisplayInfo DisplayInfo;
    std::string StatusText;

    bool bInitialized = false;
    bool bUserEnabled = true;

    /// 本帧确实绑定了 fp16 帧缓冲（BeginFrame 返回过 true）
    bool bFrameActive = false;

    /// DXGI 交换链已经在这个 HWND 上成功 Present 过，之后不能再切回 WGL。见 IsPresentationActive
    bool bPresentationLatched = false;

    int32_t SurfaceWidth = 0;
    int32_t SurfaceHeight = 0;

    /// 上次探测显示器的时间戳（毫秒），用于节流
    uint64_t LastProbeMs = 0;

    static FHdrPresenter* Instance;
};
