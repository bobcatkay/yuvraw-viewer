#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "Image/FDisplaySettings.h"
#include "Image/FImageFormat.h"

class FImageData;

/**
 * 直方图面板
 *
 * 统计在 CPU 侧完成，只在图像或色彩设置变化时重算一次，不是每帧算。
 * 大图会降采样：直方图看的是分布形状，全量统计和采样 25 万点的结果肉眼无法区分。
 */
class FHistogramPanel
{
public:
    static constexpr int32_t kBinCount = 256;

    /// 大图降采样后的目标采样点数
    static constexpr int32_t kTargetSampleCount = 250000;

    FHistogramPanel();

    void Render();

    /// “清除全部数据”后把当前会话中的显示偏好同步恢复为默认值。
    void ResetPreferences();

    /**
     * 重新统计。传 nullptr 表示清空。
     * 调用方需在图像变化或色彩设置变化后主动调用。
     */
    void Rebuild(
        const FImageData* ImageData,
        const FDisplaySettings& Display,
        EBayerPattern BayerPattern);

private:
    using FHistogramBins = std::array<float, kBinCount>;

    FHistogramBins BuildDisplayBins(const FHistogramBins& Bins) const;
    void RenderOverlayPlot();
    void RenderPlot(const char* Label, const FHistogramBins& Bins, uint32_t Color);

    FHistogramBins Red;
    FHistogramBins Green;
    FHistogramBins Blue;
    FHistogramBins Luma;

    bool bHasData;
    int64_t SampleCount;
    int32_t SampleStep;      ///< 降采样步长，1 表示全量统计

    /// 是否把四条曲线画在同一张图里
    bool bOverlay;

    /// 纵轴对数刻度：暗部/亮部的小峰在线性刻度下会被主峰压平
    bool bLogScale;
};
