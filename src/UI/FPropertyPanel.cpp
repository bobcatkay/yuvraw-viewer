#include "Core/FLocalization.h"
#include "FPropertyPanel.h"
#include "FUiScale.h"
#include "FUiTheme.h"
#include "Core/FHdrPresenter.h"
#include "Core/FUserSettings.h"
#include "Image/FColorTransform.h"
#include "Image/FImageFormatDesc.h"
#include "Image/FImageLimits.h"
#include "Image/FImageLoadParams.h"
#include "Image/FResolutionGuess.h"
#include "FToast.h"
#include "Util.h"
#include <imgui.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace
{
    const ImVec4 kPanelTabTextColor(1.0f, 1.0f, 1.0f, 1.0f);
    constexpr int32_t kPanelTabStyleColorCount = 5;

    // 两组显示属性共用 HDR 下拉框的宽度基准，随当前字体与 DPI 一起缩放。
    constexpr const char* kDisplayComboWidthText = "BT.601-525_______";
    constexpr float kHlgPeakDragSpeedNits = 10.0f;
    constexpr float kMinimumHlgPeakNits = 100.0f;
    constexpr float kMaximumHlgPeakNits = 10000.0f;

    /// 与文件大小对不上时的橙色，与 RenderFileInfo 里的提示同色
    const ImVec4 kWarnColor(0.80f, 0.50f, 0.05f, 1.0f);

    /// 候选下拉框里右侧标注那一列的横向位置
    constexpr float kSizeColumnX = 130.0f;

    /// 最多列几条候选。再多也帮不上忙，只会让人挑花眼
    constexpr int32_t kMaxCandidates = 8;

    /// 图像格式下拉框最多显示 16 行，是 ImGui 默认约 8 行高度的两倍。
    constexpr int32_t kFormatSelectorPopupVisibleItemCount = 16;

    /// 属性面板的 stride 输入上限；覆盖超宽 RGBA16 行且保持输入框易用。
    constexpr int32_t kMaximumEditableStrideBytes = 1 << 20;

    /// 存储布局表：属性名、小端/低位、大端/高位
    constexpr int32_t kStorageLayoutColumnCount = 3;

    /// 保存预设弹窗沿用设置/导出弹窗的尺寸约束、留白、圆角与底部操作区。

    constexpr float kFormatPresetPopupWidth = 500.0f;
    constexpr float kFormatPresetPopupHeight = 300.0f;
    constexpr float kFormatPresetPopupViewportMargin = 24.0f;
    constexpr float kFormatPresetHorizontalPadding = 28.0f;
    constexpr float kFormatPresetVerticalPadding = 24.0f;
    constexpr float kFormatPresetPopupRounding = 10.0f;
    constexpr float kFormatPresetFrameHorizontalPadding = 14.0f;
    constexpr float kFormatPresetFrameVerticalPadding = 7.0f;
    constexpr float kFormatPresetItemSpacingY = 10.0f;
    constexpr float kFormatPresetSaveButtonWidth = 88.0f;
    constexpr float kFormatPresetCancelButtonWidth = 80.0f;
    constexpr int32_t kFormatPresetStyleVarCount = 4;

    const ImVec4 kFormatPresetErrorColor(0.85f, 0.25f, 0.25f, 1.0f);
    const ImVec4 kFormatPresetWarningColor(0.80f, 0.50f, 0.05f, 1.0f);

    bool AreLoadParamsEqual(
        const FImageLoadParams& Left,
        const FImageLoadParams& Right)
    {
        return Left.Format == Right.Format &&
            Left.Width == Right.Width &&
            Left.Height == Right.Height &&
            Left.Stride == Right.Stride &&
            Left.BitsPerPixel == Right.BitsPerPixel &&
            Left.BayerPattern == Right.BayerPattern &&
            Left.ByteOrder == Right.ByteOrder &&
            Left.SampleAlignment == Right.SampleAlignment;
    }

    std::string TrimAsciiWhitespace(const char* Text)
    {
        std::string result = Text ? Text : "";
        const auto isWhitespace = [](unsigned char Character)
        {
            return Character == ' ' || Character == '\t' ||
                Character == '\r' || Character == '\n';
        };

        const auto first = std::find_if_not(
            result.begin(),
            result.end(),
            isWhitespace);
        const auto last = std::find_if_not(
            result.rbegin(),
            result.rend(),
            isWhitespace).base();

        if (first >= last)
        {
            return {};
        }

        return std::string(first, last);
    }

    const char* ValidateFormatPresetName(const std::string& Name)
    {
        if (Name.empty())
        {
            return FLocalization::Text(EUiText::PresetNameEmpty);
        }

        if (Name.size() > FUserSettings::kMaximumImageFormatPresetNameBytes)
        {
            return FLocalization::Text(EUiText::PresetNameTooLong);
        }

        if (Name.find("##") != std::string::npos)
        {
            return FLocalization::Text(EUiText::PresetNameHashes);
        }

        for (unsigned char character : Name)
        {
            if (character < 0x20 || character == 0x7F)
            {
                return FLocalization::Text(EUiText::PresetNameControls);
            }
        }

        for (const FFormatDesc& desc : FImageFormatDesc::GetAll())
        {
            if (Name == desc.DisplayName || Name == FLocalization::Translate(desc.DisplayName, FLocalization::ELanguage::English) || Name == desc.Name)
            {
                return FLocalization::Text(EUiText::PresetNameReserved);
            }
        }

        return nullptr;
    }

    void ConfigureFormatPresetPopup(const ImGuiViewport* Viewport)
    {
        ImVec2 popupSize(
            FUiScale::Apply(kFormatPresetPopupWidth),
            FUiScale::Apply(kFormatPresetPopupHeight));

        if (Viewport)
        {
            const float margin =
                FUiScale::Apply(kFormatPresetPopupViewportMargin);
            popupSize.x = std::min(
                popupSize.x,
                std::max(1.0f, Viewport->WorkSize.x - margin * 2.0f));
            popupSize.y = std::min(
                popupSize.y,
                std::max(1.0f, Viewport->WorkSize.y - margin * 2.0f));
        }

        ImGui::SetNextWindowSize(popupSize, ImGuiCond_Appearing);

        if (!Viewport)
        {
            return;
        }

        ImGui::SetNextWindowViewport(Viewport->ID);
        ImGui::SetNextWindowPos(
            ImVec2(
                Viewport->WorkPos.x + Viewport->WorkSize.x * 0.5f,
                Viewport->WorkPos.y + Viewport->WorkSize.y * 0.5f),
            ImGuiCond_Appearing,
            ImVec2(0.5f, 0.5f));
    }
}

FPropertyPanel::FPropertyPanel()
    : CurrentImageData(nullptr)
    , SelectedFormat(EImageFormat::Unknown)
    , SelectedWidth(0)
    , SelectedHeight(0)
    , SelectedStride(0)
    , SelectedBitsPerPixel(8)
    , SelectedBayerPattern(EBayerPattern::RGGB)
    , SelectedByteOrder(EByteOrder::LittleEndian)
    , SelectedSampleAlignment(ESampleAlignment::LeastSignificantBits)
    , Target(EPropertyTarget::Main)
    , bCompareAvailable(false)
    , bParamsEditable(true)
    , FileSize(0)
    , ImageSize(0)
    , bRequestFormatPresetPopup(false)
    , bFocusFormatPresetNameInput(false)
    , CachedFormat(EImageFormat::Unknown)
    , CachedFileSize(0)
    , FilenameWidth(0)
    , FilenameHeight(0)
    , bFilenameExact(false)
{
    FormatPresetNameInput[0] = '\0';
}

FPropertyPanel::~FPropertyPanel()
{
}

