// Android Bayer RAW 加载器离线自检：
// - RAW10 / RAW12 / RAW14 按官方位布局做真值解包
// - packed 行尾 padding 不参与像素解包
// - Android 要求的宽高对齐在读缓冲前被拒绝
// - 所有可配置 16bit 格式的大端输入、行尾 padding 与有效位对齐被正确处理

#include "Core/FLogger.h"
#include "Image/FImageData.h"
#include "Image/FImageFormatDesc.h"
#include "Image/FImageLimits.h"
#include "Image/FImageLoadParams.h"
#include "Image/FRawImageLoader.h"
#include "PublicFixtures.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace
{
    constexpr int32_t kPackedWidth = 4;
    constexpr int32_t kPackedHeight = 2;
    constexpr int32_t kBitsPerByte = 8;
    constexpr int32_t kRawWordBytes = 2;
    constexpr int32_t kStorageContainerBits = 16;
    constexpr float kScaleTolerance = 0.0001f;
    constexpr uintmax_t kTestLogLimitBytes = 1024u * 1024u;
    constexpr size_t kTestLogFileCount = 1u;

    int gFailures = 0;

    void Check(const std::string& Label, bool bCondition)
    {
        std::printf("%-70s %s\n", Label.c_str(), bCondition ? "OK" : "**FAIL**");

        if (!bCondition)
        {
            ++gFailures;
        }
    }

    class FScopedTempFile
    {
    public:
        FScopedTempFile(const char* FileName, const std::vector<uint8_t>& Bytes)
            : Path(
                std::filesystem::temp_directory_path() /
                std::filesystem::u8path(FileName))
        {
            std::ofstream file(Path, std::ios::binary | std::ios::trunc);

            if (file.is_open())
            {
                file.write(
                    reinterpret_cast<const char*>(Bytes.data()),
                    static_cast<std::streamsize>(Bytes.size()));
                bWritten = file.good();
            }
        }

        ~FScopedTempFile()
        {
            std::error_code ec;
            std::filesystem::remove(Path, ec);
        }

        bool IsWritten() const { return bWritten; }
        std::string String() const { return Path.u8string(); }

    private:
        std::filesystem::path Path;
        bool bWritten = false;
    };

    uint16_t ReadPixel(const FImageData& Image, int32_t X, int32_t Y)
    {
        const size_t offset =
            static_cast<size_t>(Y) * Image.GetStride() +
            static_cast<size_t>(X) * kRawWordBytes;
        uint16_t value = 0;
        std::memcpy(&value, Image.GetPixelData() + offset, sizeof(value));

        return value;
    }

    void CheckPixels(
        const std::string& Label,
        const FImageData& Image,
        const std::vector<uint16_t>& Expected)
    {
        bool bMatches = Expected.size() ==
            static_cast<size_t>(Image.GetWidth()) * Image.GetHeight();

        for (int32_t y = 0; bMatches && y < Image.GetHeight(); ++y)
        {
            for (int32_t x = 0; x < Image.GetWidth(); ++x)
            {
                const size_t index =
                    static_cast<size_t>(y) * Image.GetWidth() + x;

                if (ReadPixel(Image, x, y) != Expected[index])
                {
                    bMatches = false;
                    break;
                }
            }
        }

        Check(Label, bMatches);
    }

    void CheckBytes(
        const std::string& Label,
        const FImageData& Image,
        const std::vector<uint8_t>& Expected)
    {
        const bool bMatches =
            Image.GetPixelDataSize() == Expected.size() &&
            std::memcmp(
                Image.GetPixelData(),
                Expected.data(),
                Expected.size()) == 0;
        Check(Label, bMatches);
    }

    void TestPackedFormat(
        const char* Label,
        const char* TempFileName,
        EImageFormat Format,
        int32_t BitDepth,
        int32_t InputStride,
        const std::vector<uint8_t>& PackedBytes,
        const std::vector<uint16_t>& ExpectedPixels)
    {
        FScopedTempFile file(TempFileName, PackedBytes);
        Check(std::string(Label) + " 测试文件写入", file.IsWritten());

        FImageLoadParams params;
        params.Format = Format;
        params.Width = kPackedWidth;
        params.Height = kPackedHeight;
        params.Stride = InputStride;
        params.BitsPerPixel = BitDepth;

        FRawImageLoader loader;
        std::unique_ptr<FImageData> image =
            loader.LoadFromFile(file.String(), &params);

        Check(std::string(Label) + " 加载成功", image && image->IsValid());

        if (!image)
        {
            return;
        }

        Check(std::string(Label) + " 解包为 Bayer16",
              image->GetFormat() == EImageFormat::Bayer16);
        Check(std::string(Label) + " 保留有效位深",
              image->GetSourceBitDepth() == BitDepth);
        Check(std::string(Label) + " 输出为紧凑 16bit stride",
              image->GetStride() == kPackedWidth * kRawWordBytes);
        Check(std::string(Label) + " 低位对齐",
              image->GetSampleShift() == 0);
        CheckPixels(std::string(Label) + " 解包真值", *image, ExpectedPixels);
    }

    void TestUnicodeFilePath()
    {
        constexpr int32_t kWidth = 2;
        constexpr int32_t kHeight = 2;
        const std::vector<uint8_t> pixels = { 16, 64, 128, 255 };
        FScopedTempFile file(
            u8"YUVRaw_中文路径_2x2_gray.raw",
            pixels);
        Check(u8"中文路径测试文件写入", file.IsWritten());

        FImageLoadParams params;
        params.Format = EImageFormat::Grayscale8;
        params.Width = kWidth;
        params.Height = kHeight;

        FRawImageLoader loader;
        const std::unique_ptr<FImageData> image =
            loader.LoadFromFile(file.String(), &params);

        Check(u8"UTF-8 中文路径 RAW 读取", image && image->IsValid());

        if (image)
        {
            CheckBytes(u8"中文路径 RAW 像素保持不变", *image, pixels);
        }
    }

    void TestPackedTruthTables()
    {
        // RAW10：第五字节低到高依次是 P0/P1/P2/P3 的 2 个低位。
        const std::vector<uint8_t> raw10 = {
            0x00, 0x00, 0x00, 0x00, 0xE4, 0xA5, 0xA6, 0xA7,
            0xFF, 0xAA, 0x55, 0x40, 0x1B, 0xB5, 0xB6, 0xB7,
        };
        const std::vector<uint16_t> raw10Expected = {
            0x0000, 0x0001, 0x0002, 0x0003,
            0x03FF, 0x02AA, 0x0155, 0x0100,
        };
        TestPackedFormat(
            "Android RAW10",
            "YUVRaw_TestRaw10.raw10",
            EImageFormat::BayerPacked10,
            10,
            8,
            raw10,
            raw10Expected);

        // RAW12：每组第三字节的低/高半字节分别属于 P0/P1。
        const std::vector<uint8_t> raw12 = {
            0x00, 0x00, 0x10, 0x00, 0x00, 0x32, 0xA6, 0xA7,
            0xFF, 0xAB, 0xCF, 0x12, 0x45, 0x63, 0xB6, 0xB7,
        };
        const std::vector<uint16_t> raw12Expected = {
            0x0000, 0x0001, 0x0002, 0x0003,
            0x0FFF, 0x0ABC, 0x0123, 0x0456,
        };
        TestPackedFormat(
            "Android RAW12",
            "YUVRaw_TestRaw12.raw12",
            EImageFormat::BayerPacked12,
            12,
            8,
            raw12,
            raw12Expected);

        // RAW14：后三字节跨像素串接低 6 位，第二行覆盖所有跨字节边界。
        const std::vector<uint8_t> raw14 = {
            0x00, 0x00, 0x00, 0x00, 0x40, 0x20, 0x0C, 0xA7, 0xA8, 0xA9,
            0xFF, 0xAA, 0x55, 0x40, 0xBF, 0x5A, 0x01, 0xB7, 0xB8, 0xB9,
        };
        const std::vector<uint16_t> raw14Expected = {
            0x0000, 0x0001, 0x0002, 0x0003,
            0x3FFF, 0x2AAA, 0x1555, 0x1000,
        };
        TestPackedFormat(
            "Android RAW14",
            "YUVRaw_TestRaw14.raw14",
            EImageFormat::BayerPacked14,
            14,
            10,
            raw14,
            raw14Expected);
    }

    void TestPackedGeometryValidation()
    {
        const std::vector<uint8_t> dummyBytes(128u, 0u);
        FScopedTempFile file("YUVRaw_TestRawInvalid.raw", dummyBytes);
        Check("packed 几何校验测试文件写入", file.IsWritten());

        const EImageFormat formats[] = {
            EImageFormat::BayerPacked10,
            EImageFormat::BayerPacked12,
            EImageFormat::BayerPacked14,
        };

        FRawImageLoader loader;

        for (EImageFormat format : formats)
        {
            const std::string name =
                FImageFormatDesc::Get(format).Name;

            FImageLoadParams params;
            params.Format = format;
            params.Width = 6;
            params.Height = kPackedHeight;
            params.Stride = 16;

            Check(
                name + " 拒绝非 4 倍宽度",
                !loader.LoadFromFile(file.String(), &params));

            params.Width = kPackedWidth;
            params.Height = 3;

            Check(
                name + " 拒绝奇数高度",
                !loader.LoadFromFile(file.String(), &params));
        }
    }

    void TestUnpackedDefaultBitDepths()
    {
        const std::vector<uint8_t> pixels = {
            0x00, 0x00, 0x01, 0x00,
            0x02, 0x00, 0x03, 0x00,
        };
        FScopedTempFile file("YUVRaw_TestRawUnpacked.raw", pixels);
        Check("unpacked Bayer 测试文件写入", file.IsWritten());

        struct FCase
        {
            EImageFormat Format;
            int32_t ExpectedBitDepth;
        };

        const FCase cases[] = {
            { EImageFormat::Bayer10, 10 },
            { EImageFormat::Bayer12, 12 },
            { EImageFormat::Bayer14, 14 },
        };

        FRawImageLoader loader;

        for (const FCase& testCase : cases)
        {
            FImageLoadParams params;
            params.Format = testCase.Format;
            params.Width = 2;
            params.Height = 2;
            // 故意保留默认的 8，确认固定格式使用描述表位深而非残留 UI 值。
            params.SampleAlignment =
                ESampleAlignment::MostSignificantBits;

            std::unique_ptr<FImageData> image =
                loader.LoadFromFile(file.String(), &params);
            const std::string name =
                FImageFormatDesc::Get(testCase.Format).Name;

            Check(name + " 加载成功", image && image->IsValid());

            if (image)
            {
                Check(name + " 使用格式默认有效位深",
                      image->GetSourceBitDepth() == testCase.ExpectedBitDepth);
                Check(name + " 忽略不支持的高位对齐请求",
                      image->GetSampleShift() == 0);
            }
        }
    }

    void TestConfigurable16BitByteOrderFormats()
    {
        constexpr int32_t kWidth = 2;
        constexpr int32_t kHeight = 2;
        constexpr int32_t kPaddingBytes = 2;
        constexpr uint16_t kFirstSampleValue = 0x0100;
        constexpr uint8_t kFirstPaddingValue = 0xE0;

        const EImageFormat formats[] = {
            EImageFormat::RGB16,
            EImageFormat::RGBA16,
            EImageFormat::Grayscale16,
            EImageFormat::Bayer16,
            EImageFormat::Bayer10,
            EImageFormat::Bayer12,
            EImageFormat::Bayer14,
        };

        FRawImageLoader loader;

        for (EImageFormat format : formats)
        {
            const FFormatDesc& desc = FImageFormatDesc::Get(format);
            const int32_t samplesPerRow =
                kWidth * desc.Planes[0].ChannelCount;
            const int32_t activeRowBytes =
                samplesPerRow * kRawWordBytes;
            const int32_t stride =
                activeRowBytes + kPaddingBytes;
            std::vector<uint8_t> bigEndianBytes(
                static_cast<size_t>(stride) * kHeight,
                0u);
            std::vector<uint8_t> expectedLittleEndianBytes =
                bigEndianBytes;
            uint16_t sampleValue = kFirstSampleValue;

            for (int32_t row = 0; row < kHeight; ++row)
            {
                const size_t rowOffset =
                    static_cast<size_t>(row) * stride;

                for (int32_t sample = 0;
                     sample < samplesPerRow;
                     ++sample)
                {
                    const size_t offset =
                        rowOffset +
                        static_cast<size_t>(sample) *
                            kRawWordBytes;
                    bigEndianBytes[offset] =
                        static_cast<uint8_t>(
                            sampleValue >> kBitsPerByte);
                    bigEndianBytes[offset + 1] =
                        static_cast<uint8_t>(sampleValue);
                    expectedLittleEndianBytes[offset] =
                        static_cast<uint8_t>(sampleValue);
                    expectedLittleEndianBytes[offset + 1] =
                        static_cast<uint8_t>(
                            sampleValue >> kBitsPerByte);
                    ++sampleValue;
                }

                for (int32_t padding = 0;
                     padding < kPaddingBytes;
                     ++padding)
                {
                    const uint8_t paddingValue =
                        static_cast<uint8_t>(
                            kFirstPaddingValue +
                            row * kPaddingBytes +
                            padding);
                    const size_t offset =
                        rowOffset +
                        static_cast<size_t>(activeRowBytes + padding);
                    bigEndianBytes[offset] = paddingValue;
                    expectedLittleEndianBytes[offset] = paddingValue;
                }
            }

            const std::string name = desc.Name;
            const std::string fileName =
                "YUVRaw_Test" + name + "BigEndian.raw";
            FScopedTempFile file(fileName.c_str(), bigEndianBytes);
            Check(name + " 大端测试文件写入", file.IsWritten());

            FImageLoadParams params;
            params.Format = format;
            params.Width = kWidth;
            params.Height = kHeight;
            params.Stride = stride;
            params.BitsPerPixel = desc.BitDepth;
            params.ByteOrder = EByteOrder::BigEndian;

            std::unique_ptr<FImageData> image =
                loader.LoadFromFile(file.String(), &params);
            Check(name + " 大端加载成功", image && image->IsValid());

            if (image)
            {
                CheckBytes(
                    name + " 全部采样转为内部小端且 padding 不变",
                    *image,
                    expectedLittleEndianBytes);
            }
        }
    }

    void TestFixedP0xxStorageLayout()
    {
        constexpr int32_t kWidth = 2;
        constexpr int32_t kHeight = 2;
        constexpr int32_t kEffectiveBitDepth = 10;
        constexpr int32_t kExpectedShift =
            kStorageContainerBits - kEffectiveBitDepth;

        const EImageFormat formats[] = {
            EImageFormat::P010,
            EImageFormat::P210,
        };

        FRawImageLoader loader;

        for (EImageFormat format : formats)
        {
            const FFormatDesc& desc = FImageFormatDesc::Get(format);
            const size_t frameSize =
                FImageFormatDesc::CalculateFrameSize(
                    format,
                    kWidth,
                    kHeight,
                    0);
            std::vector<uint8_t> littleEndianBytes(frameSize, 0u);

            // 非对称字节值可检测“请求大端”是否被错误执行。
            littleEndianBytes[0] = 0x40;
            littleEndianBytes[1] = 0x55;

            const std::string name = desc.Name;
            const std::string fileName =
                "YUVRaw_Test" + name + "Fixed.raw";
            FScopedTempFile file(fileName.c_str(), littleEndianBytes);
            Check(name + " 固定布局测试文件写入", file.IsWritten());

            FImageLoadParams params;
            params.Format = format;
            params.Width = kWidth;
            params.Height = kHeight;
            params.ByteOrder = EByteOrder::BigEndian;
            params.SampleAlignment =
                ESampleAlignment::LeastSignificantBits;

            std::unique_ptr<FImageData> image =
                loader.LoadFromFile(file.String(), &params);
            Check(name + " 固定布局加载成功", image && image->IsValid());

            if (image)
            {
                CheckBytes(
                    name + " 忽略大端请求并保留规范小端数据",
                    *image,
                    littleEndianBytes);
                Check(
                    name + " 忽略低位请求并固定为高位对齐",
                    image->GetSampleShift() == kExpectedShift);
            }
        }
    }

    void TestConfigurableYuv420Sp16StorageLayout()
    {
        constexpr int32_t kWidth = 2;
        constexpr int32_t kHeight = 2;
        constexpr int32_t kStride = 6;
        constexpr int32_t kEffectiveBitDepth = 10;
        constexpr int32_t kMinimumYuv420Sp16BitDepth = 8;
        constexpr int32_t kHighAlignmentShift =
            kStorageContainerBits - kEffectiveBitDepth;

        struct FAlignmentCase
        {
            const char* Label;
            ESampleAlignment Alignment;
            std::vector<uint8_t> BigEndianBytes;
            std::vector<uint8_t> ExpectedLittleEndianBytes;
            int32_t ExpectedShift;
        };

        // Y 有两行，UV 有一行；每个平面的行尾都留两个 padding 字节。
        // 这样能同时验证半平面逐采样换字节序，且不会误改 padding。
        const FAlignmentCase cases[] = {
            {
                "YUV420SP16 10bit 大端低位对齐",
                ESampleAlignment::LeastSignificantBits,
                {
                    0x00, 0x00, 0x03, 0xFF, 0xA4, 0xA5,
                    0x01, 0x55, 0x02, 0xAA, 0xB4, 0xB5,
                    0x01, 0xFF, 0x02, 0x00, 0xC4, 0xC5,
                },
                {
                    0x00, 0x00, 0xFF, 0x03, 0xA4, 0xA5,
                    0x55, 0x01, 0xAA, 0x02, 0xB4, 0xB5,
                    0xFF, 0x01, 0x00, 0x02, 0xC4, 0xC5,
                },
                0,
            },
            {
                "YUV420SP16 10bit 大端高位对齐",
                ESampleAlignment::MostSignificantBits,
                {
                    0x00, 0x00, 0xFF, 0xC0, 0xA4, 0xA5,
                    0x55, 0x40, 0xAA, 0x80, 0xB4, 0xB5,
                    0x7F, 0xC0, 0x80, 0x00, 0xC4, 0xC5,
                },
                {
                    0x00, 0x00, 0xC0, 0xFF, 0xA4, 0xA5,
                    0x40, 0x55, 0x80, 0xAA, 0xB4, 0xB5,
                    0xC0, 0x7F, 0x00, 0x80, 0xC4, 0xC5,
                },
                kHighAlignmentShift,
            },
        };

        FRawImageLoader loader;
        Check("P016 兼容扩展名由 RAW 加载器支持",
              loader.SupportsFormat("fixture.P016"));

        for (size_t index = 0; index < std::size(cases); ++index)
        {
            const FAlignmentCase& testCase = cases[index];
            const std::string fileName =
                "YUVRaw_TestYuv420Sp16_" +
                std::to_string(index) +
                ".p016";
            FScopedTempFile file(
                fileName.c_str(),
                testCase.BigEndianBytes);
            Check(
                std::string(testCase.Label) + " 测试文件写入",
                file.IsWritten());

            FImageLoadParams params;
            params.Format = EImageFormat::YUV420SP16;
            params.Width = kWidth;
            params.Height = kHeight;
            params.Stride = kStride;
            params.BitsPerPixel = kEffectiveBitDepth;
            params.ByteOrder = EByteOrder::BigEndian;
            params.SampleAlignment = testCase.Alignment;

            std::unique_ptr<FImageData> image =
                loader.LoadFromFile(file.String(), &params);
            const std::string label = testCase.Label;
            Check(label + " 加载成功", image && image->IsValid());

            if (!image)
            {
                continue;
            }

            Check(label + " 保留 YUV420SP16 格式",
                  image->GetFormat() == EImageFormat::YUV420SP16);
            Check(label + " 消费 10bit 有效位深",
                  image->GetSourceBitDepth() == kEffectiveBitDepth);
            Check(label + " 消费有效位对齐",
                  image->GetSampleShift() == testCase.ExpectedShift);
            CheckBytes(
                label + " Y/UV 换为内部小端且各平面 padding 不变",
                *image,
                testCase.ExpectedLittleEndianBytes);

            constexpr int32_t kEffectiveMaximum =
                (1 << kEffectiveBitDepth) - 1;
            constexpr int32_t kContainerMaximum =
                (1 << kStorageContainerBits) - 1;
            const float expectedScale =
                static_cast<float>(kContainerMaximum) /
                static_cast<float>(
                    kEffectiveMaximum << testCase.ExpectedShift);
            Check(label + " 采样缩放",
                  std::fabs(image->GetSampleScale() - expectedScale) <
                      kScaleTolerance);
        }

        FScopedTempFile invalidFile(
            "YUVRaw_TestYuv420Sp16Invalid.p016",
            cases[0].BigEndianBytes);
        FImageLoadParams invalidParams;
        invalidParams.Format = EImageFormat::YUV420SP16;
        invalidParams.Width = kWidth;
        invalidParams.Height = kHeight;
        invalidParams.Stride = kStride;
        invalidParams.BitsPerPixel = kMinimumYuv420Sp16BitDepth - 1;
        Check("YUV420SP16 拒绝低于 8bit 的有效位深",
              !loader.LoadFromFile(invalidFile.String(), &invalidParams));

        invalidParams.BitsPerPixel = kStorageContainerBits + 1;
        Check("YUV420SP16 拒绝超过容器的有效位深",
              !loader.LoadFromFile(invalidFile.String(), &invalidParams));
    }

    void TestResourceLimits()
    {
        constexpr int32_t kBoundaryWidth = 16384;
        constexpr int32_t kAbovePixelLimitHeight = static_cast<int32_t>(
            FImageLimits::kMaximumPixelCount / static_cast<size_t>(kBoundaryWidth)) + 2;
        constexpr int32_t kSmallWidth = 4;
        constexpr int32_t kSmallHeight = 2;
        const std::vector<uint8_t> dummyBytes(128u, 0u);
        FScopedTempFile file("YUVRaw_TestRawLimits.raw", dummyBytes);
        Check("资源边界测试文件写入", file.IsWritten());

        FRawImageLoader loader;
        FImageLoadParams params;
        params.Format = EImageFormat::BayerPacked10;
        params.Width = FImageLimits::kMaximumDimension + 1;
        params.Height = kSmallHeight;

        Check(
            "RAW 加载拒绝超过最大单边尺寸",
            !loader.LoadFromFile(file.String(), &params));

        params.Width = kBoundaryWidth;
        params.Height = kAbovePixelLimitHeight;

        Check(
            "RAW 加载拒绝超过最大像素数",
            !loader.LoadFromFile(file.String(), &params));

        params.Width = kSmallWidth;
        params.Height = kSmallHeight;
        params.Stride = FImageLimits::kMaximumStrideBytes + 1;

        Check(
            "RAW 加载拒绝超过最大 stride",
            !loader.LoadFromFile(file.String(), &params));

        params.Format = EImageFormat::Grayscale8;
        params.Stride = static_cast<int32_t>(
            FImageLimits::kMaximumFrameBytes /
            static_cast<size_t>(kSmallHeight)) + 1;

        Check(
            "RAW 加载拒绝超过最大单帧内存",
            !loader.LoadFromFile(file.String(), &params));
    }

    void TestPublicSyntheticFixtures()
    {
        using namespace PublicFixtures;
        constexpr int32_t kRgbaChannelCount = 4;
        struct FCase { const char* Name; EImageFormat Format; int32_t Stride; std::vector<uint8_t> Bytes; };
        const FCase cases[] = {
            {"public_NV21.yuv", EImageFormat::NV21, kNv21Stride, Nv21()},
            {"public_P010.yuv", EImageFormat::P010, kP010Stride, P010()},
            {"public_Bayer12.raw", EImageFormat::Bayer12, kWidth * static_cast<int32_t>(sizeof(uint16_t)), Bayer()},
            {"public_RGBA8.raw", EImageFormat::RGBA8, kWidth * kRgbaChannelCount, ColorBars()},
        };
        FRawImageLoader loader;
        EImageLoadError error = EImageLoadError::None;
        for (const auto& fixture : cases)
        {
            FScopedTempFile file(fixture.Name, fixture.Bytes);
            FImageLoadParams params;
            params.Format = fixture.Format;
            params.Width = kWidth;
            params.Height = kHeight;
            params.Stride = fixture.Stride;
            const auto image = loader.LoadFromFile(file.String(), &params, &error);
            Check(fixture.Name, image && image->IsValid() && error == EImageLoadError::None);
            if (image) CheckBytes(std::string(fixture.Name) + " bytes including padding", *image, fixture.Bytes);
        }
        auto sequence = Nv21();
        const auto second = Nv21(true);
        sequence.insert(sequence.end(), second.begin(), second.end());
        FScopedTempFile file("public_two_frames_NV21.yuv", sequence);
        FImageLoadParams params;
        params.Format = EImageFormat::NV21;
        params.Width = kWidth;
        params.Height = kHeight;
        params.Stride = kNv21Stride;
        const auto firstFrame = loader.LoadFromFile(file.String(), &params, &error);
        Check("concatenated input reads one frame", firstFrame && firstFrame->GetPixelDataSize() == Nv21().size());
        if (firstFrame) CheckBytes("concatenated input returns exact first frame", *firstFrame, Nv21());

        params.Stride = kWidth - 1;
        Check("short stride rejected with parameter error", !loader.LoadFromFile(file.String(), &params, &error) && error == EImageLoadError::InvalidParameters);
        params.Stride = kNv21Stride;
        FScopedTempFile truncated("public_truncated_NV21.yuv", {0});
        Check("truncated RAW categorized", !loader.LoadFromFile(truncated.String(), &params, &error) && error == EImageLoadError::TruncatedData);
        const auto recovered = loader.LoadFromFile(file.String(), &params, &error);
        Check("RAW opens next valid file after failures", recovered && error == EImageLoadError::None);
    }

    void TestRawSensorBitDepthAndByteOrder()
    {
        constexpr int32_t kRawSensorWidth = 2;
        constexpr int32_t kRawSensorHeight = 2;
        constexpr int32_t kRawSensorStride = 6;
        constexpr int32_t kEffectiveBitDepth = 10;
        constexpr int32_t kHighAlignmentShift =
            kStorageContainerBits - kEffectiveBitDepth;

        struct FAlignmentCase
        {
            const char* Label;
            ESampleAlignment Alignment;
            std::vector<uint8_t> BigEndianPixels;
            std::vector<uint16_t> ExpectedStoredWords;
            int32_t ExpectedShift;
        };

        // 每行最后两个字节是 padding；字节序归一化不得改动它们。
        const FAlignmentCase cases[] = {
            {
                "RAW_SENSOR 大端低位对齐",
                ESampleAlignment::LeastSignificantBits,
                {
                    0x00, 0x00, 0x03, 0xFF, 0xA4, 0xA5,
                    0x01, 0x55, 0x02, 0xAA, 0xB4, 0xB5,
                },
                {
                    0x0000, 0x03FF,
                    0x0155, 0x02AA,
                },
                0,
            },
            {
                "RAW_SENSOR 大端高位对齐",
                ESampleAlignment::MostSignificantBits,
                {
                    0x00, 0x00, 0xFF, 0xC0, 0xA4, 0xA5,
                    0x55, 0x40, 0xAA, 0x80, 0xB4, 0xB5,
                },
                {
                    0x0000, 0xFFC0,
                    0x5540, 0xAA80,
                },
                kHighAlignmentShift,
            },
        };

        FRawImageLoader loader;

        for (const FAlignmentCase& testCase : cases)
        {
            const std::string fileName =
                std::string("YUVRaw_Test") +
                (testCase.Alignment ==
                    ESampleAlignment::MostSignificantBits
                        ? "RawSensorHigh.raw16"
                        : "RawSensorLow.raw16");
            FScopedTempFile file(
                fileName.c_str(),
                testCase.BigEndianPixels);
            Check(
                std::string(testCase.Label) + " 测试文件写入",
                file.IsWritten());

            FImageLoadParams params;
            params.Format = EImageFormat::Bayer16;
            params.Width = kRawSensorWidth;
            params.Height = kRawSensorHeight;
            params.Stride = kRawSensorStride;
            params.BitsPerPixel = kEffectiveBitDepth;
            params.ByteOrder = EByteOrder::BigEndian;
            params.SampleAlignment = testCase.Alignment;

            std::unique_ptr<FImageData> image =
                loader.LoadFromFile(file.String(), &params);
            const std::string label = testCase.Label;

            Check(label + " 加载成功", image && image->IsValid());

            if (!image)
            {
                continue;
            }

            Check(label + " 消费 Params.BitsPerPixel",
                  image->GetSourceBitDepth() == kEffectiveBitDepth);
            Check(label + " 消费有效位对齐",
                  image->GetSampleShift() == testCase.ExpectedShift);
            CheckPixels(
                label + " 字节交换与存储真值",
                *image,
                testCase.ExpectedStoredWords);
            Check(label + " 保留第 0 行 padding",
                  image->GetPixelData()[4] == 0xA4 &&
                  image->GetPixelData()[5] == 0xA5);

            constexpr int32_t kEffectiveMaximum =
                (1 << kEffectiveBitDepth) - 1;
            const float expectedScale =
                65535.0f /
                static_cast<float>(
                    kEffectiveMaximum <<
                    testCase.ExpectedShift);
            Check(label + " 采样缩放",
                  std::fabs(image->GetSampleScale() - expectedScale) < kScaleTolerance);
        }

        FImageLoadParams invalidParams;
        invalidParams.Format = EImageFormat::Bayer16;
        invalidParams.Width = kRawSensorWidth;
        invalidParams.Height = kRawSensorHeight;
        invalidParams.Stride = kRawSensorStride;
        invalidParams.BitsPerPixel = 0;
        FScopedTempFile invalidFile(
            "YUVRaw_TestRawSensorInvalid.raw16",
            cases[0].BigEndianPixels);
        Check("RAW_SENSOR 拒绝 0 位有效位深",
              !loader.LoadFromFile(invalidFile.String(), &invalidParams));

        invalidParams.BitsPerPixel = 17;
        Check("RAW_SENSOR 拒绝超过容器的有效位深",
              !loader.LoadFromFile(invalidFile.String(), &invalidParams));
    }
}

