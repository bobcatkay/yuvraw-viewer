#pragma once

#include <functional>
#include <memory>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include "Image/FDisplaySettings.h"
#include "Image/FImageFormat.h"

class FImageDocument;

// 前向声明，避免在头文件中包含 OpenGL / ImGui 头文件
typedef unsigned int GLuint;
struct ImVec2;

/**
 * 图像显示模式
 */
enum class EDisplayMode
{
    AutoFit,  ///< 自适应：长边顶满显示区域，居中显示（图像完整可见，含留白）
    Fill,     ///< 填充：图像刚好充满显示区域（cover 模式，多余部分被 scissor 裁剪）
    OneToOne, ///< 1:1：图像与 viewport 像素 1:1 映射（HiDPI 下按 FramebufferScale 自动换算）
    Manual,   ///< 手动：使用用户通过 Ctrl+滚轮设置的自定义缩放比例
};

/**
 * 单张图片的视图配置。
 *
 * 这些字段只描述“怎么看”，不会修改像素或重建纹理。ActualScale 由画布尺寸动态计算，
 * 不属于可缓存配置，因此没有放在这里。
 */
struct FImageViewSettings
{
    EDisplayMode DisplayMode = EDisplayMode::AutoFit;
    float ManualScale = 1.0f;
    float PanOffsetX = 0.0f;
    float PanOffsetY = 0.0f;
    int32_t RotationQuarters = 0;
    bool bFlipH = false;
    bool bFlipV = false;
};

/**
 * 只把缩放/平移相关字段从一份视图配置同步到另一份。
 *
 * 镜像与旋转属于每张图片自己的朝向，单图切换和平铺联动都不能覆盖它们。
 */
inline void CopyPanZoomViewSettings(
    const FImageViewSettings& Source,
    FImageViewSettings& Target)
{
    Target.DisplayMode = Source.DisplayMode;
    Target.ManualScale = Source.ManualScale;
    Target.PanOffsetX = Source.PanOffsetX;
    Target.PanOffsetY = Source.PanOffsetY;
}

/**
 * 双图平铺方向
 */
enum class ETileLayout
{
    Horizontal, ///< 主图在左、对比图在右
    Vertical,   ///< 主图在上、对比图在下
};

/**
 * 图像查看器
 *
 * 只负责"看"：缩放、平移、显示模式、通道隔离、像素探针。
 * 图像数据与 GPU 纹理归 FImageDocument 所有，这里只持有非拥有指针。
 */
class FImageViewer
{
public:
    FImageViewer();
    ~FImageViewer();

    /**
     * 渲染图像查看器
     */
    void Render();

    /**
     * 在所有 Dock 面板结束后提交预览区浮层的视觉图元。
     * 命中仍在 Render() 中处理，这里只负责保证图标盖在自定义 OpenGL 图像之上。
     */
    void RenderOverlayVisuals();

    /**
     * 绑定要显示的文档（非拥有）
     */
    void SetDocument(FImageDocument* InDocument) { Document = InDocument; }

    /**
     * 绑定平铺对比时显示的第二份文档（非拥有）。传 nullptr 关闭平铺模式。
     *
     * 平铺模式下两图各自保存完整视图状态；默认开启的同步开关只联动缩放与平移，
     * 镜像和旋转仍作用于当前选中图。
     */
    void SetSecondaryDocument(FImageDocument* InDocument) { SecondaryDocument = InDocument; }

    /**
     * 配置单图切换模式下当前隐藏的另一份文档。
     *
     * 该模式始终同步两图的显示模式、缩放与平移；每帧以当前显示文档为准对齐隐藏文档，
     * 确保切换时画面位置不跳变。传 nullptr 关闭单图联动。
     */
    void SetSingleImageComparePeer(FImageDocument* InDocument);

    /**
     * 设置当前被属性面板编辑的文档，用于平铺模式下高亮对应的身份标签。
     * 该方法只同步视觉状态，不触发 OnDocumentSelected。
     */
    void SetSelectedDocument(FImageDocument* InDocument) { SelectedDocument = InDocument; }

    /**
     * 标记固定文档槽位正在后台换图。旧纹理继续显示，查看器只叠加局部动画；
     * 传 false 会清除该槽位状态。
     */
    void SetDocumentLoading(
        FImageDocument* Target,
        bool bLoading,
        const std::string& Label);
    bool IsDocumentLoading(FImageDocument* Target) const;

    /**
     * 用户在平铺模式下点击任一图像时触发。
     */
    void SetOnDocumentSelected(std::function<void(FImageDocument*)> Callback)
    {
        OnDocumentSelected = std::move(Callback);
    }

    /**
     * 单图模式下在画布内点击并抬起左键时请求在主图与对比图之间切换。
     * 具体目标与属性/直方图联动由 FMainDockSpace 统一处理。
     */
    void SetOnSingleImageSwitchRequested(std::function<void()> Callback)
    {
        OnSingleImageSwitchRequested = std::move(Callback);
    }

