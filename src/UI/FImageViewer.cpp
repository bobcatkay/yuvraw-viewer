#include "Core/FLocalization.h"
#include "FImageViewer.h"

#include "Core/FHdrPresenter.h"
#include "Core/FImageDocument.h"
#include "gl/FShader.h"
#include "gl/FShaderManager.h"
#include "gl/FShaders.h"
#include "gl/FTextureData.h"
#include "gl/FSparseTexture.h"
#include "Image/FColorTransform.h"
#include "Image/FImageData.h"
#include "Image/FImageFormatDesc.h"
#include "Image/FImageSampler.h"
#include "FToast.h"
#include "FUiIcons.h"
#include "FUiScale.h"
#include "FUiTheme.h"
#include "Util.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

namespace
{
    constexpr const char* kImageViewerLogTag = "ImageViewer";
    constexpr GLsizei kQuadVertexCount = 6;
    constexpr GLsizei kQuadVertexStride = 4 * sizeof(float); // 每个顶点含二维位置和二维纹理坐标。

    /// 每个滚轮 tick 的缩放倍率
    constexpr float kZoomStep = 1.1f;
    constexpr float kMinZoom = 0.02f;
    constexpr float kMaxZoom = 50.0f;
    constexpr float kPercentScale = 100.0f;
    constexpr float kPercentToRatio = 0.01f;

    /// 缩放输入框拖满整个量程所需的像素数，用来反推 DragFloat 的速度（见 RenderToolbar）
    constexpr float kZoomDragFullRangePixels = 400.0f;

    /// 平铺模式下两侧之间分隔线的宽度（逻辑像素）
    constexpr float kDividerWidth = 2.0f;

    constexpr float kPaneSplitRatio = 0.5f;
    constexpr float kOverlayMargin = 8.0f;
    constexpr float kOverlayPadding = 4.0f;
    constexpr float kOverlayRounding = 6.0f;
    constexpr int32_t kOverlayAlpha = 89; ///< 原 70% 不透明度减半为 35%
    constexpr int32_t kOverlayButtonHoveredAlpha = 41;
    constexpr int32_t kOverlayButtonActiveAlpha = 64;
    constexpr ImU32 kOverlayBackground = IM_COL32(0, 0, 0, kOverlayAlpha);
    constexpr ImU32 kOverlayButtonHovered =
        IM_COL32(255, 255, 255, kOverlayButtonHoveredAlpha);
    constexpr ImU32 kOverlayButtonActive =
        IM_COL32(255, 255, 255, kOverlayButtonActiveAlpha);
    constexpr ImU32 kOverlayText = IM_COL32(255, 255, 255, 230);
    /// 身份高亮跟随用户的主题高亮色，但保持浮层的半透明质感。
    constexpr float kOverlayHighlightedAlpha = 235.0f / 255.0f;
    constexpr ImU32 kDividerColor = IM_COL32(120, 120, 120, 255);
    constexpr float kOverlayIconRatio = 0.80f;
    constexpr float kToolbarLayoutGap = 8.0f;
    constexpr float kToolbarGroupGapMultiplier = 2.5f;
    constexpr float kCenterAlignmentRatio = 0.5f;
    constexpr float kLoadingSpinnerRadius = 13.0f;
    constexpr float kLoadingSpinnerThickness = 3.0f;
    constexpr float kLoadingCardPadding = 16.0f;
    constexpr float kLoadingContentGap = 12.0f;
    constexpr float kLoadingCardRounding = 8.0f;
    constexpr float kLoadingPaneInset = 8.0f;
    constexpr ImU32 kLoadingPaneDim = IM_COL32(0, 0, 0, 96);
    constexpr ImU32 kLoadingCardBackground = IM_COL32(18, 25, 31, 235);
    constexpr ImU32 kLoadingCardBorder = IM_COL32(30, 140, 148, 210);


    constexpr float kSingleImageHintHorizontalPadding = 12.0f;
    constexpr float kSingleImageHintVerticalPadding = 6.0f;

    constexpr int32_t kQuarterTurnsPerCircle = 4;
    constexpr int32_t kHalfTurnQuarterSteps = 2;
    constexpr int32_t kClockwiseQuarterStep = 1;
    constexpr int32_t kCounterClockwiseQuarterStep = -1;
    constexpr int32_t kPaneOrientationButtonCount = 4;
    constexpr int32_t kPaneCloseButtonIndex = kPaneOrientationButtonCount;
    constexpr int32_t kPaneOverlayButtonCount = kPaneOrientationButtonCount + 1;
    constexpr int32_t kToolbarLayoutButtonCount = 2;
    struct FPaneRect
    {
        ImVec2 Min;
        ImVec2 Max;

        float Width() const { return std::max(0.0f, Max.x - Min.x); }
        float Height() const { return std::max(0.0f, Max.y - Min.y); }

        bool Contains(const ImVec2& Point) const
        {
            return Point.x >= Min.x && Point.x <= Max.x &&
                   Point.y >= Min.y && Point.y <= Max.y;
        }
    };

    struct FPaneOverlayLayout
    {
        FPaneRect Bounds;
        FPaneRect OrientationGroup;
    };

    FPaneOverlayLayout MakePaneOverlayLayout(const ImVec2& PaneMin, const ImVec2& PaneMax)
    {
        const float buttonSize = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float padding = FUiScale::Apply(kOverlayPadding);
        const float margin = FUiScale::Apply(kOverlayMargin);
        const float groupWidth =
            buttonSize * static_cast<float>(kPaneOrientationButtonCount)
            + spacing * static_cast<float>(kPaneOrientationButtonCount - 1)
            + padding * 2.0f;
        const float groupHeight = buttonSize + padding * 2.0f;
        // 关闭按钮独立成块，组间距复用自适应/填充按钮的 ItemSpacing。
        const float controlsWidth = groupWidth + spacing + groupHeight;

        if (PaneMax.x - PaneMin.x < controlsWidth + margin * 2.0f ||
            PaneMax.y - PaneMin.y < groupHeight + margin * 2.0f)
        {
            return { { PaneMin, PaneMin }, { PaneMin, PaneMin } };
        }

        const ImVec2 max(PaneMax.x - margin, PaneMin.y + margin + groupHeight);
        const ImVec2 min(max.x - controlsWidth, max.y - groupHeight);
        return { { min, max }, { min, ImVec2(min.x + groupWidth, max.y) } };
    }

    ImVec2 GetPaneOverlayButtonPosition(
        const ImVec2& GroupMin,
        const ImVec2& GroupMax,
        int32_t ButtonIndex)
    {
        const float padding = FUiScale::Apply(kOverlayPadding);
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        return ImVec2(
            ButtonIndex == kPaneCloseButtonIndex
                ? GroupMax.x + spacing + padding
                : GroupMin.x + padding
                    + static_cast<float>(ButtonIndex) * (ImGui::GetFrameHeight() + spacing),
            GroupMin.y + padding);
    }

    bool HasValidImage(const FImageDocument* Doc)
    {
        return Doc && Doc->GetImageData() && Doc->GetImageData()->IsValid();
    }

    std::string GetDocumentFileName(const FImageDocument* Doc)
    {
        if (!Doc || Doc->GetFilePath().empty())
        {
            return {};
        }

        // 文档路径统一是 UTF-8；只在文件系统边界转换，保证中文文件名可直接交给 ImGui。
        const std::string fileName =
            std::filesystem::u8path(Doc->GetFilePath()).filename().u8string();
        return fileName.empty() ? Doc->GetFilePath() : fileName;
    }

    float CalculateVerticallyCenteredTextY(
        const char* Text,
        float ContainerMinY,
        float ContainerMaxY)
    {
        ImFont* font = ImGui::GetFont();
        const float fontScale = ImGui::GetFontSize() / font->FontSize;
        const char* cursor = Text;
        const char* textEnd = Text + std::strlen(Text);
        float glyphMinY = 0.0f;
        float glyphMaxY = 0.0f;
        bool bHasVisibleGlyph = false;

        while (cursor < textEnd)
        {
            unsigned int codepoint = 0;
            const int32_t byteCount = ImTextCharFromUtf8(&codepoint, cursor, textEnd);

            if (byteCount <= 0)
            {
                break;
            }

            cursor += byteCount;
            const ImFontGlyph* glyph = font->FindGlyph(static_cast<ImWchar>(codepoint));

            if (!glyph || !glyph->Visible)
            {
                continue;
            }

            if (!bHasVisibleGlyph)
            {
                glyphMinY = glyph->Y0;
                glyphMaxY = glyph->Y1;
                bHasVisibleGlyph = true;
            }
            else
            {
                glyphMinY = std::min(glyphMinY, glyph->Y0);
                glyphMaxY = std::max(glyphMaxY, glyph->Y1);
            }
        }

        const float containerCenterY =
            (ContainerMinY + ContainerMaxY) * kCenterAlignmentRatio;

        if (!bHasVisibleGlyph)
        {
            return containerCenterY
                - ImGui::GetFontSize() * kCenterAlignmentRatio;
        }

        // CalcTextSize 使用整行高，微软雅黑字形在行框内略偏下；按实际字形边界居中，
        // 才能让“主图 / 对比图”在半透明底色中获得相等的上下留白。
        const float glyphCenterY =
            (glyphMinY + glyphMaxY) * fontScale * kCenterAlignmentRatio;
        return containerCenterY - glyphCenterY;
    }

    std::string FormatHex(const FPixelSample& Sample)
    {
        char buffer[16];

        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X",
                      static_cast<int32_t>(Sample.Rgb[0] * 255.0f + 0.5f),
                      static_cast<int32_t>(Sample.Rgb[1] * 255.0f + 0.5f),
                      static_cast<int32_t>(Sample.Rgb[2] * 255.0f + 0.5f));

        return buffer;
    }

    /**
     * 把像素探针里显示的内容拼成可粘贴的纯文本
     *
     * 一行一条数据，列之间用制表符 —— 粘进表格能自动分列，粘进纯文本编辑器也不会糊成一片；
     * 换行用 CRLF，这是 Windows 剪贴板的惯例（只认 CRLF 的程序会把 \n 粘成一整行）。
     *
     * @param Compare 平铺模式下的对比图取样，单图模式传 nullptr
     */
    std::string BuildProbeText(
        int32_t ImageX,
        int32_t ImageY,
        const FPixelSample& Main,
        const FPixelSample* Compare)
    {
        char line[256];
        std::string text;

        std::snprintf(line, sizeof(line), FLocalization::Text(EUiText::CopiedCoordinates), ImageX, ImageY);
        text += line;

        if (Compare)
        {
            text += FLocalization::Text(EUiText::CopiedSampleHeader);

            const int32_t count = std::min(Main.Count, Compare->Count);

            for (int32_t i = 0; i < count; ++i)
            {
                const int32_t delta = Main.Values[i] - Compare->Values[i];

                std::snprintf(line, sizeof(line), "%s\t%d\t%d\t%s%d\r\n",
                              Main.Labels[i], Main.Values[i], Compare->Values[i],
                              delta > 0 ? "+" : "", delta);
                text += line;
            }

            std::snprintf(line, sizeof(line), "RGB\t%s\t%s\r\n",
                          FormatHex(Main).c_str(), FormatHex(*Compare).c_str());
            text += line;
        }
        else
        {
            for (int32_t i = 0; i < Main.Count; ++i)
            {
                const int32_t componentMax = Main.GetComponentMaxValue(i);
                const float percent = componentMax > 0
                    ? (100.0f * static_cast<float>(Main.Values[i]) / static_cast<float>(componentMax))
                    : 0.0f;

                std::snprintf(line, sizeof(line), "%s\t%d\t%.1f%%\r\n",
                              Main.Labels[i], Main.Values[i], percent);
                text += line;
            }

            std::snprintf(line, sizeof(line), "RGB\t%s\r\n", FormatHex(Main).c_str());
            text += line;

            if (Main.bPipelineActive)
            {
                std::snprintf(line, sizeof(line), FLocalization::Text(EUiText::CopiedLuminance), Main.LinearNits);
                text += line;
            }
        }

        return text;
    }
}

