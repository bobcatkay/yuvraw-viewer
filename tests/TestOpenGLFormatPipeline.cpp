// 全格式 OpenGL 管线验证。
//
// 每个源格式都生成一幅确定性 4x4 裸图，经过真实 FRawImageLoader、
// FTextureData、运行时 GLSL 和离屏 framebuffer，最后把 glReadPixels
// 的结果与 FImageSampler 的 CPU 真值逐像素比较。
#include "Core/FLogger.h"
#include "Image/FColorTransform.h"
#include "Image/FImageData.h"
#include "Image/FImageFormatDesc.h"
#include "Image/FImageSampler.h"
#include "Image/FRawImageLoader.h"
#include "gl/FShader.h"
#include "gl/FShaderManager.h"
#include "gl/FTexture.h"
#include "gl/FTextureData.h"

#include <glad/glad.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    constexpr int32_t kTestWidth = 4;
    constexpr int32_t kTestHeight = 4;
    constexpr int32_t kRgbaChannelCount = 4;
    constexpr int32_t kRgbChannelCount = 3;
    constexpr int32_t kRgb10A2BytesPerPixel = 4;
    constexpr uint8_t kRgb10A2ExpectedAlpha8 = 170;
    constexpr int32_t kRawWordBytes = 2;
    constexpr int32_t kBitsPerByte = 8;
    constexpr int32_t kRenderTolerance = 3;
    constexpr int32_t kMaximumReportedPixelMismatches = 4;
    constexpr int32_t kRaw10PixelsPerGroup = 4;
    constexpr int32_t kRaw10BytesPerGroup = 5;
    constexpr int32_t kRaw12PixelsPerGroup = 2;
    constexpr int32_t kRaw12BytesPerGroup = 3;
    constexpr int32_t kRaw14PixelsPerGroup = 4;
    constexpr int32_t kRaw14BytesPerGroup = 7;
    constexpr int32_t kSingleBytePadding = 1;
    constexpr int32_t kTwoBytePadding = 2;
    constexpr uintmax_t kTestLogLimitBytes = 1u << 20;
    constexpr size_t kTestLogFileCount = 2;
    constexpr int32_t kSharedContextWindowSize = 1;
    constexpr GLuint64 kSharedFenceTimeoutNanoseconds = 1000000000ULL;
    constexpr int32_t kGlContextMajorVersion = 3;
    constexpr int32_t kGlContextMinorVersion = 3;

    // --- 色彩管线用例（uPipelineEnabled = 1 的那些分支）---

    /// HDR 用例固定的显示器参数。用常量而不是真机探测值，保证结果可复现
    constexpr float kHdrSdrWhiteNits = 200.0f;
    constexpr float kHdrPeakNits = 1000.0f;

    /// 低余量显示器：MaxOutputScale = 446/240 ≈ 1.86，保色相限幅会大量触发
    constexpr float kLowHeadroomSdrWhiteNits = 240.0f;
    constexpr float kLowHeadroomPeakNits = 446.0f;

    /**
     * fp16 输出的容差
     *
     * 输出是扩展 sRGB 编码值，值域约 [-1, 2.3]。误差主要来自 GPU 的 pow ——
     * PQ 的 EOTF 里有一个 pow(x, 6.277)，那是整条链路上最吃精度的一步。
     *
     * 实测（NVIDIA，全部用例）最大误差 0.00094，这里留 5 倍余量给别的驱动。
     * **不要为了让某条用例过而放宽它** —— 每条用例都会把自己的最大误差打出来，
     * 真出现 0.005 级别的偏差，那是公式对不上，不是精度问题。
     */
    constexpr float kHdrTolerance = 0.005f;

    /**
     * 色彩管线用例的源像素（RGB8，逐行从上到下）
     *
     * 三类刻意凑齐，缺一类就有分支测不到：
     *
     * 1. 中性梯度 —— 黑 / 暗部 / 默认参考白附近 / 峰值附近 / 必然超峰值的白
     * 2. **亮**饱和色 —— 过保色相限幅；逐通道裁会在这些像素上翻色
     * 3. **暗**饱和色 —— BT.2020 转 BT.709 出负分量，却又没超上限
     *
     * 第 3 类不能省：高亮判定里"超上限"优先于"色域外"，亮饱和色一律先被标红，
     * 只有暗饱和色才能把蓝色那条分支跑到。取值也刻意远离判定阈值
     * （负分量约 -0.05 ~ -0.37，而阈值是 -1e-4），免得 GPU 与 CPU 在边界上分歧。
     */
    constexpr uint8_t kPipelineSourcePixels[kTestWidth * kTestHeight][kRgbChannelCount] = {
        {   0,   0,   0 },   // 黑
        {  32,  32,  32 },   // 极暗
        {  80,  80,  80 },   // 暗部
        { 130, 130, 130 },   // PQ 码值 ≈ 0.51 → 100 nit
        { 148, 148, 148 },   // ≈ 203 nit，新的默认参考白
        { 191, 191, 191 },   // ≈ 1000 nit，显示器峰值附近
        { 255, 255, 255 },   // 10000 nit，必然触发限幅
        { 220, 120,  60 },   // 暖色高光，限幅必须保色相
        { 255,  40,  40 },   // 亮饱和红
        {  40, 255,  40 },   // 亮饱和绿
        { 140,  20,  20 },   // 暗饱和红 → G/B 出负分量，R 不超上限
        {  20, 140,  20 },   // 暗饱和绿 → R 负得最多（709 表示不出 2020 的绿）
        {  20,  20, 140 },   // 暗饱和蓝
        { 170,  90, 210 },   // 紫，跨两个原色
        {  10, 200, 120 },   // 青绿
        { 240, 240, 100 },   // 黄高光
    };

    /// 把 kPipelineSourcePixels 摊成紧凑的 RGB8 裸字节
    std::vector<uint8_t> BuildPipelineSourceBytes()
    {
        std::vector<uint8_t> bytes;
        bytes.reserve(sizeof(kPipelineSourcePixels));

        for (const auto& pixel : kPipelineSourcePixels)
        {
            bytes.push_back(pixel[0]);
            bytes.push_back(pixel[1]);
            bytes.push_back(pixel[2]);
        }

        return bytes;
    }

    constexpr float kIdentityMatrix4[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    constexpr float kFullscreenQuad[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
    };

    int32_t gFailures = 0;

    struct FRenderCase
    {
        EImageFormat Format = EImageFormat::Unknown;
        std::string Suffix;
        EByteOrder ByteOrder = EByteOrder::LittleEndian;
        ESampleAlignment SampleAlignment =
            ESampleAlignment::LeastSignificantBits;
        int32_t SourceBitDepth = 8;
        int32_t ExpectedSampleShift = 0;
        int32_t InputStride = 0;
    };

    class FScopedTempFile
    {
    public:
        FScopedTempFile(
            const std::string& Stem,
            int32_t Index,
            const std::vector<uint8_t>& Bytes)
        {
            Path =
                std::filesystem::temp_directory_path() /
                (Stem + "_" + std::to_string(Index) + ".raw");

            std::ofstream stream(Path, std::ios::binary | std::ios::trunc);

            if (stream)
            {
                stream.write(
                    reinterpret_cast<const char*>(Bytes.data()),
                    static_cast<std::streamsize>(Bytes.size()));
                bWritten = stream.good();
            }
        }

        ~FScopedTempFile()
        {
            std::error_code ec;
            std::filesystem::remove(Path, ec);
        }

        bool IsWritten() const
        {
            return bWritten;
        }

        std::string String() const
        {
            return Path.u8string();
        }

    private:
        std::filesystem::path Path;
        bool bWritten = false;
    };

    bool IsPackedBayer(EImageFormat Format)
    {
        return
            Format == EImageFormat::BayerPacked10 ||
            Format == EImageFormat::BayerPacked12 ||
            Format == EImageFormat::BayerPacked14;
    }

    uint16_t MaximumForBits(int32_t BitDepth)
    {
        if (BitDepth >= kRawWordBytes * kBitsPerByte)
        {
            return UINT16_MAX;
        }

        return static_cast<uint16_t>(
            (1u << static_cast<uint32_t>(BitDepth)) - 1u);
    }

    uint16_t FractionOfMaximum(
        uint16_t Maximum,
        uint32_t Numerator,
        uint32_t Denominator)
    {
        return static_cast<uint16_t>(
            (static_cast<uint32_t>(Maximum) * Numerator) /
            Denominator);
    }

    uint16_t BayerValue(
        int32_t X,
        int32_t Y,
        uint16_t Maximum)
    {
        const bool bEvenX = (X & 1) == 0;
        const bool bEvenY = (Y & 1) == 0;

        if (bEvenX && bEvenY)
        {
            return FractionOfMaximum(Maximum, 3, 4);
        }

        if (!bEvenX && !bEvenY)
        {
            return FractionOfMaximum(Maximum, 1, 4);
        }

        return FractionOfMaximum(Maximum, 1, 2);
    }

    void WriteSample(
        std::vector<uint8_t>& Bytes,
        size_t Offset,
        int32_t BytesPerSample,
        uint16_t StoredValue,
        EByteOrder ByteOrder)
    {
        if (BytesPerSample == 1)
        {
            Bytes[Offset] = static_cast<uint8_t>(StoredValue);
            return;
        }

        const uint8_t low = static_cast<uint8_t>(StoredValue & 0xFFu);
        const uint8_t high =
            static_cast<uint8_t>((StoredValue >> kBitsPerByte) & 0xFFu);

        if (ByteOrder == EByteOrder::BigEndian)
        {
            Bytes[Offset] = high;
            Bytes[Offset + 1] = low;
        }
        else
        {
            Bytes[Offset] = low;
            Bytes[Offset + 1] = high;
        }
    }

    uint16_t ToStoredValue(
        uint16_t LogicalValue,
        int32_t SampleShift)
    {
        return static_cast<uint16_t>(
            static_cast<uint32_t>(LogicalValue) <<
            static_cast<uint32_t>(SampleShift));
    }

    void FillRegularPlaneSample(
        std::vector<uint8_t>& Bytes,
        const FFormatDesc& Desc,
        int32_t PlaneIndex,
        int32_t X,
        int32_t Y,
        int32_t Channel,
        int32_t BaseStride,
        uint16_t LogicalValue,
        int32_t SampleShift,
        EByteOrder ByteOrder)
    {
        const FPlaneDesc& plane = Desc.Planes[PlaneIndex];
        const int32_t planeStride =
            FImageFormatDesc::GetPlaneStrideBytes(
                Desc,
                PlaneIndex,
                BaseStride);
        const size_t planeOffset =
            FImageFormatDesc::GetPlaneOffsetBytes(
                Desc,
                PlaneIndex,
                kTestHeight,
                BaseStride);
        const size_t texelOffset =
            planeOffset +
            static_cast<size_t>(Y) *
                static_cast<size_t>(planeStride) +
            (static_cast<size_t>(X) *
                 static_cast<size_t>(plane.ChannelCount) +
             static_cast<size_t>(Channel)) *
                static_cast<size_t>(plane.BytesPerSample);

        WriteSample(
            Bytes,
            texelOffset,
            plane.BytesPerSample,
            ToStoredValue(LogicalValue, SampleShift),
            ByteOrder);
    }

    void FillRgbOrGray(
        const FRenderCase& TestCase,
        const FFormatDesc& Desc,
        int32_t BaseStride,
        std::vector<uint8_t>& Bytes)
    {
        const FPlaneDesc& plane = Desc.Planes[0];
        const uint16_t maximum =
            MaximumForBits(TestCase.SourceBitDepth);

        for (int32_t y = 0; y < kTestHeight; ++y)
        {
            for (int32_t x = 0; x < kTestWidth; ++x)
            {
                for (int32_t channel = 0;
                     channel < plane.ChannelCount;
                     ++channel)
                {
                    uint16_t value =
                        FractionOfMaximum(maximum, 1, 2);

                    if (Desc.ColorModel == EColorModel::RGB)
                    {
                        constexpr uint32_t kNumerators[] = {
                            1, 2, 3, 4,
                        };
                        value = FractionOfMaximum(
                            maximum,
                            kNumerators[channel],
                            4);
                    }

                    FillRegularPlaneSample(
                        Bytes,
                        Desc,
                        0,
                        x,
                        y,
                        channel,
                        BaseStride,
                        value,
                        TestCase.ExpectedSampleShift,
                        TestCase.ByteOrder);
                }
            }
        }
    }

    void FillPackedRgb10A2(
        int32_t BaseStride,
        std::vector<uint8_t>& Bytes)
    {
        // 小端 word=0xBFFAA955：R=341, G=682, B=1023, A=2。
        // 直接使用字节真值，确保 GPU tuple 与 CPU 解包没有共享 pack 公式。
        constexpr std::array<uint8_t, kRgb10A2BytesPerPixel>
            kPixelBytes = { 0x55, 0xA9, 0xFA, 0xBF };

        for (int32_t y = 0; y < kTestHeight; ++y)
        {
            for (int32_t x = 0; x < kTestWidth; ++x)
            {
                const size_t offset =
                    static_cast<size_t>(y) *
                        static_cast<size_t>(BaseStride) +
                    static_cast<size_t>(x) *
                        kRgb10A2BytesPerPixel;
                std::copy(
                    kPixelBytes.begin(),
                    kPixelBytes.end(),
                    Bytes.begin() + offset);
            }
        }
    }

    void FillBayer(
        const FRenderCase& TestCase,
        const FFormatDesc& Desc,
        int32_t BaseStride,
        std::vector<uint8_t>& Bytes)
    {
        const uint16_t maximum =
            MaximumForBits(TestCase.SourceBitDepth);

        for (int32_t y = 0; y < kTestHeight; ++y)
        {
            for (int32_t x = 0; x < kTestWidth; ++x)
            {
                FillRegularPlaneSample(
                    Bytes,
                    Desc,
                    0,
                    x,
                    y,
                    0,
                    BaseStride,
                    BayerValue(x, y, maximum),
                    TestCase.ExpectedSampleShift,
                    TestCase.ByteOrder);
            }
        }
    }

    void FillPlanarOrSemiPlanarYuv(
        const FRenderCase& TestCase,
        const FFormatDesc& Desc,
        int32_t BaseStride,
        std::vector<uint8_t>& Bytes)
    {
        const uint16_t maximum =
            MaximumForBits(TestCase.SourceBitDepth);
        const uint16_t yValue =
            FractionOfMaximum(maximum, 1, 2);
        const uint16_t uValue =
            FractionOfMaximum(maximum, 3, 8);
        const uint16_t vValue =
            FractionOfMaximum(maximum, 5, 8);

        const int32_t yWidth =
            FImageFormatDesc::GetPlaneWidth(
                Desc,
                0,
                kTestWidth);
        const int32_t yHeight =
            FImageFormatDesc::GetPlaneHeight(
                Desc,
                0,
                kTestHeight);

        for (int32_t y = 0; y < yHeight; ++y)
        {
            for (int32_t x = 0; x < yWidth; ++x)
            {
                FillRegularPlaneSample(
                    Bytes,
                    Desc,
                    0,
                    x,
                    y,
                    0,
                    BaseStride,
                    yValue,
                    TestCase.ExpectedSampleShift,
                    TestCase.ByteOrder);
            }
        }

        if (Desc.PlaneCount == 2)
        {
            const int32_t uvWidth =
                FImageFormatDesc::GetPlaneWidth(
                    Desc,
                    1,
                    kTestWidth);
            const int32_t uvHeight =
                FImageFormatDesc::GetPlaneHeight(
                    Desc,
                    1,
                    kTestHeight);

            for (int32_t y = 0; y < uvHeight; ++y)
            {
                for (int32_t x = 0; x < uvWidth; ++x)
                {
                    const uint16_t first =
                        Desc.bSwapChroma ? vValue : uValue;
                    const uint16_t second =
                        Desc.bSwapChroma ? uValue : vValue;

                    FillRegularPlaneSample(
                        Bytes,
                        Desc,
                        1,
                        x,
                        y,
                        0,
                        BaseStride,
                        first,
                        TestCase.ExpectedSampleShift,
                        TestCase.ByteOrder);
                    FillRegularPlaneSample(
                        Bytes,
                        Desc,
                        1,
                        x,
                        y,
                        1,
                        BaseStride,
                        second,
                        TestCase.ExpectedSampleShift,
                        TestCase.ByteOrder);
                }
            }

            return;
        }

        for (int32_t physicalPlane = 1;
             physicalPlane < Desc.PlaneCount;
             ++physicalPlane)
        {
            const bool bPhysicalPlaneIsU =
                Desc.bSwapChroma
                    ? physicalPlane == 2
                    : physicalPlane == 1;
            const uint16_t value =
                bPhysicalPlaneIsU ? uValue : vValue;
            const int32_t planeWidth =
                FImageFormatDesc::GetPlaneWidth(
                    Desc,
                    physicalPlane,
                    kTestWidth);
            const int32_t planeHeight =
                FImageFormatDesc::GetPlaneHeight(
                    Desc,
                    physicalPlane,
                    kTestHeight);

            for (int32_t y = 0; y < planeHeight; ++y)
            {
                for (int32_t x = 0; x < planeWidth; ++x)
                {
                    FillRegularPlaneSample(
                        Bytes,
                        Desc,
                        physicalPlane,
                        x,
                        y,
                        0,
                        BaseStride,
                        value,
                        TestCase.ExpectedSampleShift,
                        TestCase.ByteOrder);
                }
            }
        }
    }

    void FillPackedYuv(
        EImageFormat Format,
        const FFormatDesc& Desc,
        int32_t BaseStride,
        std::vector<uint8_t>& Bytes)
    {
        constexpr uint8_t kPackedY0 = 96;
        constexpr uint8_t kPackedY1 = 160;
        constexpr uint8_t kPackedU = 88;
        constexpr uint8_t kPackedV = 168;
        const int32_t packedWidth =
            FImageFormatDesc::GetPlaneWidth(
                Desc,
                0,
                kTestWidth);
        const int32_t planeStride =
            FImageFormatDesc::GetPlaneStrideBytes(
                Desc,
                0,
                BaseStride);

        for (int32_t y = 0; y < kTestHeight; ++y)
        {
            for (int32_t x = 0; x < packedWidth; ++x)
            {
                const size_t offset =
                    static_cast<size_t>(y) *
                        static_cast<size_t>(planeStride) +
                    static_cast<size_t>(x) *
                        static_cast<size_t>(Desc.Planes[0].ChannelCount);

                if (Format == EImageFormat::UYVY)
                {
                    Bytes[offset + 0] = kPackedU;
                    Bytes[offset + 1] = kPackedY0;
                    Bytes[offset + 2] = kPackedV;
                    Bytes[offset + 3] = kPackedY1;
                }
                else
                {
                    Bytes[offset + 0] = kPackedY0;
                    Bytes[offset + 1] = kPackedU;
                    Bytes[offset + 2] = kPackedY1;
                    Bytes[offset + 3] = kPackedV;
                }
            }
        }
    }

    void PackBayerRow10(
        const std::array<uint16_t, kTestWidth>& Pixels,
        uint8_t* Destination)
    {
        for (int32_t groupStart = 0;
             groupStart < kTestWidth;
             groupStart += kRaw10PixelsPerGroup)
        {
            uint8_t* group =
                Destination +
                (groupStart / kRaw10PixelsPerGroup) *
                    kRaw10BytesPerGroup;
            uint8_t low = 0;

            for (int32_t index = 0;
                 index < kRaw10PixelsPerGroup;
                 ++index)
            {
                const uint16_t value = Pixels[groupStart + index];
                group[index] = static_cast<uint8_t>(value >> 2);
                low |= static_cast<uint8_t>(
                    (value & 0x03u) << (index * 2));
            }

            group[4] = low;
        }
    }

    void PackBayerRow12(
        const std::array<uint16_t, kTestWidth>& Pixels,
        uint8_t* Destination)
    {
        for (int32_t groupStart = 0;
             groupStart < kTestWidth;
             groupStart += kRaw12PixelsPerGroup)
        {
            uint8_t* group =
                Destination +
                (groupStart / kRaw12PixelsPerGroup) *
                    kRaw12BytesPerGroup;
            const uint16_t first = Pixels[groupStart];
            const uint16_t second = Pixels[groupStart + 1];

            group[0] = static_cast<uint8_t>(first >> 4);
            group[1] = static_cast<uint8_t>(second >> 4);
            group[2] = static_cast<uint8_t>(
                (first & 0x0Fu) |
                ((second & 0x0Fu) << 4));
        }
    }

    void PackBayerRow14(
        const std::array<uint16_t, kTestWidth>& Pixels,
        uint8_t* Destination)
    {
        for (int32_t groupStart = 0;
             groupStart < kTestWidth;
             groupStart += kRaw14PixelsPerGroup)
        {
            uint8_t* group =
                Destination +
                (groupStart / kRaw14PixelsPerGroup) *
                    kRaw14BytesPerGroup;
            const uint16_t p0 = Pixels[groupStart + 0];
            const uint16_t p1 = Pixels[groupStart + 1];
            const uint16_t p2 = Pixels[groupStart + 2];
            const uint16_t p3 = Pixels[groupStart + 3];

            group[0] = static_cast<uint8_t>(p0 >> 6);
            group[1] = static_cast<uint8_t>(p1 >> 6);
            group[2] = static_cast<uint8_t>(p2 >> 6);
            group[3] = static_cast<uint8_t>(p3 >> 6);
            group[4] = static_cast<uint8_t>(
                (p0 & 0x3Fu) |
                ((p1 & 0x03u) << 6));
            group[5] = static_cast<uint8_t>(
                ((p1 >> 2) & 0x0Fu) |
                ((p2 & 0x0Fu) << 4));
            group[6] = static_cast<uint8_t>(
                ((p2 >> 4) & 0x03u) |
                ((p3 & 0x3Fu) << 2));
        }
    }

    void FillPackedBayer(
        const FRenderCase& TestCase,
        int32_t BaseStride,
        std::vector<uint8_t>& Bytes)
    {
        const uint16_t maximum =
            MaximumForBits(TestCase.SourceBitDepth);

        for (int32_t y = 0; y < kTestHeight; ++y)
        {
            std::array<uint16_t, kTestWidth> row = {};

            for (int32_t x = 0; x < kTestWidth; ++x)
            {
                row[x] = BayerValue(x, y, maximum);
            }

            uint8_t* destination =
                Bytes.data() +
                static_cast<size_t>(y) *
                    static_cast<size_t>(BaseStride);

            switch (TestCase.Format)
            {
            case EImageFormat::BayerPacked10:
                PackBayerRow10(row, destination);
                break;

            case EImageFormat::BayerPacked12:
                PackBayerRow12(row, destination);
                break;

            case EImageFormat::BayerPacked14:
                PackBayerRow14(row, destination);
                break;

            default:
                break;
            }
        }
    }

    std::vector<uint8_t> BuildSourceBytes(
        const FRenderCase& TestCase)
    {
        const FFormatDesc& desc =
            FImageFormatDesc::Get(TestCase.Format);
        const int32_t baseStride =
            FImageFormatDesc::ResolveBaseStride(
                TestCase.Format,
                kTestWidth,
                TestCase.InputStride);
        const size_t frameSize =
            FImageFormatDesc::CalculateFrameSize(
                TestCase.Format,
                kTestWidth,
                kTestHeight,
                baseStride);
        std::vector<uint8_t> bytes(frameSize, 0);

        if (IsPackedBayer(TestCase.Format))
        {
            FillPackedBayer(TestCase, baseStride, bytes);
            return bytes;
        }

        if (TestCase.Format == EImageFormat::YUY2 ||
            TestCase.Format == EImageFormat::UYVY)
        {
            FillPackedYuv(
                TestCase.Format,
                desc,
                baseStride,
                bytes);
            return bytes;
        }

        if (TestCase.Format == EImageFormat::RGB10A2)
        {
            FillPackedRgb10A2(baseStride, bytes);
            return bytes;
        }

        switch (desc.ColorModel)
        {
        case EColorModel::RGB:
        case EColorModel::Gray:
            FillRgbOrGray(
                TestCase,
                desc,
                baseStride,
                bytes);
            break;

        case EColorModel::YUV:
            FillPlanarOrSemiPlanarYuv(
                TestCase,
                desc,
                baseStride,
                bytes);
            break;

        case EColorModel::Bayer:
            FillBayer(
                TestCase,
                desc,
                baseStride,
                bytes);
            break;
        }

        return bytes;
    }

    std::vector<FRenderCase> BuildCases()
    {
        std::vector<FRenderCase> cases;

        for (const FFormatDesc& desc : FImageFormatDesc::GetAll())
        {
            if (desc.Format == EImageFormat::Unknown)
            {
                continue;
            }

            FRenderCase testCase;
            testCase.Format = desc.Format;
            testCase.Suffix = "default";
            testCase.SourceBitDepth = desc.BitDepth;
            testCase.ExpectedSampleShift = desc.SampleShift;
            cases.push_back(testCase);

            if (desc.StorageLayout.ByteOrderMode ==
                    EFormatPropertyMode::Configurable &&
                desc.Format != EImageFormat::Bayer16)
            {
                FRenderCase bigEndian = testCase;
                bigEndian.Suffix = "big_endian";
                bigEndian.ByteOrder = EByteOrder::BigEndian;
                cases.push_back(bigEndian);
            }
        }

        struct FNonTexelPaddingCase
        {
            EImageFormat Format;
            int32_t PaddingBytes;
            EByteOrder ByteOrder;
        };

        const FNonTexelPaddingCase nonTexelPaddingCases[] = {
            { EImageFormat::RGB16,       kTwoBytePadding,    EByteOrder::BigEndian },
            { EImageFormat::RGBA16,      kTwoBytePadding,    EByteOrder::BigEndian },
            { EImageFormat::RGB10A2,     kTwoBytePadding,    EByteOrder::LittleEndian },
            { EImageFormat::Grayscale16, kSingleBytePadding, EByteOrder::BigEndian },
            { EImageFormat::NV12,        kSingleBytePadding, EByteOrder::LittleEndian },
            { EImageFormat::P010,        kTwoBytePadding,    EByteOrder::LittleEndian },
        };

        for (const FNonTexelPaddingCase& paddingCase :
             nonTexelPaddingCases)
        {
            const FFormatDesc& desc =
                FImageFormatDesc::Get(paddingCase.Format);
            FRenderCase padded;
            padded.Format = paddingCase.Format;
            padded.Suffix = "non_texel_padding";
            padded.ByteOrder = paddingCase.ByteOrder;
            padded.SourceBitDepth = desc.BitDepth;
            padded.ExpectedSampleShift = desc.SampleShift;
            padded.InputStride =
                FImageFormatDesc::ResolveBaseStride(
                    paddingCase.Format,
                    kTestWidth,
                    0) +
                paddingCase.PaddingBytes;
            cases.push_back(padded);
        }

        constexpr int32_t kConfigurableWordTestBitDepth = 10;
        constexpr int32_t kConfigurableWordHighShift =
            kRawWordBytes * kBitsPerByte -
            kConfigurableWordTestBitDepth;

        const EByteOrder orders[] = {
            EByteOrder::LittleEndian,
            EByteOrder::BigEndian,
        };
        const ESampleAlignment alignments[] = {
            ESampleAlignment::LeastSignificantBits,
            ESampleAlignment::MostSignificantBits,
        };

        const EImageFormat configurableWordFormats[] = {
            EImageFormat::Bayer16,
            EImageFormat::YUV420SP16,
        };

        for (EImageFormat format : configurableWordFormats)
        {
            for (EByteOrder order : orders)
            {
                for (ESampleAlignment alignment : alignments)
                {
                    FRenderCase testCase;
                    testCase.Format = format;
                    testCase.Suffix =
                        order == EByteOrder::BigEndian
                            ? "10bit_be"
                            : "10bit_le";
                    testCase.Suffix +=
                        alignment ==
                                ESampleAlignment::MostSignificantBits
                            ? "_high"
                            : "_low";
                    testCase.ByteOrder = order;
                    testCase.SampleAlignment = alignment;
                    testCase.SourceBitDepth =
                        kConfigurableWordTestBitDepth;
                    testCase.ExpectedSampleShift =
                        alignment ==
                                ESampleAlignment::MostSignificantBits
                            ? kConfigurableWordHighShift
                            : 0;
                    cases.push_back(testCase);
                }
            }
        }

        return cases;
    }

    void GlfwErrorCallback(int ErrorCode, const char* Description)
    {
        std::fprintf(
            stderr,
            "GLFW error %d: %s\n",
            ErrorCode,
            Description ? Description : "(no description)");
    }

    class FHiddenGlContext
    {
    public:
        bool Initialize()
        {
            glfwSetErrorCallback(GlfwErrorCallback);

            if (glfwInit() != GLFW_TRUE)
            {
                return false;
            }

            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, kGlContextMajorVersion);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, kGlContextMinorVersion);
            glfwWindowHint(
                GLFW_OPENGL_PROFILE,
                GLFW_OPENGL_CORE_PROFILE);
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_FALSE);

            Window = glfwCreateWindow(
                kTestWidth,
                kTestHeight,
                "YUVRaw GL validation",
                nullptr,
                nullptr);

            if (!Window)
            {
                glfwTerminate();
                return false;
            }

            glfwMakeContextCurrent(Window);

            if (!gladLoadGLLoader(
                    reinterpret_cast<GLADloadproc>(
                        glfwGetProcAddress)))
            {
                glfwDestroyWindow(Window);
                Window = nullptr;
                glfwTerminate();
                return false;
            }

            return true;
        }

        GLFWwindow* GetWindow() const
        {
            return Window;
        }

        ~FHiddenGlContext()
        {
            FShaderManager::Get().Shutdown();

            if (Window)
            {
                glfwMakeContextCurrent(nullptr);
                glfwDestroyWindow(Window);
            }

            glfwTerminate();
        }

    private:
        GLFWwindow* Window = nullptr;
    };

    bool ValidateSharedContextTextureHandoff(GLFWwindow* MainWindow)
    {
        if (!MainWindow)
        {
            return false;
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, kGlContextMajorVersion);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, kGlContextMinorVersion);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        GLFWwindow* uploadWindow = glfwCreateWindow(
            kSharedContextWindowSize,
            kSharedContextWindowSize,
            "YUVRaw shared upload validation",
            nullptr,
            MainWindow);

        if (!uploadWindow)
        {
            std::printf("共享 Context 创建                         **FAIL**\n");
            return false;
        }

        const std::array<uint8_t, kTestWidth * kTestHeight * kRgbaChannelCount>
            pixels = {
                255, 0, 0, 255, 0, 255, 0, 255,
                0, 0, 255, 255, 255, 255, 255, 255,
                32, 64, 96, 255, 64, 96, 128, 255,
                96, 128, 160, 255, 128, 160, 192, 255,
                12, 34, 56, 255, 78, 90, 123, 255,
                145, 167, 189, 255, 210, 220, 230, 255,
                5, 15, 25, 255, 35, 45, 55, 255,
                65, 75, 85, 255, 95, 105, 115, 255,
            };
        FImageData image;
        image.SetSize(kTestWidth, kTestHeight);
        image.SetFormat(EImageFormat::RGBA8);
        image.SetStride(0);
        image.SetPixelData(pixels.data(), pixels.size());

        std::unique_ptr<FTextureData> texture;
        GLsync fence = nullptr;
        bool bDispatchMatches = false;
        bool bUploaded = false;

        std::thread uploader([&]() {
            glfwMakeContextCurrent(uploadWindow);
            bDispatchMatches =
                reinterpret_cast<GLFWglproc>(glad_glTexImage2D) ==
                glfwGetProcAddress("glTexImage2D") &&
                reinterpret_cast<GLFWglproc>(glad_glFenceSync) ==
                glfwGetProcAddress("glFenceSync");
            texture = std::make_unique<FTextureData>();
            bUploaded = bDispatchMatches &&
                texture->CreateFromImageData(&image) &&
                texture->IsValid();

            if (bUploaded)
            {
                fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                glFlush();
            }

            glfwMakeContextCurrent(nullptr);
        });
        uploader.join();

        bool bFenceReady = false;

        if (fence)
        {
            const GLenum wait = glClientWaitSync(
                fence,
                0,
                kSharedFenceTimeoutNanoseconds);
            bFenceReady =
                wait == GL_ALREADY_SIGNALED ||
                wait == GL_CONDITION_SATISFIED;
            glDeleteSync(fence);
        }

        const FTexture* plane = texture ? texture->GetTexture(0) : nullptr;
        const bool bVisibleFromMain =
            plane && glIsTexture(plane->GetID()) == GL_TRUE;
        const bool bPassed =
            bDispatchMatches && bUploaded && bFenceReady && bVisibleFromMain;

        std::printf(
            "共享 Context 上传 + fence + 主 Context 可见性 %s\n",
            bPassed ? "OK" : "**FAIL**");

        // 共享纹理与 fence 在主 Context 上回收，再由主线程销毁隐藏窗口。
        texture.reset();
        glfwDestroyWindow(uploadWindow);
        return bPassed;
    }

    class FFullscreenQuad
    {
    public:
        bool Initialize()
        {
            glGenVertexArrays(1, &VertexArray);
            glGenBuffers(1, &VertexBuffer);
            glBindVertexArray(VertexArray);
            glBindBuffer(GL_ARRAY_BUFFER, VertexBuffer);
            glBufferData(
                GL_ARRAY_BUFFER,
                sizeof(kFullscreenQuad),
                kFullscreenQuad,
                GL_STATIC_DRAW);
            glVertexAttribPointer(
                0,
                2,
                GL_FLOAT,
                GL_FALSE,
                4 * sizeof(float),
                nullptr);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(
                1,
                2,
                GL_FLOAT,
                GL_FALSE,
                4 * sizeof(float),
                reinterpret_cast<const void*>(
                    2 * sizeof(float)));
            glEnableVertexAttribArray(1);
            glBindVertexArray(0);

            return glGetError() == GL_NO_ERROR;
        }

        void Draw() const
        {
            glBindVertexArray(VertexArray);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
        }

        ~FFullscreenQuad()
        {
            if (VertexBuffer != 0)
            {
                glDeleteBuffers(1, &VertexBuffer);
            }

            if (VertexArray != 0)
            {
                glDeleteVertexArrays(1, &VertexArray);
            }
        }

    private:
        GLuint VertexArray = 0;
        GLuint VertexBuffer = 0;
    };

    class FOffscreenTarget
    {
    public:
        bool Initialize()
        {
            glGenFramebuffers(1, &Framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer);
            glGenTextures(1, &ColorTexture);
            glBindTexture(GL_TEXTURE_2D, ColorTexture);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA8,
                kTestWidth,
                kTestHeight,
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                nullptr);
            glTexParameteri(
                GL_TEXTURE_2D,
                GL_TEXTURE_MIN_FILTER,
                GL_NEAREST);
            glTexParameteri(
                GL_TEXTURE_2D,
                GL_TEXTURE_MAG_FILTER,
                GL_NEAREST);
            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D,
                ColorTexture,
                0);

            const bool bComplete =
                glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                GL_FRAMEBUFFER_COMPLETE;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            return bComplete &&
                glGetError() == GL_NO_ERROR;
        }

        void Bind() const
        {
            glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer);
            glViewport(
                0,
                0,
                kTestWidth,
                kTestHeight);
        }

        std::vector<uint8_t> ReadPixels() const
        {
            std::vector<uint8_t> pixels(
                static_cast<size_t>(kTestWidth) *
                static_cast<size_t>(kTestHeight) *
                kRgbaChannelCount);
            glReadPixels(
                0,
                0,
                kTestWidth,
                kTestHeight,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                pixels.data());
            return pixels;
        }

        ~FOffscreenTarget()
        {
            if (ColorTexture != 0)
            {
                glDeleteTextures(1, &ColorTexture);
            }

            if (Framebuffer != 0)
            {
                glDeleteFramebuffers(1, &Framebuffer);
            }
        }

    private:
        GLuint Framebuffer = 0;
        GLuint ColorTexture = 0;
    };

    /**
     * fp16 离屏目标，HDR 输出分支专用
     *
     * `uOutputMode = 1` 写出的是**扩展 sRGB 编码值**，可以 > 1 也可以 < 0
     * （见 Core/FHdrPresenter.h 的约定），8bit UNORM 的 target 会把它钳掉，
     * 那样测出来的就不是真实管线的输出。
     */
    class FOffscreenTargetF16
    {
    public:
        bool Initialize()
        {
            glGenFramebuffers(1, &Framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer);
            glGenTextures(1, &ColorTexture);
            glBindTexture(GL_TEXTURE_2D, ColorTexture);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA16F,
                kTestWidth,
                kTestHeight,
                0,
                GL_RGBA,
                GL_HALF_FLOAT,
                nullptr);
            glTexParameteri(
                GL_TEXTURE_2D,
                GL_TEXTURE_MIN_FILTER,
                GL_NEAREST);
            glTexParameteri(
                GL_TEXTURE_2D,
                GL_TEXTURE_MAG_FILTER,
                GL_NEAREST);
            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D,
                ColorTexture,
                0);

            const bool bComplete =
                glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                GL_FRAMEBUFFER_COMPLETE;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            return bComplete &&
                glGetError() == GL_NO_ERROR;
        }

        void Bind() const
        {
            glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer);
            glViewport(
                0,
                0,
                kTestWidth,
                kTestHeight);
        }

        std::vector<float> ReadPixels() const
        {
            std::vector<float> pixels(
                static_cast<size_t>(kTestWidth) *
                static_cast<size_t>(kTestHeight) *
                kRgbaChannelCount);
            glReadPixels(
                0,
                0,
                kTestWidth,
                kTestHeight,
                GL_RGBA,
                GL_FLOAT,
                pixels.data());
            return pixels;
        }

        ~FOffscreenTargetF16()
        {
            if (ColorTexture != 0)
            {
                glDeleteTextures(1, &ColorTexture);
            }

            if (Framebuffer != 0)
            {
                glDeleteFramebuffers(1, &Framebuffer);
            }
        }

    private:
        GLuint Framebuffer = 0;
        GLuint ColorTexture = 0;
    };

    bool ValidateTextureGeometry(
        const FImageData& Image,
        const FTextureData& TextureData,
        const std::string& Label)
    {
        const FFormatDesc& desc =
            FImageFormatDesc::Get(Image.GetFormat());
        bool passed =
            TextureData.GetTextureCount() ==
            static_cast<uint32_t>(desc.PlaneCount);

        for (int32_t planeIndex = 0;
             planeIndex < desc.PlaneCount;
             ++planeIndex)
        {
            const FTexture* texture =
                TextureData.GetTexture(
                    static_cast<uint32_t>(planeIndex));
            const int32_t expectedWidth =
                FImageFormatDesc::GetPlaneWidth(
                    desc,
                    planeIndex,
                    Image.GetWidth());
            const int32_t expectedHeight =
                FImageFormatDesc::GetPlaneHeight(
                    desc,
                    planeIndex,
                    Image.GetHeight());

            passed =
                passed &&
                texture &&
                texture->IsValid() &&
                texture->GetWidth() == expectedWidth &&
                texture->GetHeight() == expectedHeight;
        }

        if (!passed)
        {
            std::printf(
                "  %s: 纹理数量或平面尺寸错误\n",
                Label.c_str());
        }

        return passed;
    }

    /**
     * 逐字段喂色彩管线 uniform
     *
     * **必须与 FImageViewer 的绘制回调保持同一份字段列表** —— 这里漏喂一个，
     * 比对用的就不再是屏幕上跑的那条管线，测试通过反而掩盖了真实的分叉。
     */
    void ApplyPipelineUniforms(
        FShader& Shader,
        const FColorTransform::FColorPipeline& Pipeline)
    {
        Shader.SetInt("uPipelineEnabled", Pipeline.bEnabled ? 1 : 0);
        Shader.SetInt("uTransfer", Pipeline.Transfer);
        Shader.SetInt("uAbsoluteTransfer", Pipeline.bAbsoluteTransfer);
        Shader.SetInt("uApplyPrimaries", Pipeline.bApplyPrimaries);
        Shader.SetMat3("uPrimariesMatrix", Pipeline.PrimariesMatrix);
        Shader.SetVec3(
            "uLumaCoef",
            Pipeline.LumaCoef[0],
            Pipeline.LumaCoef[1],
            Pipeline.LumaCoef[2]);
        Shader.SetFloat("uNormalizeNits", Pipeline.NormalizeNits);
        Shader.SetFloat("uHlgPeakNits", Pipeline.HlgPeakNits);
        Shader.SetFloat("uExposureScale", Pipeline.ExposureScale);
        Shader.SetInt("uToneMap", Pipeline.ToneMap);
        Shader.SetFloat("uToneMapWhite", Pipeline.ToneMapWhite);
        Shader.SetInt("uOutputMode", Pipeline.OutputMode);
        Shader.SetFloat("uMaxOutputScale", Pipeline.MaxOutputScale);
        Shader.SetInt("uShowOutOfRange", Pipeline.bShowOutOfRange);
    }

    void ConfigureShader(
        FShader& Shader,
        const FImageData& Image,
        const FTextureData& TextureData,
        const FDisplaySettings& Display,
        EChannelView ChannelView,
        const FColorTransform::FColorPipeline& Pipeline)
    {
        const FFormatDesc& desc =
            FImageFormatDesc::Get(Image.GetFormat());
        const int32_t textureCount =
            static_cast<int32_t>(
                TextureData.GetTextureCount());

        Shader.Use();
        Shader.SetMat4("uProjection", kIdentityMatrix4);
        Shader.SetMat4("uTransform", kIdentityMatrix4);

        if (textureCount >= 3)
        {
            const int32_t uTextureUnit =
                desc.bSwapChroma ? 2 : 1;
            const int32_t vTextureUnit =
                desc.bSwapChroma ? 1 : 2;
            Shader.SetInt("uTextureY", 0);
            Shader.SetInt("uTextureU", uTextureUnit);
            Shader.SetInt("uTextureV", vTextureUnit);
        }
        else if (textureCount == 2)
        {
            Shader.SetInt("uTextureY", 0);
            Shader.SetInt("uTextureUV", 1);
        }
        else
        {
            Shader.SetInt("uTexture", 0);
            Shader.SetInt("uTextureY", 0);
        }

        if (desc.ColorModel == EColorModel::YUV)
        {
            float matrix[9] = {};
            float offset[3] = {};
            FColorTransform::BuildYuvToRgb(
                Display.ColorSpace,
                Display.ColorRange,
                Image.GetSourceBitDepth(),
                matrix,
                offset);
            Shader.SetMat3("uYuvToRgb", matrix);
            Shader.SetVec3(
                "uYuvOffset",
                offset[0],
                offset[1],
                offset[2]);
            Shader.SetInt(
                "uSwapUV",
                desc.bSwapChroma ? 1 : 0);
        }

        if (desc.ColorModel == EColorModel::Bayer)
        {
            Shader.SetInt(
                "uBayerPattern",
                static_cast<int32_t>(
                    EBayerPattern::RGGB));
        }

        Shader.SetVec2(
            "uImageSize",
            static_cast<float>(Image.GetWidth()),
            static_cast<float>(Image.GetHeight()));
        Shader.SetFloat(
            "uSampleScale",
            Image.GetSampleScale());
        Shader.SetInt(
            "uChannelMode",
            static_cast<int32_t>(ChannelView));
        ApplyPipelineUniforms(Shader, Pipeline);
    }

    bool CompareGpuWithCpu(
        const std::string& Label,
        const std::vector<uint8_t>& GpuRgba,
        const std::vector<uint8_t>& CpuRgb)
    {
        const size_t pixelCount =
            static_cast<size_t>(kTestWidth) *
            static_cast<size_t>(kTestHeight);

        if (GpuRgba.size() !=
                pixelCount * kRgbaChannelCount ||
            CpuRgb.size() !=
                pixelCount * kRgbChannelCount)
        {
            std::printf(
                "  %s: 输出缓冲区尺寸错误\n",
                Label.c_str());
            return false;
        }

        int32_t mismatchCount = 0;
        uint64_t gpuEnergy = 0;
        uint64_t cpuEnergy = 0;

        for (size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            for (int32_t channel = 0;
                 channel < kRgbChannelCount;
                 ++channel)
            {
                const uint8_t got =
                    GpuRgba[
                        pixel * kRgbaChannelCount +
                        static_cast<size_t>(channel)];
                const uint8_t want =
                    CpuRgb[
                        pixel * kRgbChannelCount +
                        static_cast<size_t>(channel)];
                const int32_t difference =
                    std::abs(
                        static_cast<int32_t>(got) -
                        static_cast<int32_t>(want));
                gpuEnergy += got;
                cpuEnergy += want;

                if (difference > kRenderTolerance)
                {
                    if (mismatchCount <
                        kMaximumReportedPixelMismatches)
                    {
                        std::printf(
                            "  %s: pixel=%zu channel=%d got=%u want=%u diff=%d\n",
                            Label.c_str(),
                            pixel,
                            channel,
                            static_cast<unsigned int>(got),
                            static_cast<unsigned int>(want),
                            difference);
                    }

                    ++mismatchCount;
                }
            }
        }

        if (gpuEnergy == 0 || cpuEnergy == 0)
        {
            std::printf(
                "  %s: 画面意外为全黑\n",
                Label.c_str());
            return false;
        }

        if (mismatchCount > 0)
        {
            std::printf(
                "  %s: 共 %d 个通道超过容差 %d\n",
                Label.c_str(),
                mismatchCount,
                kRenderTolerance);
            return false;
        }

        return true;
    }

    bool RenderAndValidateCase(
        const FRenderCase& TestCase,
        int32_t CaseIndex,
        const FFullscreenQuad& Quad,
        const FOffscreenTarget& Target)
    {
        const FFormatDesc& sourceDesc =
            FImageFormatDesc::Get(TestCase.Format);
        const std::string label =
            std::string(sourceDesc.Name) +
            "/" +
            TestCase.Suffix;
        const std::vector<uint8_t> sourceBytes =
            BuildSourceBytes(TestCase);
        FScopedTempFile file(
            "YUVRaw_GL_" +
                std::string(sourceDesc.Name),
            CaseIndex,
            sourceBytes);

        if (!file.IsWritten())
        {
            std::printf(
                "%-34s **FAIL** 无法写入临时源图\n",
                label.c_str());
            return false;
        }

        FImageLoadParams params;
        params.Format = TestCase.Format;
        params.Width = kTestWidth;
        params.Height = kTestHeight;
        params.Stride = TestCase.InputStride;
        params.BitsPerPixel =
            TestCase.SourceBitDepth;
        params.BayerPattern =
            EBayerPattern::RGGB;
        params.ByteOrder =
            TestCase.ByteOrder;
        params.SampleAlignment =
            TestCase.SampleAlignment;
        params.ConstrainStorageLayout();

        FRawImageLoader loader;
        std::unique_ptr<FImageData> image =
            loader.LoadFromFile(
                file.String(),
                &params);
        bool passed =
            image &&
            image->IsValid();

        if (!passed)
        {
            std::printf(
                "%-34s **FAIL** Loader\n",
                label.c_str());
            return false;
        }

        const EImageFormat expectedLoadedFormat =
            IsPackedBayer(TestCase.Format)
                ? EImageFormat::Bayer16
                : TestCase.Format;
        const int32_t expectedLoadedStride =
            IsPackedBayer(TestCase.Format)
                ? FImageFormatDesc::ResolveBaseStride(
                    EImageFormat::Bayer16,
                    kTestWidth,
                    0)
                : FImageFormatDesc::ResolveBaseStride(
                    TestCase.Format,
                    kTestWidth,
                    TestCase.InputStride);

        passed =
            image->GetFormat() ==
                expectedLoadedFormat &&
            image->GetSourceBitDepth() ==
                TestCase.SourceBitDepth &&
            image->GetSampleShift() ==
                TestCase.ExpectedSampleShift &&
            image->GetStride() ==
                expectedLoadedStride;

        if (!passed)
        {
            std::printf(
                "  %s: 加载后布局错误 format=%d bits=%d shift=%d\n",
                label.c_str(),
                static_cast<int32_t>(image->GetFormat()),
                image->GetSourceBitDepth(),
                image->GetSampleShift());
        }

        FTextureData textureData;

        if (!textureData.CreateFromImageData(
                image.get()))
        {
            std::printf(
                "  %s: FTextureData 创建失败\n",
                label.c_str());
            passed = false;
        }
        else
        {
            passed =
                ValidateTextureGeometry(
                    *image,
                    textureData,
                    label) &&
                passed;

            if (!textureData.UpdateFromImageData(
                    image.get()))
            {
                std::printf(
                    "  %s: FTextureData 更新失败\n",
                    label.c_str());
                passed = false;
            }
        }

        FShader* shader =
            FShaderManager::Get().GetShaderForFormat(
                image->GetFormat());

        if (!shader || !shader->IsValid())
        {
            std::printf(
                "  %s: shader 无效\n",
                label.c_str());
            passed = false;
        }

        FDisplaySettings display;
        display.ColorRange =
            EColorRange::Full;
        std::vector<uint8_t> cpuRgb;

        if (!FImageSampler::ConvertToRgb8(
                *image,
                display,
                EBayerPattern::RGGB,
                cpuRgb))
        {
            std::printf(
                "  %s: CPU 真值生成失败\n",
                label.c_str());
            passed = false;
        }

        if (shader &&
            shader->IsValid() &&
            textureData.IsValid())
        {
            auto renderChannel =
                [&](EChannelView channelView,
                    std::vector<uint8_t>& outPixels)
            {
                while (glGetError() != GL_NO_ERROR)
                {
                }

                Target.Bind();
                glDisable(GL_BLEND);
                glDisable(GL_DITHER);
                glDisable(GL_FRAMEBUFFER_SRGB);
                glClearColor(
                    0.0f,
                    0.0f,
                    0.0f,
                    1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                ConfigureShader(
                    *shader,
                    *image,
                    textureData,
                    display,
                    channelView,
                    FColorTransform::BuildPipeline(display));
                textureData.SetMagFilterNearest(true);
                textureData.BindTextures(0);
                Quad.Draw();
                glFinish();
                outPixels = Target.ReadPixels();
                const GLenum renderError =
                    glGetError();
                glBindFramebuffer(
                    GL_FRAMEBUFFER,
                    0);

                if (renderError == GL_NO_ERROR)
                {
                    return true;
                }

                std::printf(
                    "  %s: OpenGL error=%u\n",
                    label.c_str(),
                    static_cast<unsigned int>(
                        renderError));

                return false;
            };

            std::vector<uint8_t> gpuRgba;
            passed =
                renderChannel(
                    EChannelView::Color,
                    gpuRgba) &&
                passed;
            passed =
                CompareGpuWithCpu(
                    label,
                    gpuRgba,
                    cpuRgb) &&
                passed;

            if (TestCase.Format == EImageFormat::RGB10A2)
            {
                // 颜色视图只比较 RGB；单独渲染 A 通道，锁住 2bit alpha
                // 的归一化以及 packed tuple 的最高两位位序。
                const size_t pixelCount =
                    static_cast<size_t>(kTestWidth) *
                    static_cast<size_t>(kTestHeight);
                std::vector<uint8_t> expectedAlphaRgb(
                    pixelCount * kRgbChannelCount,
                    kRgb10A2ExpectedAlpha8);
                std::vector<uint8_t> alphaRgba;
                const std::string alphaLabel =
                    label + "/alpha";

                passed =
                    renderChannel(
                        EChannelView::Channel4,
                        alphaRgba) &&
                    passed;
                passed =
                    CompareGpuWithCpu(
                        alphaLabel,
                        alphaRgba,
                        expectedAlphaRgb) &&
                    passed;
            }
        }

        std::printf(
            "%-34s %s\n",
            label.c_str(),
            passed ? "OK" : "**FAIL**");

        return passed;
    }

    // =========================================================================
    // 色彩管线分支的 GPU / CPU 比对
    //
    // 上面那些用例回答的是"这个格式的字节能不能正确变成 R'G'B'"，全部跑在
    // uPipelineEnabled = 0 上。下面这两组回答的是**R'G'B' 之后**那条链路：
    // EOTF、归一化、原色矩阵、曝光、色调映射、输出编码，GLSL 与
    // FColorTransform 是否给出同一个结果。
    //
    // 这两处的公式是手工对着抄的，抄错一位画面照样出图、只是颜色悄悄偏掉，
    // 所以必须有东西守着。
    // =========================================================================

    /// 超范围高亮的两个标记色，8bit。高亮直接返回显示编码值，不再过 OETF
    constexpr uint8_t kOverRangeMarker[kRgbChannelCount] = { 255, 0, 0 };
    constexpr uint8_t kUnderRangeMarker[kRgbChannelCount] = { 0, 102, 255 };

    /// SDR 输出下的一条管线用例。CPU 真值由 FImageSampler::ConvertToRgb8 给出
    struct FPipelineCase
    {
        std::string Name;
        EImageFormat Format = EImageFormat::RGB8;
        FDisplaySettings Display;

        /**
         * 自定义源字节，空则走 BuildSourceBytes
         *
         * BuildSourceBytes 给的是**恒定**值（YUV 是 Y=半量程、UV 固定），
         * 想触发某个分支就得自己铺数据。
         */
        std::vector<uint8_t> CustomSourceBytes;

        /**
         * 该用例必须在 CPU 真值里真的出现超范围标记
         *
         * 高亮用例最容易悄悄失效：源数据一旦全落在 [0,1] 之内，GPU 与 CPU 都不标记、
         * 比对照样通过，把 shader 里的高亮整段删掉也发现不了 —— 这是实际踩到的坑，
         * 所以让用例自己声明"我必须跑到那个分支"。
         */
        bool bRequireOutOfRangeMarkers = false;
    };

    /**
     * limited range 下会同时产生超白与超黑的 NV12 源（4x4）
     *
     * Y 铺满 0 到 255：低于 16 的经矩阵拉伸后是负值（画蓝），高于 235 的超过 1.0
     * （画红），中间那些正常。UV 保持中性，免得色度把亮度判定搅进来。
     */
    std::vector<uint8_t> BuildLimitedRangeExtremesNv12()
    {
        constexpr uint8_t kLumaValues[kTestWidth * kTestHeight] = {
              0,  16,  40,  64,
             90, 128, 160, 190,
            210, 235, 245, 255,
              0, 255,   8, 250,
        };
        constexpr uint8_t kNeutralChroma = 128;

        std::vector<uint8_t> bytes;
        bytes.reserve(sizeof(kLumaValues) + kTestWidth * kTestHeight / 2);

        for (uint8_t luma : kLumaValues)
        {
            bytes.push_back(luma);
        }

        // 4:2:0 的 UV 平面是 2x2 个交织对
        for (int32_t sample = 0;
             sample < (kTestWidth / 2) * (kTestHeight / 2) * 2;
             ++sample)
        {
            bytes.push_back(kNeutralChroma);
        }

        return bytes;
    }

    /// CPU 真值里是否真的出现了高亮标记
    void FindOutOfRangeMarkers(
        const std::vector<uint8_t>& CpuRgb,
        bool& bOutHasOver,
        bool& bOutHasUnder)
    {
        bOutHasOver = false;
        bOutHasUnder = false;

        for (size_t offset = 0;
             offset + kRgbChannelCount <= CpuRgb.size();
             offset += kRgbChannelCount)
        {
            bool bMatchesOver = true;
            bool bMatchesUnder = true;

            for (int32_t channel = 0;
                 channel < kRgbChannelCount;
                 ++channel)
            {
                const uint8_t value =
                    CpuRgb[offset + static_cast<size_t>(channel)];
                bMatchesOver =
                    bMatchesOver &&
                    value == kOverRangeMarker[channel];
                bMatchesUnder =
                    bMatchesUnder &&
                    value == kUnderRangeMarker[channel];
            }

            bOutHasOver = bOutHasOver || bMatchesOver;
            bOutHasUnder = bOutHasUnder || bMatchesUnder;
        }
    }

    std::vector<FPipelineCase> BuildSdrPipelineCases()
    {
        std::vector<FPipelineCase> cases;

        // 一律 Full range：Limited 的拉伸已经由前面的格式用例覆盖，
        // 这一组只盯 R'G'B' 之后的部分
        FDisplaySettings base;
        base.ColorRange = EColorRange::Full;

        auto add =
            [&cases](
                const char* Name,
                EImageFormat Format,
                const FDisplaySettings& Display)
            -> FPipelineCase&
        {
            FPipelineCase testCase;
            testCase.Name = Name;
            testCase.Format = Format;
            testCase.Display = Display;
            cases.push_back(testCase);

            return cases.back();
        };

        // PQ：uTransfer=2，绝对归一化 + BT.2020 -> BT.709 原色矩阵
        FDisplaySettings pq = base;
        pq.ColorSpace = EColorSpace::BT2020;
        pq.Primaries = EColorPrimaries::BT2020;
        pq.Transfer = EColorTransfer::PQ;
        add("pq_bt2020", EImageFormat::P010, pq);

        // HLG：uTransfer=3，覆盖 HlgSceneLinear（OETF 逆）+ OOTF
        //
        // 两件事都是实测得来的，别改回去：
        //
        // 1. **源必须有梯度**。BuildSourceBytes 给 YUV 的是恒定单色，
        //    整幅图一个值，暗部与高光的分支都测不到。
        // 2. **参考白要跟着 Lw 走**。HLG 的 OOTF 以 Lw 为峰值，除以默认的 203
        //    之后整幅图都远大于 1，Clip 会把它们全压成纯白 ——
        //    那样 HLG 里的任何错误都被饱和吃掉。
        //
        // 即便如此，OOTF 里 `0.42` 这个系数在 8bit 输出上只值约 0.5 LSB，
        // 这条路径**抓不到**它；那个系数由 HDR 组（fp16）的 hlg_peak_4000 守。
        // 这里守的是 HlgSceneLinear 的三个常数，它们的影响是百分数级的。
        FDisplaySettings hlg = pq;
        hlg.Transfer = EColorTransfer::HLG;
        hlg.ReferenceWhiteNits = hlg.HlgPeakNits;
        {
            FPipelineCase& testCase =
                add("hlg_bt2020", EImageFormat::RGB8, hlg);
            testCase.CustomSourceBytes = BuildPipelineSourceBytes();
        }

        // 母版峰值离开默认的 1000：OOTF 的系统 gamma 是
        // 1.2 + 0.42*log10(Lw/1000)，Lw=1000 时那一项恒为 0
        FDisplaySettings hlgPeak = hlg;
        hlgPeak.HlgPeakNits = 4000.0f;
        hlgPeak.ReferenceWhiteNits = hlgPeak.HlgPeakNits;
        {
            FPipelineCase& testCase =
                add("hlg_peak_4000", EImageFormat::RGB8, hlgPeak);
            testCase.CustomSourceBytes = BuildPipelineSourceBytes();
        }

        FDisplaySettings hlgDim = hlg;
        hlgDim.HlgPeakNits = 400.0f;
        hlgDim.ReferenceWhiteNits = hlgDim.HlgPeakNits;
        {
            FPipelineCase& testCase =
                add("hlg_peak_400", EImageFormat::RGB8, hlgDim);
            testCase.CustomSourceBytes = BuildPipelineSourceBytes();
        }

        // 一条真实配置的 HLG（默认参考白 + Clip，也就是大面积过曝的那种）：
        // 它验证的是"过曝时两边一起饱和"这个一致性，区分度低是已知的，
        // 不要把它当成 HLG 数学的守门用例
        FDisplaySettings hlgClip = pq;
        hlgClip.Transfer = EColorTransfer::HLG;
        add("hlg_clip_saturated", EImageFormat::P010, hlgClip);

        // BT.1886：uTransfer=1，纯 2.4 次幂
        FDisplaySettings bt1886 = base;
        bt1886.Transfer = EColorTransfer::BT1886;
        add("bt1886", EImageFormat::NV12, bt1886);

        // Linear：uTransfer=4，EOTF 恒等，只剩后半段
        FDisplaySettings linear = base;
        linear.Transfer = EColorTransfer::Linear;
        add("linear", EImageFormat::RGB8, linear);

        // 色调映射两个算子（Clip 已被上面几条覆盖）
        FDisplaySettings reinhard = pq;
        reinhard.ToneMap = EToneMapOperator::Reinhard;
        add("pq_reinhard", EImageFormat::P010, reinhard);

        FDisplaySettings aces = pq;
        aces.ToneMap = EToneMapOperator::ACES;
        add("pq_aces", EImageFormat::P010, aces);

        // 只开曝光：transfer 仍是 SDR，但管线被激活 ——
        // 走的是 SrgbDecode -> 乘曝光 -> SrgbEncode 这条往返
        FDisplaySettings exposure = base;
        exposure.ExposureStops = 1.5f;
        add("exposure_only", EImageFormat::RGB8, exposure);

        FDisplaySettings darker = base;
        darker.ExposureStops = -2.0f;
        add("exposure_negative", EImageFormat::RGBA8, darker);

        // 只改原色：uApplyPrimaries 单独生效，SDR 素材进线性域绕一圈
        FDisplaySettings primaries = base;
        primaries.Primaries = EColorPrimaries::BT2020;
        add("primaries_only", EImageFormat::RGB8, primaries);

        FDisplaySettings p3 = base;
        p3.Primaries = EColorPrimaries::DisplayP3;
        add("primaries_p3", EImageFormat::RGB8, p3);

        // 超范围高亮：线性路径的红/蓝两个分支。
        // 用 RGB8 源才能精确控制颜色 —— 蓝色那条要求"有负分量但不超上限"，
        // 得靠暗饱和色，从 YUV 侧凑这种像素既绕又不稳
        FDisplaySettings outOfRange = pq;
        outOfRange.bShowOutOfRange = true;
        {
            FPipelineCase& testCase =
                add("pq_out_of_range", EImageFormat::RGB8, outOfRange);
            testCase.CustomSourceBytes =
                BuildPipelineSourceBytes();
            testCase.bRequireOutOfRangeMarkers = true;
        }

        // 超范围高亮：**直通**路径的分支（bEnabled 仍为 false）。
        // limited range 的超白/超黑就落在这里，是最常见的一种超范围
        FDisplaySettings passthroughHighlight;
        passthroughHighlight.ColorRange = EColorRange::Limited;
        passthroughHighlight.bShowOutOfRange = true;
        {
            FPipelineCase& testCase =
                add("passthrough_out_of_range",
                    EImageFormat::NV12,
                    passthroughHighlight);
            testCase.CustomSourceBytes =
                BuildLimitedRangeExtremesNv12();
            testCase.bRequireOutOfRangeMarkers = true;
        }

        // 参考白：默认 203 与旧的 100 必须给出不同结果，且两边都要一致
        FDisplaySettings refWhite100 = pq;
        refWhite100.ReferenceWhiteNits = 100.0f;
        add("pq_refwhite_100", EImageFormat::P010, refWhite100);

        // 灰度 + PQ：单平面 luma dump 是真实存在的场景
        FDisplaySettings grayPq = base;
        grayPq.Transfer = EColorTransfer::PQ;
        add("gray_pq", EImageFormat::Grayscale16, grayPq);

        return cases;
    }

    bool RenderAndValidatePipelineCase(
        const FPipelineCase& TestCase,
        int32_t CaseIndex,
        const FFullscreenQuad& Quad,
        const FOffscreenTarget& Target)
    {
        const FFormatDesc& desc =
            FImageFormatDesc::Get(TestCase.Format);
        const std::string label =
            TestCase.Name +
            " (" +
            std::string(desc.Name) +
            ")";

        FRenderCase source;
        source.Format = TestCase.Format;
        source.SourceBitDepth = desc.BitDepth;
        source.ExpectedSampleShift = desc.SampleShift;

        const std::vector<uint8_t> sourceBytes =
            TestCase.CustomSourceBytes.empty()
                ? BuildSourceBytes(source)
                : TestCase.CustomSourceBytes;
        FScopedTempFile file(
            "YUVRaw_GLPipe_" + TestCase.Name,
            CaseIndex,
            sourceBytes);

        if (!file.IsWritten())
        {
            std::printf(
                "%-40s **FAIL** 无法写入临时源图\n",
                label.c_str());
            return false;
        }

        FImageLoadParams params;
        params.Format = TestCase.Format;
        params.Width = kTestWidth;
        params.Height = kTestHeight;
        params.BitsPerPixel = desc.BitDepth;
        params.BayerPattern = EBayerPattern::RGGB;
        params.ConstrainStorageLayout();

        FRawImageLoader loader;
        std::unique_ptr<FImageData> image =
            loader.LoadFromFile(file.String(), &params);

        if (!image || !image->IsValid())
        {
            std::printf(
                "%-40s **FAIL** Loader\n",
                label.c_str());
            return false;
        }

        FTextureData textureData;

        if (!textureData.CreateFromImageData(image.get()) ||
            !textureData.UpdateFromImageData(image.get()))
        {
            std::printf(
                "%-40s **FAIL** FTextureData\n",
                label.c_str());
            return false;
        }

        FShader* shader =
            FShaderManager::Get().GetShaderForFormat(
                image->GetFormat());

        if (!shader || !shader->IsValid())
        {
            std::printf(
                "%-40s **FAIL** shader 无效\n",
                label.c_str());
            return false;
        }

        std::vector<uint8_t> cpuRgb;

        if (!FImageSampler::ConvertToRgb8(
                *image,
                TestCase.Display,
                EBayerPattern::RGGB,
                cpuRgb))
        {
            std::printf(
                "%-40s **FAIL** CPU 真值生成失败\n",
                label.c_str());
            return false;
        }

        if (TestCase.bRequireOutOfRangeMarkers)
        {
            bool bHasOver = false;
            bool bHasUnder = false;
            FindOutOfRangeMarkers(cpuRgb, bHasOver, bHasUnder);

            if (!bHasOver || !bHasUnder)
            {
                std::printf(
                    "%-40s **FAIL** 用例没有真的跑到高亮分支 (超上限=%d 色域外=%d)\n",
                    label.c_str(),
                    bHasOver ? 1 : 0,
                    bHasUnder ? 1 : 0);
                return false;
            }
        }

        while (glGetError() != GL_NO_ERROR)
        {
        }

        Target.Bind();
        glDisable(GL_BLEND);
        glDisable(GL_DITHER);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ConfigureShader(
            *shader,
            *image,
            textureData,
            TestCase.Display,
            EChannelView::Color,
            FColorTransform::BuildPipeline(TestCase.Display));
        textureData.SetMagFilterNearest(true);
        textureData.BindTextures(0);
        Quad.Draw();
        glFinish();

        const std::vector<uint8_t> gpuRgba =
            Target.ReadPixels();
        const GLenum renderError = glGetError();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        bool passed = true;

        if (renderError != GL_NO_ERROR)
        {
            std::printf(
                "  %s: OpenGL error=%u\n",
                label.c_str(),
                static_cast<unsigned int>(renderError));
            passed = false;
        }

        passed =
            CompareGpuWithCpu(label, gpuRgba, cpuRgb) &&
            passed;

        std::printf(
            "%-40s %s\n",
            label.c_str(),
            passed ? "OK" : "**FAIL**");

        return passed;
    }

    /// HDR（scRGB fp16）输出下的一条管线用例
    struct FHdrPipelineCase
    {
        std::string Name;
        FDisplaySettings Display;
        FColorTransform::FDisplayOutput Output;

        /// 同 FPipelineCase：高亮用例必须真的跑到那个分支，否则就是在空转
        bool bRequireOutOfRangeMarkers = false;
    };

    std::vector<FHdrPipelineCase> BuildHdrPipelineCases()
    {
        std::vector<FHdrPipelineCase> cases;

        FColorTransform::FDisplayOutput output;
        output.bHdr = true;
        output.SdrWhiteNits = kHdrSdrWhiteNits;
        output.MaxNits = kHdrPeakNits;

        FColorTransform::FDisplayOutput lowHeadroom;
        lowHeadroom.bHdr = true;
        lowHeadroom.SdrWhiteNits = kLowHeadroomSdrWhiteNits;
        lowHeadroom.MaxNits = kLowHeadroomPeakNits;

        auto add =
            [&cases](
                const char* Name,
                const FDisplaySettings& Display,
                const FColorTransform::FDisplayOutput& Output)
            -> FHdrPipelineCase&
        {
            FHdrPipelineCase testCase;
            testCase.Name = Name;
            testCase.Display = Display;
            testCase.Output = Output;
            cases.push_back(testCase);

            return cases.back();
        };

        FDisplaySettings base;
        base.ColorRange = EColorRange::Full;

        FDisplaySettings pq = base;
        pq.Primaries = EColorPrimaries::BT2020;
        pq.Transfer = EColorTransfer::PQ;
        add("pq_bt2020", pq, output);

        // 低余量：MaxOutputScale ≈ 1.86，几乎每个亮像素都会过保色相限幅
        add("pq_low_headroom", pq, lowHeadroom);

        FDisplaySettings hlg = base;
        hlg.Primaries = EColorPrimaries::BT2020;
        hlg.Transfer = EColorTransfer::HLG;
        add("hlg_bt2020", hlg, output);

        // Lw != 1000，否则 OOTF 的 0.42 系数乘的是 0，等于没测
        FDisplaySettings hlgPeak = hlg;
        hlgPeak.HlgPeakNits = 4000.0f;
        add("hlg_peak_4000", hlgPeak, output);

        // SDR 素材在 HDR 输出下必须**逐位直通**：FBO 的约定就是
        // "扩展 sRGB 编码、1.0 = SDR 白"，源编码值原样写进去含义就是对的
        add("sdr_passthrough", base, output);

        // 直通路径的高亮**不在这一组**：归一化纹理采出来永远落在 [0,1]，
        // RGB8 源不可能越界，放在这里只会得到一条永远空转的用例。
        // 那个分支由 SDR 组的 passthrough_out_of_range 用 limited range 的
        // YUV 极值覆盖，而且直通分支根本不看 uOutputMode，无需按输出目标各测一遍。

        // 线性路径的高亮：超出显示器峰值画红，落在色域外画蓝
        FDisplaySettings highlight = pq;
        highlight.bShowOutOfRange = true;
        add("pq_out_of_range", highlight, lowHeadroom)
            .bRequireOutOfRangeMarkers = true;

        FDisplaySettings exposed = pq;
        exposed.ExposureStops = -1.0f;
        add("pq_exposure", exposed, output);

        return cases;
    }

    bool CompareGpuWithCpuFloat(
        const std::string& Label,
        const std::vector<float>& GpuRgba,
        const std::vector<float>& ExpectedRgb)
    {
        const size_t pixelCount =
            static_cast<size_t>(kTestWidth) *
            static_cast<size_t>(kTestHeight);

        if (GpuRgba.size() != pixelCount * kRgbaChannelCount ||
            ExpectedRgb.size() != pixelCount * kRgbChannelCount)
        {
            std::printf(
                "  %s: 输出缓冲区尺寸错误\n",
                Label.c_str());
            return false;
        }

        int32_t mismatchCount = 0;
        float worstDifference = 0.0f;

        for (size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            for (int32_t channel = 0;
                 channel < kRgbChannelCount;
                 ++channel)
            {
                const float got =
                    GpuRgba[
                        pixel * kRgbaChannelCount +
                        static_cast<size_t>(channel)];
                const float want =
                    ExpectedRgb[
                        pixel * kRgbChannelCount +
                        static_cast<size_t>(channel)];
                const float difference =
                    std::fabs(got - want);
                worstDifference =
                    std::max(worstDifference, difference);

                if (difference > kHdrTolerance)
                {
                    if (mismatchCount <
                        kMaximumReportedPixelMismatches)
                    {
                        std::printf(
                            "  %s: pixel=%zu channel=%d got=%.5f want=%.5f diff=%.5f\n",
                            Label.c_str(),
                            pixel,
                            channel,
                            got,
                            want,
                            difference);
                    }

                    ++mismatchCount;
                }
            }
        }

        if (mismatchCount > 0)
        {
            std::printf(
                "  %s: 共 %d 个通道超过容差 %.4f\n",
                Label.c_str(),
                mismatchCount,
                kHdrTolerance);
            return false;
        }

        // 把最大误差打出来：以后想收紧容差时不必重新加日志
        std::printf(
            "%-40s OK  (最大误差 %.5f)\n",
            Label.c_str(),
            worstDifference);

        return true;
    }

    bool RenderAndValidateHdrCase(
        const FHdrPipelineCase& TestCase,
        int32_t CaseIndex,
        const FFullscreenQuad& Quad,
        const FOffscreenTargetF16& Target)
    {
        const std::string label = "hdr/" + TestCase.Name;

        // 源刻意用 RGB8：非线性 R'G'B' 就等于"字节 / 255"，CPU 参考因此不必
        // 重走一遍取样与色度上采样 —— 那一段已经由前面的格式用例覆盖，
        // 这一组要单独盯住的是管线数学本身。
        const std::vector<uint8_t> sourceBytes =
            BuildPipelineSourceBytes();

        FScopedTempFile file(
            "YUVRaw_GLHdr_" + TestCase.Name,
            CaseIndex,
            sourceBytes);

        if (!file.IsWritten())
        {
            std::printf(
                "%-40s **FAIL** 无法写入临时源图\n",
                label.c_str());
            return false;
        }

        FImageLoadParams params;
        params.Format = EImageFormat::RGB8;
        params.Width = kTestWidth;
        params.Height = kTestHeight;
        params.ConstrainStorageLayout();

        FRawImageLoader loader;
        std::unique_ptr<FImageData> image =
            loader.LoadFromFile(file.String(), &params);

        if (!image || !image->IsValid())
        {
            std::printf(
                "%-40s **FAIL** Loader\n",
                label.c_str());
            return false;
        }

        FTextureData textureData;

        if (!textureData.CreateFromImageData(image.get()) ||
            !textureData.UpdateFromImageData(image.get()))
        {
            std::printf(
                "%-40s **FAIL** FTextureData\n",
                label.c_str());
            return false;
        }

        FShader* shader =
            FShaderManager::Get().GetShaderForFormat(
                EImageFormat::RGB8);

        if (!shader || !shader->IsValid())
        {
            std::printf(
                "%-40s **FAIL** shader 无效\n",
                label.c_str());
            return false;
        }

        const FColorTransform::FColorPipeline pipeline =
            FColorTransform::BuildPipeline(
                TestCase.Display,
                TestCase.Output);

        std::vector<float> expected(
            static_cast<size_t>(kTestWidth) *
            static_cast<size_t>(kTestHeight) *
            kRgbChannelCount);

        for (size_t pixel = 0;
             pixel < static_cast<size_t>(kTestWidth) *
                     static_cast<size_t>(kTestHeight);
             ++pixel)
        {
            float rgb[3] = {
                static_cast<float>(kPipelineSourcePixels[pixel][0]) / 255.0f,
                static_cast<float>(kPipelineSourcePixels[pixel][1]) / 255.0f,
                static_cast<float>(kPipelineSourcePixels[pixel][2]) / 255.0f,
            };

            FColorTransform::ApplyPipeline(pipeline, rgb);

            for (int32_t channel = 0;
                 channel < kRgbChannelCount;
                 ++channel)
            {
                expected[
                    pixel * kRgbChannelCount +
                    static_cast<size_t>(channel)] = rgb[channel];
            }
        }

        if (TestCase.bRequireOutOfRangeMarkers)
        {
            // 高亮色直接就是显示编码值：红 (1, 0, 0)、蓝 (0, 0.4, 1)
            bool bHasOver = false;
            bool bHasUnder = false;

            for (size_t offset = 0;
                 offset + kRgbChannelCount <= expected.size();
                 offset += kRgbChannelCount)
            {
                const float r = expected[offset];
                const float g = expected[offset + 1];
                const float b = expected[offset + 2];

                bHasOver =
                    bHasOver ||
                    (r == 1.0f && g == 0.0f && b == 0.0f);
                bHasUnder =
                    bHasUnder ||
                    (r == 0.0f && g == 0.4f && b == 1.0f);
            }

            if (!bHasOver || !bHasUnder)
            {
                std::printf(
                    "%-40s **FAIL** 用例没有真的跑到高亮分支 (超上限=%d 色域外=%d)\n",
                    label.c_str(),
                    bHasOver ? 1 : 0,
                    bHasUnder ? 1 : 0);
                return false;
            }
        }

        while (glGetError() != GL_NO_ERROR)
        {
        }

        Target.Bind();
        glDisable(GL_BLEND);
        glDisable(GL_DITHER);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ConfigureShader(
            *shader,
            *image,
            textureData,
            TestCase.Display,
            EChannelView::Color,
            pipeline);
        textureData.SetMagFilterNearest(true);
        textureData.BindTextures(0);
        Quad.Draw();
        glFinish();

        const std::vector<float> gpuRgba =
            Target.ReadPixels();
        const GLenum renderError = glGetError();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        if (renderError != GL_NO_ERROR)
        {
            std::printf(
                "%-40s **FAIL** OpenGL error=%u\n",
                label.c_str(),
                static_cast<unsigned int>(renderError));
            return false;
        }

        if (!CompareGpuWithCpuFloat(label, gpuRgba, expected))
        {
            std::printf(
                "%-40s **FAIL**\n",
                label.c_str());
            return false;
        }

        return true;
    }

    bool ValidateAllShaderPrograms()
    {
        bool passed = true;
        int32_t compiled = 0;

        for (const FFormatDesc& desc :
             FImageFormatDesc::GetAll())
        {
            if (desc.Format ==
                EImageFormat::Unknown)
            {
                continue;
            }

            FShader* shader =
                FShaderManager::Get().
                GetShaderForFormat(desc.Format);

            if (!shader || !shader->IsValid())
            {
                std::printf(
                    "Shader %-24s **FAIL**\n",
                    desc.Name);
                passed = false;
            }
            else
            {
                ++compiled;
            }
        }

        std::printf(
            "shader 编译：%d 个非 Unknown 格式 %s\n",
            compiled,
            passed ? "OK" : "**FAIL**");

        return passed;
    }
}

