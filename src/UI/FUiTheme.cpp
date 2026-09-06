#include "FUiTheme.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
    constexpr float kColorChannelMaximum = 255.0f;
    constexpr float kPopupBackgroundAlpha = 0.98f;
    constexpr float kSurfaceLuminanceThreshold = 0.50f;
    constexpr float kPopupSurfaceEdgeMix = 0.35f;
    constexpr float kMenuSurfaceEdgeMix = 0.09f;
    constexpr float kDisabledTextSurfaceMix = 0.64f;
    constexpr float kFrameHoveredTextMix = 0.08f;
    constexpr float kFrameActiveTextMix = 0.18f;
    constexpr float kButtonHoveredTextMix = 0.24f;
    constexpr float kButtonActiveTextMix = 0.46f;
    constexpr float kHeaderSurfaceControlMix = 0.12f;
    constexpr float kHeaderActiveTextMix = 0.10f;
    constexpr float kHeaderHoveredAlpha = 0.45f;
    constexpr float kHeaderActiveAlpha = 0.65f;
    constexpr float kTabTextMix = 0.24f;
    constexpr float kTabHoveredTextMix = 0.15f;
    constexpr float kTabSelectedTextMix = 0.08f;
    constexpr float kControlAccentTextMix = 0.08f;
    constexpr float kControlActiveTextMix = 0.46f;
    constexpr float kLuminanceRedWeight = 0.2126f;
    constexpr float kLuminanceGreenWeight = 0.7152f;
    constexpr float kLuminanceBlueWeight = 0.0722f;

    ImVec4 MixColor(const ImVec4& From, const ImVec4& To, float Amount)
    {
        const float amount = std::clamp(Amount, 0.0f, 1.0f);
        return ImVec4(
            From.x + (To.x - From.x) * amount,
            From.y + (To.y - From.y) * amount,
            From.z + (To.z - From.z) * amount,
            From.w + (To.w - From.w) * amount);
    }

    float GetLuminance(const ImVec4& Color)
    {
        return Color.x * kLuminanceRedWeight
            + Color.y * kLuminanceGreenWeight
            + Color.z * kLuminanceBlueWeight;
    }

    FUserSettings::FThemePalette& MutablePalette()
    {
        static FUserSettings::FThemePalette Palette =
            FUserSettings::GetThemePalette();
        return Palette;
    }
}

ImVec4 FUiTheme::ToImGuiColor(
    const FUserSettings::FThemeColor& Color,
    float Alpha)
{
    return ImVec4(
        static_cast<float>(Color.R) / kColorChannelMaximum,
        static_cast<float>(Color.G) / kColorChannelMaximum,
        static_cast<float>(Color.B) / kColorChannelMaximum,
        std::clamp(Alpha, 0.0f, 1.0f));
}

FUserSettings::FThemeColor FUiTheme::ToSettingsColor(const ImVec4& Color)
{
    const auto ToByte = [](float Channel) -> uint8_t
    {
        const float constrained = std::clamp(Channel, 0.0f, 1.0f);
        return static_cast<uint8_t>(
            std::lround(constrained * kColorChannelMaximum));
    };

    return { ToByte(Color.x), ToByte(Color.y), ToByte(Color.z) };
}

const FUserSettings::FThemePalette& FUiTheme::GetPalette()
{
    return MutablePalette();
}

const FUserSettings::FThemeColor& FUiTheme::GetColor(
    FUserSettings::EThemeColorRole Role)
{
    return MutablePalette().Get(Role);
}

ImVec4 FUiTheme::GetImGuiColor(
    FUserSettings::EThemeColorRole Role,
    float Alpha)
{
    return ToImGuiColor(GetColor(Role), Alpha);
}

void FUiTheme::SetPalette(const FUserSettings::FThemePalette& Palette)
{
    MutablePalette() = Palette;
    ApplyToImGuiStyle();
}

void FUiTheme::ReloadPalette()
{
    MutablePalette() = FUserSettings::GetThemePalette();
    ApplyToImGuiStyle();
}

