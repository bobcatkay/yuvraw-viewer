#include "Core/FLocalization.h"
#include "FExportPanel.h"

#include "Core/FFileDialog.h"
#include "FUiScale.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <string>

namespace
{
    /// 弹窗标题兼 ImGui 的 popup 标识，OpenPopup 与 BeginPopupModal 必须用同一个串


    constexpr const char* kFormatNames[] = { "PNG (.png)", "JPEG (.jpg)", "BMP (.bmp)", "WebP (.webp)" };
    constexpr const char* kColorSpaceNames[] = { "BT.601", "BT.709", "BT.2020" };
    constexpr const char* kColorRangeNames[] = { "Limited", "Full" };

    // 与“关于”弹窗使用同一套尺寸、留白与青绿色阶，保持模态界面的视觉一致性。
    constexpr float kPopupPreferredWidth = 640.0f;
    constexpr float kPopupPreferredHeight = 820.0f;
    constexpr float kPopupViewportMargin = 24.0f;
    constexpr float kHorizontalPadding = 28.0f;
    constexpr float kVerticalPadding = 24.0f;
    constexpr float kPopupRounding = 10.0f;
    constexpr float kFrameHorizontalPadding = 14.0f;
    constexpr float kFrameVerticalPadding = 7.0f;
    constexpr float kItemSpacingY = 10.0f;
    constexpr float kTitleScale = 1.4f;
    constexpr float kHeaderAdditionalGap = 8.0f;
    constexpr float kSectionGap = 16.0f;
    constexpr float kBadgeHorizontalPadding = 14.0f;
    constexpr float kBadgeVerticalPadding = 6.0f;
    constexpr float kBadgeRounding = 12.0f;
    constexpr float kFormLabelWidth = 92.0f;
    constexpr float kCompactInputWidth = 150.0f;
    constexpr float kBrowseButtonWidth = 112.0f;
    constexpr float kPrimaryButtonWidth = 120.0f;
    constexpr float kSecondaryButtonWidth = 88.0f;
    constexpr int32_t kPopupStyleVarCount = 4;
    constexpr int32_t kPrimaryButtonColorCount = 4;
    constexpr int32_t kSecondaryButtonColorCount = 5;
    constexpr float kCenterRatio = 0.5f;

    constexpr int32_t kMinimumJpegQuality = 1;
    constexpr int32_t kMaximumJpegQuality = 100;
    constexpr float kMinimumResizePercent = 1.0f;
    constexpr float kMaximumResizePercent = 800.0f;
    constexpr int32_t kMinimumTargetWidth = 1;
    constexpr int32_t kMaximumTargetWidth = 65536;
    constexpr size_t kMaximumTooltipSourceCount = 12;
    constexpr size_t kMaximumVisibleResultCount = 8;

    constexpr ImU32 kAccentColor = IM_COL32(16, 87, 100, 255);
    constexpr ImU32 kSubtitleColor = IM_COL32(92, 92, 92, 255);
    constexpr ImU32 kBadgeBackground = IM_COL32(30, 140, 148, 255);
    constexpr ImU32 kBadgeBorder = IM_COL32(16, 87, 100, 255);
    constexpr ImU32 kBadgeText = IM_COL32(255, 255, 255, 255);
    constexpr ImU32 kSecondaryButton = IM_COL32(226, 232, 240, 255);
    constexpr ImU32 kSecondaryButtonHovered = IM_COL32(203, 213, 225, 255);
    constexpr ImU32 kSecondaryButtonActive = IM_COL32(183, 196, 211, 255);
    constexpr ImU32 kSecondaryButtonBorder = IM_COL32(78, 158, 166, 255);
    constexpr ImU32 kPrimaryButton = IM_COL32(30, 140, 148, 255);
    constexpr ImU32 kPrimaryButtonHovered = IM_COL32(24, 113, 125, 255);
    constexpr ImU32 kPrimaryButtonActive = IM_COL32(16, 87, 100, 255);
    constexpr ImU32 kPrimaryButtonText = IM_COL32(255, 255, 255, 255);
    constexpr ImU32 kSuccessText = IM_COL32(38, 140, 64, 255);
    constexpr ImU32 kErrorText = IM_COL32(217, 64, 64, 255);