FImageViewer::FImageViewer()
    : Document(nullptr)
    , SecondaryDocument(nullptr)
    , SingleImageComparePeer(nullptr)
    , SelectedDocument(nullptr)
    , bSingleImageSwitchEnabled(false)
    , bSingleImageSwitchHintVisible(false)
    , bSingleImageSwitchShowingCompare(false)
    , TileLayout(ETileLayout::Horizontal)
    , bPanZoomSynchronized(true)
    , ProbeX(-1)
    , ProbeY(-1)
    , DropRectMin{ 0.0f, 0.0f }
    , DropRectMax{ 0.0f, 0.0f }
    , bDropRectValid(false)
    , PendingPaneOverlayVisualCount(0)
    , PendingLoadingVisualCount(0)
    , QuadVBO(0)
    , bOpenGLResourcesInitialized(false)
{
    InitializeOpenGLResources();
}

FImageViewer::~FImageViewer()
{
    DestroyOpenGLResources();
}

bool FImageViewer::IsSideBySide() const
{
    return HasValidImage(Document) && HasValidImage(SecondaryDocument);
}

void FImageViewer::SetSingleImageComparePeer(FImageDocument* InDocument)
{
    SingleImageComparePeer =
        InDocument && InDocument != Document ? InDocument : nullptr;

    if (SingleImageComparePeer)
    {
        // UpdateViewTarget 每帧调用本方法。以当前可见文档为唯一真值持续对齐隐藏文档，
        // 也覆盖新文件刚恢复了自己的缓存配置、但尚未首次显示的边界情况。
        SynchronizePanZoomFrom(Document, SingleImageComparePeer);
    }
}

void FImageViewer::SetDocumentLoading(
    FImageDocument* Target,
    bool bLoading,
    const std::string& Label)
{
    if (!Target)
    {
        return;
    }

    if (bLoading)
    {
        FDocumentLoadingState& state = DocumentLoadingStates[Target];
        state.Label = Label.empty() ? FLocalization::Text(EUiText::Loading) : Label;
    }
    else
    {
        DocumentLoadingStates.erase(Target);
    }
}

bool FImageViewer::IsDocumentLoading(FImageDocument* Target) const
{
    return Target && DocumentLoadingStates.find(Target) != DocumentLoadingStates.end();
}

void FImageViewer::SetTileLayout(ETileLayout Layout)
{
    if (TileLayout == Layout)
    {
        return;
    }

    TileLayout = Layout;

    // 平铺方向变化会改变每个 pane 的中心与可用尺寸，旧偏移在新方向下没有意义。
    if (Document)
    {
        GetViewState(Document).Settings.PanOffsetX = 0.0f;
        GetViewState(Document).Settings.PanOffsetY = 0.0f;
    }

    if (SecondaryDocument)
    {
        GetViewState(SecondaryDocument).Settings.PanOffsetX = 0.0f;
        GetViewState(SecondaryDocument).Settings.PanOffsetY = 0.0f;
    }

    LOGD("SetTileLayout", "Tile layout changed: %s",
         TileLayout == ETileLayout::Horizontal ? "horizontal" : "vertical");
}

void FImageViewer::Render()
{
    PendingPaneOverlayVisualCount = 0;
    PendingLoadingVisualCount = 0;
    PendingSingleImageHintVisual = FPendingSingleImageHintVisual{};

    // 所有 tab 背景已经统一为深青, 因此标题文字始终使用白色 (避免因焦点/悬停状态切换造成闪烁)
    // Begin() 内部完成 tab/标题绘制后必须立即 Pop, 保证面板内容仍使用默认文字色
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    const bool bVisible = ImGui::Begin(FLocalization::WindowTitle(EUiText::ImageViewer));
    ImGui::PopStyleColor();

    // 折叠或处于未选中的标签页时不接收拖放
    bDropRectValid = bVisible;

    if (bVisible)
    {
        const ImVec2 windowPos = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();

        DropRectMin[0] = windowPos.x;
        DropRectMin[1] = windowPos.y;
        DropRectMax[0] = windowPos.x + windowSize.x;
        DropRectMax[1] = windowPos.y + windowSize.y;

        RenderToolbar();

        ImGui::Separator();

        RenderImage();
    }

    ImGui::End();
}

void FImageViewer::RenderOverlayVisuals()
{
    constexpr FUiIcons::EViewerGlyph kButtonGlyphs[kPaneOverlayButtonCount] = {
        FUiIcons::EViewerGlyph::MirrorHorizontal,
        FUiIcons::EViewerGlyph::MirrorVertical,
        FUiIcons::EViewerGlyph::RotateClockwise,
        FUiIcons::EViewerGlyph::RotateCounterClockwise,
        FUiIcons::EViewerGlyph::Close,
    };

    for (int32_t overlayIndex = 0;
         overlayIndex < PendingPaneOverlayVisualCount;
         ++overlayIndex)
    {
        const FPendingPaneOverlayVisual& visual =
            PendingPaneOverlayVisuals[overlayIndex];
        ImGuiViewport* viewport =
            ImGui::FindViewportByID(static_cast<ImGuiID>(visual.ViewportId));

        if (!viewport)
        {
            viewport = ImGui::GetMainViewport();
        }

        ImDrawList* overlay = ImGui::GetForegroundDrawList(viewport);

        if (visual.bHasLabel && !visual.Label.empty())
        {
            const ImVec2 labelMin(visual.LabelMinX, visual.LabelMinY);
            const ImVec2 labelMax(visual.LabelMaxX, visual.LabelMaxY);
            const char* label = visual.Label.c_str();
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            const float labelPadding = FUiScale::Apply(kOverlayPadding);
            const ImU32 highlightedBackground =
                ImGui::ColorConvertFloat4ToU32(FUiTheme::GetImGuiColor(
                    FUserSettings::EThemeColorRole::Accent,
                    kOverlayHighlightedAlpha));
            const bool bHasIdentityLabel =
                visual.bHasIdentityLabel && !visual.IdentityLabel.empty();
            const char* identityLabel = bHasIdentityLabel
                ? visual.IdentityLabel.c_str()
                : nullptr;
            const float textMinX = bHasIdentityLabel
                ? visual.IdentityLabelMaxX + labelPadding
                : labelMin.x + labelPadding;
            const float textY = CalculateVerticallyCenteredTextY(
                bHasIdentityLabel ? identityLabel : label,
                labelMin.y,
                labelMax.y);
            const ImVec2 textMin(
                textMinX,
                textY);
            const ImVec2 textMax(
                labelMax.x - labelPadding,
                labelMax.y);

            if (bHasIdentityLabel)
            {
                const ImVec2 identityMax(
                    visual.IdentityLabelMaxX,
                    labelMax.y);

                // 两块底色并排绘制，避免半透明高亮叠在黑底上变暗。
                overlay->AddRectFilled(
                    labelMin,
                    identityMax,
                    highlightedBackground,
                    FUiScale::Apply(kOverlayRounding),
                    ImDrawFlags_RoundCornersLeft);
                overlay->AddText(
                    ImVec2(
                        labelMin.x + labelPadding,
                        textY),
                    kOverlayText,
                    identityLabel);
                overlay->AddRectFilled(
                    ImVec2(identityMax.x, labelMin.y),
                    labelMax,
                    kOverlayBackground,
                    FUiScale::Apply(kOverlayRounding),
                    ImDrawFlags_RoundCornersRight);
            }
            else
            {
                overlay->AddRectFilled(
                    labelMin,
                    labelMax,
                    visual.bSelected ? highlightedBackground : kOverlayBackground,
                    FUiScale::Apply(kOverlayRounding));
            }

            // 文件名可能很长；限制在标签底色内并保留 UTF-8 字符边界，由 ImGui 绘制省略号。
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ImGui::ColorConvertU32ToFloat4(kOverlayText));
            ImGui::RenderTextEllipsis(
                overlay,
                textMin,
                textMax,
                textMax.x,
                textMax.x,
                label,
                nullptr,
                &textSize);
            ImGui::PopStyleColor();
        }

        if (!visual.bHasControls)
        {
            continue;
        }

        const ImVec2 groupMin(visual.GroupMinX, visual.GroupMinY);
        const ImVec2 groupMax(visual.GroupMaxX, visual.GroupMaxY);
        const float buttonSize = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;

        overlay->AddRectFilled(
            groupMin,
            groupMax,
            kOverlayBackground,
            FUiScale::Apply(kOverlayRounding));

        const float closeGroupSize = groupMax.y - groupMin.y;
        const ImVec2 closeGroupMin(groupMax.x + spacing, groupMin.y);
        overlay->AddRectFilled(
            closeGroupMin,
            ImVec2(closeGroupMin.x + closeGroupSize, groupMax.y),
            kOverlayBackground,
            FUiScale::Apply(kOverlayRounding));

        for (int32_t buttonIndex = 0;
             buttonIndex < kPaneOverlayButtonCount;
             ++buttonIndex)
        {
            const ImVec2 buttonPos =
                GetPaneOverlayButtonPosition(groupMin, groupMax, buttonIndex);
            const ImVec2 buttonMax(
                buttonPos.x + buttonSize,
                buttonPos.y + buttonSize);
            const uint8_t buttonBit =
                static_cast<uint8_t>(1u << static_cast<uint32_t>(buttonIndex));

            if ((visual.ActiveButtonMask & buttonBit) != 0)
            {
                overlay->AddRectFilled(
                    buttonPos,
                    buttonMax,
                    kOverlayButtonActive,
                    ImGui::GetStyle().FrameRounding);
            }
            else if ((visual.HoveredButtonMask & buttonBit) != 0)
            {
                overlay->AddRectFilled(
                    buttonPos,
                    buttonMax,
                    kOverlayButtonHovered,
                    ImGui::GetStyle().FrameRounding);
            }

            const float iconSize = buttonSize * kOverlayIconRatio;
            const float iconInset = (buttonSize - iconSize) * kPaneSplitRatio;
            FUiIcons::DrawViewerGlyph(
                overlay,
                kButtonGlyphs[buttonIndex],
                ImVec2(buttonPos.x + iconInset, buttonPos.y + iconInset),
                iconSize);
        }
    }

    for (int32_t loadingIndex = 0;
         loadingIndex < PendingLoadingVisualCount;
         ++loadingIndex)
    {
        const FPendingLoadingVisual& visual =
            PendingLoadingVisuals[loadingIndex];
        ImGuiViewport* viewport =
            ImGui::FindViewportByID(static_cast<ImGuiID>(visual.ViewportId));

        if (!viewport)
        {
            viewport = ImGui::GetMainViewport();
        }

        ImDrawList* overlay = ImGui::GetForegroundDrawList(viewport);
        const ImVec2 paneMin(visual.MinX, visual.MinY);
        const ImVec2 paneMax(visual.MaxX, visual.MaxY);
        const float paneWidth = paneMax.x - paneMin.x;
        const float paneHeight = paneMax.y - paneMin.y;

        if (paneWidth <= 0.0f || paneHeight <= 0.0f)
        {
            continue;
        }

        overlay->PushClipRect(paneMin, paneMax, true);
        overlay->AddRectFilled(paneMin, paneMax, kLoadingPaneDim);

        const ImVec2 textSize = ImGui::CalcTextSize(visual.Label.c_str());
        const float spinnerRadius = FUiScale::Apply(kLoadingSpinnerRadius);
        const float cardPadding = FUiScale::Apply(kLoadingCardPadding);
        const float contentGap = FUiScale::Apply(kLoadingContentGap);
        const float spinnerDiameter = spinnerRadius * 2.0f;
        const float desiredWidth =
            cardPadding * 2.0f + spinnerDiameter +
            contentGap + textSize.x;
        const float cardWidth = std::min(
            desiredWidth,
            std::max(
                1.0f,
                paneWidth - FUiScale::Apply(kLoadingPaneInset) * 2.0f));
        const float cardHeight =
            std::max(spinnerDiameter, textSize.y) + cardPadding * 2.0f;
        const ImVec2 cardMin(
            paneMin.x + (paneWidth - cardWidth) * kCenterAlignmentRatio,
            paneMin.y + (paneHeight - cardHeight) * kCenterAlignmentRatio);
        const ImVec2 cardMax(cardMin.x + cardWidth, cardMin.y + cardHeight);

        overlay->AddRectFilled(
            cardMin,
            cardMax,
            kLoadingCardBackground,
            FUiScale::Apply(kLoadingCardRounding));
        overlay->AddRect(
            cardMin,
            cardMax,
            kLoadingCardBorder,
            FUiScale::Apply(kLoadingCardRounding));

        const ImVec2 spinnerCenter(
            cardMin.x + cardPadding + spinnerRadius,
            cardMin.y + cardHeight * kCenterAlignmentRatio);
        FUiIcons::DrawSpinner(
            overlay,
            spinnerCenter,
            spinnerRadius,
            FUiScale::Apply(kLoadingSpinnerThickness),
            ImGui::GetTime());

        const ImVec2 textPos(
            spinnerCenter.x + spinnerRadius + contentGap,
            cardMin.y + (cardHeight - textSize.y) * kCenterAlignmentRatio);
        overlay->AddText(textPos, kOverlayText, visual.Label.c_str());
        overlay->PopClipRect();
    }

    if (PendingSingleImageHintVisual.bValid)
    {
        ImGuiViewport* viewport = ImGui::FindViewportByID(
            static_cast<ImGuiID>(PendingSingleImageHintVisual.ViewportId));

        if (!viewport)
        {
            viewport = ImGui::GetMainViewport();
        }

        const ImVec2 paneMin(
            PendingSingleImageHintVisual.MinX,
            PendingSingleImageHintVisual.MinY);
        const ImVec2 paneMax(
            PendingSingleImageHintVisual.MaxX,
            PendingSingleImageHintVisual.MaxY);
        const float paneWidth = paneMax.x - paneMin.x;
        const float paneHeight = paneMax.y - paneMin.y;
        const float overlayMargin = FUiScale::Apply(kOverlayMargin);
        const float availableWidth = paneWidth - overlayMargin * 2.0f;
        const float availableHeight = paneHeight - overlayMargin * 2.0f;
        const ImVec2 textSize = ImGui::CalcTextSize(FLocalization::Text(EUiText::SwitchImageHint));
        const float desiredWidth =
            textSize.x
            + FUiScale::Apply(kSingleImageHintHorizontalPadding) * 2.0f;
        const float desiredHeight =
            textSize.y
            + FUiScale::Apply(kSingleImageHintVerticalPadding) * 2.0f;

        if (availableWidth > 0.0f && availableHeight > 0.0f)
        {
            const float cardWidth = std::min(desiredWidth, availableWidth);
            const float cardHeight = std::min(desiredHeight, availableHeight);
            const ImVec2 cardMin(
                paneMin.x + (paneWidth - cardWidth) * kCenterAlignmentRatio,
                paneMin.y + overlayMargin);
            const ImVec2 cardMax(
                cardMin.x + cardWidth,
                cardMin.y + cardHeight);
            ImDrawList* overlay = ImGui::GetForegroundDrawList(viewport);

            overlay->PushClipRect(cardMin, cardMax, true);
            overlay->AddRectFilled(
                cardMin,
                cardMax,
                kLoadingCardBackground,
                FUiScale::Apply(kOverlayRounding));
            overlay->AddRect(
                cardMin,
                cardMax,
                kLoadingCardBorder,
                FUiScale::Apply(kOverlayRounding));
            overlay->AddText(
                ImVec2(
                    cardMin.x + (cardWidth - textSize.x) * kCenterAlignmentRatio,
                    CalculateVerticallyCenteredTextY(
                        FLocalization::Text(EUiText::SwitchImageHint),
                        cardMin.y,
                        cardMax.y)),
                kOverlayText,
                FLocalization::Text(EUiText::SwitchImageHint));
            overlay->PopClipRect();
        }
    }

    PendingPaneOverlayVisualCount = 0;
    PendingLoadingVisualCount = 0;
    PendingSingleImageHintVisual = FPendingSingleImageHintVisual{};
}

