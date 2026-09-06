#include "FImageSampler.h"

#include "FColorTransform.h"
#include "FImageFormatDesc.h"

#include <algorithm>

using FSampleContext = FImageSampler::FSampleContext;

namespace
{
    constexpr float kPairAverageWeight = 0.5f;
    constexpr float kQuadAverageWeight = 0.25f;
    constexpr int32_t kBitsPerByte = 8;
    constexpr int32_t kRgb10A2ColorBitCount = 10;
    constexpr int32_t kRgb10A2AlphaBitCount = 2;
    constexpr int32_t kRgb10A2ColorChannelCount = 3;
    constexpr int32_t kRgb10A2AlphaChannelIndex =
        kRgb10A2ColorChannelCount;
    constexpr int32_t kRgb10A2GreenShift =
        kRgb10A2ColorBitCount;
    constexpr int32_t kRgb10A2BlueShift =
        kRgb10A2ColorBitCount * 2;
    constexpr int32_t kRgb10A2AlphaShift =
        kRgb10A2ColorBitCount * kRgb10A2ColorChannelCount;
    constexpr int32_t kRgb10A2PackedPixelBytes =
        static_cast<int32_t>(sizeof(uint32_t));
    constexpr uint32_t kRgb10A2ColorMask =
        (1u << kRgb10A2ColorBitCount) - 1u;
    constexpr uint32_t kRgb10A2AlphaMask =
        (1u << kRgb10A2AlphaBitCount) - 1u;

    /**
     * 从平面里读一个采样点的各分量
     * @return 读取成功返回 true；越界返回 false
     */
    bool ReadPlaneSamples(
        const FImageData& ImageData,
        const FFormatDesc& Desc,
        int32_t PlaneIndex,
        int32_t X,
        int32_t Y,
        int32_t OutValues[4])
    {
        if (PlaneIndex < 0 || PlaneIndex >= Desc.PlaneCount)
        {
            return false;
        }

        const FPlaneDesc& plane = Desc.Planes[PlaneIndex];

        const int32_t baseStride = ImageData.GetStride();
        const int32_t planeStride = FImageFormatDesc::GetPlaneStrideBytes(Desc, PlaneIndex, baseStride);
        const int32_t planeWidth = FImageFormatDesc::GetPlaneWidth(Desc, PlaneIndex, ImageData.GetWidth());
        const int32_t planeHeight = FImageFormatDesc::GetPlaneHeight(Desc, PlaneIndex, ImageData.GetHeight());

        // 平面内坐标：按该平面的降采样倍率折算
        const int32_t px = X >> plane.WidthShift;
        const int32_t py = Y >> plane.HeightShift;

        if (px < 0 || py < 0 || px >= planeWidth || py >= planeHeight)
        {
            return false;
        }

        const size_t planeOffset = FImageFormatDesc::GetPlaneOffsetBytes(Desc, PlaneIndex, ImageData.GetHeight(), baseStride);
        const int32_t bytesPerPixel = plane.ChannelCount * plane.BytesPerSample;
        const size_t byteOffset = planeOffset + static_cast<size_t>(py) * planeStride + static_cast<size_t>(px) * bytesPerPixel;

        if (byteOffset + bytesPerPixel > ImageData.GetPixelDataSize())
        {
            return false;
        }

        const uint8_t* p = ImageData.GetPixelData() + byteOffset;

        if (Desc.Format == EImageFormat::RGB10A2)
        {
            if (bytesPerPixel != kRgb10A2PackedPixelBytes ||
                plane.ChannelCount != FPixelSample::ComponentCapacity ||
                plane.BytesPerSample != 1)
            {
                return false;
            }

            // 逐字节拼成小端 word，避免未对齐 uint32_t 访问，也不依赖宿主端序。
            uint32_t packed = 0;

            for (int32_t byteIndex = 0;
                 byteIndex < kRgb10A2PackedPixelBytes;
                 ++byteIndex)
            {
                packed |=
                    static_cast<uint32_t>(p[byteIndex]) <<
                    (byteIndex * kBitsPerByte);
            }

            OutValues[0] = static_cast<int32_t>(
                packed & kRgb10A2ColorMask);
            OutValues[1] = static_cast<int32_t>(
                (packed >> kRgb10A2GreenShift) & kRgb10A2ColorMask);
            OutValues[2] = static_cast<int32_t>(
                (packed >> kRgb10A2BlueShift) & kRgb10A2ColorMask);
            OutValues[kRgb10A2AlphaChannelIndex] = static_cast<int32_t>(
                (packed >> kRgb10A2AlphaShift) & kRgb10A2AlphaMask);

            return true;
        }

        for (int32_t c = 0; c < plane.ChannelCount && c < 4; ++c)
        {
            if (plane.BytesPerSample == 2)
            {
                // 小端序：低字节在前
                OutValues[c] = static_cast<int32_t>(p[c * 2]) | (static_cast<int32_t>(p[c * 2 + 1]) << 8);
            }
            else
            {
                OutValues[c] = static_cast<int32_t>(p[c]);
            }
        }

        return true;
    }

