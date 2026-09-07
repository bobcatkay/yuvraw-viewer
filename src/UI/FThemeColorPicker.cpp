#include "FThemeColorPicker.h"

#include "FUiScale.h"
#include "FUiTheme.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr float kReferencePickerWidth = 120.0f;
    constexpr float kSaturationValueHeightRatio = 0.88f;
    constexpr float kHueBarHeight = 12.0f;
    constexpr float kSectionGap = 9.0f;
    constexpr float kSaturationValueRounding = 12.0f;
    constexpr float kHueBarRounding = 6.0f;
    constexpr float kPickerBorderThickness = 1.0f;
    constexpr float kMarkerRadius = 7.0f;
    constexpr float kMarkerShadowThickness = 4.0f;
    constexpr float kMarkerLineThickness = 2.0f;
    constexpr float kBottomSwatchWidth = 40.0f;
    constexpr float kLayoutSafetyMargin = 2.0f;
    constexpr int32_t kPickerStyleVarCount = 4;
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kHalfPi = kPi * 0.5f;
    constexpr float kTwoPi = kPi * 2.0f;
    constexpr int32_t kCornerArcSegments = 6;
    constexpr size_t kHueColorStopCount = 7;
    constexpr size_t kHexDigitCount = 6;
    constexpr int32_t kHexAlphabetFirstValue = 10;
    constexpr int32_t kInvalidHexDigit = -1;
    constexpr uint32_t kBitsPerHexDigit = 4;
    constexpr uint32_t kRedShift = 16;
    constexpr uint32_t kGreenShift = 8;
    constexpr uint32_t kChannelMask = 0xFFu;

    constexpr std::array<ImU32, kHueColorStopCount> kHueColors{
        IM_COL32(255, 0, 0, 255),
        IM_COL32(255, 255, 0, 255),
        IM_COL32(0, 255, 0, 255),
        IM_COL32(0, 255, 255, 255),
        IM_COL32(0, 0, 255, 255),
        IM_COL32(255, 0, 255, 255),
        IM_COL32(255, 0, 0, 255),
    };

    void FormatHexColor(
        const FUserSettings::FThemeColor& Color,
        FThemeColorPickerState& State)
    {
        std::snprintf(
            State.HexText.data(),
            State.HexText.size(),
            "#%02x%02x%02x",
            static_cast<uint32_t>(Color.R),
            static_cast<uint32_t>(Color.G),
            static_cast<uint32_t>(Color.B));
    }

    int32_t GetHexDigitValue(char Character)
    {
        if (Character >= '0' && Character <= '9')
        {
            return Character - '0';
        }

        if (Character >= 'a' && Character <= 'f')
        {
            return Character - 'a' + kHexAlphabetFirstValue;
        }

        if (Character >= 'A' && Character <= 'F')
        {
            return Character - 'A' + kHexAlphabetFirstValue;
        }

        return kInvalidHexDigit;
    }

    bool TryParseHexColor(
        const char* Text,
        FUserSettings::FThemeColor& OutColor)
    {
        if (!Text)
        {
            return false;
        }

        const size_t textLength = std::strlen(Text);
        const size_t digitOffset = textLength > 0 && Text[0] == '#' ? 1 : 0;

        if (textLength != digitOffset + kHexDigitCount)
        {
            return false;
        }

        uint32_t rgb = 0;

        for (size_t digitIndex = 0; digitIndex < kHexDigitCount; ++digitIndex)
        {
            const int32_t digit = GetHexDigitValue(Text[digitOffset + digitIndex]);

            if (digit < 0)
            {
                return false;
            }

            rgb = (rgb << kBitsPerHexDigit) | static_cast<uint32_t>(digit);
        }

        OutColor.R = static_cast<uint8_t>((rgb >> kRedShift) & kChannelMask);
        OutColor.G = static_cast<uint8_t>((rgb >> kGreenShift) & kChannelMask);
        OutColor.B = static_cast<uint8_t>(rgb & kChannelMask);
        return true;
    }

    int FilterHexCharacter(ImGuiInputTextCallbackData* Data)
    {
        const ImWchar character = Data ? Data->EventChar : 0;
        const bool bAllowed = character == '#'
            || (character >= '0' && character <= '9')
            || (character >= 'a' && character <= 'f')
            || (character >= 'A' && character <= 'F');
        return bAllowed ? 0 : 1;
    }

    void UpdateHsvFromColor(
        const FUserSettings::FThemeColor& Color,
        FThemeColorPickerState& State)
    {
        const ImVec4 rgb = FUiTheme::ToImGuiColor(Color);
        ImGui::ColorConvertRGBtoHSV(
            rgb.x,
            rgb.y,
            rgb.z,
            State.Hue,
            State.Saturation,
            State.Value);
        State.LastColor = Color;
        State.bInitialized = true;
    }

    void UpdateColorFromHsv(
        FUserSettings::FThemeColor& Color,
        FThemeColorPickerState& State)
    {
        ImVec4 rgb(0.0f, 0.0f, 0.0f, 1.0f);
        ImGui::ColorConvertHSVtoRGB(
            State.Hue,
            State.Saturation,
            State.Value,
            rgb.x,
            rgb.y,
            rgb.z);
        Color = FUiTheme::ToSettingsColor(rgb);
        State.LastColor = Color;
        FormatHexColor(Color, State);
    }

    void MaskRoundedCorners(
        ImDrawList* DrawList,
        const ImVec2& Min,
        const ImVec2& Max,
        float Rounding,
        ImU32 MaskColor)
    {
        const float radius = std::min(
            Rounding,
            std::min(Max.x - Min.x, Max.y - Min.y) * 0.5f);

        if (radius <= 0.0f)
        {
            return;
        }

        DrawList->PathClear();
        DrawList->PathLineTo(Min);
        DrawList->PathLineTo(ImVec2(Min.x, Min.y + radius));
        DrawList->PathArcTo(
            ImVec2(Min.x + radius, Min.y + radius),
            radius,
            kPi,
            kPi + kHalfPi,
            kCornerArcSegments);
        DrawList->PathFillConvex(MaskColor);

        DrawList->PathClear();
        DrawList->PathLineTo(ImVec2(Max.x, Min.y));
        DrawList->PathLineTo(ImVec2(Max.x - radius, Min.y));
        DrawList->PathArcTo(
            ImVec2(Max.x - radius, Min.y + radius),
            radius,
            kPi + kHalfPi,
            kTwoPi,
            kCornerArcSegments);
        DrawList->PathFillConvex(MaskColor);

        DrawList->PathClear();
        DrawList->PathLineTo(Max);
        DrawList->PathLineTo(ImVec2(Max.x, Max.y - radius));
        DrawList->PathArcTo(
            ImVec2(Max.x - radius, Max.y - radius),
            radius,
            0.0f,
            kHalfPi,
            kCornerArcSegments);
        DrawList->PathFillConvex(MaskColor);

        DrawList->PathClear();
        DrawList->PathLineTo(ImVec2(Min.x, Max.y));
        DrawList->PathLineTo(ImVec2(Min.x + radius, Max.y));
        DrawList->PathArcTo(
            ImVec2(Min.x + radius, Max.y - radius),
            radius,
            kHalfPi,
            kPi,
            kCornerArcSegments);
        DrawList->PathFillConvex(MaskColor);
    }

    void DrawMarker(ImDrawList* DrawList, const ImVec2& Center, float Scale)
    {
        const float markerRadius = FUiScale::Apply(kMarkerRadius) * Scale;
        DrawList->AddCircle(
            Center,
            markerRadius,
            IM_COL32(0, 0, 0, 150),
            0,
            FUiScale::Apply(kMarkerShadowThickness) * Scale);
        DrawList->AddCircle(
            Center,
            markerRadius,
            IM_COL32(255, 255, 255, 255),
            0,
            FUiScale::Apply(kMarkerLineThickness) * Scale);
    }
}