    /**
     * 弹窗优先使用设计尺寸；小窗口下收进主视口工作区，正文改为滚动而不是把操作区挤出屏幕。
     */
    void ConfigureNextPopup(const ImGuiViewport* Viewport)
    {
        ImVec2 popupSize(
            FUiScale::Apply(kPopupPreferredWidth),
            FUiScale::Apply(kPopupPreferredHeight));

        if (Viewport)
        {
            const float viewportMargin = FUiScale::Apply(kPopupViewportMargin);
            popupSize.x = std::min(
                popupSize.x,
                std::max(1.0f, Viewport->WorkSize.x - viewportMargin * 2.0f));
            popupSize.y = std::min(
                popupSize.y,
                std::max(1.0f, Viewport->WorkSize.y - viewportMargin * 2.0f));
        }

        ImGui::SetNextWindowSize(popupSize, ImGuiCond_Appearing);

        if (!Viewport)
        {
            return;
        }

        // 与“关于”弹窗一致，固定在主视口，避免多视口模式下出现独立且不可见的模态系统窗口。
        ImGui::SetNextWindowViewport(Viewport->ID);
        ImGui::SetNextWindowPos(
            ImVec2(
                Viewport->WorkPos.x + Viewport->WorkSize.x * kCenterRatio,
                Viewport->WorkPos.y + Viewport->WorkSize.y * kCenterRatio),
            ImGuiCond_Appearing,
            ImVec2(kCenterRatio, kCenterRatio));
    }

