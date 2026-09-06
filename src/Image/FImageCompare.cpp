#include "FImageCompare.h"

#include "FImageSampler.h"
#include "Util.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace FImageCompare
{
    std::unique_ptr<FImageData> ComputeDiff(
        const FImageData& A,
        const FImageData& B,
        float Gain,
        const FDisplaySettings& Display,
        EBayerPattern BayerPattern,
        FCompareStats& OutStats)
    {
        OutStats = FCompareStats();

        if (!A.IsValid() || !B.IsValid())
        {
            OutStats.Error = u8"有一幅图未加载";

            return nullptr;
        }

        if (A.GetWidth() != B.GetWidth() || A.GetHeight() != B.GetHeight())
        {
            OutStats.Error = u8"分辨率不一致：" +
                std::to_string(A.GetWidth()) + "x" + std::to_string(A.GetHeight()) + " vs " +
                std::to_string(B.GetWidth()) + "x" + std::to_string(B.GetHeight());

            return nullptr;
        }

        const int32_t width = A.GetWidth();
        const int32_t height = A.GetHeight();

        // 两边都归一到 RGB8 再比，这样格式不同也能对比
        std::vector<uint8_t> rgbA;
        std::vector<uint8_t> rgbB;

        if (!FImageSampler::ConvertToRgb8(A, Display, BayerPattern, rgbA) ||
            !FImageSampler::ConvertToRgb8(B, Display, BayerPattern, rgbB))
        {
            OutStats.Error = u8"转换到 RGB 失败";

            return nullptr;
        }

        const size_t sampleCount = rgbA.size();

        if (sampleCount == 0 || rgbB.size() != sampleCount)
        {
            OutStats.Error = u8"转换结果大小不一致";

            return nullptr;
        }

        auto diff = std::make_unique<FImageData>();
        diff->SetSize(width, height);
        diff->SetFormat(EImageFormat::RGB8);
        diff->SetStride(width * 3);
        diff->AllocatePixelData(sampleCount);

        uint8_t* out = diff->GetPixelData();

        int32_t maxAbsDiff = 0;
        int64_t sumAbsDiff = 0;
        int64_t sumSquaredError = 0;
        int64_t diffPixels = 0;

        const float gain = Gain > 0.0f ? Gain : 1.0f;

        for (size_t i = 0; i < sampleCount; i += 3)
        {
            bool bPixelDiffers = false;

            for (size_t c = 0; c < 3; ++c)
            {
                const int32_t a = rgbA[i + c];
                const int32_t b = rgbB[i + c];
                const int32_t d = a - b;
                const int32_t absDiff = d < 0 ? -d : d;

                maxAbsDiff = std::max(maxAbsDiff, absDiff);
                sumAbsDiff += absDiff;
                sumSquaredError += static_cast<int64_t>(d) * d;

                if (absDiff != 0)
                {
                    bPixelDiffers = true;
                }

                // 放大后写入，便于肉眼看到细微差异
                const int32_t amplified = static_cast<int32_t>(absDiff * gain + 0.5f);
                out[i + c] = static_cast<uint8_t>(std::min(255, amplified));
            }

            if (bPixelDiffers)
            {
                ++diffPixels;
            }
        }

        const int64_t pixelCount = static_cast<int64_t>(width) * height;

        OutStats.bValid = true;
        OutStats.Width = width;
        OutStats.Height = height;
        OutStats.MaxAbsDiff = maxAbsDiff;
        OutStats.MeanAbsDiff = static_cast<double>(sumAbsDiff) / static_cast<double>(sampleCount);
        OutStats.DiffPixelCount = diffPixels;
        OutStats.DiffPixelRatio = pixelCount > 0 ? (static_cast<double>(diffPixels) / static_cast<double>(pixelCount)) : 0.0;

        const double mse = static_cast<double>(sumSquaredError) / static_cast<double>(sampleCount);

        // 完全一致时 MSE 为 0，PSNR 无穷大，用 -1 表示
        OutStats.PSNR = (mse > 0.0) ? (10.0 * std::log10(255.0 * 255.0 / mse)) : -1.0;

        LOGD("ComputeDiff", "%dx%d maxDiff=%d meanDiff=%.4f diffPixels=%lld (%.4f%%) psnr=%.2f",
             width, height, maxAbsDiff, OutStats.MeanAbsDiff,
             static_cast<long long>(diffPixels), OutStats.DiffPixelRatio * 100.0, OutStats.PSNR);

        return diff;
    }
}
