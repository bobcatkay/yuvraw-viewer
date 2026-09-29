// CPU 像素转换的离线真值测试。不依赖 OpenGL / ImGui。
#include "Image/FImageData.h"
#include "Image/FImageFormatDesc.h"
#include "Image/FImageSampler.h"

#include <algorithm>
#include <array>
#include <cmath>
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
    constexpr int32_t kQuadTestDimension = 12;
    constexpr int32_t kQuadPeriod = 4;
    constexpr int32_t kQuadChannelCount = 3;
    constexpr int32_t kQuadPaddingBytes = 6;
    constexpr float kQuadRedGain = 1.5f;
    constexpr float kQuadEdgeBlue = 50.5f;
    constexpr std::array<float, kQuadPeriod> kQuadBlackLevels{ 5.0f, 10.0f, 20.0f, 30.0f };
    constexpr std::array<int32_t, kQuadPeriod> kQuadPhaseOffsets{ 0, 1, 2, 3 };
    constexpr std::array<int32_t, kQuadChannelCount> kQuadChannelLevels{ kRedLevel, kGreenLevel, kBlueLevel };
    // 独立列出四种 4x4 CFA 真值，不调用生产代码的相位计算生成测试输入。
    constexpr std::array<std::array<int32_t, kQuadPeriod * kQuadPeriod>, kQuadPeriod> kQuadChannelMaps{{
        {{ 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 2, 2, 1, 1, 2, 2 }},
        {{ 2, 2, 1, 1, 2, 2, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0 }},
        {{ 1, 1, 0, 0, 1, 1, 0, 0, 2, 2, 1, 1, 2, 2, 1, 1 }},
        {{ 1, 1, 2, 2, 1, 1, 2, 2, 0, 0, 1, 1, 0, 0, 1, 1 }},
    }};
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
    constexpr int32_t kRgbChannelCount = 3;
    constexpr int32_t kByteMaximum = (1 << kBitsPerByte) - 1;
    constexpr float kSourceRgbTolerance = 1e-5f;
    constexpr int32_t kNeutralYuvWidth = 2;
    constexpr int32_t kNeutralYuvHeight = 2;
    constexpr int32_t kNeutralYuvLumaCount = kNeutralYuvWidth * kNeutralYuvHeight;
    constexpr int32_t kNeutralYuvChromaCount = 2;
    constexpr int32_t kNeutralYuvSampleCount = kNeutralYuvLumaCount + kNeutralYuvChromaCount;
    constexpr int32_t kLimitedBlack8 = 16;
    constexpr int32_t kLimitedWhite8 = 235;
    constexpr int32_t kP010HighlightLuma = 800;
    constexpr int32_t kRawMaximum = (1 << kRawEffectiveBits) - 1;
    constexpr int32_t kConfigurableYuvBitDepths[] = { 8, 10, 12, 14, 16 };
    constexpr float kChangedExposureStops = 2.0f;
    constexpr float kChangedReferenceWhiteNits = 400.0f;
    constexpr float kChangedHlgPeakNits = 2000.0f;
    constexpr float kChangedToneMapWhite = 8.0f;

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

    bool RgbNear(const float Actual[kRgbChannelCount], float R, float G, float B)
    {
        return std::fabs(Actual[0] - R) <= kSourceRgbTolerance
            && std::fabs(Actual[1] - G) <= kSourceRgbTolerance
            && std::fabs(Actual[2] - B) <= kSourceRgbTolerance;
    }

    void StoreLittleEndianWord(uint8_t* Destination, int32_t Code, int32_t SampleShift)
    {
        const uint16_t stored = static_cast<uint16_t>(Code << SampleShift);
        Destination[0] = static_cast<uint8_t>(stored);
        Destination[1] = static_cast<uint8_t>(stored >> kBitsPerByte);
    }

    void MakeNeutralYuv16(
        FImageData& Image,
        EImageFormat Format,
        int32_t BitDepth,
        int32_t SampleShift,
        int32_t LumaCode)
    {
        Image.SetSize(kNeutralYuvWidth, kNeutralYuvHeight);
        Image.SetFormat(Format);
        Image.SetStride(kNeutralYuvWidth * kRawWordBytes);
        if (Format != EImageFormat::P010)
        {
            Image.SetSampleLayout(BitDepth, SampleShift);
        }
        Image.AllocatePixelData(kNeutralYuvSampleCount * kRawWordBytes);

        // 中性色独立于矩阵的 Kr/Kb，真值只由范围还原和有效位深决定。
        const int32_t chromaCenter = 1 << (BitDepth - 1);
        for (int32_t index = 0; index < kNeutralYuvSampleCount; ++index)
        {
            StoreLittleEndianWord(
                Image.GetPixelData() + index * kRawWordBytes,
                index < kNeutralYuvLumaCount ? LumaCode : chromaCenter,
                SampleShift);
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

                FPixelSample sample;
                CheckCondition(
                    (label + " source RGB").c_str(),
                    FImageSampler::SamplePixel(image, x, y, display, Pattern, sample)
                    && RgbNear(sample.SourceRgb,
                               static_cast<float>(kRedLevel) / kByteMaximum,
                               static_cast<float>(kGreenLevel) / kByteMaximum,
                               static_cast<float>(kBlueLevel) / kByteMaximum));
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

    void MakeQuadBayer(FImageData& Image, EImageFormat Format, EBayerPattern Pattern)
    {
        Image.SetSize(kQuadTestDimension, kQuadTestDimension);
        Image.SetFormat(Format);
        const int32_t bytesPerPixel = FImageFormatDesc::GetPlane0BytesPerPixel(Format);
        const int32_t stride = kQuadTestDimension * bytesPerPixel + kQuadPaddingBytes;
        Image.SetStride(stride);
        Image.AllocatePixelData(static_cast<size_t>(stride) * kQuadTestDimension);
        std::fill_n(Image.GetPixelData(), Image.GetPixelDataSize(), kPaddingSentinel);
        const auto& channels = kQuadChannelMaps[static_cast<size_t>(Pattern)];
        for (int32_t y = 0; y < kQuadTestDimension; ++y)
        {
            for (int32_t x = 0; x < kQuadTestDimension; ++x)
            {
                const int32_t channel = channels[(y % kQuadPeriod) * kQuadPeriod + x % kQuadPeriod];
                const int32_t offset = kQuadPhaseOffsets[(y & 1) * 2 + (x & 1)];
                const int32_t code = kQuadChannelLevels[channel] + offset;
                uint8_t* destination = Image.GetPixelData() + static_cast<size_t>(y) * stride + x * bytesPerPixel;
                if (bytesPerPixel == 1) { *destination = static_cast<uint8_t>(code); }
                else { StoreLittleEndianWord(destination, code, 0); }
            }
        }
    }

    void TestQuadBayer()
    {
        const EImageFormat formats[] = {
            EImageFormat::QuadBayer8, EImageFormat::QuadBayer10, EImageFormat::QuadBayer12,
            EImageFormat::QuadBayer14, EImageFormat::QuadBayer16 };
        const EBayerPattern patterns[] = {
            EBayerPattern::RGGB, EBayerPattern::BGGR, EBayerPattern::GRBG, EBayerPattern::GBRG };
        FDisplaySettings display;
        for (EImageFormat format : formats)
        {
            for (EBayerPattern pattern : patterns)
            {
                FImageData image;
                MakeQuadBayer(image, format, pattern);
                const float maximum = static_cast<float>((1 << image.GetSourceBitDepth()) - 1);
                bool bCorrect = true;
                for (int32_t y = kQuadPeriod; y < kQuadPeriod * 2; ++y)
                {
                    for (int32_t x = kQuadPeriod; x < kQuadPeriod * 2; ++x)
                    {
                        FPixelSample sample;
                        const int32_t offset = kQuadPhaseOffsets[(y & 1) * 2 + (x & 1)];
                        const int32_t channel = kQuadChannelMaps[static_cast<size_t>(pattern)]
                            [(y % kQuadPeriod) * kQuadPeriod + x % kQuadPeriod];
                        const char* labels[] = { "R", "G", "B" };
                        bCorrect &= FImageSampler::SamplePixel(image, x, y, display, pattern, sample)
                            && RgbNear(sample.SourceRgb, (kRedLevel + offset) / maximum,
                                (kGreenLevel + offset) / maximum, (kBlueLevel + offset) / maximum)
                            && sample.Count == 1 && sample.Values[0] == kQuadChannelLevels[channel] + offset
                            && std::string(sample.Labels[0]) == labels[channel];
                    }
                }
                const std::string label = std::string(FImageFormatDesc::Get(format).Name) +
                    " pattern " + std::to_string(static_cast<int32_t>(pattern));
                CheckCondition((label + " 四个块内位置与 RAW 读数").c_str(), bCorrect);
                std::vector<uint8_t> rgb;
                CheckCondition((label + " 导出保持原始尺寸").c_str(),
                    FImageSampler::ConvertToRgb8(image, display, pattern, rgb) &&
                    rgb.size() == static_cast<size_t>(kQuadTestDimension) * kQuadTestDimension * kRgbChannelCount);
            }
        }

        FImageData image;
        MakeQuadBayer(image, EImageFormat::QuadBayer8, EBayerPattern::RGGB);
        FPixelSample edge;
        const int32_t phaseOffset = kQuadPhaseOffsets.back();
        CheckCondition("Quad Bayer 边界保留块内位置",
            FImageSampler::SamplePixel(image, 1, 1, display, EBayerPattern::RGGB, edge) &&
            RgbNear(edge.SourceRgb, static_cast<float>(kRedLevel + phaseOffset) / kByteMaximum,
                static_cast<float>(kBoundaryGreen + phaseOffset) / kByteMaximum, kQuadEdgeBlue / kByteMaximum));

        // 四个不同黑电平必须按颜色块应用，不能误按原始像素的奇偶位置索引。
        display.Raw.bEnabled = true;
        display.Raw.bEncodeSrgb = false;
        display.Raw.BlackLevel = kQuadBlackLevels;
        display.Raw.WhiteBalance[0] = kQuadRedGain;
        FPixelSample corrected;
        const float red = (kRedLevel + phaseOffset - kQuadBlackLevels[0]) /
            (kByteMaximum - kQuadBlackLevels[0]) * kQuadRedGain;
        const float green = ((kGreenLevel + phaseOffset - kQuadBlackLevels[1]) /
            (kByteMaximum - kQuadBlackLevels[1]) + (kGreenLevel + phaseOffset - kQuadBlackLevels[2]) /
            (kByteMaximum - kQuadBlackLevels[2])) / 2.0f;
        CheckCondition("Quad Bayer 分块黑电平与白平衡",
            FImageSampler::SamplePixel(image, kQuadPeriod + 1, kQuadPeriod + 1,
                display, EBayerPattern::RGGB, corrected) && RgbNear(corrected.SourceRgb, red, green, 0.0f));
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
        CheckCondition(
            u8"Bayer16 源 RGB 保留有效位深",
            bLowSampled && bHighSampled
            && std::fabs(lowSample.SourceRgb[0]
                         - static_cast<float>(kRawRedLevel) / kRawMaximum) <= kSourceRgbTolerance
            && RgbNear(highSample.SourceRgb,
                       lowSample.SourceRgb[0], lowSample.SourceRgb[1], lowSample.SourceRgb[2]));
    }

    void TestP010SourceRgbBeforeDisplay()
    {
        FImageData image;
        MakeNeutralYuv16(image, EImageFormat::P010,
                         kRawEffectiveBits, kRawHighAlignmentShift, kP010HighlightLuma);
        FDisplaySettings display;
        display.ColorSpace = EColorSpace::BT2020;
        display.Primaries = EColorPrimaries::BT2020;
        display.Transfer = EColorTransfer::HLG;
        display.ColorRange = EColorRange::Limited;
        display.ToneMap = EToneMapOperator::Clip;

        const int32_t rangeScale = 1 << (kRawEffectiveBits - kBitsPerByte);
        const float expectedCode = static_cast<float>(kP010HighlightLuma - kLimitedBlack8 * rangeScale)
            / ((kLimitedWhite8 - kLimitedBlack8) * rangeScale);
        FPixelSample sample;
        const bool bSampled = FImageSampler::SamplePixel(
            image, 0, 0, display, EBayerPattern::RGGB, sample);
        CheckCondition(
            u8"P010 高位对齐返回 10bit 源码值",
            bSampled && sample.MaxValue == kRawMaximum
            && sample.Values[0] == kP010HighlightLuma
            && RgbNear(sample.SourceRgb, expectedCode, expectedCode, expectedCode));
        CheckCondition(
            u8"HLG 高光显示裁白而源 RGB 保留",
            bSampled && RgbNear(sample.Rgb, 1.0f, 1.0f, 1.0f)
            && RgbNear(sample.SourceRgb, expectedCode, expectedCode, expectedCode));

        for (const EColorTransfer transfer : {
                 EColorTransfer::SDR, EColorTransfer::BT1886, EColorTransfer::Linear,
                 EColorTransfer::PQ, EColorTransfer::HLG })
        {
            FDisplaySettings changed = display;
            changed.Transfer = transfer;
            changed.Primaries = EColorPrimaries::DisplayP3;
            changed.ExposureStops = kChangedExposureStops;
            changed.ReferenceWhiteNits = kChangedReferenceWhiteNits;
            changed.HlgPeakNits = kChangedHlgPeakNits;
            changed.ToneMap = EToneMapOperator::Reinhard;
            changed.ToneMapWhite = kChangedToneMapWhite;
            changed.bShowOutOfRange = true;
            FPixelSample changedSample;
            CheckCondition(
                u8"显示设置变化不改变源 RGB 码值",
                FImageSampler::SamplePixel(image, 0, 0, changed, EBayerPattern::RGGB, changedSample)
                && RgbNear(changedSample.SourceRgb, expectedCode, expectedCode, expectedCode));
        }
    }

    void TestConfigurableYuvSourceBitDepth()
    {
        FDisplaySettings display;
        display.ColorRange = EColorRange::Full;
        for (const int32_t bitDepth : kConfigurableYuvBitDepths)
        {
            const int32_t maximum = (1 << bitDepth) - 1;
            const int32_t middleCode = 1 << (bitDepth - 1);
            for (const int32_t shift : { 0, kRawContainerBits - bitDepth })
            {
                for (const int32_t lumaCode : { middleCode, maximum })
                {
                    FImageData image;
                    MakeNeutralYuv16(image, EImageFormat::YUV420SP16, bitDepth, shift, lumaCode);
                    FPixelSample sample;
                    const float expected = static_cast<float>(lumaCode) / maximum;
                    const std::string label = "YUV420SP16 " + std::to_string(bitDepth)
                        + "bit shift=" + std::to_string(shift) + " code=" + std::to_string(lumaCode);
                    CheckCondition(
                        label.c_str(),
                        FImageSampler::SamplePixel(image, 0, 0, display, EBayerPattern::RGGB, sample)
                        && sample.MaxValue == maximum && sample.Values[0] == lumaCode
                        && RgbNear(sample.SourceRgb, expected, expected, expected));
                }
            }
        }
    }

    void TestRgb16SourceAlignment()
    {
        FImageData image;
        image.SetSize(1, 1);
        image.SetFormat(EImageFormat::RGB16);
        image.SetSampleLayout(kRawEffectiveBits, kRawHighAlignmentShift);
        image.AllocatePixelData(kRgbChannelCount * kRawWordBytes);
        const std::array<int32_t, kRgbChannelCount> codes = {
            kRawRedLevel, kRawGreenLevel, kRawBlueLevel
        };
        for (int32_t channel = 0; channel < kRgbChannelCount; ++channel)
        {
            StoreLittleEndianWord(image.GetPixelData() + channel * kRawWordBytes,
                                  codes[channel], kRawHighAlignmentShift);
        }

        FPixelSample sample;
        FDisplaySettings display;
        const bool bSampled = FImageSampler::SamplePixel(
            image, 0, 0, display, EBayerPattern::RGGB, sample);
        CheckCondition(
            u8"RGB16 高位对齐恢复有效整数读数",
            bSampled && sample.MaxValue == kRawMaximum
            && sample.Values[0] == kRawRedLevel && sample.Values[1] == kRawGreenLevel
            && sample.Values[2] == kRawBlueLevel);
        const float r = static_cast<float>(kRawRedLevel) / kRawMaximum;
        const float g = static_cast<float>(kRawGreenLevel) / kRawMaximum;
        const float b = static_cast<float>(kRawBlueLevel) / kRawMaximum;
        CheckCondition(
            u8"RGB16 源 RGB 和预览按有效位深归一化",
            bSampled && RgbNear(sample.SourceRgb, r, g, b) && RgbNear(sample.Rgb, r, g, b));
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
        CheckCondition(
            u8"RGB10_A2 源 RGB 使用 10bit 满量程",
            bSampledFirst && RgbNear(first.SourceRgb,
                static_cast<float>(kRgb10A2FirstR) / kRgb10A2RgbMaximum,
                static_cast<float>(kRgb10A2FirstG) / kRgb10A2RgbMaximum,
                static_cast<float>(kRgb10A2FirstB) / kRgb10A2RgbMaximum));

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
        CheckCondition(
            u8"RGB10_A2 不同 alpha 不改变 RGB 满量程",
            bSampledSecondRow && secondRow.MaxValue == kRgb10A2RgbMaximum
            && RgbNear(secondRow.SourceRgb, 1.0f, 0.0f, 0.0f));

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
    TestQuadBayer();
    TestBayer16AlignmentEquivalence();

    std::printf("\n=== RGB10_A2 packed 采样 ===\n");
    TestRgb10A2Sampling();

    std::printf(u8"\n=== 源 RGB 码值与有效位深 ===\n");
    TestP010SourceRgbBeforeDisplay();
    TestConfigurableYuvSourceBitDepth();
    TestRgb16SourceAlignment();

    std::printf("\n%s\n", gFailures == 0 ? "全部通过" : "存在失败");

    return gFailures == 0 ? 0 : 1;
}
