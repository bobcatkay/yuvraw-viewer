#include "Image/FCodeHistogram.h"

#include <cstdio>
#include <limits>
#include <numeric>

namespace
{
    int32_t gFailures = 0;
    constexpr int32_t kTenBitDepth = 10;

    void Check(const char* Label, bool bCondition)
    {
        std::printf("[%s] %s\n", bCondition ? "OK" : "FAIL", Label);
        if (!bCondition)
        {
            ++gFailures;
        }
    }

    void TestBitDepthAndPrecision()
    {
        struct FBitDepthCase { int32_t Depth; int32_t MaxCode; };
        const FBitDepthCase kCases[] = {
            { 8, 255 }, { 10, 1023 }, { 12, 4095 }, { 14, 16383 }, { 16, 65535 }
        };
        FCodeHistogram histogram;

        for (const auto& test : kCases)
        {
            std::printf("\nBit depth: %d\n", test.Depth);
            const bool bReset = histogram.Reset(test.Depth);
            Check(u8"支持的有效位深能初始化", bReset);
            if (!bReset)
            {
                continue;
            }

            Check(u8"桶数与最大码值符合实际位深",
                histogram.GetBitDepth() == test.Depth && histogram.GetMaxCode() == test.MaxCode
                && histogram.GetBins().size() == static_cast<std::size_t>(test.MaxCode + 1));
            histogram.Add(0.0f);
            histogram.Add(1.0f);
            // 10bit 相邻码值与 16bit 低位码值均不能先量化成 8bit，否则会合并到黑场。
            histogram.Add(1.0f / test.MaxCode);
            histogram.Add(2.0f / test.MaxCode);
            const auto& bins = histogram.GetBins();
            Check(u8"端点与低位相邻码值分别入桶",
                bins.front() == 1.0f && bins[1] == 1.0f && bins[2] == 1.0f && bins.back() == 1.0f);
        }
    }

    void TestRangeAndReset()
    {
        const int32_t kEndpointDepths[] = {
            FCodeHistogram::kMinimumBitDepth, FCodeHistogram::kMaximumBitDepth
        };
        const float kRoundoff = std::numeric_limits<float>::epsilon();
        const float kInvalidValues[] = {
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity()
        };
        constexpr int64_t kInvalidValueCount = sizeof(kInvalidValues) / sizeof(kInvalidValues[0]);
        FCodeHistogram histogram;

        for (int32_t depth : kEndpointDepths)
        {
            histogram.Reset(depth);
            histogram.Add(-kRoundoff);
            histogram.Add(1.0f + kRoundoff);
            const float oneCode = 1.0f / histogram.GetMaxCode();
            histogram.Add(-oneCode);
            histogram.Add(1.0f + oneCode);
            for (float value : kInvalidValues)
            {
                histogram.Add(value);
            }

            const auto& bins = histogram.GetBins();
            Check(u8"浮点微误差舍入到合法端点，真正越界不堆入首尾桶",
                bins.front() == 1.0f && bins.back() == 1.0f
                && std::accumulate(bins.begin(), bins.end(), 0.0f) == 2.0f
                && histogram.GetBelowRangeCount() == 1 && histogram.GetAboveRangeCount() == 1);
            Check(u8"NaN 与正负 Inf 单独计数", histogram.GetInvalidCount() == kInvalidValueCount);
        }

        histogram.Clear();
        histogram.Add(1.0f);
        const auto emptyRange = histogram.GetCodeRange(0, 1);
        Check(u8"Clear 清空桶、位深和计数，空状态操作安全",
            histogram.GetBins().empty() && histogram.GetBitDepth() == 0 && histogram.GetMaxCode() == 0
            && histogram.GetBelowRangeCount() == 0 && histogram.GetAboveRangeCount() == 0
            && histogram.GetInvalidCount() == 0 && histogram.BuildPlotBins(1).empty()
            && emptyRange.Last < emptyRange.First);

        const int32_t kInvalidDepths[] = {
            FCodeHistogram::kMinimumBitDepth - 1, FCodeHistogram::kMaximumBitDepth + 1
        };
        for (int32_t depth : kInvalidDepths)
        {
            histogram.Reset(kTenBitDepth);
            histogram.Add(1.0f);
            histogram.Add(-1.0f);
            histogram.Add(kInvalidValues[0]);
            Check(u8"无效位深拒绝并清除此前状态",
                !histogram.Reset(depth) && histogram.GetBins().empty() && histogram.GetBitDepth() == 0
                && histogram.GetMaxCode() == 0 && histogram.GetBelowRangeCount() == 0
                && histogram.GetAboveRangeCount() == 0 && histogram.GetInvalidCount() == 0);
        }
    }

    void TestPlotAggregation()
    {
        constexpr int32_t kLastBinExtraSamples = 7;
        const int32_t kPlotCounts[] = { 3, 257 };
        FCodeHistogram histogram;
        histogram.Reset(kTenBitDepth);
        for (int32_t code = 0; code <= histogram.GetMaxCode(); ++code)
        {
            histogram.Add(static_cast<float>(code) / histogram.GetMaxCode());
        }
        for (int32_t index = 0; index < kLastBinExtraSamples; ++index)
        {
            histogram.Add(1.0f);
        }

        const float expectedTotal = static_cast<float>(histogram.GetMaxCode() + 1 + kLastBinExtraSamples);
        for (int32_t plotCount : kPlotCounts)
        {
            const auto plot = histogram.BuildPlotBins(plotCount);
            int32_t nextCode = 0;
            bool bCoversEveryCode = plot.size() == static_cast<std::size_t>(plotCount);
            for (int32_t index = 0; index < static_cast<int32_t>(plot.size()); ++index)
            {
                const auto range = histogram.GetCodeRange(index, plotCount);
                const float expectedCount = static_cast<float>(range.Last - range.First + 1
                    + (range.Last == histogram.GetMaxCode() ? kLastBinExtraSamples : 0));
                bCoversEveryCode = bCoversEveryCode && range.First == nextCode
                    && range.Last >= range.First && plot[index] == expectedCount;
                nextCode = range.Last + 1;
            }

            Check(u8"非整除绘图区间连续覆盖全部码值并保留最末桶尖峰",
                bCoversEveryCode && nextCode == histogram.GetMaxCode() + 1);
            Check(u8"聚合前后计数守恒",
                std::accumulate(plot.begin(), plot.end(), 0.0f) == expectedTotal);
        }

        const auto singleColumn = histogram.BuildPlotBins(0);
        const auto fullResolution = histogram.BuildPlotBins(std::numeric_limits<int32_t>::max());
        Check(u8"绘图列数下限为一列，上限为实际桶数",
            singleColumn.size() == 1 && singleColumn.front() == expectedTotal
            && fullResolution == histogram.GetBins());
        const auto invalidCount = histogram.GetCodeRange(0, 0);
        const auto invalidIndex = histogram.GetCodeRange(1, 1);
        Check(u8"非法绘图区间返回空范围",
            invalidCount.Last < invalidCount.First && invalidIndex.Last < invalidIndex.First);
    }
}

int main()
{
    TestBitDepthAndPrecision();
    TestRangeAndReset();
    TestPlotAggregation();
    std::printf("\nCode histogram failures: %d\n", gFailures);
    return gFailures == 0 ? 0 : 1;
}