void FPropertyPanel::Render()
{
    // ImGui 会在 Begin() 时把这些颜色存进窗口的 DockStyle，
    // 因此可只覆盖这个标签，不污染其它面板和控件。
    const ImVec4 selectedTabColor = FUiTheme::GetImGuiColor(
        FUserSettings::EThemeColorRole::Accent);
    ImGui::PushStyleColor(ImGuiCol_Text, kPanelTabTextColor);
    ImGui::PushStyleColor(ImGuiCol_TabSelected, selectedTabColor);
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, selectedTabColor);
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, selectedTabColor);
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelectedOverline, selectedTabColor);
    ImGui::Begin(FLocalization::WindowTitle(EUiText::Properties));
    ImGui::PopStyleColor(kPanelTabStyleColorCount);

    RenderTargetSelector();
    RenderImageInfo();
    ImGui::Separator();

    if (!bParamsEditable)
    {
        ImGui::TextDisabled(FLocalization::Text(EUiText::SelfDescribingFormatHelp));
    }

    // 自带文件头的格式改这些参数只会让加载失败，直接置灰而不是让用户白试一遍
    ImGui::BeginDisabled(!bParamsEditable);

    RenderFormatSelector();
    ImGui::Separator();
    RenderResolutionEditor();
    ImGui::Separator();
    RenderStrideEditor();
    ImGui::Separator();
    RenderBayerPatternSelector();
    RenderBitsPerPixelSelector();
    ImGui::Separator();
    RenderStorageLayoutSelectors();

    ImGui::EndDisabled();

    RenderFormatPresetPopup();

    ImGui::Separator();
    RenderDisplaySettings();

    ImGui::End();
}

void FPropertyPanel::RenderTargetSelector()
{
    // 只有一张图时没什么可选的，下拉框只会占地方
    if (!bCompareAvailable)
    {
        return;
    }

    ImGui::Text(FLocalization::Text(EUiText::EditingImage));

    const char* targetNames[] = { FLocalization::Text(EUiText::MainImage), FLocalization::Text(EUiText::CompareImage) };
    int32_t targetIndex = static_cast<int32_t>(Target);

    ImGui::SetNextItemWidth(-1.0f);

    if (ImGui::Combo(u8"##PropertyTarget", &targetIndex, targetNames, IM_ARRAYSIZE(targetNames)))
    {
        const EPropertyTarget newTarget = static_cast<EPropertyTarget>(targetIndex);

        if (newTarget != Target)
        {
            Target = newTarget;

            if (OnTargetChanged)
            {
                OnTargetChanged(Target);
            }
        }
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(FLocalization::Text(EUiText::EditingImageHelp));
    }

    ImGui::Separator();
}

void FPropertyPanel::SetImageData(const FImageData* ImageData)
{
    CurrentImageData = ImageData;

    // 格式/分辨率/stride 一律以**加载参数**为准（调用方在此之前已经用
    // SetFormat/SetResolution/SetStride 填过），这里不再从图像数据倒推。
    //
    // 曾经在这里回写过：加载失败时 ImageData 还是上一幅图，会把用户刚填的分辨率
    // 悄悄改回旧值，看起来就像输入框"自己跳回去了"。
    // 位深同样必须以加载参数为准。Reload 失败时文档会保留上一幅 ImageData，
    // 从旧图回写会让新格式的固定/可选布局状态显示错误。
}

void FPropertyPanel::SetLoadParams(const FImageLoadParams& Params)
{
    FImageLoadParams normalized = Params;
    normalized.ConstrainStorageLayout();

    SelectedFormat = normalized.Format;
    SelectedWidth = normalized.Width;
    SelectedHeight = normalized.Height;
    SelectedStride = normalized.Stride > 0
        ? normalized.Stride
        : GetCompactStride(SelectedFormat, SelectedWidth);
    SelectedBitsPerPixel = normalized.BitsPerPixel;
    SelectedBayerPattern = normalized.BayerPattern;
    SelectedByteOrder = normalized.ByteOrder;
    SelectedSampleAlignment = normalized.SampleAlignment;
    ConstrainStorageLayout();
}

void FPropertyPanel::SetFileInfo(uint64_t InFileSize, uint64_t InImageSize)
{
    FileSize = InFileSize;
    ImageSize = InImageSize;
}

void FPropertyPanel::SetSourceFile(const std::string& InPath)
{
    SourceFile = InPath;
}

void FPropertyPanel::SetOnFormatChanged(std::function<void(EImageFormat)> Callback)
{
    OnFormatChanged = Callback;
}

void FPropertyPanel::SetOnResolutionChanged(std::function<void(int32_t, int32_t)> Callback)
{
    OnResolutionChanged = Callback;
}

void FPropertyPanel::SetOnBitsPerPixelChanged(std::function<void(int32_t)> Callback)
{
    OnBitsPerPixelChanged = Callback;
}

void FPropertyPanel::SetOnStrideChanged(std::function<void(int32_t)> Callback)
{
    OnStrideChanged = Callback;
}

void FPropertyPanel::SetOnBayerPatternChanged(std::function<void(EBayerPattern)> Callback)
{
    OnBayerPatternChanged = Callback;
}

void FPropertyPanel::SetOnByteOrderChanged(
    std::function<void(EByteOrder)> Callback)
{
    OnByteOrderChanged = Callback;
}

void FPropertyPanel::SetOnSampleAlignmentChanged(
    std::function<void(ESampleAlignment)> Callback)
{
    OnSampleAlignmentChanged = Callback;
}

void FPropertyPanel::SetOnFormatPresetApplied(std::function<void()> Callback)
{
    OnFormatPresetApplied = Callback;
}

void FPropertyPanel::SetOnTargetChanged(std::function<void(EPropertyTarget)> Callback)
{
    OnTargetChanged = Callback;
}

void FPropertyPanel::SetOnDisplaySettingsChanged(std::function<void(const FDisplaySettings&)> Callback)
{
    OnDisplaySettingsChanged = Callback;
}

void FPropertyPanel::NotifyDisplaySettingsChanged()
{
    if (OnDisplaySettingsChanged)
    {
        OnDisplaySettingsChanged(SelectedDisplay);
    }
}

void FPropertyPanel::RenderImageInfo()
{
    ImGui::Text(FLocalization::Text(EUiText::ImageInfo));

    if (CurrentImageData && CurrentImageData->IsValid())
    {
        ImGui::Text(FLocalization::Text(EUiText::ImageWidth), CurrentImageData->GetWidth());
        ImGui::Text(FLocalization::Text(EUiText::ImageHeight), CurrentImageData->GetHeight());
        ImGui::Text(FLocalization::Text(EUiText::ImageStride), CurrentImageData->GetStride());
        ImGui::Text(FLocalization::Text(EUiText::ImageBitsPerPixel), CurrentImageData->GetBitsPerPixel());
        ImGui::Text(FLocalization::Text(EUiText::ImageChannels), CurrentImageData->GetChannelCount());
        ImGui::Text(FLocalization::Text(EUiText::ImageDataSize), CurrentImageData->GetPixelDataSize());
    }
    else
    {
        ImGui::Text(FLocalization::Text(EUiText::NoImageData));
    }

    RenderFileInfo();

    if (!LoadError.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
        ImGui::TextWrapped("%s", FLocalization::Translate(LoadError.c_str()));
        ImGui::PopStyleColor();
    }
}

void FPropertyPanel::RenderFileInfo()
{
    if (FileSize == 0)
    {
        return;
    }

    ImGui::Text(FLocalization::Text(EUiText::ImageFileSize), static_cast<unsigned long long>(FileSize));

    if (ImageSize == 0)
    {
        return;
    }

    ImGui::Text(FLocalization::Text(EUiText::ExpectedFileSize), static_cast<unsigned long long>(ImageSize));

    // 无头格式最常见的死法：参数看着"差不多"，但算出来的字节数和文件对不上。
    // 与其让用户对着一幅花屏反复猜，不如把差多少直接摆出来。
    if (ImageSize != FileSize)
    {
        const long long delta = static_cast<long long>(ImageSize) - static_cast<long long>(FileSize);

        ImGui::PushStyleColor(ImGuiCol_Text, kWarnColor);
        ImGui::TextWrapped(FLocalization::Text(EUiText::FileSizeMismatch), delta);
        ImGui::PopStyleColor();
    }
}

