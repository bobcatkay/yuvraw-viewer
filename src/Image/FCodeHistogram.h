#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

/** 按有效位深统计 RGB 码值，绘图时聚合完整码值区间。 */
class FCodeHistogram
{
public:
    static constexpr int32_t kMinimumBitDepth = 8;
    static constexpr int32_t kMaximumBitDepth = 16;

    struct FCodeRange
    {
        int32_t First = 0;
        int32_t Last = -1; ///< Last < First 表示空区间。
    };

    bool Reset(int32_t InBitDepth)
    {
        Clear();

        if (InBitDepth < kMinimumBitDepth || InBitDepth > kMaximumBitDepth)
        {
            return false;
        }

        const int32_t binCount = 1 << InBitDepth;
        Bins.assign(static_cast<std::size_t>(binCount), 0.0f);
        BitDepth = InBitDepth;
        MaxCode = binCount - 1;
        return true;
    }

    void Clear()
    {
        Bins.clear();
        BitDepth = 0;
        MaxCode = 0;
        BelowRangeCount = 0;
        AboveRangeCount = 0;
        InvalidCount = 0;
    }

    void Add(float NormalizedCode)
    {
        if (Bins.empty())
        {
            return;
        }

        if (!std::isfinite(NormalizedCode))
        {
            ++InvalidCount;
            return;
        }

        // 先还原并舍入到整数码值，再判越界；YUV 浮点矩阵可能让合法黑白端点略超 [0,1]。
        constexpr double kNearestCodeOffset = 0.5;
        const double code = std::floor(
            static_cast<double>(NormalizedCode) * MaxCode + kNearestCodeOffset);

        if (code < 0.0)
        {
            ++BelowRangeCount;
        }
        else if (code > MaxCode)
        {
            ++AboveRangeCount;
        }
        else
        {
            Bins[static_cast<std::size_t>(code)] += 1.0f;
        }
    }

    const std::vector<float>& GetBins() const { return Bins; }
    int32_t GetBitDepth() const { return BitDepth; }
    int32_t GetMaxCode() const { return MaxCode; }
    int64_t GetBelowRangeCount() const { return BelowRangeCount; }
    int64_t GetAboveRangeCount() const { return AboveRangeCount; }
    int64_t GetInvalidCount() const { return InvalidCount; }

    FCodeRange GetCodeRange(int32_t PlotIndex, int32_t PlotCount) const
    {
        if (Bins.empty() || PlotCount <= 0 || PlotIndex < 0 || PlotIndex >= PlotCount)
        {
            return {};
        }

        // 乘法使用宽整数，16bit 的桶数与绘图区间下标相乘可能超过 int32_t。
        const int64_t binCount = static_cast<int64_t>(Bins.size());
        return {
            static_cast<int32_t>(static_cast<int64_t>(PlotIndex) * binCount / PlotCount),
            static_cast<int32_t>((static_cast<int64_t>(PlotIndex) + 1) * binCount / PlotCount) - 1
        };
    }

    std::vector<float> BuildPlotBins(int32_t MaximumPlotBins) const
    {
        if (Bins.empty())
        {
            return {};
        }

        const int32_t plotCount = std::min(
            std::max(1, MaximumPlotBins), static_cast<int32_t>(Bins.size()));
        std::vector<float> plotBins(static_cast<std::size_t>(plotCount), 0.0f);

        // 窄面板必须累加区间内所有原始桶，抽样取桶会漏掉高位深图像中的窄峰。
        for (int32_t plotIndex = 0; plotIndex < plotCount; ++plotIndex)
        {
            const FCodeRange range = GetCodeRange(plotIndex, plotCount);

            for (int32_t code = range.First; code <= range.Last; ++code)
            {
                plotBins[static_cast<std::size_t>(plotIndex)] += Bins[static_cast<std::size_t>(code)];
            }
        }

        return plotBins;
    }

private:
    std::vector<float> Bins;
    int32_t BitDepth = 0;
    int32_t MaxCode = 0;
    int64_t BelowRangeCount = 0;
    int64_t AboveRangeCount = 0;
    int64_t InvalidCount = 0;
};