bool FImageViewer::ContainsScreenPoint(float X, float Y) const
{
    return bDropRectValid &&
           X >= DropRectMin[0] && X <= DropRectMax[0] &&
           Y >= DropRectMin[1] && Y <= DropRectMax[1];
}

void FImageViewer::ResetView()
{
    FImageDocument* target = GetInteractionTarget();

    if (!target)
    {
        return;
    }

    GetViewState(target) = FDocumentViewState{};

    if (FImageDocument* peer = GetPanZoomPeer(target))
    {
        // “重置”仍只清除当前图片的朝向；同步开启时，另一侧只跟随缩放与平移复位。
        FDocumentViewState& peerState = GetViewState(peer);
        peerState.Settings.DisplayMode = EDisplayMode::AutoFit;
        peerState.Settings.ManualScale = 1.0f;
        peerState.Settings.PanOffsetX = 0.0f;
        peerState.Settings.PanOffsetY = 0.0f;
        peerState.ActualScale = 1.0f;
    }

    LOGD("ResetView", "Current image view reset: panZoomSynchronized=%d",
         GetPanZoomPeer(target) ? 1 : 0);
}

void FImageViewer::SetDisplayMode(EDisplayMode Mode)
{
    FImageDocument* target = GetInteractionTarget();

    if (!target)
    {
        return;
    }

    auto applyMode = [&](FImageDocument* Target)
    {
        FImageViewSettings& settings = GetViewState(Target).Settings;
        settings.DisplayMode = Mode;
        settings.PanOffsetX = 0.0f;
        settings.PanOffsetY = 0.0f;
    };

    applyMode(target);

    if (FImageDocument* peer = GetPanZoomPeer(target))
    {
        applyMode(peer);
    }
}

EDisplayMode FImageViewer::GetDisplayMode() const
{
    const FDocumentViewState* state = FindViewState(GetInteractionTarget());

    return state ? state->Settings.DisplayMode : EDisplayMode::AutoFit;
}

float FImageViewer::GetZoom() const
{
    const FDocumentViewState* state = FindViewState(GetInteractionTarget());

    return state ? state->ActualScale : 1.0f;
}

void FImageViewer::SetZoom(float Zoom)
{
    FImageDocument* target = GetInteractionTarget();

    if (!target)
    {
        return;
    }

    const float clampedZoom = std::clamp(Zoom, kMinZoom, kMaxZoom);
    auto applyZoom = [&](FImageDocument* Target)
    {
        FDocumentViewState& state = GetViewState(Target);
        state.Settings.DisplayMode = EDisplayMode::Manual;
        state.Settings.ManualScale = clampedZoom;
    };

    applyZoom(target);

    if (FImageDocument* peer = GetPanZoomPeer(target))
    {
        applyZoom(peer);
    }
}

FImageViewSettings FImageViewer::GetViewSettings(FImageDocument* Target) const
{
    const FDocumentViewState* state = FindViewState(Target);

    return state ? state->Settings : FImageViewSettings{};
}

void FImageViewer::SetViewSettings(
    FImageDocument* Target,
    const FImageViewSettings& InSettings)
{
    if (!Target)
    {
        return;
    }

    FDocumentViewState& state = GetViewState(Target);
    state = FDocumentViewState{};
    state.Settings = InSettings;
    state.Settings.ManualScale =
        std::clamp(state.Settings.ManualScale, kMinZoom, kMaxZoom);
    state.Settings.RotationQuarters =
        ((state.Settings.RotationQuarters % kQuarterTurnsPerCircle)
            + kQuarterTurnsPerCircle)
        % kQuarterTurnsPerCircle;
    NormalizeOrientation(state.Settings);
}

FImageViewer::FDocumentViewState& FImageViewer::GetViewState(FImageDocument* Target)
{
    return DocumentViewStates[Target];
}

const FImageViewer::FDocumentViewState* FImageViewer::FindViewState(
    FImageDocument* Target) const
{
    const auto found = DocumentViewStates.find(Target);

    return found != DocumentViewStates.end() ? &found->second : nullptr;
}

FImageDocument* FImageViewer::GetInteractionTarget() const
{
    if (SelectedDocument &&
        (SelectedDocument == Document || SelectedDocument == SecondaryDocument))
    {
        return SelectedDocument;
    }

    return Document;
}

FImageDocument* FImageViewer::GetPanZoomPeer(FImageDocument* Target) const
{
    if (IsSideBySide())
    {
        if (!bPanZoomSynchronized)
        {
            return nullptr;
        }

        if (Target == Document)
        {
            return SecondaryDocument;
        }

        return Target == SecondaryDocument ? Document : nullptr;
    }

    return Target == Document ? SingleImageComparePeer : nullptr;
}

void FImageViewer::SynchronizePanZoomFrom(
    FImageDocument* Source,
    FImageDocument* Target)
{
    if (!Source || !Target || Source == Target)
    {
        return;
    }

    // Target 可能尚未进入状态表；先按值拍下 Source，避免插入 Target 时
    // unordered_map rehash 使 Source 的引用失效。
    const FImageViewSettings sourceSettings = GetViewState(Source).Settings;
    FImageViewSettings& targetSettings = GetViewState(Target).Settings;
    CopyPanZoomViewSettings(sourceSettings, targetSettings);
}

void FImageViewer::SetPanZoomSynchronized(bool bSynchronized)
{
    if (bPanZoomSynchronized == bSynchronized)
    {
        return;
    }

    bPanZoomSynchronized = bSynchronized;

    if (bPanZoomSynchronized && IsSideBySide())
    {
        FImageDocument* source = GetInteractionTarget();
        FImageDocument* peer = GetPanZoomPeer(source);

        if (source && peer)
        {
            SynchronizePanZoomFrom(source, peer);
        }
    }

    LOGI("PanZoomSync", "Tiled pan/zoom synchronization %s",
         bPanZoomSynchronized ? "enabled" : "disabled");
}

void FImageViewer::SelectDocument(FImageDocument* Target)
{
    if (!Target || Target == SelectedDocument)
    {
        return;
    }

    SelectedDocument = Target;

    if (OnDocumentSelected)
    {
        OnDocumentSelected(Target);
    }
}

void FImageViewer::RotateView(FImageDocument* Target, int32_t QuarterSteps)
{
    if (!Target)
    {
        return;
    }

    SelectDocument(Target);
    FImageViewSettings& settings = GetViewState(Target).Settings;

    // 旋转之间互相交换，直接累加即可；镜像标志不受影响
    settings.RotationQuarters =
        ((settings.RotationQuarters + QuarterSteps) % kQuarterTurnsPerCircle
            + kQuarterTurnsPerCircle)
        % kQuarterTurnsPerCircle;

    LOGD("RotateView", "Document view rotated: quarterSteps=%d result=%d",
         QuarterSteps, settings.RotationQuarters);
}

void FImageViewer::MirrorView(FImageDocument* Target, bool bHorizontal)
{
    if (!Target)
    {
        return;
    }

    SelectDocument(Target);
    FImageViewSettings& settings = GetViewState(Target).Settings;

    // 屏幕空间的反射 Fs 作用在 M = R(θ)·F 上：Fs·R(θ)·F = R(-θ)·(Fs·F)。
    // 所以除了翻对应的标志位，还要把旋转取反，否则"旋转 90 度后再水平镜像"
    // 会变成沿垂直方向翻转。
    settings.RotationQuarters =
        (kQuarterTurnsPerCircle - settings.RotationQuarters) % kQuarterTurnsPerCircle;

    if (bHorizontal)
    {
        settings.bFlipH = !settings.bFlipH;
    }
    else
    {
        settings.bFlipV = !settings.bFlipV;
    }

    NormalizeOrientation(settings);

    LOGD("MirrorView", "Document view mirrored: axis=%s rotation=%d flipH=%d flipV=%d",
         bHorizontal ? "horizontal" : "vertical",
         settings.RotationQuarters,
         settings.bFlipH ? 1 : 0,
         settings.bFlipV ? 1 : 0);
}

void FImageViewer::NormalizeOrientation(FImageViewSettings& Settings)
{
    if (Settings.bFlipH && Settings.bFlipV)
    {
        Settings.bFlipH = false;
        Settings.bFlipV = false;
        Settings.RotationQuarters =
            (Settings.RotationQuarters + kHalfTurnQuarterSteps) % kQuarterTurnsPerCircle;
    }
}

