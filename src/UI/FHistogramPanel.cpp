#include "Core/FLocalization.h"
#include "FHistogramPanel.h"

#include "Core/FUserSettings.h"
#include "Image/FImageData.h"
#include "Image/FImageFormatDesc.h"
#include "Image/FImageSampler.h"
#include "FUiScale.h"
#include "Util.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace
{
    constexpr const char* kHistogramLogTag = "Histogram";
    constexpr int32_t kHistogramChannelCount = 4;
    constexpr float kHistogramPlotHeight = 80.0f;
    constexpr float kMinimumPlotExtent = 1.0f;
    constexpr float kBinCenterOffset = 0.5f;
    constexpr float kOverlayLumaLineThickness = 2.0f;
    constexpr float kOverlayColorLineThickness = 1.5f;
    constexpr float kGuideLineThickness = 1.0f;
    constexpr float kTooltipWrapCharacters = 28.0f;
    constexpr size_t kAxisLabelCapacity = 32;

    constexpr uint32_t kRedPlotColor = IM_COL32(220, 80, 80, 255);
    constexpr uint32_t kGreenPlotColor = IM_COL32(80, 200, 80, 255);
    constexpr uint32_t kBluePlotColor = IM_COL32(90, 130, 230, 255);
    constexpr uint32_t kLumaPlotColor = IM_COL32(160, 160, 160, 255);

    float CountRange(const FCodeHistogram& Histogram, const FCodeHistogram::FCodeRange& Range)
    {
        const auto& bins = Histogram.GetBins();
        return std::accumulate(bins.begin() + Range.First, bins.begin() + Range.Last + 1, 0.0f);
    }
}

FHistogramPanel::FHistogramPanel()
    : bHasData(false)
    , SampleCount(0)
    , SampleStep(1)
    , bOverlay(FUserSettings::GetHistogramOverlayEnabled())
    , bLogScale(FUserSettings::GetHistogramLogScaleEnabled())
{
}

void FHistogramPanel::ResetPreferences()
{
    bOverlay = FUserSettings::kDefaultHistogramOverlayEnabled;
    bLogScale = FUserSettings::kDefaultHistogramLogScaleEnabled;
}

void FHistogramPanel::Rebuild(
    const FImageData* ImageData,
    const FDisplaySettings& Display,
    EBayerPattern BayerPattern)
{
    const int32_t previousBitDepth = Red.GetBitDepth();
    Red.Clear();
    Green.Clear();
    Blue.Clear();
    Luma.Clear();
    bHasData = false;
    SampleCount = 0;
    SampleStep = 1;

    if (!ImageData || !ImageData->IsValid())
    {
        return;
    }

    // 使用加载后数据的有效位深：P010 是 10 位，解包 RAW 也不能按 16 位容器统计。
    // WIC / DNG 已转为 RGBA8 的图像则应按当前内存中的 8 位码值统计。
    const int32_t bitDepth = ImageData->GetSourceBitDepth();
    if (!Red.Reset(bitDepth))
    {
        LOGW(kHistogramLogTag, "Unsupported histogram bit depth: %d", bitDepth);
        return;
    }
    Green.Reset(bitDepth);
    Blue.Reset(bitDepth);
    Luma.Reset(bitDepth);

    const int32_t width = ImageData->GetWidth();
    const int32_t height = ImageData->GetHeight();
    const int64_t pixelCount = static_cast<int64_t>(width) * height;
    if (pixelCount > kTargetSampleCount)
    {
        SampleStep = std::max(1, static_cast<int32_t>(
            std::sqrt(static_cast<double>(pixelCount) / kTargetSampleCount)));
    }

    // YUV 亮度使用所选矩阵的系数，从 R'G'B' 恢复对应的 Y'。
    // Bayer 不参与色彩解释，固定使用默认原色系数计算去马赛克码值的亮度代理。
    const EColorModel colorModel = FImageFormatDesc::Get(ImageData->GetFormat()).ColorModel;
    float lumaCoefficients[3];
    if (colorModel == EColorModel::YUV)
    {
        FColorTransform::GetLumaCoefficients(Display.ColorSpace, lumaCoefficients[0], lumaCoefficients[2]);
        lumaCoefficients[1] = 1.0f - lumaCoefficients[0] - lumaCoefficients[2];
    }
    else
    {
        FColorTransform::GetPrimariesLumaCoef(
            colorModel == EColorModel::Bayer ? EColorPrimaries::BT709 : Display.Primaries,
            lumaCoefficients);
    }

    // 复用采样上下文；SourceRgb 在 EOTF、曝光、色调映射及显示裁剪前取得。
    const FImageSampler::FSampleContext context =
        FImageSampler::MakeContext(*ImageData, Display, BayerPattern);
    FPixelSample sample;
    for (int32_t y = 0; y < height; y += SampleStep)
    {
        for (int32_t x = 0; x < width; x += SampleStep)
        {
            if (!FImageSampler::SamplePixel(*ImageData, x, y, context, sample))
            {
                continue;
            }

            Red.Add(sample.SourceRgb[0]);
            Green.Add(sample.SourceRgb[1]);
            Blue.Add(sample.SourceRgb[2]);
            Luma.Add(lumaCoefficients[0] * sample.SourceRgb[0]
                + lumaCoefficients[1] * sample.SourceRgb[1]
                + lumaCoefficients[2] * sample.SourceRgb[2]);
            ++SampleCount;
        }
    }

    bHasData = SampleCount > 0;
    if (previousBitDepth != bitDepth)
    {
        LOGD(kHistogramLogTag, "Source RGB histogram: bitDepth=%d maxCode=%d samples=%lld",
             bitDepth, Red.GetMaxCode(), static_cast<long long>(SampleCount));
    }
}

