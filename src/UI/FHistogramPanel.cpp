#include "Core/FLocalization.h"
#include "FHistogramPanel.h"

#include "Core/FUserSettings.h"
#include "Image/FImageData.h"
#include "Image/FImageSampler.h"
#include "FUiScale.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace
{
    /// BT.601 亮度权重，用于把 RGB 折算成一条亮度曲线
    constexpr float kLumaR = 0.299f;
    constexpr float kLumaG = 0.587f;
    constexpr float kLumaB = 0.114f;

    constexpr float kHistogramPlotHeight = 80.0f;
    constexpr float kMinimumPlotExtent = 1.0f;
    constexpr float kOverlayLumaLineThickness = 2.0f;
    constexpr float kOverlayColorLineThickness = 1.5f;
    constexpr float kOverlayGuideLineThickness = 1.0f;

    constexpr uint32_t kRedPlotColor = IM_COL32(220, 80, 80, 255);
    constexpr uint32_t kGreenPlotColor = IM_COL32(80, 200, 80, 255);
    constexpr uint32_t kBluePlotColor = IM_COL32(90, 130, 230, 255);
    constexpr uint32_t kLumaPlotColor = IM_COL32(160, 160, 160, 255);
}

FHistogramPanel::FHistogramPanel()
    : bHasData(false)
    , SampleCount(0)
    , SampleStep(1)
    , bOverlay(FUserSettings::GetHistogramOverlayEnabled())
    , bLogScale(FUserSettings::GetHistogramLogScaleEnabled())
{
    Red.fill(0.0f);
    Green.fill(0.0f);
    Blue.fill(0.0f);
    Luma.fill(0.0f);
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
    Red.fill(0.0f);
    Green.fill(0.0f);
    Blue.fill(0.0f);
    Luma.fill(0.0f);

    bHasData = false;
    SampleCount = 0;
    SampleStep = 1;

    if (!ImageData || !ImageData->IsValid())
    {
        return;
    }

    const int32_t width = ImageData->GetWidth();
    const int32_t height = ImageData->GetHeight();
    const int64_t pixelCount = static_cast<int64_t>(width) * height;

    // 降采样步长：让总采样点数落在目标附近
    int32_t step = 1;

    if (pixelCount > kTargetSampleCount)
    {
        step = static_cast<int32_t>(std::sqrt(static_cast<double>(pixelCount) / kTargetSampleCount));
        step = std::max(1, step);
    }

    SampleStep = step;

    // 色彩管线只展开一次：25 万个采样点各建一遍矩阵会明显卡顿
    const FImageSampler::FSampleContext context =
        FImageSampler::MakeContext(*ImageData, Display, BayerPattern);

    FPixelSample sample;
    int64_t counted = 0;

    for (int32_t y = 0; y < height; y += step)
    {
        for (int32_t x = 0; x < width; x += step)
        {
            if (!FImageSampler::SamplePixel(*ImageData, x, y, context, sample))
            {
                continue;
            }

            const int32_t r = std::clamp(static_cast<int32_t>(sample.Rgb[0] * 255.0f + 0.5f), 0, kBinCount - 1);
            const int32_t g = std::clamp(static_cast<int32_t>(sample.Rgb[1] * 255.0f + 0.5f), 0, kBinCount - 1);
            const int32_t b = std::clamp(static_cast<int32_t>(sample.Rgb[2] * 255.0f + 0.5f), 0, kBinCount - 1);

            Red[r] += 1.0f;
            Green[g] += 1.0f;
            Blue[b] += 1.0f;

            const float lumaValue = kLumaR * sample.Rgb[0] + kLumaG * sample.Rgb[1] + kLumaB * sample.Rgb[2];
            const int32_t l = std::clamp(static_cast<int32_t>(lumaValue * 255.0f + 0.5f), 0, kBinCount - 1);
            Luma[l] += 1.0f;

            ++counted;
        }
    }

    SampleCount = counted;
    bHasData = (counted > 0);
}