    void PushPrimaryButtonStyle()
    {
        ImGui::PushStyleColor(ImGuiCol_Button, kPrimaryButton);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kPrimaryButtonHovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kPrimaryButtonActive);
        ImGui::PushStyleColor(ImGuiCol_Text, kPrimaryButtonText);
    }

    void PopPrimaryButtonStyle()
    {
        ImGui::PopStyleColor(kPrimaryButtonColorCount);
    }

    void PushSecondaryButtonStyle()
    {
        ImGui::PushStyleColor(ImGuiCol_Button, kSecondaryButton);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kSecondaryButtonHovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kSecondaryButtonActive);
        ImGui::PushStyleColor(ImGuiCol_Border, kSecondaryButtonBorder);
        ImGui::PushStyleColor(ImGuiCol_Text, kAccentColor);
        ImGui::PushStyleVar(
            ImGuiStyleVar_FrameBorderSize,
            FUiScale::Apply(1.0f));
    }

    void PopSecondaryButtonStyle()
    {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(kSecondaryButtonColorCount);
    }

    void RenderCenteredWrappedText(const char* Text, ImU32 Color)
    {
        const float contentWidth = ImGui::GetContentRegionAvail().x;
        const ImVec2 textSize = ImGui::CalcTextSize(Text);
        const float cursorX = ImGui::GetCursorPosX();

        if (textSize.x < contentWidth)
        {
            ImGui::SetCursorPosX(cursorX + (contentWidth - textSize.x) * kCenterRatio);
        }

        ImGui::PushStyleColor(ImGuiCol_Text, Color);
        ImGui::TextWrapped("%s", Text);
        ImGui::PopStyleColor();
    }

    void RenderSectionHeading(const char* Label)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, kAccentColor);
        ImGui::TextUnformatted(Label);
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    bool BeginFormTable(const char* Id)
    {
        if (!ImGui::BeginTable(
                Id,
                2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
        {
            return false;
        }

        ImGui::TableSetupColumn(
            "##Label",
            ImGuiTableColumnFlags_WidthFixed,
            FUiScale::Apply(kFormLabelWidth));
        ImGui::TableSetupColumn(
            "##Value",
            ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    void RenderFormLabel(const char* Label)
    {
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(Label);
        ImGui::TableSetColumnIndex(1);
    }
}

FExportPanel::FExportPanel()
    : SourceFormat(EImageFormat::Unknown)
    , SourceWidth(0)
    , SourceHeight(0)
    , bRequestOpen(false)
    , bIsOpen(false)
    , bResetBodyScroll(false)
    , bScrollToResult(false)
{
}

void FExportPanel::SetResult(const FExportResult& InResult)
{
    Result = InResult;
    bScrollToResult = true;

    if (!bIsOpen)
    {
        bRequestOpen = true;
    }
}

void FExportPanel::Open(
    const FExportRequest& InRequest,
    const std::string& DefaultDirectory,
    EImageFormat InSourceFormat,
    int32_t InSourceWidth,
    int32_t InSourceHeight,
    const FDisplaySettings& Display)
{
    Request = InRequest;
    SourceFormat = InSourceFormat;
    SourceWidth = InSourceWidth;
    SourceHeight = InSourceHeight;

    // 整套显示设置都沿用这幅图当前的：用户在属性面板调好的解读方式，
    // 导出时默认就该保持一致，否则导出的颜色和屏幕上看到的对不上。
    //
    // 传输函数与色调映射尤其不能漏 —— 一帧 PQ 素材若按原始码值落盘，
    // 导出的 PNG 会是那张灰蒙蒙的图，和查看器里看到的完全两回事
    Settings.Display = Display;

    Settings.OutputDirectory = DefaultDirectory;
    Settings.TargetWidth = InSourceWidth;

    Result = FExportResult();

    bResetBodyScroll = true;
    bScrollToResult = false;
    bRequestOpen = true;
}

void FExportPanel::RenderSourceSection()
{
    const char* title = FLocalization::Text(EUiText::ExportImage);
    const float contentWidth = ImGui::GetContentRegionAvail().x;
    const float titleFontSize = ImGui::GetFontSize() * kTitleScale;
    const ImVec2 titleSize = ImGui::GetFont()->CalcTextSizeA(
        titleFontSize,
        FLT_MAX,
        0.0f,
        title);
    const ImVec2 titleCursor = ImGui::GetCursorScreenPos();
    const ImVec2 titlePos(
        titleCursor.x + std::max(0.0f, (contentWidth - titleSize.x) * kCenterRatio),
        titleCursor.y);

    ImGui::GetWindowDrawList()->AddText(
        ImGui::GetFont(),
        titleFontSize,
        titlePos,
        kAccentColor,
        title);
    ImGui::Dummy(ImVec2(contentWidth, titleSize.y));

    std::string subtitle;

    if (Request.SourcePaths.empty())
    {
        subtitle = FLocalization::Text(EUiText::NoExportSelection);
    }
    else if (Request.SourcePaths.size() == 1)
    {
        subtitle = std::filesystem::u8path(Request.SourcePaths.front()).filename().u8string();
    }
    else
    {
        subtitle = FLocalization::Text(EUiText::SelectedPrefix) + std::to_string(Request.SourcePaths.size()) + FLocalization::Text(EUiText::FilesSuffix);
    }

    RenderCenteredWrappedText(subtitle.c_str(), kSubtitleColor);

    ImGui::SetCursorPosY(
        ImGui::GetCursorPosY() + FUiScale::Apply(kHeaderAdditionalGap));

    std::string badgeText;

    if (Request.SourcePaths.empty())
    {
        badgeText = FLocalization::Text(EUiText::NoSourceFile);
    }
    else if (Request.SourcePaths.size() > 1)
    {
        badgeText = std::to_string(Request.SourcePaths.size()) + FLocalization::Text(EUiText::FilesSuffix);

        if (SourceWidth > 0 && SourceHeight > 0)
        {
            badgeText += FLocalization::Text(EUiText::FirstImagePrefix);
            badgeText += std::to_string(SourceWidth);
            badgeText += u8" × ";
            badgeText += std::to_string(SourceHeight);
        }
    }
    else if (SourceWidth > 0 && SourceHeight > 0)
    {
        badgeText = FLocalization::Text(EUiText::OriginalSizePrefix);
        badgeText += std::to_string(SourceWidth);
        badgeText += u8" × ";
        badgeText += std::to_string(SourceHeight);
    }
    else
    {
        badgeText = FLocalization::Text(EUiText::WaitingSourceInfo);
    }

    const ImVec2 badgeTextSize = ImGui::CalcTextSize(badgeText.c_str());
    const ImVec2 badgeSize(
        badgeTextSize.x + FUiScale::Apply(kBadgeHorizontalPadding) * 2.0f,
        badgeTextSize.y + FUiScale::Apply(kBadgeVerticalPadding) * 2.0f);
    const ImVec2 badgeCursor = ImGui::GetCursorScreenPos();
    const ImVec2 badgeMin(
        badgeCursor.x + std::max(0.0f, (contentWidth - badgeSize.x) * kCenterRatio),
        badgeCursor.y);
    const ImVec2 badgeMax(
        badgeMin.x + badgeSize.x,
        badgeMin.y + badgeSize.y);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        badgeMin,
        badgeMax,
        kBadgeBackground,
        FUiScale::Apply(kBadgeRounding));
    drawList->AddRect(
        badgeMin,
        badgeMax,
        kBadgeBorder,
        FUiScale::Apply(kBadgeRounding));
    drawList->AddText(
        ImVec2(
            badgeMin.x + FUiScale::Apply(kBadgeHorizontalPadding),
            badgeMin.y + FUiScale::Apply(kBadgeVerticalPadding)),
        kBadgeText,
        badgeText.c_str());
    ImGui::Dummy(ImVec2(contentWidth, badgeSize.y));

    if (ImGui::IsItemHovered() && !Request.SourcePaths.empty())
    {
        std::string tooltip;
        const size_t shown = std::min(
            Request.SourcePaths.size(),
            kMaximumTooltipSourceCount);

        for (size_t i = 0; i < shown; ++i)
        {
            tooltip += std::filesystem::u8path(Request.SourcePaths[i]).filename().u8string();
            tooltip += "\n";
        }

        if (shown < Request.SourcePaths.size())
        {
            tooltip += u8"…";
        }

        ImGui::SetTooltip("%s", tooltip.c_str());
    }
}

void FExportPanel::RenderFormatSection()
{
    RenderSectionHeading(FLocalization::Text(EUiText::FileFormat));

    if (!BeginFormTable("##ExportFormatTable"))
    {
        return;
    }

    ImGui::TableNextRow();
    RenderFormLabel(FLocalization::Text(EUiText::Format));

    int32_t formatIndex = static_cast<int32_t>(Settings.Format);
    ImGui::SetNextItemWidth(-FLT_MIN);

    if (ImGui::Combo(u8"##ExportFormat", &formatIndex, kFormatNames, IM_ARRAYSIZE(kFormatNames)))
    {
        Settings.Format = static_cast<EExportFormat>(formatIndex);
    }

    if (Settings.Format == EExportFormat::WEBP && ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            FLocalization::Text(EUiText::WebpEncodingHelp));
    }

    ImGui::TableNextRow();
    RenderFormLabel(FLocalization::Text(EUiText::EncodingQuality));

    if (Settings.Format == EExportFormat::JPEG)
    {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt(
            u8"##ExportQuality",
            &Settings.JpegQuality,
            kMinimumJpegQuality,
            kMaximumJpegQuality);
    }
    else
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled(FLocalization::Text(EUiText::LosslessQualityHelp));
    }

    ImGui::EndTable();
}