    /**
     * 读取 Bayer 采样并钳制到图像边界，与 GPU shader 的 Fetch() 规则一致。
     */
    bool FetchBayerLevel(
        const FImageData& ImageData,
        const FFormatDesc& Desc,
        int32_t X,
        int32_t Y,
        int32_t SampleShift,
        float MaxValue,
        float& OutLevel)
    {
        const int32_t clampedX = std::min(std::max(X, 0), ImageData.GetWidth() - 1);
        const int32_t clampedY = std::min(std::max(Y, 0), ImageData.GetHeight() - 1);

        int32_t values[4] = { 0, 0, 0, 0 };

        if (!ReadPlaneSamples(ImageData, Desc, 0, clampedX, clampedY, values))
        {
            return false;
        }

        OutLevel = static_cast<float>(values[0] >> SampleShift) / MaxValue;

        return true;
    }

    /**
     * 把非线性 R'G'B' 过一遍色彩管线，写进采样结果
     */
    void FinishRgb(const FSampleContext& Ctx, float R, float G, float B, FPixelSample& OutSample)
    {
        float rgb[3] = { R, G, B };
        float nits = 0.0f;

        FColorTransform::ApplyPipeline(Ctx.Pipeline, rgb, &nits);

        OutSample.Rgb[0] = rgb[0];
        OutSample.Rgb[1] = rgb[1];
        OutSample.Rgb[2] = rgb[2];
        OutSample.LinearNits = nits;
        OutSample.bPipelineActive = Ctx.Pipeline.bEnabled;
    }

