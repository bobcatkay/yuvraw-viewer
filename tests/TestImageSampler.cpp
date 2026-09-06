// CPU 像素转换的离线真值测试。不依赖 OpenGL / ImGui。
#include "Image/FImageData.h"
#include "Image/FImageSampler.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    constexpr int32_t kTestWidth = 4;
    constexpr int32_t kTestHeight = 4;
    constexpr uint8_t kRedLevel = 100;
    constexpr uint8_t kGreenLevel = 40;
    constexpr uint8_t kBlueLevel = 10;
    constexpr uint8_t kBoundaryGreen = 70;
    constexpr uint8_t kBoundaryBlue = 48;
    constexpr int32_t kBitsPerByte = 8;
    constexpr int32_t kRawWordBytes = 2;
    constexpr int32_t kRawContainerBits = 16;
    constexpr int32_t kRawEffectiveBits = 10;
    constexpr int32_t kRawHighAlignmentShift =
        kRawContainerBits - kRawEffectiveBits;
    constexpr uint16_t kRawRedLevel = 800;
    constexpr uint16_t kRawGreenLevel = 400;
    constexpr uint16_t kRawBlueLevel = 100;
    constexpr int32_t kRgb10A2TestHeight = 2;
    constexpr int32_t kRgb10A2BytesPerPixel = 4;
    constexpr int32_t kRgb10A2PaddingBytes = 4;
    constexpr int32_t kRgb10A2Stride =
        kTestWidth * kRgb10A2BytesPerPixel + kRgb10A2PaddingBytes;
    constexpr int32_t kRgb10A2RgbMaximum = 1023;
    constexpr int32_t kRgb10A2AlphaMaximum = 3;
    constexpr int32_t kAlphaChannelIndex = 3;
    constexpr int32_t kRgb10A2FirstR = 341;
    constexpr int32_t kRgb10A2FirstG = 682;
    constexpr int32_t kRgb10A2FirstB = 1023;
    constexpr int32_t kRgb10A2FirstA = 2;
    constexpr uint8_t kRgb10A2FirstRgb8R = 85;
    constexpr uint8_t kRgb10A2FirstRgb8G = 170;
    constexpr uint8_t kRgb10A2FirstRgb8B = 255;
    constexpr uint8_t kRgb10A2SecondRgb8R = 255;
    constexpr uint8_t kPaddingSentinel = 0xFF;

    // word=0xBFFAA955：R=341, G=682, B=1023, A=2。
    // 直接写字节真值，避免测试与实现复制同一个 pack 公式后一起写反位序。
    constexpr std::array<uint8_t, kRgb10A2BytesPerPixel>
        kRgb10A2FirstPixelBytes = { 0x55, 0xA9, 0xFA, 0xBF };
    constexpr std::array<uint8_t, kRgb10A2BytesPerPixel>
        kRgb10A2SecondPixelBytes = { 0xFF, 0x03, 0x00, 0xC0 };

    int gFailures = 0;

    void CheckCondition(const char* Label, bool bCondition)
    {
        std::printf(
            "%-34s %s\n",
            Label,
            bCondition ? "OK" : "**FAIL**");

        if (!bCondition)
        {
            ++gFailures;
        }
    }

    void GetRedOrigin(EBayerPattern Pattern, int32_t& OutX, int32_t& OutY)
    {
        switch (Pattern)
        {
        case EBayerPattern::RGGB: OutX = 0; OutY = 0; break;
        case EBayerPattern::BGGR: OutX = 1; OutY = 1; break;
        case EBayerPattern::GRBG: OutX = 1; OutY = 0; break;
        case EBayerPattern::GBRG: OutX = 0; OutY = 1; break;
        }
    }

    void MakeConstantColorBayer(FImageData& Image, EBayerPattern Pattern)
    {
        Image.SetSize(kTestWidth, kTestHeight);
        Image.SetFormat(EImageFormat::Bayer8);
        Image.SetStride(kTestWidth);
        Image.AllocatePixelData(static_cast<size_t>(kTestWidth) * kTestHeight);

        int32_t redX = 0;
        int32_t redY = 0;
        GetRedOrigin(Pattern, redX, redY);

        uint8_t* data = Image.GetPixelData();

        for (int32_t y = 0; y < kTestHeight; ++y)
        {
            for (int32_t x = 0; x < kTestWidth; ++x)
            {
                const int32_t dx = (x - redX) & 1;
                const int32_t dy = (y - redY) & 1;

                uint8_t level = kGreenLevel;

                if (dx == 0 && dy == 0)
                {
                    level = kRedLevel;
                }
                else if (dx == 1 && dy == 1)
                {
                    level = kBlueLevel;
                }

                data[static_cast<size_t>(y) * kTestWidth + x] = level;
            }
        }
    }

    void CheckRgb(
        const char* Label,
        const std::vector<uint8_t>& Rgb,
        int32_t X,
        int32_t Y,
        uint8_t ExpectedR,
        uint8_t ExpectedG,
        uint8_t ExpectedB)
    {
        const size_t offset = (static_cast<size_t>(Y) * kTestWidth + X) * 3;
        const bool passed =
            offset + 2 < Rgb.size() &&
            Rgb[offset + 0] == ExpectedR &&
            Rgb[offset + 1] == ExpectedG &&
            Rgb[offset + 2] == ExpectedB;

        std::printf(
            "%-34s got=(%3d,%3d,%3d) want=(%3d,%3d,%3d) %s\n",
            Label,
            offset + 2 < Rgb.size() ? Rgb[offset + 0] : 0,
            offset + 2 < Rgb.size() ? Rgb[offset + 1] : 0,
            offset + 2 < Rgb.size() ? Rgb[offset + 2] : 0,
            ExpectedR,
            ExpectedG,
            ExpectedB,
            passed ? "OK" : "**FAIL**");

        if (!passed)
        {
            ++gFailures;
        }
    }

    void TestPattern(EBayerPattern Pattern, const char* PatternName)
    {
        FImageData image;
        MakeConstantColorBayer(image, Pattern);

        FDisplaySettings display;
        std::vector<uint8_t> rgb;

        if (!FImageSampler::ConvertToRgb8(image, display, Pattern, rgb))
        {
            ++gFailures;
            std::printf("%-34s **FAIL** ConvertToRgb8 返回 false\n", PatternName);

            return;
        }

        // 中央 2x2 恰好覆盖 R、B 与两个方向的 G 插值分支。
        for (int32_t y = 1; y <= 2; ++y)
        {
            for (int32_t x = 1; x <= 2; ++x)
            {
                const std::string label =
                    std::string(PatternName) + " center(" +
                    std::to_string(x) + "," + std::to_string(y) + ")";

                CheckRgb(label.c_str(), rgb, x, y, kRedLevel, kGreenLevel, kBlueLevel);
            }
        }

        if (Pattern == EBayerPattern::RGGB)
        {
            // GPU shader 的 Fetch() 会钳制越界坐标；左上角真值可锁定 CPU 同样的边界规则。
            CheckRgb(
                "RGGB clamped edge(0,0)",
                rgb,
                0,
                0,
                kRedLevel,
                kBoundaryGreen,
                kBoundaryBlue);
        }
    }

    void MakeEquivalentBayer16(
        FImageData& Image,
        int32_t SampleShift)
    {
        Image.SetSize(kTestWidth, kTestHeight);
        Image.SetFormat(EImageFormat::Bayer16);
        Image.SetStride(kTestWidth * kRawWordBytes);
        Image.SetSampleLayout(kRawEffectiveBits, SampleShift);
        Image.AllocatePixelData(
            static_cast<size_t>(kTestWidth) *
            kTestHeight *
            kRawWordBytes);

        uint8_t* data = Image.GetPixelData();

        for (int32_t y = 0; y < kTestHeight; ++y)
        {
            for (int32_t x = 0; x < kTestWidth; ++x)
            {
                const int32_t dx = x & 1;
                const int32_t dy = y & 1;
                uint16_t level = kRawGreenLevel;

                if (dx == 0 && dy == 0)
                {
                    level = kRawRedLevel;
                }
                else if (dx == 1 && dy == 1)
                {
                    level = kRawBlueLevel;
                }

                const uint16_t stored =
                    static_cast<uint16_t>(level << SampleShift);
                const size_t offset =
                    (static_cast<size_t>(y) * kTestWidth + x) *
                    kRawWordBytes;
                data[offset] = static_cast<uint8_t>(stored);
                data[offset + 1] =
                    static_cast<uint8_t>(stored >> kBitsPerByte);
            }
        }
    }

    void TestBayer16AlignmentEquivalence()
    {
        FImageData lowAligned;
        FImageData highAligned;
        MakeEquivalentBayer16(lowAligned, 0);
        MakeEquivalentBayer16(
            highAligned,
            kRawHighAlignmentShift);

        FDisplaySettings display;
        std::vector<uint8_t> lowRgb;
        std::vector<uint8_t> highRgb;
        const bool bLowConverted =
            FImageSampler::ConvertToRgb8(
                lowAligned,
                display,
                EBayerPattern::RGGB,
                lowRgb);
        const bool bHighConverted =
            FImageSampler::ConvertToRgb8(
                highAligned,
                display,
                EBayerPattern::RGGB,
                highRgb);

        CheckCondition(
            "Bayer16 低位对齐转换成功",
            bLowConverted);
        CheckCondition(
            "Bayer16 高位对齐转换成功",
            bHighConverted);
        CheckCondition(
            "高低位对齐产生相同 RGB 画面",
            !lowRgb.empty() && lowRgb == highRgb);

        FPixelSample lowSample;
        FPixelSample highSample;
        const bool bLowSampled =
            FImageSampler::SamplePixel(
                lowAligned,
                0,
                0,
                display,
                EBayerPattern::RGGB,
                lowSample);
        const bool bHighSampled =
            FImageSampler::SamplePixel(
                highAligned,
                0,
                0,
                display,
                EBayerPattern::RGGB,
                highSample);
        CheckCondition(
            "高低位对齐返回相同原始读数",
            bLowSampled &&
            bHighSampled &&
            lowSample.Values[0] == kRawRedLevel &&
            highSample.Values[0] == kRawRedLevel);
    }

    void TestRgb10A2Sampling()
    {
        FImageData image;
        image.SetSize(kTestWidth, kRgb10A2TestHeight);
        image.SetFormat(EImageFormat::RGB10A2);
        image.SetStride(kRgb10A2Stride);
        image.AllocatePixelData(
            static_cast<size_t>(kRgb10A2Stride) *
            kRgb10A2TestHeight);

        uint8_t* data = image.GetPixelData();
        std::fill(
            data,
            data + image.GetPixelDataSize(),
            0);
        std::copy(
            kRgb10A2FirstPixelBytes.begin(),
            kRgb10A2FirstPixelBytes.end(),
            data);

        const int32_t activeRowBytes =
            kTestWidth * kRgb10A2BytesPerPixel;
        std::fill(
            data + activeRowBytes,
            data + kRgb10A2Stride,
            kPaddingSentinel);
        std::copy(
            kRgb10A2SecondPixelBytes.begin(),
            kRgb10A2SecondPixelBytes.end(),
            data + kRgb10A2Stride);

        FDisplaySettings display;
        FPixelSample first;
        const bool bSampledFirst =
            FImageSampler::SamplePixel(
                image,
                0,
                0,
                display,
                EBayerPattern::RGGB,
                first);

        CheckCondition(
            "RGB10_A2 原始 RGBA 解包",
            bSampledFirst &&
            first.Count == FPixelSample::ComponentCapacity &&
            first.Values[0] == kRgb10A2FirstR &&
            first.Values[1] == kRgb10A2FirstG &&
            first.Values[2] == kRgb10A2FirstB &&
            first.Values[kAlphaChannelIndex] == kRgb10A2FirstA);
        CheckCondition(
            "RGB10_A2 分量满量程",
            bSampledFirst &&
            first.MaxValue == kRgb10A2RgbMaximum &&
            first.GetComponentMaxValue(0) == kRgb10A2RgbMaximum &&
            first.GetComponentMaxValue(1) == kRgb10A2RgbMaximum &&
            first.GetComponentMaxValue(2) == kRgb10A2RgbMaximum &&
            first.GetComponentMaxValue(kAlphaChannelIndex) ==
                kRgb10A2AlphaMaximum);

        FPixelSample secondRow;
        const bool bSampledSecondRow =
            FImageSampler::SamplePixel(
                image,
                0,
                1,
                display,
                EBayerPattern::RGGB,
                secondRow);
        CheckCondition(
            "RGB10_A2 采样遵守 stride padding",
            bSampledSecondRow &&
            secondRow.Values[0] == kRgb10A2RgbMaximum &&
            secondRow.Values[1] == 0 &&
            secondRow.Values[2] == 0 &&
            secondRow.Values[kAlphaChannelIndex] ==
                kRgb10A2AlphaMaximum);

        std::vector<uint8_t> rgb;
        const bool bConverted =
            FImageSampler::ConvertToRgb8(
                image,
                display,
                EBayerPattern::RGGB,
                rgb);
        CheckCondition("RGB10_A2 转 RGB8 成功", bConverted);

        if (bConverted)
        {
            CheckRgb(
                "RGB10_A2 10bit 归一化",
                rgb,
                0,
                0,
                kRgb10A2FirstRgb8R,
                kRgb10A2FirstRgb8G,
                kRgb10A2FirstRgb8B);
            CheckRgb(
                "RGB10_A2 padding 后首像素",
                rgb,
                0,
                1,
                kRgb10A2SecondRgb8R,
                0,
                0);
        }
    }
}

int main()
{
    std::printf("=== Bayer 双线性去马赛克 ===\n");

    TestPattern(EBayerPattern::RGGB, "RGGB");
    TestPattern(EBayerPattern::BGGR, "BGGR");
    TestPattern(EBayerPattern::GRBG, "GRBG");
    TestPattern(EBayerPattern::GBRG, "GBRG");
    TestBayer16AlignmentEquivalence();

    std::printf("\n=== RGB10_A2 packed 采样 ===\n");
    TestRgb10A2Sampling();

    std::printf("\n%s\n", gFailures == 0 ? "全部通过" : "存在失败");

    return gFailures == 0 ? 0 : 1;
}