    /**
     * 配置当前单图是否允许左键切换，以及是否显示首次使用提示。
     */
    void SetSingleImageSwitchState(
        bool bEnabled,
        bool bShowHint,
        bool bShowingCompare)
    {
        bSingleImageSwitchEnabled = bEnabled;
        bSingleImageSwitchHintVisible = bEnabled && bShowHint;
        bSingleImageSwitchShowingCompare = bEnabled && bShowingCompare;
    }

    /**
     * 当前是否处于双图平铺模式
     */
    bool IsSideBySide() const;

    /**
     * 设置双图平铺方向
     */
    void SetTileLayout(ETileLayout Layout);
    ETileLayout GetTileLayout() const { return TileLayout; }

    /**
     * 设置显示模式
     */
    void SetDisplayMode(EDisplayMode Mode);

    /**
     * 获取显示模式
     */
    EDisplayMode GetDisplayMode() const;

    /**
     * 获取当前实际缩放比例（仅供 UI 显示）
     */
    float GetZoom() const;

    /**
     * 设置缩放比例并切到手动模式（工具栏的缩放输入框用）
     *
     * 不动平移偏移：从工具栏改缩放时画面应当"原地放大"，而不是跳回居中。
     */
    void SetZoom(float Zoom);

    /**
     * 读取/恢复指定文档的视图配置，供按文件配置缓存使用。
     */
    FImageViewSettings GetViewSettings(FImageDocument* Target) const;
    void SetViewSettings(FImageDocument* Target, const FImageViewSettings& Settings);

    /**
     * 绕屏幕中心旋转 90 度的整数倍，正数为顺时针
     */
    void RotateView(FImageDocument* Target, int32_t QuarterSteps);

    /**
     * 沿屏幕的水平/垂直方向镜像
     */
    void MirrorView(FImageDocument* Target, bool bHorizontal);

    /**
     * 把当前选中图片重置为默认自适应模式（含朝向与平移）
     */
    void ResetView();

    /**
     * 判断一个屏幕坐标是否落在本面板内，用于把系统拖放派发到正确的面板。
     * 面板被折叠或位于未选中的标签页时恒为 false。
     */
    bool ContainsScreenPoint(float X, float Y) const;

private:
    struct FDocumentViewState
    {
        FImageViewSettings Settings;
        float ActualScale = 1.0f;
    };

    struct FPendingPaneOverlayVisual
    {
        float GroupMinX = 0.0f;
        float GroupMinY = 0.0f;
        float GroupMaxX = 0.0f;
        float GroupMaxY = 0.0f;
        float LabelMinX = 0.0f;
        float LabelMinY = 0.0f;
        float LabelMaxX = 0.0f;
        float LabelMaxY = 0.0f;
        float IdentityLabelMaxX = 0.0f;
        std::string Label;
        std::string IdentityLabel;
        uint32_t ViewportId = 0;
        uint8_t HoveredButtonMask = 0;
        uint8_t ActiveButtonMask = 0;
        bool bHasControls = false;
        bool bHasLabel = false;
        bool bHasIdentityLabel = false;
        bool bSelected = false;
    };

    struct FDocumentLoadingState
    {
        std::string Label;
    };

    struct FPendingLoadingVisual
    {
        float MinX = 0.0f;
        float MinY = 0.0f;
        float MaxX = 0.0f;
        float MaxY = 0.0f;
        uint32_t ViewportId = 0;
        std::string Label;
    };

    struct FPendingSingleImageHintVisual
    {
        float MinX = 0.0f;
        float MinY = 0.0f;
        float MaxX = 0.0f;
        float MaxY = 0.0f;
        uint32_t ViewportId = 0;
        bool bValid = false;
    };

    void RenderImage();
    void RenderToolbar();

    /**
     * 切换平铺模式的缩放/平移同步。开启时以当前选中图片为基准对齐另一侧，
     * 只复制缩放与平移字段，不改变两侧各自的镜像和旋转。
     */
    void SetPanZoomSynchronized(bool bSynchronized);

    /// 单图切换时返回隐藏文档；平铺时仅在链条开启后返回另一侧文档。
    FImageDocument* GetPanZoomPeer(FImageDocument* Target) const;

    /// 将 Source 的缩放/平移字段复制给 Target，保留 Target 自己的镜像与旋转。
    void SynchronizePanZoomFrom(FImageDocument* Source, FImageDocument* Target);

    /**
     * 在一张图的右上角绘制独立的镜像/旋转控件；Label 为空时不绘制左上角标签。
     * IdentityLabel 只用于单图切换的主图/对比图高亮前缀。
     */
    void RenderPaneOverlay(
        FImageDocument* Doc,
        const ImVec2& PaneMin,
        const ImVec2& PaneMax,
        const char* Label,
        const char* IdentityLabel);

    void QueueLoadingVisual(
        FImageDocument* Doc,
        const ImVec2& PaneMin,
        const ImVec2& PaneMax);

