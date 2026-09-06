#include "Core/FLocalization.h"
#include "FComparePanel.h"

#include "Core/FFileDialog.h"
#include "Core/FImageDocument.h"
#include "Core/FUserSettings.h"
#include "Image/FImageLoader.h"
#include "FUiTheme.h"

#include <imgui.h>

#include <filesystem>

namespace
{
    const ImVec4 kPanelTabTextColor(1.0f, 1.0f, 1.0f, 1.0f);
    constexpr int32_t kPanelTabStyleColorCount = 5;
}

FComparePanel::FComparePanel()
    : MainDocument(nullptr)
    , CompareDocument(nullptr)
    , DiffDocument(nullptr)
    , ViewTarget(EViewTarget::Main)
    , SingleImageTarget(EViewTarget::Main)
    , Gain(8.0f)
{
}

EViewTarget FComparePanel::GetRememberedCompareViewTarget() const
{
    return FUserSettings::GetCompareMode() ==
            FUserSettings::ECompareMode::SideBySide
        ? EViewTarget::SideBySide
        : EViewTarget::Compare;
}

void FComparePanel::SetUserSelectedViewTarget(EViewTarget InTarget)
{
    SetViewTarget(InTarget);

    // 差值图只反映本次计算结果，不能覆盖用户下次加载对比图时要恢复的常用模式。
    if (IsSingleImageViewTarget(InTarget))
    {
        FUserSettings::SetCompareMode(
            FUserSettings::ECompareMode::SingleImageSwitch);
    }
    else if (InTarget == EViewTarget::SideBySide)
    {
        FUserSettings::SetCompareMode(
            FUserSettings::ECompareMode::SideBySide);
    }
}

bool FComparePanel::RenderClearButton(const char* Id, const char* Tooltip, bool bEnabled)
{
    ImGui::BeginDisabled(!bEnabled);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    // 用 U+00D7 而不是字母 X：字体是 msyh + 中文字形范围，这个码点在里面
    const std::string label = std::string(u8"×") + Id;
    const bool bClicked = ImGui::SmallButton(label.c_str());

    ImGui::PopStyleColor();
    ImGui::EndDisabled();

    if (bEnabled && ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", Tooltip);
    }

    return bClicked;
}