    /**
     * 用展开好的上下文采样一个像素
     */
    bool SampleWithContext(
        const FImageData& ImageData,
        int32_t X,
        int32_t Y,
        const FSampleContext& Ctx,
        FPixelSample& OutSample)
    {
        if (X < 0 || Y < 0 || X >= ImageData.GetWidth() || Y >= ImageData.GetHeight())
        {
            return false;
        }

        const EImageFormat format = ImageData.GetFormat();
        const FFormatDesc& desc = FImageFormatDesc::Get(format);

        OutSample = FPixelSample();
        OutSample.MaxValue = Ctx.MaxValue;

        std::fill_n(
            OutSample.ComponentMaxValues,
            FPixelSample::ComponentCapacity,
            Ctx.MaxValue);

        // 采样值可能左移存放在更宽的容器里（P010 是 10bit 放在 16bit 高位），
        // 右移回来才是用户想看到的原始数值
        const int32_t sampleShift = Ctx.SampleShift;
        const float maxF = static_cast<float>(Ctx.MaxValue);

        int32_t plane0[4] = { 0, 0, 0, 0 };

        if (!ReadPlaneSamples(ImageData, desc, 0, X, Y, plane0))
        {
            return false;
        }

        switch (desc.ColorModel)
        {
        case EColorModel::Gray:
        {
            OutSample.Count = 1;
            OutSample.Labels[0] = "V";
            OutSample.Values[0] = plane0[0] >> sampleShift;

            const float g = static_cast<float>(OutSample.Values[0]) / maxF;

            FinishRgb(Ctx, g, g, g, OutSample);

            return true;
        }

        case EColorModel::Bayer:
        {
            // 原始读数仍只报告中心 CFA 点；显示 RGB 则与 GPU 一样做双线性去马赛克。
            // Bayer 是传感器线性读数，因此刻意不走传输函数/原色管线。
            OutSample.Count = 1;
            OutSample.Values[0] = plane0[0] >> sampleShift;

            // 各排布下红色滤片在 2x2 中的位置，与着色器保持一致
            int32_t redX = 0;
            int32_t redY = 0;

            switch (Ctx.BayerPattern)
            {
            case EBayerPattern::RGGB: redX = 0; redY = 0; break;
            case EBayerPattern::BGGR: redX = 1; redY = 1; break;
            case EBayerPattern::GRBG: redX = 1; redY = 0; break;
            case EBayerPattern::GBRG: redX = 0; redY = 1; break;
            }

            const int32_t dx = (X - redX) & 1;
            const int32_t dy = (Y - redY) & 1;

            const float center = static_cast<float>(OutSample.Values[0]) / maxF;
            float left = 0.0f;
            float right = 0.0f;
            float up = 0.0f;
            float down = 0.0f;
            float upLeft = 0.0f;
            float upRight = 0.0f;
            float downLeft = 0.0f;
            float downRight = 0.0f;

            if (!FetchBayerLevel(ImageData, desc, X - 1, Y,     sampleShift, maxF, left) ||
                !FetchBayerLevel(ImageData, desc, X + 1, Y,     sampleShift, maxF, right) ||
                !FetchBayerLevel(ImageData, desc, X,     Y - 1, sampleShift, maxF, up) ||
                !FetchBayerLevel(ImageData, desc, X,     Y + 1, sampleShift, maxF, down) ||
                !FetchBayerLevel(ImageData, desc, X - 1, Y - 1, sampleShift, maxF, upLeft) ||
                !FetchBayerLevel(ImageData, desc, X + 1, Y - 1, sampleShift, maxF, upRight) ||
                !FetchBayerLevel(ImageData, desc, X - 1, Y + 1, sampleShift, maxF, downLeft) ||
                !FetchBayerLevel(ImageData, desc, X + 1, Y + 1, sampleShift, maxF, downRight))
            {
                return false;
            }

            const float horizontal = (left + right) * kPairAverageWeight;
            const float vertical = (up + down) * kPairAverageWeight;
            const float cross4 = (left + right + up + down) * kQuadAverageWeight;
            const float diagonal4 = (upLeft + upRight + downLeft + downRight) * kQuadAverageWeight;

            float rgb[3] = { 0.0f, 0.0f, 0.0f };

            if (dx == 0 && dy == 0)
            {
                OutSample.Labels[0] = "R";
                rgb[0] = center;
                rgb[1] = cross4;
                rgb[2] = diagonal4;
            }
            else if (dx == 1 && dy == 1)
            {
                OutSample.Labels[0] = "B";
                rgb[0] = diagonal4;
                rgb[1] = cross4;
                rgb[2] = center;
            }
            else if (dy == 0)
            {
                OutSample.Labels[0] = "G";
                rgb[0] = horizontal;
                rgb[1] = center;
                rgb[2] = vertical;
            }
            else
            {
                OutSample.Labels[0] = "G";
                rgb[0] = vertical;
                rgb[1] = center;
                rgb[2] = horizontal;
            }

            for (int32_t c = 0; c < 3; ++c)
            {
                OutSample.Rgb[c] = std::max(0.0f, std::min(1.0f, rgb[c]));
            }

            return true;
        }

        case EColorModel::RGB:
        {
            const int32_t channels = std::min(desc.Planes[0].ChannelCount, 4);
            static const char* kRgbLabels[4] = { "R", "G", "B", "A" };

            OutSample.Count = channels;

            for (int32_t c = 0; c < channels; ++c)
            {
                OutSample.Labels[c] = kRgbLabels[c];
                OutSample.Values[c] = plane0[c];
            }

            if (format == EImageFormat::RGB10A2 &&
                channels > kRgb10A2AlphaChannelIndex)
            {
                OutSample.ComponentMaxValues[kRgb10A2AlphaChannelIndex] =
                    static_cast<int32_t>(kRgb10A2AlphaMask);
            }

            const float r = (channels > 0) ? (plane0[0] / maxF) : 0.0f;
            const float g = (channels > 1) ? (plane0[1] / maxF) : 0.0f;
            const float b = (channels > 2) ? (plane0[2] / maxF) : 0.0f;

            FinishRgb(Ctx, r, g, b, OutSample);

            return true;
        }

        case EColorModel::YUV:
        default:
            break;
        }

        // --- YUV ---
        int32_t yv = 0;
        int32_t uv = 0;
        int32_t vv = 0;

        if (desc.bIsPacked)
        {
            // YUY2: [Y0 U Y1 V]   UYVY: [U Y0 V Y1]
            const bool bOddPixel = (X & 1) != 0;

            if (desc.bSwapChroma)  // UYVY
            {
                uv = plane0[0];
                vv = plane0[2];
                yv = bOddPixel ? plane0[3] : plane0[1];
            }
            else                   // YUY2
            {
                uv = plane0[1];
                vv = plane0[3];
                yv = bOddPixel ? plane0[2] : plane0[0];
            }
        }
        else if (desc.PlaneCount == 2)
        {
            // 半平面：色度交织在第 1 平面
            int32_t plane1[4] = { 0, 0, 0, 0 };

            if (!ReadPlaneSamples(ImageData, desc, 1, X, Y, plane1))
            {
                return false;
            }

            yv = plane0[0] >> sampleShift;

            // NV21 的存储顺序是 V,U
            uv = (desc.bSwapChroma ? plane1[1] : plane1[0]) >> sampleShift;
            vv = (desc.bSwapChroma ? plane1[0] : plane1[1]) >> sampleShift;
        }
        else if (desc.PlaneCount == 3)
        {
            // 平面格式：YV12 的平面顺序是 Y,V,U
            const int32_t uPlaneIndex = desc.bSwapChroma ? 2 : 1;
            const int32_t vPlaneIndex = desc.bSwapChroma ? 1 : 2;

            int32_t uPlane[4] = { 0, 0, 0, 0 };
            int32_t vPlane[4] = { 0, 0, 0, 0 };

            if (!ReadPlaneSamples(ImageData, desc, uPlaneIndex, X, Y, uPlane) ||
                !ReadPlaneSamples(ImageData, desc, vPlaneIndex, X, Y, vPlane))
            {
                return false;
            }

            yv = plane0[0] >> sampleShift;
            uv = uPlane[0] >> sampleShift;
            vv = vPlane[0] >> sampleShift;
        }
        else
        {
            return false;
        }

        OutSample.Count = 3;
        OutSample.Labels[0] = "Y";
        OutSample.Labels[1] = "U";
        OutSample.Labels[2] = "V";
        OutSample.Values[0] = yv;
        OutSample.Values[1] = uv;
        OutSample.Values[2] = vv;

        // YCbCr -> R'G'B'，**在非线性域**做，之后才轮到传输函数
        const float y = yv / maxF - Ctx.YuvOffset[0];
        const float u = uv / maxF - Ctx.YuvOffset[1];
        const float v = vv / maxF - Ctx.YuvOffset[2];

        // YuvMatrix 是列主序：[col * 3 + row]
        const float r = Ctx.YuvMatrix[0] * y + Ctx.YuvMatrix[3] * u + Ctx.YuvMatrix[6] * v;
        const float g = Ctx.YuvMatrix[1] * y + Ctx.YuvMatrix[4] * u + Ctx.YuvMatrix[7] * v;
        const float b = Ctx.YuvMatrix[2] * y + Ctx.YuvMatrix[5] * u + Ctx.YuvMatrix[8] * v;

        FinishRgb(Ctx, r, g, b, OutSample);

        return true;
    }
}