void FExportPanel::RenderColorMatrixSection()
{
    // 源不是 YUV 时矩阵不参与任何计算，置灰而不是隐藏 —— 让用户看得见“这项不适用”。
    const bool bEnabled = FImageExporter::NeedsColorMatrix(SourceFormat, Settings.Format);

    RenderSectionHeading(FLocalization::Text(EUiText::Color));
    ImGui::BeginDisabled(!bEnabled);

    if (BeginFormTable("##ExportColorTable"))
    {
        ImGui::TableNextRow();
        RenderFormLabel(FLocalization::Text(EUiText::ColorStandard));

        int32_t colorSpaceIndex = static_cast<int32_t>(Settings.Display.ColorSpace);
        ImGui::SetNextItemWidth(-FLT_MIN);

        if (ImGui::Combo(
                u8"##ExportColorSpace",
                &colorSpaceIndex,
                kColorSpaceNames,
                IM_ARRAYSIZE(kColorSpaceNames)))
        {
            Settings.Display.ColorSpace = static_cast<EColorSpace>(colorSpaceIndex);
        }

        ImGui::TableNextRow();
        RenderFormLabel(FLocalization::Text(EUiText::ColorRange));

        int32_t colorRangeIndex = static_cast<int32_t>(Settings.Display.ColorRange);
        ImGui::SetNextItemWidth(-FLT_MIN);

        if (ImGui::Combo(
                u8"##ExportColorRange",
                &colorRangeIndex,
                kColorRangeNames,
                IM_ARRAYSIZE(kColorRangeNames)))
        {
            Settings.Display.ColorRange = static_cast<EColorRange>(colorRangeIndex);
        }

        ImGui::EndTable();
    }

    ImGui::EndDisabled();

    if (!bEnabled)
    {
        ImGui::TextDisabled(FLocalization::Text(EUiText::NonYuvMatrixHelp));
    }

    // 另外两轴（原色 / 传输函数）与色调映射沿用属性面板的设置，这里只读展示。
    // 它们确实参与导出计算，必须明确显示，避免用户误以为导出只跟矩阵有关。
    if (Settings.Display.Transfer != EColorTransfer::SDR
        || Settings.Display.Primaries != EColorPrimaries::BT709)
    {
        static constexpr const char* kPrimariesShort[] = {
            "BT.709", "BT.2020", "P3-D65", "BT.601-525", "BT.601-625"
        };
        static constexpr const char* kTransferShort[] = {
            "SDR", "BT.1886", "PQ", "HLG", "Linear"
        };
        const char* kToneMapShort[] = {
            FLocalization::Text(EUiText::Clip), "Reinhard", "ACES"
        };

        ImGui::TextDisabled(
            FLocalization::Text(EUiText::ExportColorSummary),
            kPrimariesShort[static_cast<int32_t>(Settings.Display.Primaries)],
            kTransferShort[static_cast<int32_t>(Settings.Display.Transfer)],
            kToneMapShort[static_cast<int32_t>(Settings.Display.ToneMap)]);
    }
}