void FPropertyPanel::RenderFormatSelector()
{
    ImGui::Text(FLocalization::Text(EUiText::FormatSelection));

    // 用户可见顺序与枚举追加顺序解耦，避免新 RGB 格式只能出现在列表末尾。
    const std::vector<EImageFormat>& displayOrder =
        FImageFormatDesc::GetDisplayOrder();
    const FFormatDesc& selectedDesc =
        FImageFormatDesc::Get(SelectedFormat);
    const FImageLoadParams currentParams = GetLoadParams();
    const std::vector<FUserSettings::FImageFormatPreset>& presets =
        FUserSettings::GetImageFormatPresets();

    const FUserSettings::FImageFormatPreset* activePreset = nullptr;

    if (!SelectedFormatPresetName.empty())
    {
        const auto activeIt = std::find_if(
            presets.begin(),
            presets.end(),
            [this, &currentParams](
                const FUserSettings::FImageFormatPreset& Preset)
            {
                return Preset.Name == SelectedFormatPresetName &&
                    AreLoadParamsEqual(Preset.Params, currentParams);
            });

        if (activeIt != presets.end())
        {
            activePreset = &*activeIt;
        }
    }

    const char* preview = activePreset
        ? activePreset->Name.c_str()
        : FLocalization::Translate(selectedDesc.DisplayName);

    ImGui::SetNextItemWidth(ImGui::CalcTextSize("000000000000000000").x);

    // 沿用 ImGui 计算 Combo 高度的方式，随字体和 DPI 缩放，避免固定像素高度失真。
    const ImGuiStyle& style = ImGui::GetStyle();
    const float popupMaxHeight =
        ImGui::GetTextLineHeightWithSpacing() *
            kFormatSelectorPopupVisibleItemCount
        - style.ItemSpacing.y
        + style.WindowPadding.y * 2.0f;
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(0.0f, 0.0f),
        ImVec2(FLT_MAX, popupMaxHeight));

    if (ImGui::BeginCombo(FLocalization::Text(EUiText::ImageFormat), preview))
    {
        // Unknown 固定在第一项；预设紧随其后且按新保存优先排列；其余内建格式
        // 继续沿用描述表提供的稳定显示顺序。
        for (EImageFormat format : displayOrder)
        {
            if (format != EImageFormat::Unknown)
            {
                continue;
            }

            const FFormatDesc& desc = FImageFormatDesc::Get(format);
            const bool bSelected =
                !activePreset && format == SelectedFormat;

            if (ImGui::Selectable(FLocalization::Translate(desc.DisplayName), bSelected))
            {
                SelectedFormatPresetName.clear();

                if (format != SelectedFormat)
                {
                    ApplyFormatSelection(format);
                }
            }

            if (bSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }

        for (size_t presetIndex = 0;
             presetIndex < presets.size();
             ++presetIndex)
        {
            const FUserSettings::FImageFormatPreset& preset =
                presets[presetIndex];
            const bool bSelected = activePreset &&
                activePreset->Name == preset.Name;

            ImGui::PushID("ImageFormatPreset");
            ImGui::PushID(static_cast<int>(presetIndex));

            if (ImGui::Selectable(preset.Name.c_str(), bSelected))
            {
                SetLoadParams(preset.Params);
                SelectedFormatPresetName = preset.Name;

                if (OnFormatPresetApplied)
                {
                    OnFormatPresetApplied();
                }
            }

            if (bSelected)
            {
                ImGui::SetItemDefaultFocus();
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    u8"%s · %d × %d · stride %d",
                    FLocalization::Translate(FImageFormatDesc::Get(preset.Params.Format).DisplayName),
                    preset.Params.Width,
                    preset.Params.Height,
                    preset.Params.Stride);
            }

            ImGui::PopID();
            ImGui::PopID();
        }

        if (!presets.empty())
        {
            ImGui::Separator();
        }

        for (EImageFormat format : displayOrder)
        {
            if (format == EImageFormat::Unknown)
            {
                continue;
            }

            const FFormatDesc& desc = FImageFormatDesc::Get(format);
            const bool bSelected =
                !activePreset && format == SelectedFormat;

            if (ImGui::Selectable(FLocalization::Translate(desc.DisplayName), bSelected))
            {
                SelectedFormatPresetName.clear();

                if (format != SelectedFormat)
                {
                    ApplyFormatSelection(format);
                }
            }

            if (bSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
    }

    ImGui::SameLine();

    const bool bCanSavePreset =
        SelectedFormat != EImageFormat::Unknown &&
        SelectedWidth > 0 &&
        SelectedHeight > 0;
    ImGui::BeginDisabled(!bCanSavePreset);

    if (ImGui::Button(FLocalization::Text(EUiText::SaveFormatPresetAction)))
    {
        FormatPresetDraftParams = currentParams;
        FormatPresetSaveError.clear();

        if (activePreset)
        {
            std::snprintf(
                FormatPresetNameInput,
                sizeof(FormatPresetNameInput),
                "%s",
                activePreset->Name.c_str());
        }
        else
        {
            FormatPresetNameInput[0] = '\0';
        }

        bRequestFormatPresetPopup = true;
        bFocusFormatPresetNameInput = true;
    }

    ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip(
            bCanSavePreset
                ? FLocalization::Text(EUiText::SaveFormatPresetHelp)
                : FLocalization::Text(EUiText::InvalidPresetParameters));
    }
}

void FPropertyPanel::RenderFormatPresetPopup()
{
    if (bRequestFormatPresetPopup)
    {
        ImGui::OpenPopup(FLocalization::WindowTitle(EUiText::SaveFormatPresetPopup));
        bRequestFormatPresetPopup = false;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ConfigureFormatPresetPopup(viewport);

    const ImGuiStyle& baseStyle = ImGui::GetStyle();
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        FUiScale::Apply(
            kFormatPresetHorizontalPadding,
            kFormatPresetVerticalPadding));
    ImGui::PushStyleVar(
        ImGuiStyleVar_PopupRounding,
        FUiScale::Apply(kFormatPresetPopupRounding));
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        FUiScale::Apply(
            kFormatPresetFrameHorizontalPadding,
            kFormatPresetFrameVerticalPadding));
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(
            baseStyle.ItemSpacing.x,
            FUiScale::Apply(kFormatPresetItemSpacingY)));

    if (!ImGui::BeginPopupModal(
            FLocalization::WindowTitle(EUiText::SaveFormatPresetPopup),
            nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::PopStyleVar(kFormatPresetStyleVarCount);
        return;
    }

    ImGui::TextUnformatted(FLocalization::Text(EUiText::PresetName));
    ImGui::SetNextItemWidth(-1.0f);

    if (bFocusFormatPresetNameInput)
    {
        ImGui::SetKeyboardFocusHere();
        bFocusFormatPresetNameInput = false;
    }

    const bool bSubmitFromKeyboard = ImGui::InputText(
        "##ImageFormatPresetName",
        FormatPresetNameInput,
        sizeof(FormatPresetNameInput),
        ImGuiInputTextFlags_EnterReturnsTrue);

    if (ImGui::IsItemEdited())
    {
        FormatPresetSaveError.clear();
    }

    const std::string normalizedName =
        TrimAsciiWhitespace(FormatPresetNameInput);
    const char* validationError = ValidateFormatPresetName(normalizedName);

    const std::vector<FUserSettings::FImageFormatPreset>& presets =
        FUserSettings::GetImageFormatPresets();
    const bool bWillReplace = std::any_of(
        presets.begin(),
        presets.end(),
        [&normalizedName](const FUserSettings::FImageFormatPreset& Preset)
        {
            return Preset.Name == normalizedName;
        });

    ImGui::TextDisabled(
        FLocalization::Text(EUiText::PresetParameters),
        FLocalization::Translate(FImageFormatDesc::Get(FormatPresetDraftParams.Format).DisplayName),
        FormatPresetDraftParams.Width,
        FormatPresetDraftParams.Height,
        FormatPresetDraftParams.Stride);
    ImGui::TextDisabled(
        FLocalization::Text(EUiText::PresetSampleLayout),
        FormatPresetDraftParams.BitsPerPixel);

    if (validationError)
    {
        ImGui::TextColored(kFormatPresetErrorColor, "%s", validationError);
    }
    else if (!FormatPresetSaveError.empty())
    {
        ImGui::TextColored(
            kFormatPresetErrorColor,
            "%s",
            FormatPresetSaveError.c_str());
    }
    else if (bWillReplace)
    {
        ImGui::TextColored(
            kFormatPresetWarningColor,
            FLocalization::Text(EUiText::PresetOverwriteHelp));
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float footerWidth = FUiScale::Apply(kFormatPresetCancelButtonWidth)
        + style.ItemSpacing.x
        + FUiScale::Apply(kFormatPresetSaveButtonWidth);
    const float footerY = ImGui::GetWindowHeight()
        - style.WindowPadding.y
        - ImGui::GetFrameHeight();
    const float separatorY = footerY - style.ItemSpacing.y;

    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), separatorY));
    ImGui::Separator();
    ImGui::SetCursorPosY(footerY);
    ImGui::SetCursorPosX(std::max(
        style.WindowPadding.x,
        ImGui::GetWindowWidth() - style.WindowPadding.x - footerWidth));

    const bool bCancel = ImGui::Button(
        FLocalization::Text(EUiText::Cancel),
        ImVec2(FUiScale::Apply(kFormatPresetCancelButtonWidth), 0.0f));

    ImGui::SameLine();
    ImGui::BeginDisabled(validationError != nullptr);
    const bool bSave = ImGui::Button(
        bWillReplace ? FLocalization::Text(EUiText::Overwrite) : FLocalization::Text(EUiText::Save),
        ImVec2(FUiScale::Apply(kFormatPresetSaveButtonWidth), 0.0f));
    ImGui::EndDisabled();

    if (bCancel || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        FormatPresetSaveError.clear();
        ImGui::CloseCurrentPopup();
    }
    else if ((bSave || bSubmitFromKeyboard) && !validationError)
    {
        FUserSettings::FImageFormatPreset preset;
        preset.Name = normalizedName;
        preset.Params = FormatPresetDraftParams;

        if (FUserSettings::SaveImageFormatPreset(preset))
        {
            SelectedFormatPresetName = normalizedName;
            FormatPresetSaveError.clear();
            FToast::Show(
                bWillReplace
                    ? FLocalization::Text(EUiText::PresetUpdated)
                    : FLocalization::Text(EUiText::PresetSaved));
            ImGui::CloseCurrentPopup();
        }
        else
        {
            FormatPresetSaveError = FLocalization::Text(EUiText::PresetSaveFailed);
        }
    }

    ImGui::EndPopup();
    ImGui::PopStyleVar(kFormatPresetStyleVarCount);
}