namespace FImageSampler
{
    FSampleContext MakeContext(
        const FImageData& ImageData,
        const FDisplaySettings& Display,
        EBayerPattern BayerPattern)
    {
        FSampleContext ctx;

        const int32_t bitDepth = ImageData.GetSourceBitDepth();

        ctx.BayerPattern = BayerPattern;
        ctx.MaxValue = (1 << bitDepth) - 1;
        ctx.SampleShift = ImageData.GetSampleShift();

        FColorTransform::BuildYuvToRgb(
            Display.ColorSpace, Display.ColorRange, bitDepth, ctx.YuvMatrix, ctx.YuvOffset);

        // CPU 侧的目标永远是 8bit sRGB（导出/直方图/差值/探针色块），
        // 与屏幕是不是 HDR 无关，所以 FDisplayOutput 取默认值
        ctx.Pipeline = FColorTransform::BuildPipeline(Display);

        return ctx;
    }

    bool SamplePixel(
        const FImageData& ImageData,
        int32_t X,
        int32_t Y,
        const FSampleContext& Context,
        FPixelSample& OutSample)
    {
        if (!ImageData.IsValid())
        {
            return false;
        }

        return SampleWithContext(ImageData, X, Y, Context, OutSample);
    }

    bool SamplePixel(
        const FImageData& ImageData,
        int32_t X,
        int32_t Y,
        const FDisplaySettings& Display,
        EBayerPattern BayerPattern,
        FPixelSample& OutSample)
    {
        if (!ImageData.IsValid())
        {
            return false;
        }

        return SampleWithContext(ImageData, X, Y, MakeContext(ImageData, Display, BayerPattern), OutSample);
    }

    bool ConvertToRgb8(
        const FImageData& ImageData,
        const FDisplaySettings& Display,
        EBayerPattern BayerPattern,
        std::vector<uint8_t>& OutRgb)
    {
        if (!ImageData.IsValid())
        {
            return false;
        }

        const int32_t width = ImageData.GetWidth();
        const int32_t height = ImageData.GetHeight();

        OutRgb.assign(static_cast<size_t>(width) * height * 3, 0);

        // 色彩管线在循环外展开一次。逐像素重建矩阵会让 4K 图慢上一个数量级
        const FSampleContext ctx = MakeContext(ImageData, Display, BayerPattern);

        FPixelSample sample;

        for (int32_t y = 0; y < height; ++y)
        {
            uint8_t* row = OutRgb.data() + static_cast<size_t>(y) * width * 3;

            for (int32_t x = 0; x < width; ++x)
            {
                if (!SampleWithContext(ImageData, x, y, ctx, sample))
                {
                    continue;
                }

                for (int32_t c = 0; c < 3; ++c)
                {
                    const float value = std::min(std::max(sample.Rgb[c], 0.0f), 1.0f);

                    row[x * 3 + c] = static_cast<uint8_t>(value * 255.0f + 0.5f);
                }
            }
        }

        return true;
    }
}