    void QueueSingleImageSwitchHint(
        const ImVec2& PaneMin,
        const ImVec2& PaneMax);

    /**
     * 把一个文档画到指定矩形。平铺模式下会调用两次，各自带自己的 scissor 区域。
     */
    void IssueDrawCallback(
        FImageDocument* Doc,
        const ImVec2& RenderPos,
        const ImVec2& RenderSize,
        const ImVec2& ClipMin,
        const ImVec2& ClipMax,
        float Scale,
        const FImageViewSettings& ViewSettings);

    /**
     * 像素探针。平铺模式下同时给出两图在同一坐标的取值与差值。
     */
    void RenderPixelProbe(int32_t ImageX, int32_t ImageY);

    /// 旋转 90/270 度时图像在屏幕上的宽高互换
    static bool IsAxisSwapped(const FImageViewSettings& Settings)
    {
        return (Settings.RotationQuarters & 1) != 0;
    }

    FDocumentViewState& GetViewState(FImageDocument* Target);
    const FDocumentViewState* FindViewState(FImageDocument* Target) const;

    /// 工具栏操作优先作用于平铺模式下当前选中的文档，否则作用于当前显示文档。
    FImageDocument* GetInteractionTarget() const;

    /// 由画布/悬浮控件发起交互时，同步属性面板的当前编辑对象。
    void SelectDocument(FImageDocument* Target);

    /// 两个镜像同时置位等价于旋转 180 度，折进旋转里，保证同一朝向只有一种表示
    static void NormalizeOrientation(FImageViewSettings& Settings);

    void InitializeOpenGLResources();
    void DestroyOpenGLResources();

    /// 非拥有：文档由 FMainDockSpace 持有
    FImageDocument* Document;

    /// 非拥有：平铺模式下的第二份文档，nullptr 表示单图模式
    FImageDocument* SecondaryDocument;

    /// 非拥有：单图切换模式下当前隐藏的另一份文档，nullptr 表示不联动。
    FImageDocument* SingleImageComparePeer;

    /// 非拥有：属性面板当前编辑的文档，决定平铺身份标签的选中背景
    FImageDocument* SelectedDocument;

    std::function<void(FImageDocument*)> OnDocumentSelected;
    std::function<void()> OnSingleImageSwitchRequested;

    bool bSingleImageSwitchEnabled;
    bool bSingleImageSwitchHintVisible;
    /// 单图切换模式的当前槽位身份，用于文件名前的高亮标签。
    bool bSingleImageSwitchShowingCompare;

    /**
     * 每个当前文档槽位独立的视图状态。
     *
     * 约定的变换是 **M = 旋转(90*RotationQuarters) · 镜像**，即先镜像再旋转。
     * 四个悬浮按钮都作用在**屏幕空间**上（"把现在看到的这幅图再翻一下"），
     * 所以镜像按钮除了翻标志位还要把旋转取反 —— 反射会把旋转共轭成它的逆，
     * Fx·R(θ) = R(-θ)·Fx。这样八种朝向都能用这三个字段唯一表示。
     *
     * 状态按 FImageDocument* 保存，使主图和对比图在同屏时仍有各自的完整视图配置；
     * 单图切换始终联动缩放/平移；平铺则由链条开关决定。两种模式都不会合并
     * 两份状态或影响各自朝向。
     * 文档对象被用于打开另一文件前，FMainDockSpace 会把这里的状态存入有界的按路径缓存；
     * 因此本映射最多只有主图/对比图/差值图几个固定槽位，不会随浏览文件数增长。
     */
    std::unordered_map<const FImageDocument*, FDocumentViewState> DocumentViewStates;
    std::unordered_map<const FImageDocument*, FDocumentLoadingState> DocumentLoadingStates;

    ETileLayout TileLayout;

    /// 平铺模式下是否同步两侧的缩放与平移；镜像和旋转始终独立。
    bool bPanZoomSynchronized;

    /// 像素探针：鼠标所在的图像像素坐标，-1 表示不在图像内
    int32_t ProbeX;
    int32_t ProbeY;

    /// 本帧窗口在屏幕坐标下的矩形，供拖放命中判定；不可见时 bDropRectValid 为 false
    float DropRectMin[2];
    float DropRectMax[2];
    bool bDropRectValid;

    static constexpr int32_t kMaximumPendingPaneOverlayVisuals = 2;
    FPendingPaneOverlayVisual PendingPaneOverlayVisuals[kMaximumPendingPaneOverlayVisuals];
    int32_t PendingPaneOverlayVisualCount;

    static constexpr int32_t kMaximumPendingLoadingVisuals = 2;
    FPendingLoadingVisual PendingLoadingVisuals[kMaximumPendingLoadingVisuals];
    int32_t PendingLoadingVisualCount;

    FPendingSingleImageHintVisual PendingSingleImageHintVisual;

    // OpenGL渲染资源（VAO/VBO）
    GLuint QuadVAO;
    GLuint QuadVBO;
    bool bOpenGLResourcesInitialized;
};
