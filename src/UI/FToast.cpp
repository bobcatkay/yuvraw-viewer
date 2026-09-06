#include "FToast.h"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace
{
    // 深底浅字：提示常常压在图像上，而图像的明暗是不可控的，
    // 深色胶囊配浅色文字在纯白和纯黑背景上都读得出来
    constexpr ImU32 kToastFill   = IM_COL32(38, 46, 58, 242);
    constexpr ImU32 kToastBorder = IM_COL32(78, 90, 106, 255);
    constexpr ImU32 kToastText   = IM_COL32(240, 244, 248, 255);

    /// 淡入 / 淡出时长（秒），都算在总时长之内
    constexpr float kFadeIn  = 0.12f;
    constexpr float kFadeOut = 0.35f;

    std::string GMessage;
    double GStartTime = 0.0;
    float GDuration = 0.0f;

    /// 把颜色自带的 alpha 再乘一个系数，用于整体淡入淡出
    ImU32 WithAlpha(ImU32 Color, float Alpha)
    {
        const ImU32 a = static_cast<ImU32>(((Color >> IM_COL32_A_SHIFT) & 0xFF) * Alpha);

        return (Color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
    }
}

void FToast::Show(const char* Text, float DurationSeconds)
{
    if (!Text || !*Text)
    {
        return;
    }

    GMessage = Text;
    GStartTime = ImGui::GetTime();

    // 总时长至少要装得下淡入淡出，否则会出现"还没显全就开始消失"
    GDuration = std::max(DurationSeconds, kFadeIn + kFadeOut);
}

void FToast::Render()
{
    if (GMessage.empty())
    {
        return;
    }

    const float elapsed = static_cast<float>(ImGui::GetTime() - GStartTime);

    if (elapsed >= GDuration)
    {
        GMessage.clear();

        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();

    if (!viewport)
    {
        return;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);

    if (!drawList)
    {
        return;
    }

    float alpha = 1.0f;

    if (elapsed < kFadeIn)
    {
        alpha = elapsed / kFadeIn;
    }
    else if (elapsed > GDuration - kFadeOut)
    {
        alpha = (GDuration - elapsed) / kFadeOut;
    }

    alpha = std::clamp(alpha, 0.0f, 1.0f);

    const float fontSize = ImGui::GetFontSize();
    const ImVec2 textSize = ImGui::CalcTextSize(GMessage.c_str());

    const float padX = fontSize * 1.1f;
    const float padY = fontSize * 0.55f;

    // 底部居中，抬起几行的高度：贴着窗口下沿容易和滚动条、状态文字挤在一起
    const float centerX = viewport->Pos.x + viewport->Size.x * 0.5f;
    const float baselineY = viewport->Pos.y + viewport->Size.y - fontSize * 3.5f;

    // 淡入时顺带上浮一点。只改透明度的话余光很难注意到它出现过
    const float rise = (1.0f - alpha) * fontSize * 0.6f;

    const float halfWidth = textSize.x * 0.5f;
    const ImVec2 boxMin(centerX - halfWidth - padX, baselineY - textSize.y - padY * 2.0f + rise);
    const ImVec2 boxMax(centerX + halfWidth + padX, baselineY + rise);

    const float rounding = (boxMax.y - boxMin.y) * 0.5f;

    drawList->AddRectFilled(boxMin, boxMax, WithAlpha(kToastFill, alpha), rounding);
    drawList->AddRect(boxMin, boxMax, WithAlpha(kToastBorder, alpha), rounding);
    drawList->AddText(ImVec2(centerX - halfWidth, boxMin.y + padY), WithAlpha(kToastText, alpha), GMessage.c_str());
}