bool FPropertyPanel::DrawIntInput(const char* Label, int32_t& InOutValue, int32_t MinValue, int32_t MaxValue)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", InOutValue);

    ImGui::SetNextItemWidth(ImGui::CalcTextSize("000000").x);
    ImGui::InputText(Label, buffer, sizeof(buffer), ImGuiInputTextFlags_CharsDecimal);

    // 失去焦点时才提交，避免边输入边触发重新加载
    if (!ImGui::IsItemDeactivatedAfterEdit())
    {
        return false;
    }

    int32_t newValue = std::max(MinValue, std::min(MaxValue, atoi(buffer)));

    if (newValue == InOutValue)
    {
        return false;
    }

    InOutValue = newValue;

    return true;
}

void FPropertyPanel::RenderResolutionEditor()
{
    ImGui::Text(FLocalization::Text(EUiText::Resolution));

    int32_t width = SelectedWidth;
    int32_t height = SelectedHeight;

    // 在同一行显示：宽度 x 高度
    bool widthChanged = DrawIntInput(
        "##Width",
        width,
        FImageLimits::kMinimumDimension,
        FImageLimits::kMaximumDimension);

    ImGui::SameLine();
    ImGui::Text(" x ");
    ImGui::SameLine();

    bool heightChanged = DrawIntInput(
        "##Height",
        height,
        FImageLimits::kMinimumDimension,
        FImageLimits::kMaximumDimension);

    if (widthChanged || heightChanged)
    {
        const int32_t oldWidth = SelectedWidth;

        SelectedWidth = width;
        SelectedHeight = height;

        RefreshStrideForGeometry(SelectedFormat, oldWidth);

        if (OnResolutionChanged)
        {
            OnResolutionChanged(SelectedWidth, SelectedHeight);
        }
    }

    RefreshCandidates();

    // 文件名分辨率对不上时不在这里单独提示 —— 下拉框里"来自文件名"那条已经标了"对不上"，
    // 收起时的橙色预览也在说同一件事，面板上再来一行只是重复
    RenderResolutionGuess();
}

void FPropertyPanel::RefreshCandidates()
{
    // 没有文件大小就无从猜起；自带文件头的格式则是文件大小本身不可用 ——
    // 那是**压缩后**的字节数，与解码后的像素字节数毫无可比性，拿去因数分解
    // 只会得到一串看着挺像样的假分辨率（GetImageSize() 对这类文件返回 0 是同一个理由）。
    //
    // 这里把状态清干净而不是直接 return：否则从 a.yuv 切到 b.png 时，
    // 面板上会留着 a.yuv 的文件名分辨率提示。
    if (FileSize == 0 || !bParamsEditable)
    {
        CachedFormat = EImageFormat::Unknown;
        CachedFileSize = 0;
        CachedSourceFile.clear();

        Candidates.clear();
        AltFormats.clear();

        FilenameWidth = 0;
        FilenameHeight = 0;
        bFilenameExact = false;

        return;
    }

    // 格式、文件、文件大小三者都没变就直接用上一次的结果
    if (CachedFormat == SelectedFormat && CachedFileSize == FileSize && CachedSourceFile == SourceFile)
    {
        return;
    }

    CachedFormat = SelectedFormat;
    CachedFileSize = FileSize;
    CachedSourceFile = SourceFile;

    Candidates.clear();
    AltFormats.clear();
    FilenameWidth = 0;
    FilenameHeight = 0;
    bFilenameExact = false;

    if (FileSize == 0 || SelectedFormat == EImageFormat::Unknown)
    {
        return;
    }

    Candidates = FResolutionGuess::Guess(SelectedFormat, FileSize, kMaxCandidates);

    if (!SourceFile.empty())
    {
        ParseImageInfoFromFilename(SourceFile, FilenameWidth, FilenameHeight);
    }

    if (FilenameWidth <= 0 || FilenameHeight <= 0)
    {
        // 一点线索都没有时才去问"哪些格式下有合理分辨率"
        if (Candidates.empty())
        {
            AltFormats = FResolutionGuess::GuessFormats(FileSize, 0, 0, SelectedFormat);
        }

        return;
    }

    bFilenameExact = FResolutionGuess::Matches(SelectedFormat, FilenameWidth, FilenameHeight, 0, FileSize);

    // 文件名候选与算出来的候选可能撞车，此时保留文件名那条（带标签、置顶），去掉重复项
    Candidates.erase(
        std::remove_if(Candidates.begin(), Candidates.end(), [this](const FResolutionCandidate& C)
        {
            return C.Width == FilenameWidth && C.Height == FilenameHeight;
        }),
        Candidates.end());

    FResolutionCandidate fromName;
    fromName.Width = FilenameWidth;
    fromName.Height = FilenameHeight;
    fromName.bFromFilename = true;
    fromName.bExact = bFilenameExact;

    Candidates.insert(Candidates.begin(), fromName);

    if (!bFilenameExact)
    {
        // 文件名的分辨率对不上，但它在别的格式下可能正好整除 —— 那说明错的是格式而不是分辨率
        AltFormats = FResolutionGuess::GuessFormats(FileSize, FilenameWidth, FilenameHeight, SelectedFormat);
    }
}

void FPropertyPanel::ApplyCandidate(int32_t Width, int32_t Height)
{
    if (Width <= 0 || Height <= 0)
    {
        return;
    }

    if (Width == SelectedWidth && Height == SelectedHeight)
    {
        return;
    }

    SelectedWidth = Width;
    SelectedHeight = Height;

    // 候选一律按紧凑排列算出，沿用上一幅图的 padding 只会让它对不上
    SelectedStride = GetCompactStride(SelectedFormat, SelectedWidth);

    if (OnStrideChanged)
    {
        OnStrideChanged(SelectedStride);
    }

    if (OnResolutionChanged)
    {
        OnResolutionChanged(SelectedWidth, SelectedHeight);
    }
}