FHistogramPanel::FHistogramBins FHistogramPanel::BuildDisplayBins(const FHistogramBins& Bins) const
{
    FHistogramBins display = Bins;

    if (bLogScale)
    {
        for (float& v : display)
        {
            v = std::log10(v + 1.0f);
        }
    }

    return display;
}

void FHistogramPanel::RenderOverlayPlot()
{
    const FHistogramBins redDisplay = BuildDisplayBins(Red);
    const FHistogramBins greenDisplay = BuildDisplayBins(Green);
    const FHistogramBins blueDisplay = BuildDisplayBins(Blue);
    const FHistogramBins lumaDisplay = BuildDisplayBins(Luma);

    // 四个通道必须共用同一纵轴，否则曲线高度无法横向比较。
    const float maxValue = std::max({
        *std::max_element(redDisplay.begin(), redDisplay.end()),
        *std::max_element(greenDisplay.begin(), greenDisplay.end()),
        *std::max_element(blueDisplay.begin(), blueDisplay.end()),
        *std::max_element(lumaDisplay.begin(), lumaDisplay.end())});
    const float scaleMax = maxValue > 0.0f ? maxValue : 1.0f;

    const ImVec2 plotSize(
        std::max(ImGui::GetContentRegionAvail().x, kMinimumPlotExtent),
        FUiScale::Apply(kHistogramPlotHeight));
    ImGui::InvisibleButton("##OverlayHistogram", plotSize);

    const bool bHovered = ImGui::IsItemHovered();
    const ImVec2 plotMin = ImGui::GetItemRectMin();
    const ImVec2 plotMax = ImGui::GetItemRectMax();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(
        plotMin,
        plotMax,
        ImGui::GetColorU32(ImGuiCol_FrameBg),
        style.FrameRounding);

    if (style.FrameBorderSize > 0.0f)
    {
        drawList->AddRect(
            plotMin,
            plotMax,
            ImGui::GetColorU32(ImGuiCol_Border),
            style.FrameRounding,
            ImDrawFlags_None,
            style.FrameBorderSize);
    }

    const ImVec2 graphMin(
        plotMin.x + style.FramePadding.x,
        plotMin.y + style.FramePadding.y);
    const ImVec2 graphMax(
        plotMax.x - style.FramePadding.x,
        plotMax.y - style.FramePadding.y);

    if (graphMax.x <= graphMin.x || graphMax.y <= graphMin.y)
    {
        return;
    }

    auto DrawSeries = [&](const FHistogramBins& Bins, uint32_t Color, float Thickness)
    {
        std::array<ImVec2, kBinCount> points;
        const float graphWidth = graphMax.x - graphMin.x;
        const float graphHeight = graphMax.y - graphMin.y;
        const float maximumBinIndex = static_cast<float>(kBinCount - 1);

        for (int32_t index = 0; index < kBinCount; ++index)
        {
            const float xRatio = static_cast<float>(index) / maximumBinIndex;
            const float yRatio = std::clamp(Bins[index] / scaleMax, 0.0f, 1.0f);
            points[index] = ImVec2(
                graphMin.x + xRatio * graphWidth,
                graphMax.y - yRatio * graphHeight);
        }

        drawList->AddPolyline(
            points.data(),
            static_cast<int>(points.size()),
            ImGui::GetColorU32(ImColor(Color).Value),
            ImDrawFlags_None,
            Thickness);
    };

    const ImVec2 mousePosition = ImGui::GetIO().MousePos;
    const bool bHoveringGraph = bHovered
        && mousePosition.x >= graphMin.x
        && mousePosition.x <= graphMax.x
        && mousePosition.y >= graphMin.y
        && mousePosition.y <= graphMax.y;
    int32_t hoveredBin = 0;

    if (bHoveringGraph)
    {
        const float mouseRatio = std::clamp(
            (mousePosition.x - graphMin.x) / (graphMax.x - graphMin.x),
            0.0f,
            1.0f);
        hoveredBin = std::clamp(
            static_cast<int32_t>(mouseRatio * static_cast<float>(kBinCount - 1) + 0.5f),
            0,
            kBinCount - 1);
    }

    drawList->PushClipRect(graphMin, graphMax, true);

    // 亮度先打底，RGB 再覆盖；所有曲线只创建一个 ImGui 布局项。
    DrawSeries(
        lumaDisplay,
        kLumaPlotColor,
        FUiScale::Apply(kOverlayLumaLineThickness));
    DrawSeries(
        redDisplay,
        kRedPlotColor,
        FUiScale::Apply(kOverlayColorLineThickness));
    DrawSeries(
        greenDisplay,
        kGreenPlotColor,
        FUiScale::Apply(kOverlayColorLineThickness));
    DrawSeries(
        blueDisplay,
        kBluePlotColor,
        FUiScale::Apply(kOverlayColorLineThickness));

    if (bHoveringGraph)
    {
        const float guideX = graphMin.x
            + (static_cast<float>(hoveredBin) / static_cast<float>(kBinCount - 1))
            * (graphMax.x - graphMin.x);
        drawList->AddLine(
            ImVec2(guideX, graphMin.y),
            ImVec2(guideX, graphMax.y),
            ImGui::GetColorU32(ImGuiCol_PlotLinesHovered),
            FUiScale::Apply(kOverlayGuideLineThickness));
    }

    drawList->PopClipRect();

    if (bHoveringGraph)
    {
        ImGui::BeginTooltip();
        ImGui::Text(FLocalization::Text(EUiText::HistogramBin), hoveredBin);
        ImGui::Separator();
        ImGui::TextColored(ImColor(kRedPlotColor).Value, "R: %.0f", Red[hoveredBin]);
        ImGui::TextColored(ImColor(kGreenPlotColor).Value, "G: %.0f", Green[hoveredBin]);
        ImGui::TextColored(ImColor(kBluePlotColor).Value, "B: %.0f", Blue[hoveredBin]);
        ImGui::TextColored(ImColor(kLumaPlotColor).Value, FLocalization::Text(EUiText::LumaValue), Luma[hoveredBin]);
        ImGui::EndTooltip();
    }
}