void FComparePanel::Render()
{
    // Begin() 会把标签颜色保存到该窗口的 DockStyle，全局主题不受影响。
    const ImVec4 selectedTabColor = FUiTheme::GetImGuiColor(
        FUserSettings::EThemeColorRole::Accent);
    ImGui::PushStyleColor(ImGuiCol_Text, kPanelTabTextColor);
    ImGui::PushStyleColor(ImGuiCol_TabSelected, selectedTabColor);
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, selectedTabColor);
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, selectedTabColor);
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelectedOverline, selectedTabColor);
    ImGui::Begin(FLocalization::WindowTitle(EUiText::Compare));
    ImGui::PopStyleColor(kPanelTabStyleColorCount);

    // --- 主图信息 ---
    ImGui::Text(FLocalization::Text(EUiText::MainImage));
    ImGui::SameLine();

    if (RenderClearButton("##ClearMain", FLocalization::Text(EUiText::RemoveMainImage), MainDocument && MainDocument->GetImageData()) && OnClearMain)
    {
        OnClearMain();
    }

    // 注意：清空回调会就地释放文档里的 ImageData，因此下面必须重新取一次状态。
    // 缓存调用前的结果会在点击"×"的那一帧解引用已经变成空的 ImageData。
    if (MainDocument && MainDocument->GetImageData())
    {
        const std::string name = std::filesystem::u8path(MainDocument->GetFilePath()).filename().u8string();
        ImGui::TextWrapped("%s", name.c_str());
        ImGui::TextDisabled("%d x %d", MainDocument->GetImageData()->GetWidth(), MainDocument->GetImageData()->GetHeight());
    }
    else
    {
        ImGui::TextDisabled(FLocalization::Text(EUiText::NotLoaded));
    }

    ImGui::Separator();

    // --- 对比图 ---
    ImGui::Text(FLocalization::Text(EUiText::CompareImage));
    ImGui::SameLine();

    if (RenderClearButton("##ClearCompare", FLocalization::Text(EUiText::RemoveCompareImage), CompareDocument && CompareDocument->GetImageData()) && OnClearCompare)
    {
        OnClearCompare();
    }

    // 同上：状态必须在回调之后重新取
    if (CompareDocument && CompareDocument->GetImageData())
    {
        const std::string name = std::filesystem::u8path(CompareDocument->GetFilePath()).filename().u8string();
        ImGui::TextWrapped("%s", name.c_str());
        ImGui::TextDisabled("%d x %d", CompareDocument->GetImageData()->GetWidth(), CompareDocument->GetImageData()->GetHeight());
    }
    else
    {
        ImGui::TextDisabled(FLocalization::Text(EUiText::NotLoaded));
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    if (ImGui::Button(FLocalization::Text(EUiText::ChooseCompareImageAction)))
    {
        std::string path;

        if (FFileDialog::OpenFile(FLocalization::Text(EUiText::ChooseCompareImage), FImageLoaderFactory::GetAllSupportedExtensions(), path))
        {
            if (OnPickCompareFile)
            {
                OnPickCompareFile(path);
            }
        }
    }

    ImGui::PopStyleColor();

    ImGui::Separator();

    // --- 差值 ---
    ImGui::Text(FLocalization::Text(EUiText::Difference));

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat(u8"##Gain", &Gain, 1.0f, 64.0f, FLocalization::Text(EUiText::DifferenceGain));

    const bool bCanCompare =
        MainDocument && MainDocument->GetImageData() &&
        CompareDocument && CompareDocument->GetImageData();

    ImGui::BeginDisabled(!bCanCompare);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    if (ImGui::Button(FLocalization::Text(EUiText::ComputeDifference)))
    {
        if (OnComputeDiff)
        {
            OnComputeDiff(Gain);
        }
    }

    ImGui::PopStyleColor();
    ImGui::EndDisabled();

    if (!bCanCompare)
    {
        ImGui::TextDisabled(FLocalization::Text(EUiText::LoadBothImages));
    }

    // --- 统计量 ---
    if (Stats.bValid)
    {
        ImGui::Separator();

        ImGui::Text(FLocalization::Text(EUiText::MaximumDifference), Stats.MaxAbsDiff);
        ImGui::Text(FLocalization::Text(EUiText::MeanDifference), Stats.MeanAbsDiff);
        ImGui::Text(FLocalization::Text(EUiText::DifferentPixels),
                    static_cast<long long>(Stats.DiffPixelCount),
                    Stats.DiffPixelRatio * 100.0);

        if (Stats.PSNR < 0.0)
        {
            ImGui::TextColored(ImVec4(0.2f, 0.7f, 0.3f, 1.0f), FLocalization::Text(EUiText::IdenticalPsnr));
        }
        else
        {
            ImGui::Text(u8"PSNR: %.2f dB", Stats.PSNR);
        }
    }
    else if (!Stats.Error.empty())
    {
        ImGui::Separator();
        const char* sizeMismatchPrefix = FLocalization::kTextResources[
            static_cast<size_t>(EUiText::CompareSizeMismatchPrefix)].Chinese;
        // The comparison service appends dimensions to this diagnostic; preserve that numeric payload.
        const std::string error = Stats.Error.rfind(sizeMismatchPrefix, 0) == 0
            ? std::string(FLocalization::Text(EUiText::CompareSizeMismatchPrefix))
                + Stats.Error.substr(std::char_traits<char>::length(sizeMismatchPrefix))
            : FLocalization::Translate(Stats.Error.c_str());
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", error.c_str());
    }

    // --- 显示切换 ---
    ImGui::Separator();
    ImGui::Text(FLocalization::Text(EUiText::CompareMode));

    const bool bHasCompare = CompareDocument && CompareDocument->GetImageData();
    const bool bHasDiff = DiffDocument && DiffDocument->GetImageData();
    const bool bSingleImage = IsSingleImageViewTarget(ViewTarget);

    if (ImGui::RadioButton(FLocalization::Text(EUiText::SingleImageSwitch), bSingleImage))
    {
        SetUserSelectedViewTarget(
            SingleImageTarget == EViewTarget::Compare && bHasCompare
                ? EViewTarget::Compare
                : EViewTarget::Main);
    }

    ImGui::BeginDisabled(!bHasDiff);

    if (ImGui::RadioButton(FLocalization::Text(EUiText::DifferenceImage), ViewTarget == EViewTarget::Diff))
    {
        SetUserSelectedViewTarget(EViewTarget::Diff);
    }

    ImGui::EndDisabled();

    ImGui::BeginDisabled(!bHasCompare);

    if (ImGui::RadioButton(FLocalization::Text(EUiText::TileImages), ViewTarget == EViewTarget::SideBySide))
    {
        SetUserSelectedViewTarget(EViewTarget::SideBySide);
    }

    ImGui::EndDisabled();

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(FLocalization::Text(EUiText::TileImagesHelp));
    }

    ImGui::End();
}