void FUiTheme::ApplyToImGuiStyle()
{
    if (!ImGui::GetCurrentContext())
    {
        return;
    }

    using FUserSettings::EThemeColorRole;

    ImVec4* colors = ImGui::GetStyle().Colors;
    const ImVec4 surface = GetImGuiColor(EThemeColorRole::InterfaceBackground);
    const ImVec4 text = GetImGuiColor(EThemeColorRole::Text);
    const ImVec4 input = GetImGuiColor(EThemeColorRole::InputBackground);
    const ImVec4 control = GetImGuiColor(EThemeColorRole::Control);
    const bool bLightSurface =
        GetLuminance(surface) >= kSurfaceLuminanceThreshold;
    const ImVec4 popupEdge = bLightSurface
        ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
        : ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    const ImVec4 menuEdge = bLightSurface
        ? ImVec4(0.0f, 0.0f, 0.0f, 1.0f)
        : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

    colors[ImGuiCol_WindowBg] = surface;
    colors[ImGuiCol_DockingEmptyBg] = surface;
    colors[ImGuiCol_PopupBg] = MixColor(
        surface,
        popupEdge,
        kPopupSurfaceEdgeMix);
    colors[ImGuiCol_PopupBg].w = kPopupBackgroundAlpha;
    colors[ImGuiCol_MenuBarBg] = MixColor(
        surface,
        menuEdge,
        kMenuSurfaceEdgeMix);
    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = MixColor(
        text,
        surface,
        kDisabledTextSurfaceMix);
    colors[ImGuiCol_InputTextCursor] = colors[ImGuiCol_Text];

    colors[ImGuiCol_Border] = GetImGuiColor(EThemeColorRole::Border);
    colors[ImGuiCol_FrameBg] = input;
    colors[ImGuiCol_FrameBgHovered] = MixColor(
        input,
        text,
        kFrameHoveredTextMix);
    colors[ImGuiCol_FrameBgActive] = MixColor(
        input,
        text,
        kFrameActiveTextMix);

    colors[ImGuiCol_Button] = control;
    colors[ImGuiCol_ButtonHovered] = MixColor(
        control,
        text,
        kButtonHoveredTextMix);
    colors[ImGuiCol_ButtonActive] = MixColor(
        control,
        text,
        kButtonActiveTextMix);

    colors[ImGuiCol_Header] = MixColor(
        surface,
        control,
        kHeaderSurfaceControlMix);
    colors[ImGuiCol_HeaderHovered] = control;
    colors[ImGuiCol_HeaderHovered].w = kHeaderHoveredAlpha;
    colors[ImGuiCol_HeaderActive] = MixColor(
        control,
        text,
        kHeaderActiveTextMix);
    colors[ImGuiCol_HeaderActive].w = kHeaderActiveAlpha;

    const ImVec4 tabColor = MixColor(control, text, kTabTextMix);
    const ImVec4 tabHoveredColor = MixColor(
        control,
        text,
        kTabHoveredTextMix);
    const ImVec4 tabSelectedColor = MixColor(
        control,
        text,
        kTabSelectedTextMix);
    colors[ImGuiCol_Tab] = tabColor;
    colors[ImGuiCol_TabHovered] = tabHoveredColor;
    colors[ImGuiCol_TabSelected] = tabSelectedColor;
    colors[ImGuiCol_TabSelectedOverline] = tabSelectedColor;
    colors[ImGuiCol_TabDimmed] = tabColor;
    colors[ImGuiCol_TabDimmedSelected] = tabSelectedColor;
    colors[ImGuiCol_TabDimmedSelectedOverline] = tabSelectedColor;

    const ImVec4 controlAccentColor = MixColor(
        control,
        text,
        kControlAccentTextMix);
    colors[ImGuiCol_CheckMark] = controlAccentColor;
    colors[ImGuiCol_SliderGrab] = controlAccentColor;
    colors[ImGuiCol_SliderGrabActive] = MixColor(
        control,
        text,
        kControlActiveTextMix);
}