void FHistogramPanel::RenderPlot(const char* Label, const FHistogramBins& Bins, uint32_t Color)
{
    const FHistogramBins display = BuildDisplayBins(Bins);

    const float maxValue = *std::max_element(display.begin(), display.end());

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImColor(Color).Value);
    ImGui::PlotHistogram(Label,
                         display.data(),
                         static_cast<int>(display.size()),
                         0,
                         nullptr,
                         0.0f,
                         maxValue > 0.0f ? maxValue : 1.0f,
                         ImVec2(-1.0f, FUiScale::Apply(kHistogramPlotHeight)));
    ImGui::PopStyleColor();
}

void FHistogramPanel::Render()
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::Begin(FLocalization::WindowTitle(EUiText::Histogram));
    ImGui::PopStyleColor();

    if (!bHasData)
    {
        ImGui::Text(FLocalization::Text(EUiText::NoImageData));
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

    if (SampleStep > 1)
    {
        ImGui::TextDisabled(FLocalization::Text(EUiText::HistogramSampleCount), static_cast<long long>(SampleCount), SampleStep);
    }
    else
    {
        ImGui::TextDisabled(FLocalization::Text(EUiText::HistogramFullCount), static_cast<long long>(SampleCount));
    }

    ImGui::Separator();

    if (bOverlay)
    {
        RenderOverlayPlot();
    }
    else
    {
        ImGui::Text(FLocalization::Text(EUiText::Luma));
        RenderPlot("##Luma", Luma, kLumaPlotColor);
        ImGui::Text("R");
        RenderPlot("##R", Red, kRedPlotColor);
        ImGui::Text("G");
        RenderPlot("##G", Green, kGreenPlotColor);
        ImGui::Text("B");
        RenderPlot("##B", Blue, kBluePlotColor);
    }

    ImGui::End();
}