int main()
{
    const std::filesystem::path logDirectory =
        std::filesystem::temp_directory_path() / "YUVRawRawLoaderTestLogs";
    std::error_code ec;
    std::filesystem::create_directories(logDirectory, ec);
    FLogger::Initialize(logDirectory, kTestLogLimitBytes, kTestLogFileCount);

    std::printf("=== Android Bayer RAW 解包 ===\n");
    TestPackedTruthTables();

    std::printf("\n=== Android packed 几何约束 ===\n");
    TestPackedGeometryValidation();

    std::printf("\n=== Unpacked Bayer 位深 ===\n");
    TestUnpackedDefaultBitDepths();
    TestConfigurable16BitByteOrderFormats();
    TestRawSensorBitDepthAndByteOrder();
    TestFixedP0xxStorageLayout();
    TestConfigurableYuv420Sp16StorageLayout();

    std::printf("\n=== UTF-8 文件路径 ===\n");
    TestUnicodeFilePath();

    std::printf("\n=== RAW 尺寸与内存边界 ===\n");
    TestResourceLimits();
    TestPublicSyntheticFixtures();

    FLogger::Shutdown();
    std::filesystem::remove(logDirectory / "YUVRaw.log", ec);
    std::filesystem::remove(logDirectory, ec);

    std::printf("\n==================================================\n");
    std::printf(gFailures == 0 ? "全部通过\n" : "%d 项失败\n", gFailures);

    return gFailures == 0 ? 0 : 1;
}