void FImageViewer::QueueLoadingVisual(
    FImageDocument* Doc,
    const ImVec2& PaneMin,
    const ImVec2& PaneMax)
{
    const auto found = DocumentLoadingStates.find(Doc);

    if (found == DocumentLoadingStates.end() ||
        PendingLoadingVisualCount >= kMaximumPendingLoadingVisuals)
    {
        return;
    }

    const int32_t visualIndex = PendingLoadingVisualCount;

    if (visualIndex < 0 || visualIndex >= kMaximumPendingLoadingVisuals)
    {
        return;
    }

    PendingLoadingVisualCount = visualIndex + 1;
    FPendingLoadingVisual& visual = PendingLoadingVisuals[visualIndex];
    visual = FPendingLoadingVisual{};
    visual.MinX = PaneMin.x;
    visual.MinY = PaneMin.y;
    visual.MaxX = PaneMax.x;
    visual.MaxY = PaneMax.y;
    visual.ViewportId =
        static_cast<uint32_t>(ImGui::GetWindowViewport()->ID);
    visual.Label = found->second.Label;
}

void FImageViewer::QueueSingleImageSwitchHint(
    const ImVec2& PaneMin,
    const ImVec2& PaneMax)
{
    PendingSingleImageHintVisual.MinX = PaneMin.x;
    PendingSingleImageHintVisual.MinY = PaneMin.y;
    PendingSingleImageHintVisual.MaxX = PaneMax.x;
    PendingSingleImageHintVisual.MaxY = PaneMax.y;
    PendingSingleImageHintVisual.ViewportId =
        static_cast<uint32_t>(ImGui::GetWindowViewport()->ID);
    PendingSingleImageHintVisual.bValid = true;
}

void FImageViewer::RenderImage()
{
    const FImageData* imageData = Document ? Document->GetImageData() : nullptr;
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();

    if (!imageData || !imageData->IsValid())
    {
        ProbeX = ProbeY = -1;

        if (DocumentLoadingStates.empty())
        {
            ImGui::Text(FLocalization::Text(EUiText::NoImageLoaded));
            return;
        }

        const ImVec2 safeSize =
            (canvasSize.x > 0.0f && canvasSize.y > 0.0f)
            ? canvasSize
            : ImVec2(1.0f, 1.0f);
        ImGui::InvisibleButton("##ImageLoadingCanvas", safeSize);
        FImageDocument* loadingDocument =
            const_cast<FImageDocument*>(DocumentLoadingStates.begin()->first);
        QueueLoadingVisual(
            loadingDocument,
            canvasPos,
            ImVec2(canvasPos.x + safeSize.x, canvasPos.y + safeSize.y));

        return;
    }

    if (!bOpenGLResourcesInitialized)
    {
        return;
    }

    const bool bTiled = IsSideBySide();
    const float dividerWidth = FUiScale::Apply(kDividerWidth);

    if (imageData->GetWidth() <= 0 || imageData->GetHeight() <= 0 ||
        canvasSize.x <= 0.0f || canvasSize.y <= 0.0f)
    {
        const ImVec2 safeSize =
            (canvasSize.x > 0.0f && canvasSize.y > 0.0f) ? canvasSize : ImVec2(1.0f, 1.0f);
        ImGui::InvisibleButton("##ImageCanvas", safeSize);

        return;
    }

    FPaneRect panes[2] = {
        { canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y) },
        { canvasPos, canvasPos },
    };

    if (bTiled && TileLayout == ETileLayout::Horizontal)
    {
        const float splitX = canvasPos.x + canvasSize.x * kPaneSplitRatio;
        panes[0].Max.x = splitX - dividerWidth * kPaneSplitRatio;
        panes[1].Min = ImVec2(splitX + dividerWidth * kPaneSplitRatio, canvasPos.y);
        panes[1].Max = ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);
    }
    else if (bTiled)
    {
        const float splitY = canvasPos.y + canvasSize.y * kPaneSplitRatio;
        panes[0].Max.y = splitY - dividerWidth * kPaneSplitRatio;
        panes[1].Min = ImVec2(canvasPos.x, splitY + dividerWidth * kPaneSplitRatio);
        panes[1].Max = ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);
    }

    const FPaneRect overlayRects[2] = {
        MakePaneOverlayLayout(panes[0].Min, panes[0].Max).Bounds,
        MakePaneOverlayLayout(panes[1].Min, panes[1].Max).Bounds,
    };

    // 画布允许后提交的悬浮按钮覆盖命中；同时显式排除控件矩形，
    // 避免点镜像时顺带拖动画面或弹出像素探针。
    ImGui::SetNextItemAllowOverlap();
    const bool bCanvasClickReleased =
        ImGui::InvisibleButton("##ImageCanvas", canvasSize);
    const ImVec2 cursorAfterCanvas = ImGui::GetCursorScreenPos();
    ImGuiIO& io = ImGui::GetIO();

    bool bMouseOverOverlay = overlayRects[0].Contains(io.MousePos);

    if (bTiled)
    {
        bMouseOverOverlay = bMouseOverOverlay || overlayRects[1].Contains(io.MousePos);
    }

    const bool bCanvasHovered = ImGui::IsItemHovered() && !bMouseOverOverlay;
    int32_t hoveredPane = 0;

    if (bTiled && panes[1].Contains(io.MousePos))
    {
        hoveredPane = 1;
    }

    FImageDocument* paneDocuments[2] = { Document, SecondaryDocument };

    const bool bSingleImageSwitchInteraction =
        bSingleImageSwitchEnabled && !bTiled;
    const bool bSingleImageSwitchRequested =
        bSingleImageSwitchInteraction &&
        bCanvasHovered &&
        bCanvasClickReleased &&
        !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left);

    if (bCanvasHovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !bSingleImageSwitchInteraction)
    {
        SelectDocument(paneDocuments[hoveredPane]);
    }

    FDocumentViewState* viewStates[2] = {
        &GetViewState(Document),
        bTiled ? &GetViewState(SecondaryDocument) : nullptr,
    };

    auto GetOrientedSourceSize = [&](int32_t PaneIndex) -> ImVec2
    {
        const FImageData* data = paneDocuments[PaneIndex]->GetImageData();
        const float width = static_cast<float>(data->GetWidth());
        const float height = static_cast<float>(data->GetHeight());

        return IsAxisSwapped(viewStates[PaneIndex]->Settings)
            ? ImVec2(height, width)
            : ImVec2(width, height);
    };

    const bool bSynchronizePanZoom = bTiled && bPanZoomSynchronized;
    FImageDocument* const singleImagePanZoomPeer =
        !bTiled ? GetPanZoomPeer(paneDocuments[0]) : nullptr;

    // 1) 双击左键：复位鼠标所在图片的缩放与拖动偏移；同步开启时另一侧一起复位。
    if (bCanvasHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        auto resetPanZoom = [&](int32_t PaneIndex)
        {
            FImageViewSettings& settings = viewStates[PaneIndex]->Settings;
            settings.DisplayMode = EDisplayMode::AutoFit;
            settings.ManualScale = 1.0f;
            settings.PanOffsetX = 0.0f;
            settings.PanOffsetY = 0.0f;
        };

        resetPanZoom(hoveredPane);

        if (bSynchronizePanZoom)
        {
            resetPanZoom(1 - hoveredPane);
        }
        else if (singleImagePanZoomPeer)
        {
            SynchronizePanZoomFrom(
                paneDocuments[hoveredPane],
                singleImagePanZoomPeer);
        }
    }

    // 2) 拖动：左键在画布上按下并保持时，累计偏移到 PanOffset，并将指针切换为手形
    // IsItemActive() 仅在左键于画布上按下且仍未释放时为 true，松开后自动恢复 false，
    // 由于松开后这一帧不会再调用 SetMouseCursor, ImGui 会自动恢复默认指针。
    // 同步开启时两侧累计相同的屏幕位移；关闭时只修改鼠标所在图片。
    if (ImGui::IsItemActive() && !bMouseOverOverlay)
    {
        auto applyPanDelta = [&](int32_t PaneIndex)
        {
            FImageViewSettings& settings = viewStates[PaneIndex]->Settings;
            settings.PanOffsetX += io.MouseDelta.x;
            settings.PanOffsetY += io.MouseDelta.y;
        };

        applyPanDelta(hoveredPane);

        if (bSynchronizePanZoom)
        {
            applyPanDelta(1 - hoveredPane);
        }
        else if (singleImagePanZoomPeer)
        {
            SynchronizePanZoomFrom(
                paneDocuments[hoveredPane],
                singleImagePanZoomPeer);
        }

        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    // 3) Ctrl + 鼠标滚轮：以鼠标位置为中心进行缩放
    //    数学原理：保持"鼠标下方的图像像素"在屏幕上的位置不变。
    //    设当前缩放为 s0, 求出此时鼠标位置对应的图像像素 (u, v)；新缩放 s1 = s0 * factor 后,
    //    令该像素仍落在鼠标位置, 反解出新的 PanOffset 即可。
    //    同步开启时，另一侧以相同的 pane 内相对位置为锚点缩放到同一比例；
    //    这样两张图的朝向即使不同，也不会互相覆盖各自的镜像/旋转状态。
    if (bCanvasHovered && io.KeyCtrl && io.MouseWheel != 0.0f)
    {
        SelectDocument(paneDocuments[hoveredPane]);

        const float zoomFactor = std::pow(kZoomStep, io.MouseWheel);
        const float hoveredScaleOld =
            std::max(viewStates[hoveredPane]->ActualScale, kMinZoom);
        const float synchronizedScaleNew =
            std::clamp(hoveredScaleOld * zoomFactor, kMinZoom, kMaxZoom);
        const FPaneRect& hoveredPaneRect = panes[hoveredPane];
        const float anchorRatioX = hoveredPaneRect.Width() > 0.0f
            ? (io.MousePos.x - hoveredPaneRect.Min.x) / hoveredPaneRect.Width()
            : kPaneSplitRatio;
        const float anchorRatioY = hoveredPaneRect.Height() > 0.0f
            ? (io.MousePos.y - hoveredPaneRect.Min.y) / hoveredPaneRect.Height()
            : kPaneSplitRatio;

        auto applyZoomAtAnchor = [&](int32_t PaneIndex, float ScaleNew)
        {
            FDocumentViewState& viewState = *viewStates[PaneIndex];
            FImageViewSettings& settings = viewState.Settings;
            const float scaleOld = std::max(viewState.ActualScale, kMinZoom);
            const ImVec2 sourceSize = GetOrientedSourceSize(PaneIndex);
            const FPaneRect& pane = panes[PaneIndex];
            const float mouseInPaneX = pane.Width() * anchorRatioX;
            const float mouseInPaneY = pane.Height() * anchorRatioY;
            const float displayWidthOld = sourceSize.x * scaleOld;
            const float displayHeightOld = sourceSize.y * scaleOld;
            const float offsetXOld =
                (pane.Width() - displayWidthOld) * kPaneSplitRatio + settings.PanOffsetX;
            const float offsetYOld =
                (pane.Height() - displayHeightOld) * kPaneSplitRatio + settings.PanOffsetY;
            const float imageU = (mouseInPaneX - offsetXOld) / scaleOld;
            const float imageV = (mouseInPaneY - offsetYOld) / scaleOld;
            const float offsetXNew = mouseInPaneX - imageU * ScaleNew;
            const float offsetYNew = mouseInPaneY - imageV * ScaleNew;

            settings.PanOffsetX =
                offsetXNew - (pane.Width() - sourceSize.x * ScaleNew) * kPaneSplitRatio;
            settings.PanOffsetY =
                offsetYNew - (pane.Height() - sourceSize.y * ScaleNew) * kPaneSplitRatio;
            settings.DisplayMode = EDisplayMode::Manual;
            settings.ManualScale = ScaleNew;
        };

        applyZoomAtAnchor(hoveredPane, synchronizedScaleNew);

        if (bSynchronizePanZoom)
        {
            applyZoomAtAnchor(1 - hoveredPane, synchronizedScaleNew);
        }
        else if (singleImagePanZoomPeer)
        {
            SynchronizePanZoomFrom(
                paneDocuments[hoveredPane],
                singleImagePanZoomPeer);
        }
    }

    auto GetContainScale = [&](int32_t PaneIndex) -> float
    {
        const ImVec2 sourceSize = GetOrientedSourceSize(PaneIndex);

        return std::min(
            panes[PaneIndex].Width() / sourceSize.x,
            panes[PaneIndex].Height() / sourceSize.y);
    };

    auto GetCoverScale = [&](int32_t PaneIndex) -> float
    {
        const ImVec2 sourceSize = GetOrientedSourceSize(PaneIndex);

        return std::max(
            panes[PaneIndex].Width() / sourceSize.x,
            panes[PaneIndex].Height() / sourceSize.y);
    };

    const ImVec2 framebufferScale = io.DisplayFramebufferScale;
    // 取 X/Y 中较大者作为统一缩放分母，确保 X、Y 都不会超过 1:1（各向同性缩放）。
    const float oneToOneDenominator =
        std::max(framebufferScale.x, framebufferScale.y);
    const float oneToOneScale =
        oneToOneDenominator > 0.0f ? (1.0f / oneToOneDenominator) : 1.0f;

    auto ResolveScale = [&](int32_t PaneIndex) -> float
    {
        const FImageViewSettings& settings = viewStates[PaneIndex]->Settings;

        switch (settings.DisplayMode)
        {
        case EDisplayMode::AutoFit:  return GetContainScale(PaneIndex);
        case EDisplayMode::Fill:     return GetCoverScale(PaneIndex);
        case EDisplayMode::OneToOne: return oneToOneScale;
        case EDisplayMode::Manual:
        default:                     return settings.ManualScale;
        }
    };

    float paneScales[2] = { ResolveScale(0), 1.0f };
    viewStates[0]->ActualScale = paneScales[0];

    if (bTiled)
    {
        paneScales[1] = ResolveScale(1);
        viewStates[1]->ActualScale = paneScales[1];
    }

    auto PaneRenderSize = [&](int32_t PaneIndex) -> ImVec2
    {
        const ImVec2 sourceSize = GetOrientedSourceSize(PaneIndex);
        return ImVec2(
            sourceSize.x * paneScales[PaneIndex],
            sourceSize.y * paneScales[PaneIndex]);
    };

    auto PaneRenderPos = [&](int32_t PaneIndex) -> ImVec2
    {
        const ImVec2 displaySize = PaneRenderSize(PaneIndex);
        const FPaneRect& pane = panes[PaneIndex];
        const FImageViewSettings& settings = viewStates[PaneIndex]->Settings;

        return ImVec2(
            pane.Min.x + (pane.Width() - displaySize.x) * kPaneSplitRatio
                + settings.PanOffsetX,
            pane.Min.y + (pane.Height() - displaySize.y) * kPaneSplitRatio
                + settings.PanOffsetY);
    };

    // 像素探针：按当前 pane 的独立朝向逆映射回源图坐标
    ProbeX = ProbeY = -1;

    if (bCanvasHovered)
    {
        const FImageData* hoveredData = paneDocuments[hoveredPane]->GetImageData();
        const FImageViewSettings& settings = viewStates[hoveredPane]->Settings;
        const ImVec2 paneOrigin = PaneRenderPos(hoveredPane);
        const ImVec2 paneSize = PaneRenderSize(hoveredPane);
        const float u = (io.MousePos.x - paneOrigin.x) / paneSize.x;
        const float v = (io.MousePos.y - paneOrigin.y) / paneSize.y;
        float a = u;
        float b = v;

        switch (settings.RotationQuarters)
        {
        case kClockwiseQuarterStep:
            a = v;
            b = 1.0f - u;
            break;

        case kHalfTurnQuarterSteps:
            a = 1.0f - u;
            b = 1.0f - v;
            break;

        case kQuarterTurnsPerCircle - kClockwiseQuarterStep:
            a = 1.0f - v;
            b = u;
            break;

        default:                                  break;
        }

        if (settings.bFlipH) { a = 1.0f - a; }
        if (settings.bFlipV) { b = 1.0f - b; }

        const int32_t pixelX =
            static_cast<int32_t>(std::floor(a * static_cast<float>(hoveredData->GetWidth())));
        const int32_t pixelY =
            static_cast<int32_t>(std::floor(b * static_cast<float>(hoveredData->GetHeight())));

        if (pixelX >= 0 && pixelY >= 0 &&
            pixelX < hoveredData->GetWidth() && pixelY < hoveredData->GetHeight())
        {
            ProbeX = pixelX;
            ProbeY = pixelY;
        }
    }

    IssueDrawCallback(
        Document,
        PaneRenderPos(0),
        PaneRenderSize(0),
        panes[0].Min,
        panes[0].Max,
        paneScales[0],
        viewStates[0]->Settings);

    if (bTiled)
    {
        IssueDrawCallback(
            SecondaryDocument,
            PaneRenderPos(1),
            PaneRenderSize(1),
            panes[1].Min,
            panes[1].Max,
            paneScales[1],
            viewStates[1]->Settings);

        ImDrawList* overlay = ImGui::GetWindowDrawList();

        if (TileLayout == ETileLayout::Horizontal)
        {
            const float dividerX = canvasPos.x + canvasSize.x * kPaneSplitRatio;
            overlay->AddLine(
                ImVec2(dividerX, canvasPos.y),
                ImVec2(dividerX, canvasPos.y + canvasSize.y),
                kDividerColor,
                dividerWidth);
        }
        else
        {
            const float dividerY = canvasPos.y + canvasSize.y * kPaneSplitRatio;
            overlay->AddLine(
                ImVec2(canvasPos.x, dividerY),
                ImVec2(canvasPos.x + canvasSize.x, dividerY),
                kDividerColor,
                dividerWidth);
        }
    }

    // 这些绘制命令排在图像的 OpenGL 回调及状态复位命令之后，
    // 因而背景、图标和命中都位于同一个预览窗口的最上层。
    const char* primaryLabel = nullptr;
    const char* primaryIdentityLabel = nullptr;
    std::string singleImageFileName;

    if (bTiled)
    {
        primaryLabel = FLocalization::Text(EUiText::MainImage);
    }
    else if (bSingleImageSwitchEnabled)
    {
        singleImageFileName = GetDocumentFileName(Document);
        primaryLabel = singleImageFileName.c_str();
        primaryIdentityLabel = bSingleImageSwitchShowingCompare
            ? FLocalization::Text(EUiText::CompareImage)
            : FLocalization::Text(EUiText::MainImage);
    }

    RenderPaneOverlay(
        Document,
        panes[0].Min,
        panes[0].Max,
        primaryLabel,
        primaryIdentityLabel);

    if (bTiled)
    {
        RenderPaneOverlay(
            SecondaryDocument,
            panes[1].Min,
            panes[1].Max,
            FLocalization::Text(EUiText::CompareImage),
            nullptr);
    }

    if (bTiled)
    {
        QueueLoadingVisual(Document, panes[0].Min, panes[0].Max);
        QueueLoadingVisual(SecondaryDocument, panes[1].Min, panes[1].Max);
    }
    else if (!DocumentLoadingStates.empty())
    {
        // 当前查看的可能是对比图/差值图，而请求目标是主图；单图模式仍在整个
        // 画布上显示唯一的加载状态，避免请求正在进行却没有任何反馈。
        FImageDocument* loadingDocument =
            const_cast<FImageDocument*>(DocumentLoadingStates.begin()->first);
        QueueLoadingVisual(loadingDocument, panes[0].Min, panes[0].Max);
    }

    if (bSingleImageSwitchHintVisible &&
        !bTiled &&
        DocumentLoadingStates.empty())
    {
        QueueSingleImageSwitchHint(panes[0].Min, panes[0].Max);
    }

    // 悬浮按钮会临时移动 ImGui 光标；恢复到画布后的布局位置，避免像素探针或后续控件跳位。
    ImGui::SetCursorScreenPos(cursorAfterCanvas);
    RenderPixelProbe(ProbeX, ProbeY);

    // 切换会重绑 Document 并同步属性/直方图，因此必须等本帧所有旧文档局部变量
    // 使用完后再通知 DockSpace；视觉从下一帧开始显示新目标。
    if (bSingleImageSwitchRequested && OnSingleImageSwitchRequested)
    {
        OnSingleImageSwitchRequested();
    }
}