FHistogramPanel::FPlotBins FHistogramPanel::BuildDisplayBins(
    const FCodeHistogram& Histogram, int32_t PlotCount) const
{
    // 高位深的全部码值先汇总到可见列，再转换纵轴；不能抽取少数码值导致窄面板漏峰。
    FPlotBins display = Histogram.BuildPlotBins(PlotCount);
    if (bLogScale)
    {
        for (float& value : display)
        {
            value = std::log10(value + 1.0f);
        }
    }
    return display;
}

void FHistogramPanel::RenderAxis() const
{
    char maximumLabel[kAxisLabelCapacity];
    std::snprintf(maximumLabel, sizeof(maximumLabel),
                  FLocalization::Text(EUiText::HistogramAxisCode), Red.GetMaxCode());
    const float startX = ImGui::GetCursorPosX();
    const float rightX = startX + ImGui::GetContentRegionAvail().x
        - ImGui::CalcTextSize(maximumLabel).x;

    ImGui::TextDisabled(FLocalization::Text(EUiText::HistogramAxisCode), 0);
    // 窄窗口下让末端标签换行，避免为了放刻度而撑出水平滚动区域。
    if (rightX >= startX + ImGui::GetItemRectSize().x + ImGui::GetStyle().ItemSpacing.x)
    {
        ImGui::SameLine();
        ImGui::SetCursorPosX(rightX);
    }
    ImGui::TextDisabled("%s", maximumLabel);
}

void FHistogramPanel::RenderRangeCounts(const FCodeHistogram& Histogram, const char* Label) const
{
    ImGui::PushTextWrapPos();
    if (Histogram.GetBelowRangeCount() > 0 || Histogram.GetAboveRangeCount() > 0)
    {
        ImGui::TextDisabled(FLocalization::Text(EUiText::HistogramOutOfRangeCounts), Label,
                            static_cast<long long>(Histogram.GetBelowRangeCount()),
                            static_cast<long long>(Histogram.GetAboveRangeCount()));
    }
    if (Histogram.GetInvalidCount() > 0)
    {
        ImGui::TextDisabled(FLocalization::Text(EUiText::HistogramInvalidCount), Label,
                            static_cast<long long>(Histogram.GetInvalidCount()));
    }
    ImGui::PopTextWrapPos();
}

