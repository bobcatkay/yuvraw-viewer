#pragma once

#include <imgui.h>

/**
 * 应用级 UI 缩放。
 *
 * Dear ImGui 的字体和样式都是全局资源，因此当前比例跟随主窗口所在显示器。
 * 自定义绘制和显式控件尺寸通过这里使用同一比例，避免字体放大后弹窗、留白与线宽仍停留在 100%。
 */
namespace FUiScale
{
    inline float GScale = 1.0f;

    inline void Set(float Scale)
    {
        GScale = Scale > 0.0f ? Scale : 1.0f;
    }

    inline float Get()
    {
        return GScale;
    }

    inline float Apply(float Value)
    {
        return Value * GScale;
    }

    inline ImVec2 Apply(float X, float Y)
    {
        return ImVec2(X * GScale, Y * GScale);
    }
}