void FImageViewer::RenderPaneOverlay(
    FImageDocument* Doc,
    const ImVec2& PaneMin,
    const ImVec2& PaneMax,
    const char* Label,
    const char* IdentityLabel)
{
    if (!HasValidImage(Doc))
    {
        return;
    }

    const float buttonSize = ImGui::GetFrameHeight();
    const float overlayPadding = FUiScale::Apply(kOverlayPadding);
    const float overlayMargin = FUiScale::Apply(kOverlayMargin);
    const float groupHeight = buttonSize + overlayPadding * 2.0f;
    const FPaneOverlayLayout layout = MakePaneOverlayLayout(PaneMin, PaneMax);
    const bool bCanRenderControls = layout.Bounds.Width() > 0.0f;
    const ImVec2 groupMin = layout.OrientationGroup.Min;
    const ImVec2 groupMax = layout.OrientationGroup.Max;

    FPendingPaneOverlayVisual* visual = nullptr;

    if (PendingPaneOverlayVisualCount < kMaximumPendingPaneOverlayVisuals)
    {
        visual = &PendingPaneOverlayVisuals[PendingPaneOverlayVisualCount++];
        *visual = FPendingPaneOverlayVisual{};
        visual->GroupMinX = groupMin.x;
        visual->GroupMinY = groupMin.y;
        visual->GroupMaxX = groupMax.x;
        visual->GroupMaxY = groupMax.y;
        visual->ViewportId =
            static_cast<uint32_t>(ImGui::GetWindowViewport()->ID);
        visual->bHasControls = bCanRenderControls;
        // 单图模式只有一个可见目标，不需要选中强调；文件名始终使用普通半透明底色。
        visual->bSelected = IsSideBySide() && (Doc == SelectedDocument);
    }

    if (Label && *Label)
    {
        const ImVec2 textSize = ImGui::CalcTextSize(Label);
        const bool bHasIdentityLabel = IdentityLabel && *IdentityLabel;
        const ImVec2 identityTextSize = bHasIdentityLabel
            ? ImGui::CalcTextSize(IdentityLabel)
            : ImVec2(0.0f, 0.0f);
        const float identityWidth = bHasIdentityLabel
            ? identityTextSize.x + overlayPadding * 2.0f
            : 0.0f;
        ImVec2 labelMin(PaneMin.x + overlayMargin, PaneMin.y + overlayMargin);
        ImVec2 labelMax(
            labelMin.x + identityWidth + textSize.x + overlayPadding * 2.0f,
            labelMin.y + groupHeight);
        const float paneLabelMaxX = PaneMax.x - overlayMargin;

        if (bCanRenderControls)
        {
            const float topRowMaxX = groupMin.x - overlayPadding;
            const float minimumFileTextWidth = ImGui::GetFontSize();
            const float minimumLabelWidth =
                identityWidth + overlayPadding * 2.0f + minimumFileTextWidth;
            const bool bOverlapsControls = labelMax.x > topRowMaxX;
            const bool bTopRowHasUsefulWidth =
                topRowMaxX >= labelMin.x + minimumLabelWidth;

            if (bOverlapsControls && !bTopRowHasUsefulWidth)
            {
                // 只有按钮左侧连“完整身份 + 一个字宽”都放不下时才换行；
                // 长文件名优先留在第一行做 ellipsis，避免低矮 pane 中整块消失。
                labelMin.y = groupMax.y + overlayPadding;
                labelMax.y = labelMin.y + groupHeight;
            }
            else
            {
                labelMax.x = std::min(labelMax.x, topRowMaxX);
            }
        }

        // 动态文件名可能超过 pane 宽度；底色必须留在画布内，绘制阶段会自动加省略号。
        labelMax.x = std::min(labelMax.x, paneLabelMaxX);

        if (labelMax.x > labelMin.x + identityWidth + overlayPadding * 2.0f &&
            labelMax.y <= PaneMax.y - overlayMargin)
        {
            if (visual)
            {
                visual->LabelMinX = labelMin.x;
                visual->LabelMinY = labelMin.y;
                visual->LabelMaxX = labelMax.x;
                visual->LabelMaxY = labelMax.y;
                visual->Label = Label;
                visual->bHasLabel = true;

                if (bHasIdentityLabel)
                {
                    visual->IdentityLabelMaxX = labelMin.x + identityWidth;
                    visual->IdentityLabel = IdentityLabel;
                    visual->bHasIdentityLabel = true;
                }
            }
        }
    }

    if (!bCanRenderControls)
    {
        return;
    }

    ImGui::PushID(static_cast<const void*>(Doc));

    auto OverlayButton =
        [&](int32_t ButtonIndex,
            const char* Id,
            const char* Tooltip) -> bool
    {
        const ImVec2 buttonPos =
            GetPaneOverlayButtonPosition(groupMin, groupMax, ButtonIndex);

        ImGui::SetCursorScreenPos(buttonPos);
        const bool bClicked = ImGui::InvisibleButton(Id, ImVec2(buttonSize, buttonSize));
        const bool bHovered = ImGui::IsItemHovered();
        const bool bActive = ImGui::IsItemActive();

        if (visual)
        {
            const uint8_t buttonBit =
                static_cast<uint8_t>(1u << static_cast<uint32_t>(ButtonIndex));

            if (bHovered)
            {
                visual->HoveredButtonMask |= buttonBit;
            }

            if (bActive)
            {
                visual->ActiveButtonMask |= buttonBit;
            }
        }

        if (Tooltip && *Tooltip && bHovered)
        {
            ImGui::SetTooltip("%s", Tooltip);
        }

        return bClicked;
    };

    if (OverlayButton(
            0,
            "##MirrorH",
            FLocalization::Text(EUiText::FlipHorizontal)))
    {
        MirrorView(Doc, true);
    }

    if (OverlayButton(
            1,
            "##MirrorV",
            FLocalization::Text(EUiText::FlipVertical)))
    {
        MirrorView(Doc, false);
    }

    if (OverlayButton(
            2,
            "##RotateCw",
            FLocalization::Text(EUiText::RotateClockwise)))
    {
        RotateView(Doc, kClockwiseQuarterStep);
    }

    if (OverlayButton(
            3,
            "##RotateCcw",
            FLocalization::Text(EUiText::RotateCounterclockwise)))
    {
        RotateView(Doc, kCounterClockwiseQuarterStep);
    }

    if (OverlayButton(
            kPaneCloseButtonIndex,
            "##CloseImage",
            FLocalization::Text(EUiText::CloseCurrentImage)) && OnDocumentCloseRequested)
    {
        OnDocumentCloseRequested(Doc);
    }

    ImGui::PopID();
}