void FPropertyPanel::RenderResolutionGuess()
{
    // 猜不了的场景由 RefreshCandidates() 判定并清空状态，这里只看结果。
    //
    // SameLine 放在这个早退之后 —— 它只对紧接着的那个控件生效，
    // 提前调用而又不画东西的话，下一个 Separator 会被拉到分辨率输入框那一行去
    if (FileSize == 0 || !bParamsEditable)
    {
        return;
    }

    ImGui::SameLine();

    // 面板上的 stride 可能带 padding，校验当前值时必须带上它，
    // 否则一个配好了 stride 的图会被误报成"对不上"
    const bool bCurrentExact = FResolutionGuess::Matches(
        SelectedFormat, SelectedWidth, SelectedHeight, SelectedStride, FileSize);

    char preview[64];

    if (Candidates.empty())
    {
        snprintf(preview, sizeof(preview), FLocalization::Text(EUiText::NoMatch));
    }
    else if (bCurrentExact)
    {
        snprintf(preview, sizeof(preview), FLocalization::Text(EUiText::ResolutionCandidates), static_cast<int32_t>(Candidates.size()));
    }
    else
    {
        snprintf(preview, sizeof(preview), FLocalization::Text(EUiText::MismatchedCandidates), static_cast<int32_t>(Candidates.size()));
    }

    // 收起时就当状态指示器用：当前宽高与文件大小对不上就变橙，不必额外加图标
    const bool bWarn = !bCurrentExact;

    if (bWarn)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarnColor);
    }

    ImGui::SetNextItemWidth(ImGui::CalcTextSize(FLocalization::Text(EUiText::CandidateWidthSample)).x + ImGui::GetFrameHeight() * 2.0f);

    // 弹出层要比收起时宽得多，才放得下"帧数"那一列和底部说明
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(FUiScale::Apply(320.0f), 0.0f),
        ImVec2(FLT_MAX, FLT_MAX));

    const bool bOpen = ImGui::BeginCombo(u8"##ResolutionGuess", preview, ImGuiComboFlags_HeightLarge);

    if (bWarn)
    {
        ImGui::PopStyleColor();
    }

    if (!bOpen)
    {
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(FLocalization::Text(EUiText::ResolutionCandidatesHelp));
        }

        return;
    }

    // 列表里的点击会改动 SelectedFormat / Candidates，而它们正被遍历着，
    // 所以先记下来，等 EndCombo 之后再落地
    int32_t pendingWidth = 0;
    int32_t pendingHeight = 0;
    EImageFormat pendingFormat = EImageFormat::Unknown;

    bool bHeaderDrawn = false;

    for (size_t i = 0; i < Candidates.size(); ++i)
    {
        const FResolutionCandidate& c = Candidates[i];

        if (c.bFromFilename)
        {
            ImGui::TextDisabled(FLocalization::Text(EUiText::FromFileName));
        }
        else if (!bHeaderDrawn)
        {
            ImGui::TextDisabled(FLocalization::Text(EUiText::ExactFileSize));
            bHeaderDrawn = true;
        }

        char label[64];
        snprintf(label, sizeof(label), "%d x %d##cand%zu", c.Width, c.Height, i);

        const bool bSelected = (c.Width == SelectedWidth && c.Height == SelectedHeight);

        if (ImGui::Selectable(label, bSelected))
        {
            pendingWidth = c.Width;
            pendingHeight = c.Height;
        }

        // 能整除的候选不必再标注什么（它们按定义都正好装满文件），
        // 只有文件名那条可能对不上，标出来
        if (!c.bExact)
        {
            ImGui::SameLine(FUiScale::Apply(kSizeColumnX));
            ImGui::TextColored(kWarnColor, FLocalization::Text(EUiText::SizeMismatch));
        }
    }

    if (Candidates.empty())
    {
        ImGui::TextDisabled(FLocalization::Text(EUiText::NoMatchingResolution));
    }

    ImGui::Separator();

    if (!AltFormats.empty())
    {
        ImGui::TextDisabled(FLocalization::Text(EUiText::MatchingFormats));

        for (size_t i = 0; i < AltFormats.size(); ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine();
            }

            ImGui::PushID(static_cast<int>(i));

            if (ImGui::TextLink(FImageFormatDesc::Get(AltFormats[i]).Name))
            {
                pendingFormat = AltFormats[i];
            }

            ImGui::PopID();
        }
    }

    ImGui::TextDisabled(FLocalization::Text(EUiText::PackedFileSize),
                        FLocalization::Translate(FImageFormatDesc::Get(SelectedFormat).DisplayName),
                        static_cast<unsigned long long>(FileSize));

    // 带行尾 padding 的排布与紧凑排布字节数完全相同（1440x1920 stride 1472 与
    // 1472x1920 紧凑都是 4239360 字节），数学上无法区分，只能靠现象反推
    ImGui::TextDisabled(FLocalization::Text(EUiText::AmbiguousStrideHelp));

    ImGui::EndCombo();

    if (pendingFormat != EImageFormat::Unknown)
    {
        ApplyFormatSelection(pendingFormat);
    }
    else
    {
        ApplyCandidate(pendingWidth, pendingHeight);
    }
}

void FPropertyPanel::ApplyFormatSelection(EImageFormat NewFormat)
{
    if (NewFormat == EImageFormat::Unknown || NewFormat == SelectedFormat)
    {
        return;
    }

    const EImageFormat oldFormat = SelectedFormat;
    const FFormatDesc& newDesc = FImageFormatDesc::Get(NewFormat);

    SelectedFormat = NewFormat;
    SelectedBitsPerPixel = newDesc.BitDepth;
    SelectedByteOrder = newDesc.StorageLayout.DefaultByteOrder;
    SelectedSampleAlignment =
        newDesc.StorageLayout.DefaultSampleAlignment;
    ConstrainStorageLayout();

    // 用户已经明确选了格式；若旧宽高/stride 无法解释当前文件，就在触发唯一一次
    // 重载回调前把参数补全为该格式最可信的精确候选。否则会先提交一次 0x0，
    // 而候选列表虽然随后出现，却仍要求用户再点一次分辨率。
    const bool bCurrentGeometryMatches =
        FileSize > 0 &&
        FResolutionGuess::Matches(
            SelectedFormat,
            SelectedWidth,
            SelectedHeight,
            SelectedStride,
            FileSize);
    const std::vector<FResolutionCandidate> bestCandidates =
        !bCurrentGeometryMatches && FileSize > 0
            ? FResolutionGuess::Guess(SelectedFormat, FileSize, 1)
            : std::vector<FResolutionCandidate>{};

    if (!bestCandidates.empty())
    {
        SelectedWidth = bestCandidates.front().Width;
        SelectedHeight = bestCandidates.front().Height;
        SelectedStride = GetCompactStride(SelectedFormat, SelectedWidth);
    }
    else
    {
        // 已有几何参数仍然精确匹配时保留它（包括用户设置的 padding）；没有候选时
        // 沿用原行为，只按新格式更新紧凑 stride。
        RefreshStrideForGeometry(oldFormat, SelectedWidth);
    }

    if (OnFormatChanged)
    {
        OnFormatChanged(SelectedFormat);
    }
}

void FPropertyPanel::RenderStrideEditor()
{
    ImGui::Text(u8"Stride");

    int32_t stride = SelectedStride;

    if (DrawIntInput(
            FLocalization::Text(EUiText::BytesPerRow),
            stride,
            FImageLimits::kMinimumStrideBytes,
            kMaximumEditableStrideBytes))
    {
        // 允许清成 0 当作"恢复默认"：文档那边按紧凑排列解析，
        // 回填时 SetStride() 会把具体的字节数再填回输入框
        SelectedStride = stride;

        if (OnStrideChanged)
        {
            OnStrideChanged(SelectedStride);
        }
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(FLocalization::Text(EUiText::StrideHelp));
    }
}