void FThemeColorPicker::Synchronize(
    const FUserSettings::FThemeColor& Color,
    FThemeColorPickerState& State)
{
    UpdateHsvFromColor(Color, State);
    FormatHexColor(Color, State);
}

ImVec2 FThemeColorPicker::CalculateFittingSize(const ImVec2& AvailableSize)
{
    const float referenceWidth = FUiScale::Apply(kReferencePickerWidth);
    const float referenceHeight = referenceWidth * kSaturationValueHeightRatio
        + FUiScale::Apply(kHueBarHeight + kSectionGap * 2.0f)
        + ImGui::GetFrameHeight();
    const float scale = std::max(0.0f, std::min(
        AvailableSize.x / referenceWidth,
        (AvailableSize.y - FUiScale::Apply(kLayoutSafetyMargin)) / referenceHeight));
    return ImVec2(referenceWidth * scale, referenceHeight * scale);
}

bool FThemeColorPicker::Render(
    const char* Id,
    FUserSettings::FThemeColor& Color,
    FThemeColorPickerState& State,
    float Width)
{
    if (Width <= 0.0f)
    {
        return false;
    }

    if (!State.bInitialized || State.LastColor != Color)
    {
        Synchronize(Color, State);
    }

    ImGui::PushID(Id);

    // 整个控件使用同一比例，不能只拉大饱和度区域；局部字体和样式在返回前恢复。
    const float pickerScale = Width / FUiScale::Apply(kReferencePickerWidth);
    const float previousFontScale = ImGui::GetCurrentWindow()->FontWindowScale;
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
        ImVec2(style.FramePadding.x * pickerScale, style.FramePadding.y * pickerScale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
        ImVec2(style.ItemSpacing.x * pickerScale, style.ItemSpacing.y * pickerScale));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, style.FrameRounding * pickerScale);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, style.FrameBorderSize * pickerScale);
    ImGui::SetWindowFontScale(previousFontScale * pickerScale);

    bool bChanged = false;
    const float pickerWidth = Width;
    const float saturationValueHeight =
        pickerWidth * kSaturationValueHeightRatio;
    const float hueBarHeight = FUiScale::Apply(kHueBarHeight) * pickerScale;
    const float sectionGap = FUiScale::Apply(kSectionGap) * pickerScale;
    const ImU32 borderColor = ImGui::GetColorU32(ImGuiCol_Border);
    ImVec4 maskColorValue = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
    maskColorValue.w = 1.0f;
    const ImU32 maskColor = ImGui::ColorConvertFloat4ToU32(maskColorValue);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImVec2 saturationValueMin = ImGui::GetCursorScreenPos();
    const ImVec2 saturationValueMax(
        saturationValueMin.x + pickerWidth,
        saturationValueMin.y + saturationValueHeight);
    ImGui::InvisibleButton(
        "##SaturationValue",
        ImVec2(pickerWidth, saturationValueHeight));

    if (ImGui::IsItemActive())
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        State.Saturation = std::clamp(
            (mouse.x - saturationValueMin.x) / pickerWidth,
            0.0f,
            1.0f);
        State.Value = 1.0f - std::clamp(
            (mouse.y - saturationValueMin.y) / saturationValueHeight,
            0.0f,
            1.0f);
        UpdateColorFromHsv(Color, State);
        bChanged = true;
    }

    ImVec4 hueColor(0.0f, 0.0f, 0.0f, 1.0f);
    ImGui::ColorConvertHSVtoRGB(
        State.Hue,
        1.0f,
        1.0f,
        hueColor.x,
        hueColor.y,
        hueColor.z);
    const ImU32 hue = ImGui::ColorConvertFloat4ToU32(hueColor);
    drawList->AddRectFilledMultiColor(
        saturationValueMin,
        saturationValueMax,
        IM_COL32_WHITE,
        hue,
        hue,
        IM_COL32_WHITE);
    drawList->AddRectFilledMultiColor(
        saturationValueMin,
        saturationValueMax,
        IM_COL32(0, 0, 0, 0),
        IM_COL32(0, 0, 0, 0),
        IM_COL32(0, 0, 0, 255),
        IM_COL32(0, 0, 0, 255));
    const float saturationValueRounding =
        FUiScale::Apply(kSaturationValueRounding) * pickerScale;
    MaskRoundedCorners(
        drawList,
        saturationValueMin,
        saturationValueMax,
        saturationValueRounding,
        maskColor);
    drawList->AddRect(
        saturationValueMin,
        saturationValueMax,
        borderColor,
        saturationValueRounding,
        ImDrawFlags_RoundCornersAll,
        FUiScale::Apply(kPickerBorderThickness) * pickerScale);
    const float markerRadius = FUiScale::Apply(kMarkerRadius) * pickerScale;
    DrawMarker(
        drawList,
        ImVec2(
            std::clamp(
                saturationValueMin.x + State.Saturation * pickerWidth,
                saturationValueMin.x + markerRadius,
                saturationValueMax.x - markerRadius),
            std::clamp(
                saturationValueMin.y +
                    (1.0f - State.Value) * saturationValueHeight,
                saturationValueMin.y + markerRadius,
                saturationValueMax.y - markerRadius)),
        pickerScale);

    const ImVec2 hueMin(
        saturationValueMin.x,
        saturationValueMax.y + sectionGap);
    const ImVec2 hueMax(hueMin.x + pickerWidth, hueMin.y + hueBarHeight);
    ImGui::SetCursorScreenPos(hueMin);
    ImGui::InvisibleButton("##Hue", ImVec2(pickerWidth, hueBarHeight));

    if (ImGui::IsItemActive())
    {
        State.Hue = std::clamp(
            (ImGui::GetIO().MousePos.x - hueMin.x) / pickerWidth,
            0.0f,
            1.0f);
        UpdateColorFromHsv(Color, State);
        bChanged = true;
    }

    const float hueSegmentWidth = pickerWidth /
        static_cast<float>(kHueColorStopCount - 1);
    for (size_t stopIndex = 0;
         stopIndex + 1 < kHueColorStopCount;
         ++stopIndex)
    {
        const ImVec2 segmentMin(
            hueMin.x + hueSegmentWidth * static_cast<float>(stopIndex),
            hueMin.y);
        const ImVec2 segmentMax(
            stopIndex + 2 == kHueColorStopCount
                ? hueMax.x
                : segmentMin.x + hueSegmentWidth,
            hueMax.y);
        drawList->AddRectFilledMultiColor(
            segmentMin,
            segmentMax,
            kHueColors[stopIndex],
            kHueColors[stopIndex + 1],
            kHueColors[stopIndex + 1],
            kHueColors[stopIndex]);
    }

    const float hueBarRounding = FUiScale::Apply(kHueBarRounding) * pickerScale;
    MaskRoundedCorners(
        drawList,
        hueMin,
        hueMax,
        hueBarRounding,
        maskColor);
    drawList->AddRect(
        hueMin,
        hueMax,
        borderColor,
        hueBarRounding,
        ImDrawFlags_RoundCornersAll,
        FUiScale::Apply(kPickerBorderThickness) * pickerScale);
    DrawMarker(
        drawList,
        ImVec2(
            std::clamp(
                hueMin.x + State.Hue * pickerWidth,
                hueMin.x + markerRadius,
                hueMax.x - markerRadius),
            (hueMin.y + hueMax.y) * 0.5f),
        pickerScale);

    ImGui::SetCursorScreenPos(ImVec2(hueMin.x, hueMax.y + sectionGap));
    const float swatchWidth = FUiScale::Apply(kBottomSwatchWidth) * pickerScale;
    const float hexInputWidth = std::max(
        1.0f,
        pickerWidth - swatchWidth - ImGui::GetStyle().ItemSpacing.x);
    ImGui::SetNextItemWidth(hexInputWidth);
    const bool bHexEdited = ImGui::InputText(
        "##Hex",
        State.HexText.data(),
        State.HexText.size(),
        ImGuiInputTextFlags_CallbackCharFilter,
        FilterHexCharacter);

    if (bHexEdited)
    {
        FUserSettings::FThemeColor parsedColor;

        if (TryParseHexColor(State.HexText.data(), parsedColor))
        {
            Color = parsedColor;
            UpdateHsvFromColor(Color, State);
            bChanged = true;
        }
    }

    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        // 输入未满六位或包含多余 # 时恢复最近一个有效值，避免留下“看似已应用”的坏文本。
        FormatHexColor(Color, State);
    }

    ImGui::SameLine();
    ImGui::ColorButton(
        "##CurrentColor",
        FUiTheme::ToImGuiColor(Color),
        ImGuiColorEditFlags_NoAlpha |
            ImGuiColorEditFlags_NoTooltip |
            ImGuiColorEditFlags_NoDragDrop |
            ImGuiColorEditFlags_NoBorder,
        ImVec2(swatchWidth, ImGui::GetFrameHeight()));

    State.LastColor = Color;
    ImGui::SetWindowFontScale(previousFontScale);
    ImGui::PopStyleVar(kPickerStyleVarCount);
    ImGui::PopID();
    return bChanged;
}