void FHistogramPanel::RenderPlot(const char* Id, const FCodeHistogram* Channel, uint32_t Color)
{
    const ImVec2 plotSize(
        std::max(ImGui::GetContentRegionAvail().x, kMinimumPlotExtent),
        FUiScale::Apply(kHistogramPlotHeight));
    ImGui::InvisibleButton(Id, plotSize);
    if (!ImGui::IsItemVisible())
    {
        RenderAxis();
        return;
    }
    const bool bHovered = ImGui::IsItemHovered();
    const ImVec2 plotMin = ImGui::GetItemRectMin();
    const ImVec2 plotMax = ImGui::GetItemRectMax();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(plotMin, plotMax, ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
    if (style.FrameBorderSize > 0.0f)
    {
        drawList->AddRect(plotMin, plotMax, ImGui::GetColorU32(ImGuiCol_Border),
                          style.FrameRounding, ImDrawFlags_None, style.FrameBorderSize);
    }

    const ImVec2 graphMin(plotMin.x + style.FramePadding.x, plotMin.y + style.FramePadding.y);
    const ImVec2 graphMax(plotMax.x - style.FramePadding.x, plotMax.y - style.FramePadding.y);
    const float graphWidth = graphMax.x - graphMin.x;
    const float graphHeight = graphMax.y - graphMin.y;
    if (graphWidth <= 0.0f || graphHeight <= 0.0f)
    {
        return;
    }

    const int32_t plotCount = std::clamp(static_cast<int32_t>(graphWidth), 1,
                                         static_cast<int32_t>(Red.GetBins().size()));
    const FCodeHistogram* channels[kHistogramChannelCount] = {
        Channel ? Channel : &Luma, &Red, &Green, &Blue
    };
    const uint32_t colors[kHistogramChannelCount] = {
        Channel ? Color : kLumaPlotColor, kRedPlotColor, kGreenPlotColor, kBluePlotColor
    };
    const EUiText labels[kHistogramChannelCount] = {
        EUiText::Luma, EUiText::HistogramRed, EUiText::HistogramGreen, EUiText::HistogramBlue
    };
    const int32_t channelCount = Channel ? 1 : kHistogramChannelCount;
    std::array<FPlotBins, kHistogramChannelCount> display;
    float scaleMax = 0.0f;
    for (int32_t channel = 0; channel < channelCount; ++channel)
    {
        display[channel] = BuildDisplayBins(*channels[channel], plotCount);
        scaleMax = std::max(scaleMax, *std::max_element(display[channel].begin(), display[channel].end()));
    }
    scaleMax = scaleMax > 0.0f ? scaleMax : 1.0f;

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool bHoveringGraph = bHovered
        && mouse.x >= graphMin.x && mouse.x <= graphMax.x
        && mouse.y >= graphMin.y && mouse.y <= graphMax.y;
    const float mouseRatio = std::clamp((mouse.x - graphMin.x) / graphWidth, 0.0f, 1.0f);
    const int32_t hoveredBin = std::min(static_cast<int32_t>(mouseRatio * plotCount), plotCount - 1);
    const float columnWidth = graphWidth / static_cast<float>(plotCount);

    drawList->PushClipRect(graphMin, graphMax, true);
    for (int32_t channel = 0; channel < channelCount; ++channel)
    {
        const ImU32 color = ImGui::GetColorU32(ImColor(colors[channel]).Value);
        if (Channel)
        {
            for (int32_t index = 0; index < plotCount; ++index)
            {
                const float top = graphMax.y - display[channel][index] / scaleMax * graphHeight;
                drawList->AddRectFilled(
                    ImVec2(graphMin.x + index * columnWidth, top),
                    ImVec2(graphMin.x + (index + 1) * columnWidth, graphMax.y),
                    bHoveringGraph && index == hoveredBin
                        ? ImGui::GetColorU32(ImGuiCol_PlotHistogramHovered) : color);
            }
        }
        else
        {
            std::vector<ImVec2> points(static_cast<size_t>(plotCount));
            for (int32_t index = 0; index < plotCount; ++index)
            {
                points[index] = ImVec2(
                    graphMin.x + (index + kBinCenterOffset) * columnWidth,
                    graphMax.y - display[channel][index] / scaleMax * graphHeight);
            }
            const float thickness = FUiScale::Apply(
                channel == 0 ? kOverlayLumaLineThickness : kOverlayColorLineThickness);
            if (plotCount == 1)
            {
                drawList->AddLine(ImVec2(graphMin.x, points[0].y),
                                  ImVec2(graphMax.x, points[0].y), color, thickness);
            }
            else
            {
                drawList->AddPolyline(points.data(), plotCount, color, ImDrawFlags_None, thickness);
            }
        }
    }
    if (bHoveringGraph)
    {
        const float guideX = graphMin.x + (hoveredBin + kBinCenterOffset) * columnWidth;
        drawList->AddLine(ImVec2(guideX, graphMin.y), ImVec2(guideX, graphMax.y),
                          ImGui::GetColorU32(ImGuiCol_PlotLinesHovered),
                          FUiScale::Apply(kGuideLineThickness));
    }
    drawList->PopClipRect();

    if (bHoveringGraph)
    {
        const auto range = Red.GetCodeRange(hoveredBin, plotCount);
        ImGui::BeginTooltip();
        if (range.First == range.Last)
        {
            ImGui::Text(FLocalization::Text(EUiText::HistogramCode), range.First);
        }
        else
        {
            ImGui::Text(FLocalization::Text(EUiText::HistogramCodeRange), range.First, range.Last);
        }
        ImGui::Separator();
        for (int32_t channel = 0; channel < channelCount; ++channel)
        {
            const float count = CountRange(*channels[channel], range);
            if (Channel)
            {
                ImGui::Text(FLocalization::Text(EUiText::HistogramCount), count);
            }
            else
            {
                ImGui::TextColored(ImColor(colors[channel]).Value,
                    FLocalization::Text(EUiText::HistogramChannelCount),
                    FLocalization::Text(labels[channel]), count);
            }
        }
        ImGui::EndTooltip();
    }
    RenderAxis();
}

void FHistogramPanel::Render()
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    const bool bVisible = ImGui::Begin(FLocalization::WindowTitle(EUiText::Histogram));
    ImGui::PopStyleColor();
    if (!bVisible)
    {
        ImGui::End();
        return;
    }
    if (!bHasData)
    {
        ImGui::TextUnformatted(FLocalization::Text(EUiText::NoImageData));
        ImGui::End();
        return;
    }

    if (ImGui::Checkbox(FLocalization::Text(EUiText::HistogramOverlay), &bOverlay))
    {
        FUserSettings::SetHistogramOverlayEnabled(bOverlay);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox(FLocalization::Text(EUiText::HistogramLogScale), &bLogScale))
    {
        FUserSettings::SetHistogramLogScaleEnabled(bLogScale);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(FLocalization::Text(EUiText::HistogramLogScaleHelp));
    }

    ImGui::PushTextWrapPos();
    ImGui::TextDisabled(FLocalization::Text(EUiText::HistogramSourceCodes),
                        Red.GetBitDepth(), Red.GetMaxCode());
    ImGui::PopTextWrapPos();
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * kTooltipWrapCharacters);
        ImGui::TextUnformatted(FLocalization::Text(EUiText::HistogramSourceCodesHelp));
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    if (SampleStep > 1)
    {
        ImGui::TextDisabled(FLocalization::Text(EUiText::HistogramSampleCount),
                            static_cast<long long>(SampleCount), SampleStep, SampleStep);
    }
    else
    {
        ImGui::TextDisabled(FLocalization::Text(EUiText::HistogramFullCount),
                            static_cast<long long>(SampleCount));
    }
    ImGui::Separator();

    if (bOverlay)
    {
        RenderPlot("##OverlayHistogram");
        RenderRangeCounts(Luma, FLocalization::Text(EUiText::Luma));
        RenderRangeCounts(Red, FLocalization::Text(EUiText::HistogramRed));
        RenderRangeCounts(Green, FLocalization::Text(EUiText::HistogramGreen));
        RenderRangeCounts(Blue, FLocalization::Text(EUiText::HistogramBlue));
    }
    else
    {
        ImGui::TextUnformatted(FLocalization::Text(EUiText::Luma));
        RenderPlot("##Luma", &Luma, kLumaPlotColor);
        RenderRangeCounts(Luma, FLocalization::Text(EUiText::Luma));
        ImGui::TextUnformatted(FLocalization::Text(EUiText::HistogramRed));
        RenderPlot("##R", &Red, kRedPlotColor);
        RenderRangeCounts(Red, FLocalization::Text(EUiText::HistogramRed));
        ImGui::TextUnformatted(FLocalization::Text(EUiText::HistogramGreen));
        RenderPlot("##G", &Green, kGreenPlotColor);
        RenderRangeCounts(Green, FLocalization::Text(EUiText::HistogramGreen));
        ImGui::TextUnformatted(FLocalization::Text(EUiText::HistogramBlue));
        RenderPlot("##B", &Blue, kBluePlotColor);
        RenderRangeCounts(Blue, FLocalization::Text(EUiText::HistogramBlue));
    }
    ImGui::End();
}