void FPropertyPanel::RenderBayerPatternSelector()
{
    // 只有 Bayer 家族才需要选排布，其它格式下这个控件没有意义
    if (!FImageFormatDesc::IsBayer(SelectedFormat))
    {
        return;
    }

    ImGui::Text(FLocalization::Text(EUiText::BayerPattern));

    const char* patternNames[] = { "RGGB", "BGGR", "GRBG", "GBRG" };
    int32_t patternIndex = static_cast<int32_t>(SelectedBayerPattern);

    ImGui::SetNextItemWidth(ImGui::CalcTextSize("00000000").x);

    if (ImGui::Combo(u8"CFA", &patternIndex, patternNames, IM_ARRAYSIZE(patternNames)))
    {
        const EBayerPattern newPattern = static_cast<EBayerPattern>(patternIndex);

        if (newPattern != SelectedBayerPattern)
        {
            SelectedBayerPattern = newPattern;

            if (OnBayerPatternChanged)
            {
                OnBayerPatternChanged(SelectedBayerPattern);
            }
        }
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(FLocalization::Text(EUiText::BayerPatternHelp));
    }

    ImGui::Separator();
}

void FPropertyPanel::RenderBitsPerPixelSelector()
{
    ImGui::Text(FLocalization::Text(EUiText::BitDepth));

    const FBitDepthPropertyState bitDepth =
        FImageFormatDesc::ResolveBitDepth(
            SelectedFormat,
            SelectedBitsPerPixel);
    const bool bConfigurable =
        bitDepth.Mode == EFormatPropertyMode::Configurable;
    int32_t bits = SelectedBitsPerPixel;

    ImGui::BeginDisabled(!bConfigurable);

    if (DrawIntInput(
            FLocalization::Text(EUiText::BitsPerPixel),
            bits,
            bitDepth.Minimum,
            bitDepth.Maximum))
    {
        SelectedBitsPerPixel = bits;
        ConstrainStorageLayout();

        if (OnBitsPerPixelChanged)
        {
            OnBitsPerPixelChanged(SelectedBitsPerPixel);
        }
    }

    ImGui::EndDisabled();

    if (!bConfigurable)
    {
        ImGui::SameLine();
        ImGui::TextDisabled(FLocalization::Text(EUiText::FixedByFormat));
    }
}

void FPropertyPanel::RenderDisplaySettings()
{
    ImGui::Text(FLocalization::Text(EUiText::DisplaySettings));

    if (!CurrentImageData || !CurrentImageData->IsValid())
    {
        ImGui::TextDisabled(FLocalization::Text(EUiText::NoImageDataHint));

        return;
    }

    const bool bIsYUV = FImageFormatDesc::IsYUV(CurrentImageData->GetFormat());
    const float itemWidth = ImGui::CalcTextSize(kDisplayComboWidthText).x;

    // --- YUV 矩阵与数值范围：仅对 YUV 有意义 ---
    if (bIsYUV)
    {
        const char* colorSpaceNames[] = { "BT.601", "BT.709", "BT.2020" };
        int32_t colorSpaceIndex = static_cast<int32_t>(SelectedDisplay.ColorSpace);

        ImGui::SetNextItemWidth(itemWidth);

        if (ImGui::Combo(FLocalization::Text(EUiText::YuvMatrix), &colorSpaceIndex, colorSpaceNames, IM_ARRAYSIZE(colorSpaceNames)))
        {
            SelectedDisplay.ColorSpace = static_cast<EColorSpace>(colorSpaceIndex);
            NotifyDisplaySettingsChanged();
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(FLocalization::Text(EUiText::YuvMatrixHelp));
        }

        const char* rangeNames[] = { "Limited", "Full" };
        int32_t rangeIndex = static_cast<int32_t>(SelectedDisplay.ColorRange);

        ImGui::SetNextItemWidth(itemWidth);

        if (ImGui::Combo(FLocalization::Text(EUiText::ColorRange), &rangeIndex, rangeNames, IM_ARRAYSIZE(rangeNames)))
        {
            SelectedDisplay.ColorRange = static_cast<EColorRange>(rangeIndex);
            NotifyDisplaySettingsChanged();
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(FLocalization::Text(EUiText::ColorRangeHelp));
        }
    }

    // --- 通道隔离 ---
    const char* yuvChannels[] = { FLocalization::Text(EUiText::FullColor), "Y", "U", "V" };
    const char* rgbaChannels[] = { FLocalization::Text(EUiText::FullColor), "R", "G", "B", "A" };

    const char** channelNames = bIsYUV ? yuvChannels : rgbaChannels;
    const int32_t channelCount = bIsYUV
        ? IM_ARRAYSIZE(yuvChannels) : IM_ARRAYSIZE(rgbaChannels);

    int32_t channelIndex = std::min(static_cast<int32_t>(SelectedDisplay.ChannelView), channelCount - 1);

    ImGui::SetNextItemWidth(itemWidth);

    if (ImGui::Combo(FLocalization::Text(EUiText::DisplayChannel), &channelIndex, channelNames, channelCount))
    {
        SelectedDisplay.ChannelView = static_cast<EChannelView>(channelIndex);
        NotifyDisplaySettingsChanged();
    }

    if (ImGui::IsItemHovered())
    {
        const EColorModel colorModel =
            FImageFormatDesc::Get(CurrentImageData->GetFormat()).ColorModel;

        if (bIsYUV)
        {
            ImGui::SetTooltip(FLocalization::Text(EUiText::YuvChannelHelp));
        }
        else if (colorModel == EColorModel::Bayer)
        {
            ImGui::SetTooltip(FLocalization::Text(EUiText::BayerChannelHelp));
        }
        else if (colorModel == EColorModel::Gray)
        {
            ImGui::SetTooltip(FLocalization::Text(EUiText::GrayscaleChannelHelp));
        }
        else
        {
            ImGui::SetTooltip(FLocalization::Text(EUiText::RgbChannelHelp));
        }
    }

    RenderTransferSettings();
}

