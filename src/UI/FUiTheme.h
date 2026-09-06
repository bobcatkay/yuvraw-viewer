#pragma once

#include "Core/FUserSettings.h"

#include <imgui.h>

/**
 * 当前进程正在使用的六项基础主题色。
 *
 * 设置弹窗拖动取色器时只更新这里并立即应用到 ImGui 样式，避免逐帧写 settings.ini；
 * 点“应用”后再由 FUserSettings 持久化，取消则从持久值重载。浮层、次要文字和控件状态色
 * 在 ApplyToImGuiStyle() 中自动推导，不进入持久层。
 */
namespace FUiTheme
{
    ImVec4 ToImGuiColor(
        const FUserSettings::FThemeColor& Color,
        float Alpha = 1.0f);
    FUserSettings::FThemeColor ToSettingsColor(const ImVec4& Color);

    const FUserSettings::FThemePalette& GetPalette();
    const FUserSettings::FThemeColor& GetColor(
        FUserSettings::EThemeColorRole Role);
    ImVec4 GetImGuiColor(
        FUserSettings::EThemeColorRole Role,
        float Alpha = 1.0f);

    /// 只更新运行时调色板，用于设置页实时预览。
    void SetPalette(const FUserSettings::FThemePalette& Palette);

    /// 从已持久化设置重载，并立即恢复 ImGui 样式。
    void ReloadPalette();

    /// 把运行时调色板应用到全局 ImGui 样式；不做任何磁盘 I/O。
    void ApplyToImGuiStyle();
}