namespace
{
    FTextureViewRegion GetVisibleTextureRegion(const ImVec2& Position, const ImVec2& Size,
        const ImVec2& ClipMin, const ImVec2& ClipMax, const ImVec2& FramebufferScale,
        const FImageViewSettings& Settings)
    {
        // scissor 转为整数时可能向外覆盖一个 framebuffer 像素；缩小时它会对应多个源像素。
        const float guardX = FramebufferScale.x > 0.0f ? 1.0f / FramebufferScale.x : 1.0f;
        const float guardY = FramebufferScale.y > 0.0f ? 1.0f / FramebufferScale.y : 1.0f;
        const float left = std::max(Position.x, ClipMin.x - guardX), top = std::max(Position.y, ClipMin.y - guardY);
        const float right = std::min(Position.x + Size.x, ClipMax.x + guardX);
        const float bottom = std::min(Position.y + Size.y, ClipMax.y + guardY);
        if (Size.x <= 0 || Size.y <= 0 || right <= left || bottom <= top) return {0, 0, 0, 0};
        FTextureViewRegion region{1, 1, 0, 0};
        for (int32_t y = 0; y < 2; ++y)
        {
            for (int32_t x = 0; x < 2; ++x)
            {
                float u = ((x == 0 ? left : right) - Position.x) / Size.x;
                float v = ((y == 0 ? top : bottom) - Position.y) / Size.y;
                // 与像素探针相同的逆变换：先撤销旋转，再撤销镜像。
                switch (Settings.RotationQuarters)
                {
                case kClockwiseQuarterStep: { const float oldU = u; u = v; v = 1.0f - oldU; break; }
                case kHalfTurnQuarterSteps: u = 1.0f - u; v = 1.0f - v; break;
                case kQuarterTurnsPerCircle - kClockwiseQuarterStep: { const float oldU = u; u = 1.0f - v; v = oldU; break; }
                default: break;
                }
                if (Settings.bFlipH) u = 1.0f - u;
                if (Settings.bFlipV) v = 1.0f - v;
                region.MinU = std::min(region.MinU, u);
                region.MinV = std::min(region.MinV, v);
                region.MaxU = std::max(region.MaxU, u);
                region.MaxV = std::max(region.MaxV, v);
            }
        }
        return region;
    }
}