void FPropertyPanel::RenderTransferSettings()
{
    // Bayer 是传感器线性读数，传输函数/原色在它上面没有意义，着色器那边也没接
    if (FImageFormatDesc::Get(CurrentImageData->GetFormat()).ColorModel == EColorModel::Bayer)
    {
        return;
    }

    const float kItemWidth = ImGui::CalcTextSize(kDisplayComboWidthText).x;
    const float kNumberWidth = ImGui::CalcTextSize("BT.2020____").x;

    ImGui::Separator();
    ImGui::TextUnformatted(FLocalization::Text(EUiText::HdrTransfer));

    // --- 三原色 ---
    const char* primariesNames[] = { "BT.709 / sRGB", "BT.2020", "Display P3", "BT.601-525", "BT.601-625" };
    int32_t primariesIndex = static_cast<int32_t>(SelectedDisplay.Primaries);

    ImGui::SetNextItemWidth(kItemWidth);

    if (ImGui::Combo(FLocalization::Text(EUiText::SourcePrimaries), &primariesIndex, primariesNames, IM_ARRAYSIZE(primariesNames)))
    {
        SelectedDisplay.Primaries = static_cast<EColorPrimaries>(primariesIndex);
        NotifyDisplaySettingsChanged();
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(FLocalization::Text(EUiText::SourcePrimariesHelp));
    }

    // --- 传输函数 ---
    const char* transferNames[] = { "SDR (sRGB)", "BT.1886 (2.4)", "PQ (ST 2084)", "HLG", FLocalization::Text(EUiText::Linear) };
    int32_t transferIndex = static_cast<int32_t>(SelectedDisplay.Transfer);

    ImGui::SetNextItemWidth(kItemWidth);

    if (ImGui::Combo(FLocalization::Text(EUiText::TransferFunction), &transferIndex, transferNames, IM_ARRAYSIZE(transferNames)))
    {
        SelectedDisplay.Transfer = static_cast<EColorTransfer>(transferIndex);
        NotifyDisplaySettingsChanged();
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(FLocalization::Text(EUiText::TransferFunctionHelp));
    }

    const bool bAbsolute = FColorTransform::IsAbsoluteTransfer(SelectedDisplay.Transfer);
    const bool bNonSdr = (SelectedDisplay.Transfer != EColorTransfer::SDR);

    // --- 参考白：PQ/HLG 转 SDR 时使用，HDR 输出另用显示器 SDR 白归一化 ---
    ImGui::BeginDisabled(!bAbsolute);

    float refWhite = SelectedDisplay.ReferenceWhiteNits;

    ImGui::SetNextItemWidth(kNumberWidth);

    if (ImGui::DragFloat(FLocalization::Text(EUiText::ReferenceWhite), &refWhite, 1.0f, 10.0f, 1000.0f, "%.0f"))
    {
        SelectedDisplay.ReferenceWhiteNits = std::clamp(refWhite, 10.0f, 1000.0f);
        NotifyDisplaySettingsChanged();
    }

    // 置灰时仍需显示提示，说明此属性的适用条件。
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip(FLocalization::Text(EUiText::ReferenceWhiteHelp));
    }

    ImGui::EndDisabled();

    // --- HLG 解读时采用的标称显示峰值，不代表文件携带的母版元数据 ---
    if (SelectedDisplay.Transfer == EColorTransfer::HLG)
    {
        float peak = SelectedDisplay.HlgPeakNits;

        ImGui::SetNextItemWidth(kNumberWidth);

        if (ImGui::DragFloat(FLocalization::Text(EUiText::HlgPeak), &peak, kHlgPeakDragSpeedNits,
                            kMinimumHlgPeakNits, kMaximumHlgPeakNits, "%.0f"))
        {
            SelectedDisplay.HlgPeakNits = std::clamp(peak, kMinimumHlgPeakNits, kMaximumHlgPeakNits);
            NotifyDisplaySettingsChanged();
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(FLocalization::Text(EUiText::HlgPeakHelp));
        }
    }

    // --- 色调映射 ---
    ImGui::BeginDisabled(!bNonSdr);

    const char* toneMapNames[] = { FLocalization::Text(EUiText::Clip), "Reinhard", "ACES" };
    int32_t toneMapIndex = static_cast<int32_t>(SelectedDisplay.ToneMap);

    ImGui::SetNextItemWidth(kItemWidth);

    if (ImGui::Combo(FLocalization::Text(EUiText::ToneMapping), &toneMapIndex, toneMapNames, IM_ARRAYSIZE(toneMapNames)))
    {
        SelectedDisplay.ToneMap = static_cast<EToneMapOperator>(toneMapIndex);
        NotifyDisplaySettingsChanged();
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip(FLocalization::Text(EUiText::ToneMappingHelp));
    }

    if (SelectedDisplay.ToneMap == EToneMapOperator::Reinhard)
    {
        float white = SelectedDisplay.ToneMapWhite;

        ImGui::SetNextItemWidth(kNumberWidth);

        if (ImGui::DragFloat(FLocalization::Text(EUiText::MappingWhite), &white, 0.1f, 1.0f, 64.0f, "%.1f"))
        {
            SelectedDisplay.ToneMapWhite = std::clamp(white, 1.0f, 64.0f);
            NotifyDisplaySettingsChanged();
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip(FLocalization::Text(EUiText::MappingWhiteHelp));
        }
    }

    ImGui::EndDisabled();

    // --- 曝光 ---
    float exposure = SelectedDisplay.ExposureStops;

    ImGui::SetNextItemWidth(kNumberWidth);

    if (ImGui::DragFloat(FLocalization::Text(EUiText::Exposure), &exposure, 0.1f, -10.0f, 10.0f, "%+.2f"))
    {
        SelectedDisplay.ExposureStops = std::clamp(exposure, -10.0f, 10.0f);
        NotifyDisplaySettingsChanged();
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(FLocalization::Text(EUiText::ExposureHelp));
    }

    // --- 超范围高亮 ---
    bool bShowOutOfRange = SelectedDisplay.bShowOutOfRange;

    if (ImGui::Checkbox(FLocalization::Text(EUiText::HighlightOutOfRange), &bShowOutOfRange))
    {
        SelectedDisplay.bShowOutOfRange = bShowOutOfRange;
        NotifyDisplaySettingsChanged();
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(FLocalization::Text(EUiText::HighlightOutOfRangeHelp));
    }

    RenderHdrOutputStatus();
}

void FPropertyPanel::RenderHdrOutputStatus()
{
    FHdrPresenter* presenter = FHdrPresenter::Get();

    if (!presenter)
    {
        return;
    }

    const FHdrDisplayInfo& info = presenter->GetDisplayInfo();

    ImGui::Separator();

    // 通路没就绪或这块屏没开 HDR 时，勾上也不会有任何效果 ——
    // 置灰比让用户以为"勾了没生效"要好，具体原因由 GetStatusText 给出
    const bool bAvailable = presenter->IsHdrAvailable();

    ImGui::BeginDisabled(!bAvailable);

    // 开关留给用户：同一幅图在 HDR 输出与 SDR 色调映射下来回切，是判断
    // "到底是素材的问题还是显示链路的问题"最快的办法。不可用时显示实际生效状态
    // （未勾选），但 presenter 内仍保留用户偏好，窗口回到 HDR 屏后可以自动恢复。
    bool bEnabled = presenter->IsHdrActive();

    if (ImGui::Checkbox(FLocalization::Text(EUiText::HdrOutput), &bEnabled))
    {
        presenter->SetUserEnabled(bEnabled);
    }

    ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        if (bAvailable)
        {
            ImGui::SetTooltip(FLocalization::Text(EUiText::HdrOutputHelp));
        }
        else
        {
            ImGui::SetTooltip(FLocalization::Text(EUiText::HdrUnavailableHelp),
                              FLocalization::Translate(presenter->GetStatusText().c_str()));
        }
    }

    ImGui::SameLine();

    if (presenter->IsHdrActive())
    {
        ImGui::TextColored(ImVec4(0.13f, 0.55f, 0.35f, 1.0f), FLocalization::Text(EUiText::HdrEnabled));
        ImGui::TextDisabled(FLocalization::Text(EUiText::HdrDisplayInfo),
                            info.OutputName.c_str(), info.MaxNits, info.SdrWhiteNits);
        ImGui::TextDisabled(FLocalization::Text(EUiText::HdrLuminanceHelp));
    }
    else if (bAvailable)
    {
        // 通路可用、只是用户自己关掉了：这是对比"HDR 前后"的正常状态
        ImGui::TextDisabled(FLocalization::Text(EUiText::HdrDisabled));
        ImGui::TextDisabled(FLocalization::Text(EUiText::HdrDisplayInfo),
                            info.OutputName.c_str(), info.MaxNits, info.SdrWhiteNits);
    }
    else
    {
        ImGui::TextDisabled(FLocalization::Text(EUiText::Unavailable));
        ImGui::TextDisabled("%s", FLocalization::Translate(presenter->GetStatusText().c_str()));
    }
}

void FPropertyPanel::RenderByteOrderSelector()
{
    const FByteOrderPropertyState state =
        FImageFormatDesc::ResolveByteOrder(
            SelectedFormat,
            SelectedByteOrder);
    const bool bApplicable =
        state.Mode != EFormatPropertyMode::NotApplicable;
    const bool bConfigurable =
        state.Mode == EFormatPropertyMode::Configurable;
    const char* fixedOrUnavailableTooltip = nullptr;

    if (!bApplicable)
    {
        fixedOrUnavailableTooltip =
            FLocalization::Text(EUiText::ByteOrderNotApplicable);
    }
    else if (!bConfigurable)
    {
        fixedOrUnavailableTooltip =
            state.Value == EByteOrder::LittleEndian
                ? FLocalization::Text(EUiText::FixedLittleEndian)
                : FLocalization::Text(EUiText::FixedBigEndian);
    }

    ImGui::TableNextRow();
    ImGui::BeginDisabled(!bConfigurable);
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(FLocalization::Text(EUiText::ByteOrder));

    ImGui::TableSetColumnIndex(1);

    const bool bLittleSelected =
        bApplicable &&
        state.Value == EByteOrder::LittleEndian;

    if (ImGui::RadioButton(FLocalization::Text(EUiText::LittleEndian), bLittleSelected))
    {
        SelectedByteOrder = EByteOrder::LittleEndian;

        if (OnByteOrderChanged)
        {
            OnByteOrderChanged(SelectedByteOrder);
        }
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip(
            "%s",
            fixedOrUnavailableTooltip
                ? fixedOrUnavailableTooltip
                : FLocalization::Text(EUiText::LittleEndianHelp));
    }

    ImGui::TableSetColumnIndex(2);
    const bool bBigSelected =
        bApplicable &&
        state.Value == EByteOrder::BigEndian;

    if (ImGui::RadioButton(FLocalization::Text(EUiText::BigEndian), bBigSelected))
    {
        SelectedByteOrder = EByteOrder::BigEndian;

        if (OnByteOrderChanged)
        {
            OnByteOrderChanged(SelectedByteOrder);
        }
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip(
            "%s",
            fixedOrUnavailableTooltip
                ? fixedOrUnavailableTooltip
                : FLocalization::Text(EUiText::BigEndianHelp));
    }

    ImGui::EndDisabled();
}