void FExportPanel::RenderResolutionSection()
{
    RenderSectionHeading(FLocalization::Text(EUiText::OutputSize));

    int32_t mode = static_cast<int32_t>(Settings.ResizeMode);

    if (ImGui::RadioButton(FLocalization::Text(EUiText::OriginalSize), &mode, 0))
    {
        Settings.ResizeMode = EExportResizeMode::Original;
    }

    ImGui::SameLine();

    if (ImGui::RadioButton(FLocalization::Text(EUiText::ScaleByPercent), &mode, 1))
    {
        Settings.ResizeMode = EExportResizeMode::Percent;
    }

    ImGui::SameLine();

    if (ImGui::RadioButton(FLocalization::Text(EUiText::ScaleByWidth), &mode, 2))
    {
        Settings.ResizeMode = EExportResizeMode::Width;
    }

    if (BeginFormTable("##ExportResolutionTable"))
    {
        if (Settings.ResizeMode == EExportResizeMode::Percent)
        {
            ImGui::TableNextRow();
            RenderFormLabel(FLocalization::Text(EUiText::ScaleFactor));
            ImGui::SetNextItemWidth(FUiScale::Apply(kCompactInputWidth));
            ImGui::DragFloat(
                u8"##ExportPercent",
                &Settings.Percent,
                1.0f,
                kMinimumResizePercent,
                kMaximumResizePercent,
                "%.1f %%",
                ImGuiSliderFlags_AlwaysClamp);
        }
        else if (Settings.ResizeMode == EExportResizeMode::Width)
        {
            ImGui::TableNextRow();
            RenderFormLabel(FLocalization::Text(EUiText::TargetWidth));
            ImGui::SetNextItemWidth(FUiScale::Apply(kCompactInputWidth));
            ImGui::DragInt(
                u8"##ExportWidth",
                &Settings.TargetWidth,
                1.0f,
                kMinimumTargetWidth,
                kMaximumTargetWidth,
                "%d px",
                ImGuiSliderFlags_AlwaysClamp);
        }

        ImGui::TableNextRow();
        RenderFormLabel(FLocalization::Text(EUiText::ExportAs));

        if (SourceWidth > 0 && SourceHeight > 0)
        {
            int32_t outWidth = 0;
            int32_t outHeight = 0;
            FImageExporter::GetTargetSize(
                Settings,
                SourceWidth,
                SourceHeight,
                outWidth,
                outHeight);

            ImGui::PushStyleColor(ImGuiCol_Text, kAccentColor);

            if (Request.SourcePaths.size() > 1)
            {
                ImGui::Text(FLocalization::Text(EUiText::FirstOutputDimensions), outWidth, outHeight);
            }
            else
            {
                ImGui::Text(u8"%d × %d px", outWidth, outHeight);
            }

            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::TextDisabled(FLocalization::Text(EUiText::WaitingSourceSize));
        }

        ImGui::EndTable();
    }

    if (Settings.ResizeMode != EExportResizeMode::Original)
    {
        ImGui::TextDisabled(FLocalization::Text(EUiText::AspectRatioHelp));
    }
}