void FImageViewer::IssueDrawCallback(
    FImageDocument* Doc,
    const ImVec2& RenderPos,
    const ImVec2& RenderSize,
    const ImVec2& ClipMin,
    const ImVec2& ClipMax,
    float Scale,
    const FImageViewSettings& ViewSettings)
{
    if (!HasValidImage(Doc))
    {
        return;
    }

    const FImageData* imageData = Doc->GetImageData();
    FTextureData* textureData = Doc->GetTextureData();

    if (!textureData || !textureData->IsValid())
    {
        return;
    }

    const EImageFormat currentFormat = imageData->GetFormat();
    // 失败只在下一帧的 UI 构建阶段记录一次，不在 GL 绘制回调中打印。
    textureData->LogPendingDrawEvents();

    // 从着色器管理器获取当前格式的着色器
    FShader* shader = FShaderManager::Get().GetShaderForFormat(currentFormat);

    if (!shader || !shader->IsValid())
    {
        return;
    }

    struct RenderCallbackData
    {
        FImageViewer* Viewer;
        FImageDocument* Doc;
        ImVec2 RenderPos;
        ImVec2 RenderSize;
        ImVec2 ClipMin;
        ImVec2 ClipMax;
        FTextureViewRegion VisibleRegion;

        // 色彩转换参数在这里算好，回调里只负责喂给 uniform
        float   YuvToRgb[9];
        float   YuvOffset[3];
        float   SampleScale;
        int32_t SwapUV;
        int32_t ChannelMode;
        int32_t BayerPattern;
        int32_t TextureCount;
        bool    bIsYUV;
        bool    bIsBayer;
        bool    bMagNearest;

        /// 视图朝向。RenderSize 是旋转之后的包围盒，四边形本身仍按未旋转的尺寸建
        int32_t RotationQuarters;
        float   FlipX;
        float   FlipY;

        /**
         * 本次绘制所属视口的坐标基准
         *
         * 不能在回调里用 ImGui::GetDrawData() 现取 —— 它返回的恒为**主**视口的数据
         * （imgui.cpp 里就是 g.Viewports[0]），面板被拖出主窗口之后取到的原点是错的。
         */
        ImVec2 DisplayPos;
        ImVec2 DisplaySize;
        ImVec2 FramebufferScale;

        /// EOTF / 原色 / 色调映射 / 输出编码。展开一次，回调里逐字段喂 uniform
        FColorTransform::FColorPipeline Pipeline;
    };

    // 值初始化让新增字段也有确定值；随后仍会逐项覆盖全部渲染参数。
    RenderCallbackData callbackData{};
    callbackData.Viewer = this;
    callbackData.Doc = Doc;
    callbackData.RenderPos = RenderPos;
    callbackData.RenderSize = RenderSize;
    callbackData.ClipMin = ClipMin;
    callbackData.ClipMax = ClipMax;

    // 显示设置取自被画的这个文档：平铺时两图可以各用各的色彩标准与通道
    const FDisplaySettings& display = Doc->GetDisplaySettings();

    FColorTransform::BuildYuvToRgb(
        display.ColorSpace,
        display.ColorRange,
        imageData->GetSourceBitDepth(),
        callbackData.YuvToRgb,
        callbackData.YuvOffset);

    const FFormatDesc& formatDesc = FImageFormatDesc::Get(currentFormat);

    // 由数据而非格式决定：同为 Bayer16，来自 MIPI RAW10 解包的和真 16bit 的缩放不同
    callbackData.SampleScale = imageData->GetSampleScale();
    callbackData.SwapUV = formatDesc.bSwapChroma ? 1 : 0;
    callbackData.ChannelMode = static_cast<int32_t>(display.ChannelView);
    callbackData.BayerPattern = static_cast<int32_t>(Doc->GetParams().BayerPattern);
    callbackData.TextureCount = static_cast<int32_t>(textureData->GetTextureCount());
    callbackData.bIsYUV = (formatDesc.ColorModel == EColorModel::YUV);
    callbackData.bIsBayer = (formatDesc.ColorModel == EColorModel::Bayer);

    // 放大到 1 个图像像素 >= 1 个屏幕像素时切最近邻，让像素边界清晰可见
    callbackData.bMagNearest = (Scale >= 1.0f);

    // 回调发生在本函数返回之后，因此把该文档的朝向值复制进 userdata，不能保存引用。
    callbackData.RotationQuarters = ViewSettings.RotationQuarters;
    callbackData.FlipX = ViewSettings.bFlipH ? -1.0f : 1.0f;
    callbackData.FlipY = ViewSettings.bFlipV ? -1.0f : 1.0f;

    // 这个面板可以被拖出主窗口。副视口有自己的 GL 上下文与 **8bit 默认帧缓冲**，
    // 呈现层的 fp16 FBO 只挂在主上下文上，所以那里既不能按 HDR 编码输出，
    // 也不能沿用主视口的坐标基准。
    const ImGuiViewport* windowViewport = ImGui::GetWindowViewport();
    const bool bMainViewport = (windowViewport == ImGui::GetMainViewport());

    callbackData.DisplayPos = windowViewport->Pos;
    callbackData.DisplaySize = windowViewport->Size;
    callbackData.FramebufferScale = ImGui::GetIO().DisplayFramebufferScale;
    callbackData.VisibleRegion = GetVisibleTextureRegion(RenderPos, RenderSize, ClipMin, ClipMax,
        callbackData.FramebufferScale, ViewSettings);

    // 色彩管线要知道往哪儿输出：SDR 后台缓冲和 scRGB fp16 的编码方式不同。
    // 显示器信息由呈现层持有，HDR 未启用时返回的默认值会让管线退回 SDR 分支
    callbackData.Pipeline = FColorTransform::BuildPipeline(
        display,
        bMainViewport ? FHdrPresenter::GetActiveDisplayOutput() : FColorTransform::FDisplayOutput());

    // 添加渲染回调（在 ImGui 渲染时会被调用）
    // 使用 userdata_size 让 ImGui 复制数据（避免局部变量失效）
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddCallback([](const ImDrawList* parent_list, const ImDrawCmd* cmd) {
        const RenderCallbackData* data = static_cast<const RenderCallbackData*>(cmd->UserCallbackData);

        if (!data || !data->Viewer || !data->Doc)
        {
            return;
        }

        // 注意：虽然 data 是 const，但 Viewer / Doc 指向的对象本身不是 const
        FImageViewer* viewer = const_cast<FImageViewer*>(data->Viewer);
        FImageDocument* document = data->Doc;

        const FImageData* imageData = document->GetImageData();
        FTextureData* textureData = document->GetTextureData();

        if (!imageData || !imageData->IsValid() || !textureData || !textureData->IsValid())
        {
            return;
        }

        FShader* shader = FShaderManager::Get().GetShaderForFormat(imageData->GetFormat());

        if (!shader || !shader->IsValid())
        {
            return;
        }

        // 注意：这个回调每帧都会执行，**不要在这里打日志**

        // 坐标基准来自所属视口，不能改用主视口的 ImGui::GetDrawData()。
        const ImVec2 displayPos = data->DisplayPos;
        const ImVec2 framebufferScale = data->FramebufferScale;
        const int32_t fbWidth = static_cast<int32_t>(data->DisplaySize.x * framebufferScale.x);
        const int32_t fbHeight = static_cast<int32_t>(data->DisplaySize.y * framebufferScale.y);
        const float clipMinX = (data->ClipMin.x - displayPos.x) * framebufferScale.x;
        const float clipMinY = (data->ClipMin.y - displayPos.y) * framebufferScale.y;
        const float clipMaxX = (data->ClipMax.x - displayPos.x) * framebufferScale.x;
        const float clipMaxY = (data->ClipMax.y - displayPos.y) * framebufferScale.y;
        // 提前返回必须放在任何 GL 状态修改之前，包括保存纹理绑定时的 glActiveTexture。
        if (fbWidth <= 0 || fbHeight <= 0 || clipMaxX <= clipMinX || clipMaxY <= clipMinY)
            return;

        // 保存 OpenGL 状态（参考 ImGui_ImplOpenGL3 的实现）
        GLint lastProgram, lastArrayBuffer, lastVertexArray;
        GLint lastViewport[4], lastScissorBox[4];
        GLint lastTexture[3];
        GLenum lastActiveTexture;

        glGetIntegerv(GL_CURRENT_PROGRAM, &lastProgram);
        glGetIntegerv(GL_ACTIVE_TEXTURE, (GLint*)&lastActiveTexture);

        for (int32_t unit = 0; unit < 3; ++unit)
        {
            glActiveTexture(GL_TEXTURE0 + unit);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexture[unit]);
        }

        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &lastArrayBuffer);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &lastVertexArray);
        glGetIntegerv(GL_VIEWPORT, lastViewport);
        glGetIntegerv(GL_SCISSOR_BOX, lastScissorBox);

        GLboolean lastEnableBlend = glIsEnabled(GL_BLEND);
        GLboolean lastEnableScissorTest = glIsEnabled(GL_SCISSOR_TEST);

        GLint lastBlendSrcRgb, lastBlendDstRgb, lastBlendSrcAlpha, lastBlendDstAlpha;
        glGetIntegerv(GL_BLEND_SRC_RGB, &lastBlendSrcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &lastBlendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &lastBlendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &lastBlendDstAlpha);

        // 设置 OpenGL 状态
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_SCISSOR_TEST);

        // 设置视口和裁剪区域 (OpenGL 中 Y 轴是翻转的)
        glViewport(0, 0, fbWidth, fbHeight);
        glScissor((int32_t)clipMinX,
                  (int32_t)((float)fbHeight - clipMaxY),
                  (int32_t)(clipMaxX - clipMinX),
                  (int32_t)(clipMaxY - clipMinY));

        // 使用 glm 计算投影矩阵（覆盖整个帧缓冲区，左上角为原点，Y向下）
        glm::mat4 projection = glm::ortho(0.0f, (float)fbWidth, (float)fbHeight, 0.0f, -1.0f, 1.0f);

        // 将屏幕坐标转换为帧缓冲区坐标
        float renderPosX = (data->RenderPos.x - displayPos.x) * framebufferScale.x;
        float renderPosY = (data->RenderPos.y - displayPos.y) * framebufferScale.y;
        float renderSizeX = data->RenderSize.x * framebufferScale.x;
        float renderSizeY = data->RenderSize.y * framebufferScale.y;

        // RenderPos/RenderSize 描述的是**旋转之后**的屏幕包围盒，四边形本身要按未旋转的
        // 尺寸来建，所以 90/270 度时把两边换回去
        float quadWidth = renderSizeX;
        float quadHeight = renderSizeY;

        if ((data->RotationQuarters & 1) != 0)
        {
            std::swap(quadWidth, quadHeight);
        }

        // 顶点坐标范围是 -1 到 1（NDC），先按朝向镜像/缩放到显示尺寸的一半，
        // 旋转之后再平移到渲染位置的中心。矩阵右乘的顺序即"先镜像后旋转"，
        // 与 MirrorView() 里的推导一致。
        //
        // 投影是 glm::ortho(0, w, h, 0)，Y 轴朝下，因此绕 +Z 的**正**角度在屏幕上
        // 表现为顺时针 —— RotationQuarters 数的正是顺时针步数，无需取反。
        glm::mat4 transform = glm::mat4(1.0f);
        transform = glm::translate(transform, glm::vec3(
            renderPosX + renderSizeX * 0.5f,
            renderPosY + renderSizeY * 0.5f,
            0.0f));
        transform = glm::rotate(transform,
                                glm::radians(90.0f * static_cast<float>(data->RotationQuarters)),
                                glm::vec3(0.0f, 0.0f, 1.0f));
        transform = glm::scale(transform, glm::vec3(
            quadWidth * 0.5f * data->FlipX,
            quadHeight * 0.5f * data->FlipY,
            1.0f));

        float projectionArray[16];
        float transformArray[16];
        memcpy(projectionArray, glm::value_ptr(projection), sizeof(projectionArray));
        memcpy(transformArray, glm::value_ptr(transform), sizeof(transformArray));

        shader->Use();
        shader->SetMat4("uProjection", projectionArray);
        shader->SetMat4("uTransform", transformArray);

        // 过滤方式要在 BindTextures 之前设置：SetMagFilterNearest 内部会 glBindTexture，
        // 随后的 BindTextures 会把各纹理单元的绑定重新摆正
        textureData->PrepareForDraw(imageData, data->VisibleRegion);
        textureData->SetMagFilterNearest(data->bMagNearest);

        // 采样器：纹理数量由格式描述表决定
        if (data->TextureCount >= 3)
        {
            // YV12 的物理平面顺序是 Y,V,U；shader 仍按语义接收 Y,U,V。
            const int32_t uTextureUnit = data->SwapUV != 0 ? 2 : 1;
            const int32_t vTextureUnit = data->SwapUV != 0 ? 1 : 2;

            shader->SetInt("uTextureY", 0);
            shader->SetInt("uTextureU", uTextureUnit);
            shader->SetInt("uTextureV", vTextureUnit);
        }
        else if (data->TextureCount == 2)
        {
            shader->SetInt("uTextureY", 0);
            shader->SetInt("uTextureUV", 1);
        }
        else
        {
            shader->SetInt("uTexture", 0);
            shader->SetInt("uTextureY", 0);
        }

        if (data->bIsYUV)
        {
            shader->SetMat3("uYuvToRgb", data->YuvToRgb);
            shader->SetVec3("uYuvOffset", data->YuvOffset[0], data->YuvOffset[1], data->YuvOffset[2]);
            shader->SetInt("uSwapUV", data->SwapUV);
        }

        if (data->bIsBayer)
        {
            shader->SetInt("uBayerPattern", data->BayerPattern);
        }

        // packed 与 Bayer 着色器都用 texelFetch 按整数坐标取样，需要知道图像尺寸
        shader->SetVec2("uImageSize",
                        static_cast<float>(imageData->GetWidth()),
                        static_cast<float>(imageData->GetHeight()));
        shader->SetFloat("uSampleScale", data->SampleScale);
        shader->SetInt("uChannelMode", data->ChannelMode);

        // 色彩管线。Bayer 着色器没有这些 uniform，FShader::SetXxx 遇到 -1 会静默跳过，
        // 所以这里不必按格式分支
        const FColorTransform::FColorPipeline& pipeline = data->Pipeline;

        shader->SetInt("uPipelineEnabled", pipeline.bEnabled ? 1 : 0);
        shader->SetInt("uTransfer", pipeline.Transfer);
        shader->SetInt("uAbsoluteTransfer", pipeline.bAbsoluteTransfer);
        shader->SetInt("uApplyPrimaries", pipeline.bApplyPrimaries);
        shader->SetMat3("uPrimariesMatrix", pipeline.PrimariesMatrix);
        shader->SetVec3("uLumaCoef", pipeline.LumaCoef[0], pipeline.LumaCoef[1], pipeline.LumaCoef[2]);
        shader->SetFloat("uNormalizeNits", pipeline.NormalizeNits);
        shader->SetFloat("uHlgPeakNits", pipeline.HlgPeakNits);
        shader->SetFloat("uExposureScale", pipeline.ExposureScale);
        shader->SetInt("uToneMap", pipeline.ToneMap);
        shader->SetFloat("uToneMapWhite", pipeline.ToneMapWhite);
        shader->SetInt("uOutputMode", pipeline.OutputMode);
        shader->SetFloat("uMaxOutputScale", pipeline.MaxOutputScale);
        shader->SetInt("uShowOutOfRange", pipeline.bShowOutOfRange);

        // VBO 可以在共享 Context 间复用，VAO 不可以。与 ImGui OpenGL 后端一致，
        // 在实际绘制的 Context 内创建并释放 VAO，避免脱离 Dock 后绑定主窗口的 VAO。
        GLuint quadVAO = 0;
        glGenVertexArrays(1, &quadVAO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, viewer->QuadVBO);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, kQuadVertexStride, nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, kQuadVertexStride,
            reinterpret_cast<const void*>(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        const auto setPlaneCoordinates = [&](const char* ScaleUniform, const char* OffsetUniform,
            uint32_t Plane, uint32_t DrawIndex) {
            float x = 1.0f, y = 1.0f;
            textureData->GetTextureCoordinateScale(Plane, x, y, DrawIndex);
            shader->SetVec2(ScaleUniform, x, y);
            textureData->GetTextureCoordinateOffset(Plane, x, y, DrawIndex);
            shader->SetVec2(OffsetUniform, x, y);
        };
        // 每块只画核心区域，边缘冗余纹素仅用于滤波；完整图变换保证旋转和翻转一致。
        for (uint32_t drawIndex = 0; drawIndex < textureData->GetDrawCount(); ++drawIndex)
        {
            FTextureViewRegion region;
            if (!textureData->GetDrawRegion(drawIndex, region)) continue;
            textureData->BindTextures(0, drawIndex);
            shader->SetVec4("uDrawRegion", region.MinU, region.MinV, region.MaxU, region.MaxV);
            setPlaneCoordinates("uTextureScale0", "uTextureOffset0", 0, drawIndex);
            if (data->TextureCount >= 2)
            {
                const uint32_t uPlane = data->TextureCount >= 3 && data->SwapUV != 0 ? 2 : 1;
                setPlaneCoordinates("uTextureScale1", "uTextureOffset1", uPlane, drawIndex);
            }
            if (data->TextureCount >= 3)
            {
                const uint32_t vPlane = data->SwapUV != 0 ? 1 : 2;
                setPlaneCoordinates("uTextureScale2", "uTextureOffset2", vPlane, drawIndex);
            }
            float originX = 0.0f, originY = 0.0f;
            textureData->GetTextureTexelOrigin(0, originX, originY, drawIndex);
            shader->SetVec2("uTextureOrigin0", originX, originY);
            glDrawArrays(GL_TRIANGLES, 0, kQuadVertexCount);
        }
        textureData->FinishDraw();
        glDeleteVertexArrays(1, &quadVAO);

        // 恢复 OpenGL 状态
        glUseProgram(lastProgram);

        for (int32_t unit = 2; unit >= 0; --unit)
        {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, lastTexture[unit]);
        }

        glActiveTexture(lastActiveTexture);
        glBindVertexArray(lastVertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, lastArrayBuffer);
        glViewport(lastViewport[0], lastViewport[1], lastViewport[2], lastViewport[3]);
        glScissor(lastScissorBox[0], lastScissorBox[1], lastScissorBox[2], lastScissorBox[3]);

        glBlendFuncSeparate(lastBlendSrcRgb, lastBlendDstRgb, lastBlendSrcAlpha, lastBlendDstAlpha);

        if (!lastEnableBlend)
        {
            glDisable(GL_BLEND);
        }

        if (!lastEnableScissorTest)
        {
            glDisable(GL_SCISSOR_TEST);
        }
    }, &callbackData, sizeof(RenderCallbackData));

    // 添加一个重置渲染状态的回调（确保后续 ImGui 渲染正常）
    drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    if (textureData->HasSparseFailure())
    {
        const char* message = FLocalization::Text(EUiText::SparsePreviewFallback);
        const float margin = FUiScale::Apply(kOverlayMargin);
        const float padding = FUiScale::Apply(kOverlayPadding);
        const float wrapWidth = std::max(1.0f, ClipMax.x - ClipMin.x - 2 * (margin + padding));
        const ImVec2 textSize = ImGui::CalcTextSize(message, nullptr, false, wrapWidth);
        const ImVec2 position(ClipMin.x + margin + padding, ClipMax.y - margin - padding - textSize.y);
        drawList->PushClipRect(ClipMin, ClipMax, true);
        drawList->AddRectFilled(ImVec2(position.x - padding, position.y - padding),
            ImVec2(position.x + textSize.x + padding, position.y + textSize.y + padding),
            kOverlayBackground, FUiScale::Apply(kOverlayRounding));
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), position, kOverlayText, message, nullptr, wrapWidth);
        drawList->PopClipRect();
    }
}

