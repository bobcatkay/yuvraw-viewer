#pragma once

#include <imgui.h>

/**
 * 小图标绘制
 *
 * 界面字体由 FUiFont 构建中英文 UI 字形集，**不含 emoji**
 * （📁 这类码点在 BMP 之外，ImGui 默认的 ImWchar 也只有 16 位），
 * 直接写 emoji 会画成问号或豆腐块。这里的图标一律用 ImDrawList 现画矢量图形，
 * 不依赖字体字形，也不需要位图资源：任何缩放下都是按当前像素密度重新描边。
 */
namespace FUiIcons
{
    /**
     * 画一个文件夹图标
     * @param Pos   图标外接矩形的左上角（屏幕坐标）
     * @param Size  外接正方形边长
     */
    void DrawFolder(ImDrawList* DrawList, const ImVec2& Pos, float Size);

    /**
     * 画一个文件（折角纸张）图标
     */
    void DrawFile(ImDrawList* DrawList, const ImVec2& Pos, float Size);

    /**
     * 纯文件夹图标的按钮，用于"浏览目录"这类入口
     * @return 是否被点击
     */
    bool FolderButton(const char* Id);

    /**
     * 图像查看器使用的小图标
     *
     * 这一族图标尺寸、线宽、配色完全一致，只是形状不同，所以合成一个枚举 + 一个绘制函数，
     * 不像文件夹/文件那样各写一份。
     */
    enum class EViewerGlyph
    {
        MirrorHorizontal,       ///< 水平镜像：竖实线两侧一对圆角空心折面
        MirrorVertical,         ///< 垂直镜像：横实线两侧一对圆角梯形
        RotateClockwise,        ///< 顺时针旋转：平滑圆弧 + 放大的右上圆角箭头
        RotateCounterClockwise, ///< 逆时针旋转：由顺时针组件水平镜像生成
        ChainLink,              ///< 同步缩放/平移：两个斜向相扣的链环
        TileHorizontal,         ///< 水平并联：左右两个极简圆角框
        TileVertical,           ///< 垂直并联：由水平组件旋转 90° 生成
    };

    /**
     * 图标按钮的视觉样式
     */
    enum class EViewerButtonStyle
    {
        Toolbar, ///< 顶部功能栏：主题青色实底
        Overlay, ///< 图像区域悬浮控件：透明底，由调用方统一绘制 30% 黑色组背景
    };

    /**
     * 画一个查看器图标
     * @param Pos   图标外接正方形的左上角（屏幕坐标）
     * @param Size  外接正方形边长
     */
    void DrawViewerGlyph(ImDrawList* DrawList, EViewerGlyph Glyph, const ImVec2& Pos, float Size);

    /**
     * 查看器图标按钮（正方形，边长为一个控件行高）
     *
     * @param Id       以 "##" 开头的唯一标识
     * @param Tooltip  悬停说明，空指针则不显示
     * @param bSelected 工具栏切换按钮是否为当前选项
     * @param Style     顶部功能栏或图像区域悬浮样式
     * @return 是否被点击
     */
    bool ViewerButton(
        const char* Id,
        EViewerGlyph Glyph,
        const char* Tooltip,
        bool bSelected = false,
        EViewerButtonStyle Style = EViewerButtonStyle::Toolbar);

    /**
     * 带图标的整行可选项
     *
     * 图标与文字都是手动绘制的，Selectable 本身只提供一个空标签的命中区域，
     * 因此调用方仍可以在其后直接使用 BeginPopupContextItem 等"上一个控件"接口。
     *
     * @param Id        以 "##" 开头的唯一标识
     * @param Label     显示的文字
     * @param bIsFolder true 画文件夹图标，false 画文件图标
     * @param bCompared 当前项是否是对比图；对比图使用区别于普通选中的琥珀色背景
     * @param bActivateOnNavigation 键盘导航移动到当前项时是否立即激活
     * @return 是否被鼠标、确认键或键盘导航激活
     */
    bool IconSelectable(
        const char* Id,
        const char* Label,
        bool bSelected,
        bool bIsFolder,
        bool bCompared = false,
        bool bActivateOnNavigation = false);

    /**
     * 画一段绕圈跑的圆弧，作为"正在忙"的指示
     *
     * @param Time 秒，通常传 ImGui::GetTime()
     */
    void DrawSpinner(ImDrawList* DrawList, const ImVec2& Center, float Radius, float Thickness, double Time);

    /**
     * 全屏忙碌遮罩：压暗整个主视口，中间一张卡片放转圈动画与说明文字
     *
     * 直接画在主视口的**前景绘制列表**上，不开 ImGui 窗口 —— 开窗口就得处理焦点、
     * 输入捕获和 DockSpace 的交互。"挡住输入"由调用方的 BeginDisabled 负责，
     * 这里只管画，因此也不受那层置灰的透明度影响。
     *
     * @param Label  主文字，例如 u8"正在导出..."
     * @param Detail 副文字，为空指针则不画（批量导出时传 "3 / 10"）
     */
    void DrawBusyOverlay(const char* Label, const char* Detail);
}
