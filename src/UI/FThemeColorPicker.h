#pragma once

#include "Core/FUserSettings.h"

#include <array>
#include <cstddef>

/**
 * 设置页自绘取色器的瞬时状态。
 *
 * Hue 必须独立保留：纯黑、纯白和灰色转回 HSV 时没有色相，若只从 RGB 逐帧反算，
 * 用户在这些颜色上拖动饱和度时会突然跳回红色。
 */
struct FThemeColorPickerState
{
    static constexpr size_t kHexTextBufferSize = 8;

    float Hue = 0.0f;
    float Saturation = 0.0f;
    float Value = 0.0f;
    std::array<char, kHexTextBufferSize> HexText{};
    FUserSettings::FThemeColor LastColor{};
    bool bInitialized = false;
};

namespace FThemeColorPicker
{
    /// 切换主题项、取消或重置后，让 HSV/十六进制编辑状态与颜色重新同步。
    void Synchronize(
        const FUserSettings::FThemeColor& Color,
        FThemeColorPickerState& State);

    /**
     * 在首选宽度内同时适配可用宽高，避免内嵌编辑区出现滚动条。
     * 所有参数和返回值均已按当前 DPI 换算。
     */
    float CalculateFittingWidth(
        float PreferredWidth,
        float AvailableWidth,
        float AvailableHeight);

    /**
     * 绘制“明度/饱和度区域 + 水平色相条 + Hex 输入 + 色块”的内嵌取色器。
     * @param Width 已按当前 DPI 换算的控件宽度。
     * @return 本帧颜色是否发生有效变化。
     */
    bool Render(
        const char* Id,
        FUserSettings::FThemeColor& Color,
        FThemeColorPickerState& State,
        float Width);
}