int main()
{
    const std::filesystem::path logDirectory =
        std::filesystem::temp_directory_path() /
        "YUVRawOpenGLValidationLogs";
    std::error_code ec;
    std::filesystem::create_directories(
        logDirectory,
        ec);
    FLogger::Initialize(
        logDirectory,
        kTestLogLimitBytes,
        kTestLogFileCount);

    std::printf(
        "=== 全格式 OpenGL 隐藏上下文验证 ===\n");

    {
        FHiddenGlContext context;

        if (!context.Initialize())
        {
            std::printf(
                "无法创建隐藏 OpenGL 3.3 上下文\n");
            FLogger::Shutdown();
            return 1;
        }

        const char* renderer =
            reinterpret_cast<const char*>(
                glGetString(GL_RENDERER));
        const char* version =
            reinterpret_cast<const char*>(
                glGetString(GL_VERSION));
        std::printf(
            "Renderer: %s\nVersion: %s\n\n",
            renderer ? renderer : "(unknown)",
            version ? version : "(unknown)");

        if (!ValidateSharedContextTextureHandoff(context.GetWindow()))
        {
            ++gFailures;
        }

        if (!ValidateAllShaderPrograms())
        {
            ++gFailures;
        }

        FFullscreenQuad quad;
        FOffscreenTarget target;
        FOffscreenTargetF16 hdrTarget;

        if (!quad.Initialize() ||
            !target.Initialize() ||
            !hdrTarget.Initialize())
        {
            std::printf(
                "离屏渲染资源初始化失败\n");
            ++gFailures;
        }
        else
        {
            const std::vector<FRenderCase> cases =
                BuildCases();
            int32_t passedCases = 0;

            std::printf(
                "\n=== Loader -> Texture -> Shader -> FBO ===\n");

            for (size_t index = 0;
                 index < cases.size();
                 ++index)
            {
                if (RenderAndValidateCase(
                        cases[index],
                        static_cast<int32_t>(index),
                        quad,
                        target))
                {
                    ++passedCases;
                }
                else
                {
                    ++gFailures;
                }
            }

            std::printf(
                "\n渲染用例：%d / %zu 通过\n",
                passedCases,
                cases.size());

            const std::vector<FPipelineCase> pipelineCases =
                BuildSdrPipelineCases();
            int32_t passedPipelineCases = 0;

            std::printf(
                "\n=== 色彩管线 GPU/CPU 比对（SDR 输出）===\n");

            for (size_t index = 0;
                 index < pipelineCases.size();
                 ++index)
            {
                if (RenderAndValidatePipelineCase(
                        pipelineCases[index],
                        static_cast<int32_t>(index),
                        quad,
                        target))
                {
                    ++passedPipelineCases;
                }
                else
                {
                    ++gFailures;
                }
            }

            std::printf(
                "\n色彩管线用例：%d / %zu 通过\n",
                passedPipelineCases,
                pipelineCases.size());

            const std::vector<FHdrPipelineCase> hdrCases =
                BuildHdrPipelineCases();
            int32_t passedHdrCases = 0;

            std::printf(
                "\n=== 色彩管线 GPU/CPU 比对（HDR scRGB fp16 输出）===\n");

            for (size_t index = 0;
                 index < hdrCases.size();
                 ++index)
            {
                if (RenderAndValidateHdrCase(
                        hdrCases[index],
                        static_cast<int32_t>(index),
                        quad,
                        hdrTarget))
                {
                    ++passedHdrCases;
                }
                else
                {
                    ++gFailures;
                }
            }

            std::printf(
                "\nHDR 管线用例：%d / %zu 通过\n",
                passedHdrCases,
                hdrCases.size());
        }
    }

    FLogger::Shutdown();
    std::filesystem::remove_all(
        logDirectory,
        ec);

    std::printf(
        "\n%s\n",
        gFailures == 0
            ? "全格式 OpenGL 验证全部通过"
            : "全格式 OpenGL 验证存在失败");

    return gFailures == 0 ? 0 : 1;
}