void FExportPanel::RenderDirectorySection()
{
    RenderSectionHeading(FLocalization::Text(EUiText::SaveLocation));

    PushSecondaryButtonStyle();
    const bool bBrowse = ImGui::Button(
        FLocalization::Text(EUiText::ChooseDirectoryAction),
        ImVec2(FUiScale::Apply(kBrowseButtonWidth), 0.0f));
    PopSecondaryButtonStyle();

    if (bBrowse)
    {
        std::string path;

        if (FFileDialog::OpenDirectory(FLocalization::Text(EUiText::ChooseSaveDirectory), path))
        {
            Settings.OutputDirectory = path;
        }
    }

    ImGui::SameLine();
    ImGui::Checkbox(FLocalization::Text(EUiText::OverwriteFiles), &Settings.bOverwrite);

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(FLocalization::Text(EUiText::OverwriteFilesHelp));
    }

    ImGui::TextDisabled(FLocalization::Text(EUiText::SaveTo));

    if (Settings.OutputDirectory.empty())
    {
        ImGui::TextWrapped(FLocalization::Text(EUiText::SourceDirectory));
    }
    else
    {
        ImGui::TextWrapped("%s", Settings.OutputDirectory.c_str());
    }
}

void FExportPanel::RenderResultSection()
{
    if (Result.Text.empty())
    {
        return;
    }

    RenderSectionHeading(FLocalization::Text(EUiText::ExportResults));
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        Result.bHasError ? kErrorText : kSuccessText);
    ImGui::TextWrapped("%s", Result.Text.c_str());
    ImGui::PopStyleColor();

    // 文件多时只列前几个：正文区域可滚动，但结果仍应保持简洁。
    const size_t shown = std::min(
        Result.OutputPaths.size(),
        kMaximumVisibleResultCount);

    for (size_t i = 0; i < shown; ++i)
    {
        const std::string& path = Result.OutputPaths[i];
        const std::string name = std::filesystem::u8path(path).filename().u8string();

        // 不同目录下可能有同名文件，而 TextLink 拿标签当 ID。
        ImGui::PushID(static_cast<int>(i));

        if (ImGui::TextLink(name.c_str()))
        {
            FFileDialog::RevealInExplorer(path);
        }

        ImGui::PopID();

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(FLocalization::Text(EUiText::RevealExportHelp), path.c_str());
        }
    }

    if (shown < Result.OutputPaths.size())
    {
        ImGui::TextDisabled(
            FLocalization::Text(EUiText::MoreResults),
            static_cast<int>(Result.OutputPaths.size() - shown));
    }
}