void FImageViewer::RenderPixelProbe(int32_t ImageX, int32_t ImageY)
{
    if (ImageX < 0 || ImageY < 0 || !Document)
    {
        return;
    }

    const FImageData* mainData = Document->GetImageData();

    if (!mainData)
    {
        return;
    }

    const FDisplaySettings& mainDisplay = Document->GetDisplaySettings();

    FPixelSample mainSample;

    if (!FImageSampler::SamplePixel(*mainData, ImageX, ImageY,
                                    mainDisplay, Document->GetParams().BayerPattern, mainSample))
    {
        return;
    }

    // 平铺模式下同时取对比图在**同一坐标**的值，直接把差值摆出来
    const bool bSideBySide = IsSideBySide();
    FPixelSample compareSample;
    bool bHasCompare = false;

    if (bSideBySide)
    {
        const FImageData* compareData = SecondaryDocument->GetImageData();
        const FDisplaySettings& compareDisplay = SecondaryDocument->GetDisplaySettings();

        bHasCompare = FImageSampler::SamplePixel(
            *compareData, ImageX, ImageY,
            compareDisplay, SecondaryDocument->GetParams().BayerPattern, compareSample);
    }

    // 右键把探针里的内容整条拷走。
    // 探针只在 ProbeX/ProbeY 有效时才走到这里，而它们有效就意味着鼠标正悬在画布上，
    // 所以这里不需要再判一次 hover；忙碌时画布被 BeginDisabled 罩住，探针本身就不会出现。
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        const std::string text = BuildProbeText(
            ImageX, ImageY, mainSample, bHasCompare ? &compareSample : nullptr);

        ImGui::SetClipboardText(text.c_str());
        FToast::Show(FLocalization::Text(EUiText::CopiedToClipboard));
    }

    ImGui::BeginTooltip();

    ImGui::Text(FLocalization::Text(EUiText::Coordinates), ImageX, ImageY);
    ImGui::Separator();

    if (bHasCompare)
    {
        // 三列：分量名 / 主图 / 对比图 / 差值
        if (ImGui::BeginTable("##ProbeTable", 4, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("");
            ImGui::TableSetupColumn(FLocalization::Text(EUiText::MainImage));
            ImGui::TableSetupColumn(FLocalization::Text(EUiText::CompareImage));
            ImGui::TableSetupColumn(FLocalization::Text(EUiText::Difference));
            ImGui::TableHeadersRow();

            const int32_t count = std::min(mainSample.Count, compareSample.Count);

            for (int32_t i = 0; i < count; ++i)
            {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", mainSample.Labels[i]);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", mainSample.Values[i]);

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", compareSample.Values[i]);

                ImGui::TableSetColumnIndex(3);
                const int32_t delta = mainSample.Values[i] - compareSample.Values[i];

                if (delta == 0)
                {
                    ImGui::TextDisabled("0");
                }
                else
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f), "%+d", delta);
                }
            }

            ImGui::EndTable();
        }

        ImGui::Separator();

        ImGui::ColorButton("##MainColor",
                           ImVec4(mainSample.Rgb[0], mainSample.Rgb[1], mainSample.Rgb[2], 1.0f),
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                           FUiScale::Apply(40.0f, 20.0f));
        ImGui::SameLine();
        ImGui::ColorButton("##CompareColor",
                           ImVec4(compareSample.Rgb[0], compareSample.Rgb[1], compareSample.Rgb[2], 1.0f),
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                           FUiScale::Apply(40.0f, 20.0f));
        ImGui::SameLine();
        ImGui::TextDisabled(FLocalization::Text(EUiText::MainAndComparison));
    }
    else
    {
        for (int32_t i = 0; i < mainSample.Count; ++i)
        {
            const int32_t componentMax =
                mainSample.GetComponentMaxValue(i);
            const float percent = componentMax > 0
                ? (100.0f * static_cast<float>(mainSample.Values[i]) / static_cast<float>(componentMax))
                : 0.0f;

            ImGui::Text("%-4s %5d  (%.1f%%)", mainSample.Labels[i], mainSample.Values[i], percent);
        }

        ImGui::Separator();

        ImGui::ColorButton("##ProbeColor",
                           ImVec4(mainSample.Rgb[0], mainSample.Rgb[1], mainSample.Rgb[2], 1.0f),
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                           FUiScale::Apply(48.0f, 24.0f));
        ImGui::SameLine();
        ImGui::Text("#%02X%02X%02X",
                    static_cast<int32_t>(mainSample.Rgb[0] * 255.0f + 0.5f),
                    static_cast<int32_t>(mainSample.Rgb[1] * 255.0f + 0.5f),
                    static_cast<int32_t>(mainSample.Rgb[2] * 255.0f + 0.5f));

        // 只在传输函数确实生效时给 nits —— SDR 素材上这个数字是按参考白折算出来的，
        // 摆出来会被当成真实亮度
        if (mainSample.bPipelineActive)
        {
            ImGui::Text(FLocalization::Text(EUiText::Luminance), mainSample.LinearNits);
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled(FLocalization::Text(EUiText::CopySampleHelp));

    ImGui::EndTooltip();
}

void FImageViewer::RenderToolbar()
{
    const float groupGap = ImGui::GetStyle().ItemSpacing.x * kToolbarGroupGapMultiplier;

    // 按钮文字统一白色（仅对下方按钮生效）
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    if (ImGui::Button(FLocalization::Text(EUiText::AutoFit))) { SetDisplayMode(EDisplayMode::AutoFit); }
    ImGui::SameLine();
    if (ImGui::Button(FLocalization::Text(EUiText::Fill)))   { SetDisplayMode(EDisplayMode::Fill); }
    ImGui::SameLine();
    if (ImGui::Button(u8"1:1"))    { SetDisplayMode(EDisplayMode::OneToOne); }
    ImGui::SameLine();
    if (ImGui::Button(FLocalization::Text(EUiText::Reset)))   { ResetView(); }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            GetPanZoomPeer(GetInteractionTarget())
                ? FLocalization::Text(EUiText::ResetLinkedViewHelp)
                : FLocalization::Text(EUiText::ResetViewHelp));
    }

    ImGui::PopStyleColor();   // 恢复，后面的文字仍是默认色

    // 缩放：与属性面板里的"曝光"用同一种控件（DragFloat），可拖动也可双击直接输入。
    //
    // 多加一个对数刻度是因为量程横跨 2%~5000%：线性拖动时每像素的增量是固定的，
    // 在 100% 附近要么慢得没法用、要么在高端一格跳过好几十个百分点。
    //
    // 对数模式下 ImGui 把 v_speed 先除以量程再作用在 [0,1] 的参数空间上，
    // 所以速度直接由"拖满全程要多少像素"反推 —— 这样改 kMinZoom/kMaxZoom 时
    // 手感自动跟着走，不会留下一个按老量程调出来的魔数。
    ImGui::SameLine(0.0f, groupGap);

    const float zoomMinPercent = kMinZoom * kPercentScale;
    const float zoomMaxPercent = kMaxZoom * kPercentScale;

    float zoomPercent = GetZoom() * kPercentScale;

    ImGui::SetNextItemWidth(ImGui::CalcTextSize("5000.0%____").x);

    if (ImGui::DragFloat(FLocalization::Text(EUiText::Zoom), &zoomPercent,
                         (zoomMaxPercent - zoomMinPercent)
                            / FUiScale::Apply(kZoomDragFullRangePixels),
                         zoomMinPercent, zoomMaxPercent,
                         "%.1f%%", ImGuiSliderFlags_Logarithmic))
    {
        SetZoom(zoomPercent * kPercentToRatio);
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(FLocalization::Text(EUiText::ZoomHelp));
    }

    const bool bTiled = IsSideBySide();

    if (bTiled)
    {
        ImGui::SameLine();

        const char* syncTooltip = bPanZoomSynchronized
            ? FLocalization::Text(EUiText::LinkedViewHelp)
            : FLocalization::Text(EUiText::UnlinkedViewHelp);

        if (FUiIcons::ViewerButton(
                "##SynchronizePanZoom",
                FUiIcons::EViewerGlyph::ChainLink,
                syncTooltip,
                bPanZoomSynchronized))
        {
            SetPanZoomSynchronized(!bPanZoomSynchronized);
        }
    }

    if (bTiled)
    {
        const float buttonSize = ImGui::GetFrameHeight();
        const float layoutWidth =
            buttonSize * static_cast<float>(kToolbarLayoutButtonCount)
            + ImGui::GetStyle().ItemSpacing.x;
        const float layoutStartX = ImGui::GetWindowContentRegionMax().x - layoutWidth;
        const float layoutStartScreenX = ImGui::GetWindowPos().x + layoutStartX;

        // 正常宽度下固定在同一行最右侧；窗口过窄时换到下一行，避免盖住缩放输入框。
        if (ImGui::GetItemRectMax().x + FUiScale::Apply(kToolbarLayoutGap)
            <= layoutStartScreenX)
        {
            ImGui::SameLine();
        }
        else
        {
            ImGui::NewLine();
        }

        ImGui::SetCursorPosX(layoutStartX);

        if (FUiIcons::ViewerButton(
                "##TileHorizontal",
                FUiIcons::EViewerGlyph::TileHorizontal,
                FLocalization::Text(EUiText::TileHorizontalHelp),
                TileLayout == ETileLayout::Horizontal))
        {
            SetTileLayout(ETileLayout::Horizontal);
        }

        ImGui::SameLine();

        if (FUiIcons::ViewerButton(
                "##TileVertical",
                FUiIcons::EViewerGlyph::TileVertical,
                FLocalization::Text(EUiText::TileVerticalHelp),
                TileLayout == ETileLayout::Vertical))
        {
            SetTileLayout(ETileLayout::Vertical);
        }
    }

    // 色彩标准 / 数值范围 / 通道隔离都在属性面板里设置：
    // 它们是"这幅图怎么解读"的属性，且平铺对比时两侧各有一份，
    // 放在工具栏就没法表达是在改哪一张图。
}

void FImageViewer::InitializeOpenGLResources()
{
    if (bOpenGLResourcesInitialized)
    {
        return;
    }

    // 只持久保存可跨 Context 共享的顶点缓冲；VAO 在绘制回调中管理。
    const float quadVertices[] = {
        // 位置        // 纹理坐标
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,

        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 1.0f
    };

    GLint lastArrayBuffer = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &lastArrayBuffer);
    glGenBuffers(1, &QuadVBO);
    glBindBuffer(GL_ARRAY_BUFFER, QuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, lastArrayBuffer);

    bOpenGLResourcesInitialized = true;
    LOGD(kImageViewerLogTag, "Shared image quad vertex buffer initialized");
}

void FImageViewer::DestroyOpenGLResources()
{
    if (QuadVBO != 0)
    {
        glDeleteBuffers(1, &QuadVBO);
        QuadVBO = 0;
        LOGD(kImageViewerLogTag, "Shared image quad vertex buffer released");
    }

    bOpenGLResourcesInitialized = false;
}