void FPropertyPanel::RenderSampleAlignmentSelector()
{
    const FStorageLayoutDesc& layout =
        FImageFormatDesc::Get(SelectedFormat).StorageLayout;
    const FSampleAlignmentPropertyState state =
        FImageFormatDesc::ResolveSampleAlignment(
            SelectedFormat,
            SelectedBitsPerPixel,
            SelectedSampleAlignment);
    const bool bApplicable =
        state.Mode != EFormatPropertyMode::NotApplicable;
    const bool bConfigurable =
        state.Mode == EFormatPropertyMode::Configurable;
    const char* fixedOrUnavailableTooltip = nullptr;

    if (!bApplicable)
    {
        fixedOrUnavailableTooltip =
            FLocalization::Text(EUiText::AlignmentNotApplicable);
    }
    else if (!bConfigurable)
    {
        // YUV420SP16/Bayer16 的有效位占满容器时，两种对齐完全等价；把原因说清楚，
        // 避免用户误以为格式没有开放该选项。
        if (layout.SampleAlignmentMode ==
            EFormatPropertyMode::Configurable)
        {
            fixedOrUnavailableTooltip =
                FLocalization::Text(EUiText::FullContainerAlignment);
        }
        else
        {
            fixedOrUnavailableTooltip =
                state.Value == ESampleAlignment::LeastSignificantBits
                    ? FLocalization::Text(EUiText::FixedLowAlignment)
                    : FLocalization::Text(EUiText::FixedHighAlignment);
        }
    }

    ImGui::TableNextRow();
    ImGui::BeginDisabled(!bConfigurable);
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(FLocalization::Text(EUiText::BitAlignment));

    ImGui::TableSetColumnIndex(1);

    const bool bLowSelected =
        bApplicable &&
        state.Value == ESampleAlignment::LeastSignificantBits;

    if (ImGui::RadioButton(FLocalization::Text(EUiText::LowAlignment), bLowSelected))
    {
        SelectedSampleAlignment =
            ESampleAlignment::LeastSignificantBits;

        if (OnSampleAlignmentChanged)
        {
            OnSampleAlignmentChanged(SelectedSampleAlignment);
        }
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip(
            "%s",
            fixedOrUnavailableTooltip
                ? fixedOrUnavailableTooltip
                : FLocalization::Text(EUiText::LowAlignmentHelp));
    }

    ImGui::TableSetColumnIndex(2);
    const bool bHighSelected =
        bApplicable &&
        state.Value == ESampleAlignment::MostSignificantBits;

    if (ImGui::RadioButton(FLocalization::Text(EUiText::HighAlignment), bHighSelected))
    {
        SelectedSampleAlignment =
            ESampleAlignment::MostSignificantBits;

        if (OnSampleAlignmentChanged)
        {
            OnSampleAlignmentChanged(SelectedSampleAlignment);
        }
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip(
            "%s",
            fixedOrUnavailableTooltip
                ? fixedOrUnavailableTooltip
                : FLocalization::Text(EUiText::HighAlignmentHelp));
    }

    ImGui::EndDisabled();
}

void FPropertyPanel::RenderStorageLayoutSelectors()
{
    constexpr ImGuiTableFlags kStorageLayoutTableFlags =
        ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_NoSavedSettings |
        ImGuiTableFlags_NoPadOuterX;

    if (ImGui::BeginTable(
            "##StorageLayout",
            kStorageLayoutColumnCount,
            kStorageLayoutTableFlags))
    {
        RenderByteOrderSelector();
        RenderSampleAlignmentSelector();
        ImGui::EndTable();
    }
}

void FPropertyPanel::ConstrainStorageLayout()
{
    SelectedBitsPerPixel =
        FImageFormatDesc::ResolveBitDepth(
            SelectedFormat,
            SelectedBitsPerPixel).Value;

    SelectedByteOrder =
        FImageFormatDesc::ResolveByteOrder(
            SelectedFormat,
            SelectedByteOrder).Value;
    SelectedSampleAlignment =
        FImageFormatDesc::ResolveSampleAlignment(
            SelectedFormat,
            SelectedBitsPerPixel,
            SelectedSampleAlignment).Value;
}

FImageLoadParams FPropertyPanel::GetLoadParams() const
{
    FImageLoadParams params;
    params.Format = SelectedFormat;
    params.Width = SelectedWidth;
    params.Height = SelectedHeight;
    params.Stride = SelectedStride;
    params.BitsPerPixel = SelectedBitsPerPixel;
    params.BayerPattern = SelectedBayerPattern;
    params.ByteOrder = SelectedByteOrder;
    params.SampleAlignment = SelectedSampleAlignment;
    params.ConstrainStorageLayout();
    return params;
}

void FPropertyPanel::SetFormat(EImageFormat Format)
{
    if (Format != SelectedFormat)
    {
        SelectedFormat = Format;
        const FFormatDesc& desc =
            FImageFormatDesc::Get(SelectedFormat);
        SelectedBitsPerPixel = desc.BitDepth;
        SelectedByteOrder = desc.StorageLayout.DefaultByteOrder;
        SelectedSampleAlignment =
            desc.StorageLayout.DefaultSampleAlignment;
        ConstrainStorageLayout();
    }
}

void FPropertyPanel::SetResolution(int32_t Width, int32_t Height)
{
    if (Width > 0 && Height > 0)
    {
        SelectedWidth = Width;
        SelectedHeight = Height;
    }
}

void FPropertyPanel::SetStride(int32_t Stride)
{
    // 面板上不再出现"0"这个需要额外解释的值：文档里的 0 表示紧凑排列，
    // 到了输入框里就换算成具体的字节数（8bit 的 NV21 即等于宽度），
    // 用户直接在这个数上加 padding 就行
    SelectedStride = Stride > 0 ? Stride : GetCompactStride(SelectedFormat, SelectedWidth);
}

int32_t FPropertyPanel::GetCompactStride(EImageFormat Format, int32_t Width) const
{
    return FImageFormatDesc::ResolveBaseStride(Format, Width, 0);
}

void FPropertyPanel::RefreshStrideForGeometry(EImageFormat OldFormat, int32_t OldWidth)
{
    const int32_t oldCompactStride = GetCompactStride(OldFormat, OldWidth);
    const bool bHadCustomStride =
        SelectedStride > 0 && SelectedStride != oldCompactStride;

    if (bHadCustomStride)
    {
        const FFormatDesc& newDesc = FImageFormatDesc::Get(SelectedFormat);
        const bool bLargeEnough =
            FImageFormatDesc::ResolveBaseStride(
                SelectedFormat,
                SelectedWidth,
                SelectedStride) == SelectedStride;
        bool bPlaneAligned = bLargeEnough;

        // GL_UNPACK_ROW_LENGTH 以纹素为单位；只有每个平面的物理行都包含整数个
        // 纹素时，旧 padding 才能安全跨格式/宽度沿用。
        for (int32_t planeIndex = 0;
             bPlaneAligned && planeIndex < newDesc.PlaneCount;
             ++planeIndex)
        {
            const FPlaneDesc& plane = newDesc.Planes[planeIndex];
            const int32_t bytesPerTexel =
                plane.ChannelCount * plane.BytesPerSample;
            const int32_t planeStride =
                FImageFormatDesc::GetPlaneStrideBytes(
                    newDesc,
                    planeIndex,
                    SelectedStride);

            bPlaneAligned =
                bytesPerTexel > 0 &&
                planeStride > 0 &&
                planeStride % bytesPerTexel == 0;
        }

        if (bPlaneAligned)
        {
            return;
        }
    }

    SelectedStride = GetCompactStride(SelectedFormat, SelectedWidth);
}

void FPropertyPanel::SetBitsPerPixel(int32_t BitsPerPixel)
{
    if (BitsPerPixel > 0)
    {
        SelectedBitsPerPixel = BitsPerPixel;
        ConstrainStorageLayout();
    }
}