void FExportPanel::Render()
{
    if (bRequestOpen)
    {
        ImGui::OpenPopup(FLocalization::WindowTitle(EUiText::ExportImage));
        bRequestOpen = false;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ConfigureNextPopup(viewport);

    const ImGuiStyle& baseStyle = ImGui::GetStyle();
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        FUiScale::Apply(kHorizontalPadding, kVerticalPadding));
    ImGui::PushStyleVar(
        ImGuiStyleVar_PopupRounding,
        FUiScale::Apply(kPopupRounding));
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        FUiScale::Apply(kFrameHorizontalPadding, kFrameVerticalPadding));
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(baseStyle.ItemSpacing.x, FUiScale::Apply(kItemSpacingY)));

    if (!ImGui::BeginPopupModal(
            FLocalization::WindowTitle(EUiText::ExportImage),
            nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        bIsOpen = false;
        ImGui::PopStyleVar(kPopupStyleVarCount);
        return;
    }

    bIsOpen = true;

    RenderSourceSection();
    ImGui::SetCursorPosY(
        ImGui::GetCursorPosY() + FUiScale::Apply(kHeaderAdditionalGap));
    ImGui::Separator();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float footerHeight = ImGui::GetFrameHeight() + style.ItemSpacing.y;

    // 固定标题与操作区，只有设置正文滚动；批量结果再多也不会改变弹窗尺寸或按钮位置。
    if (ImGui::BeginChild(
            "##ExportSettingsBody",
            ImVec2(0.0f, -footerHeight),
            0))
    {
        if (bResetBodyScroll)
        {
            ImGui::SetScrollY(0.0f);
            bResetBodyScroll = false;
        }

        RenderFormatSection();
        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() + FUiScale::Apply(kSectionGap));

        RenderColorMatrixSection();
        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() + FUiScale::Apply(kSectionGap));

        RenderResolutionSection();
        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() + FUiScale::Apply(kSectionGap));

        RenderDirectorySection();

        if (!Result.Text.empty())
        {
            ImGui::SetCursorPosY(
                ImGui::GetCursorPosY() + FUiScale::Apply(kSectionGap));
            RenderResultSection();
        }

        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() + FUiScale::Apply(kSectionGap));
        ImGui::TextDisabled(FLocalization::Text(EUiText::BackgroundExportHelp));

        if (bScrollToResult && !Result.Text.empty())
        {
            ImGui::SetScrollHereY(1.0f);
            bScrollToResult = false;
        }
    }
    ImGui::EndChild();

    ImGui::Separator();

    const float footerWidth = FUiScale::Apply(kSecondaryButtonWidth)
        + style.ItemSpacing.x
        + FUiScale::Apply(kPrimaryButtonWidth);
    ImGui::SetCursorPosX(std::max(
        style.WindowPadding.x,
        ImGui::GetWindowWidth() - style.WindowPadding.x - footerWidth));

    PushSecondaryButtonStyle();
    const bool bCloseClicked = ImGui::Button(
        FLocalization::Text(EUiText::Close),
        ImVec2(FUiScale::Apply(kSecondaryButtonWidth), 0.0f));
    PopSecondaryButtonStyle();

    if (bCloseClicked)
    {
        bIsOpen = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    const bool bCanExport = !Request.SourcePaths.empty() && OnExport != nullptr;
    ImGui::BeginDisabled(!bCanExport);
    PushPrimaryButtonStyle();
    const bool bExportClicked = ImGui::Button(
        FLocalization::Text(EUiText::StartExport),
        ImVec2(FUiScale::Apply(kPrimaryButtonWidth), 0.0f));
    PopPrimaryButtonStyle();
    ImGui::EndDisabled();

    if (bExportClicked)
    {
        // 这里只发起后台任务。先清掉上次结果，避免新一轮处理期间仍显示旧的“导出完成”。
        Result = FExportResult();
        bScrollToResult = false;
        OnExport(Settings, Request);
    }

    ImGui::EndPopup();
    ImGui::PopStyleVar(kPopupStyleVarCount);
}
