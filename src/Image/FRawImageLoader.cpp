#include "FRawImageLoader.h"
#include "FImageData.h"
#include "FImageFormatDesc.h"
#include "FImageLimits.h"
#include "FImageLoadParams.h"
#include "Util.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <new>
#include <stdexcept>

namespace
{
    constexpr int32_t kRawWordBytes = 2;

    // Android RAW10/12/14 都要求宽度为 4 的倍数、高度为偶数。
    // 先校验再解包，既遵守平台格式约束，也避免尾组读取越过行末。
    constexpr int32_t kAndroidPackedWidthAlignment = 4;
    constexpr int32_t kAndroidPackedHeightAlignment = 2;

    constexpr int32_t kRaw10BitDepth = 10;
    constexpr int32_t kRaw10PixelsPerGroup = 4;
    constexpr int32_t kRaw10BytesPerGroup = 5;
    constexpr int32_t kRaw10LowBitCount = 2;
    constexpr int32_t kRaw10LowByteIndex = 4;
    constexpr uint8_t kRaw10LowMask = 0x03;

    constexpr int32_t kRaw12BitDepth = 12;
    constexpr int32_t kRaw12PixelsPerGroup = 2;
    constexpr int32_t kRaw12BytesPerGroup = 3;
    constexpr int32_t kRaw12LowBitCount = 4;
    constexpr int32_t kRaw12LowByteIndex = 2;
    constexpr uint8_t kRaw12LowMask = 0x0F;

    constexpr int32_t kRaw14BitDepth = 14;
    constexpr int32_t kRaw14PixelsPerGroup = 4;
    constexpr int32_t kRaw14BytesPerGroup = 7;
    constexpr int32_t kRaw14LowBitCount = 6;
    constexpr int32_t kRaw14Low2BitCount = 2;
    constexpr int32_t kRaw14Low4BitCount = 4;
    constexpr int32_t kRaw14LowByte0Index = 4;
    constexpr int32_t kRaw14LowByte1Index = 5;
    constexpr int32_t kRaw14LowByte2Index = 6;
    constexpr uint8_t kRaw14Low2Mask = 0x03;
    constexpr uint8_t kRaw14Low4Mask = 0x0F;
    constexpr uint8_t kRaw14Low6Mask = 0x3F;

    bool IsAndroidPackedBayer(EImageFormat Format)
    {
        return Format == EImageFormat::BayerPacked10 ||
               Format == EImageFormat::BayerPacked12 ||
               Format == EImageFormat::BayerPacked14;
    }

    int32_t GetPackedBayerBitDepth(EImageFormat Format)
    {
        switch (Format)
        {
        case EImageFormat::BayerPacked10: return kRaw10BitDepth;
        case EImageFormat::BayerPacked12: return kRaw12BitDepth;
        case EImageFormat::BayerPacked14: return kRaw14BitDepth;
        default:                          return 0;
        }
    }

    bool ValidateAndroidPackedGeometry(
        EImageFormat Format,
        int32_t Width,
        int32_t Height,
        const char* LogContext)
    {
        if (!IsAndroidPackedBayer(Format))
        {
            return true;
        }

        if ((Width % kAndroidPackedWidthAlignment) != 0 ||
            (Height % kAndroidPackedHeightAlignment) != 0)
        {
            LOGE(
                LogContext,
                "%s requires width divisible by %d and height divisible by %d, got %dx%d",
                FImageFormatDesc::Get(Format).Name,
                kAndroidPackedWidthAlignment,
                kAndroidPackedHeightAlignment,
                Width,
                Height);

            return false;
        }

        return true;
    }

    /**
     * Android RAW10 解包：每 4 个像素占 5 字节
     *   byte0..byte3 = 4 个像素的高 8 位
     *   byte4        = 4 个像素的低 2 位，按 [p3p2p1p0] 从低位到高位排列
     * 解包结果左移到 16bit 的低位对齐（即输出 0..1023）
     */
    void UnpackMipi10(const uint8_t* Src, uint16_t* Dst, int32_t PixelCount)
    {
        int32_t written = 0;

        while (written < PixelCount)
        {
            const uint8_t low = Src[kRaw10LowByteIndex];

            for (int32_t i = 0; i < kRaw10PixelsPerGroup && written < PixelCount; ++i, ++written)
            {
                const uint16_t highBits =
                    static_cast<uint16_t>(Src[i]) << kRaw10LowBitCount;
                const uint16_t lowBits =
                    static_cast<uint16_t>((low >> (kRaw10LowBitCount * i)) & kRaw10LowMask);

                Dst[written] = static_cast<uint16_t>(highBits | lowBits);
            }

            Src += kRaw10BytesPerGroup;
        }
    }

    /**
     * Android RAW12 解包：每 2 个像素占 3 字节
     *   byte0 = 像素0 高 8 位, byte1 = 像素1 高 8 位
     *   byte2 = 低 4 位，低半字节属于像素0，高半字节属于像素1
     */
    void UnpackMipi12(const uint8_t* Src, uint16_t* Dst, int32_t PixelCount)
    {
        int32_t written = 0;

        while (written < PixelCount)
        {
            const uint8_t low = Src[kRaw12LowByteIndex];

            for (int32_t i = 0; i < kRaw12PixelsPerGroup && written < PixelCount; ++i, ++written)
            {
                const uint16_t highBits =
                    static_cast<uint16_t>(Src[i]) << kRaw12LowBitCount;
                const uint16_t lowBits =
                    static_cast<uint16_t>((low >> (kRaw12LowBitCount * i)) & kRaw12LowMask);

                Dst[written] = static_cast<uint16_t>(highBits | lowBits);
            }

            Src += kRaw12BytesPerGroup;
        }
    }

    /**
     * Android RAW14 解包：每 4 个像素占 7 字节。
     *
     * 前 4 字节分别保存 P0..P3 的高 8 位；后 3 字节把四个像素的低 6 位
     * 按 Android ImageFormat.RAW14 规定跨字节串接。这里显式重组每个像素，
     * 避免依赖主机位域布局或大小端。
     */
    void UnpackAndroid14(const uint8_t* Src, uint16_t* Dst, int32_t PixelCount)
    {
        int32_t written = 0;

        while (written < PixelCount)
        {
            const uint8_t low0 = Src[kRaw14LowByte0Index];
            const uint8_t low1 = Src[kRaw14LowByte1Index];
            const uint8_t low2 = Src[kRaw14LowByte2Index];

            const uint16_t lowBits[kRaw14PixelsPerGroup] = {
                static_cast<uint16_t>(low0 & kRaw14Low6Mask),
                static_cast<uint16_t>(
                    ((low1 & kRaw14Low4Mask) << kRaw14Low2BitCount) |
                    ((low0 >> kRaw14LowBitCount) & kRaw14Low2Mask)),
                static_cast<uint16_t>(
                    ((low2 & kRaw14Low2Mask) << kRaw14Low4BitCount) |
                    ((low1 >> kRaw14Low4BitCount) & kRaw14Low4Mask)),
                static_cast<uint16_t>((low2 >> kRaw14Low2BitCount) & kRaw14Low6Mask),
            };

            for (int32_t i = 0; i < kRaw14PixelsPerGroup && written < PixelCount; ++i, ++written)
            {
                const uint16_t highBits =
                    static_cast<uint16_t>(Src[i]) << kRaw14LowBitCount;
                Dst[written] = static_cast<uint16_t>(highBits | lowBits[i]);
            }

            Src += kRaw14BytesPerGroup;
        }
    }

    /**
     * 把 Android packed Bayer 数据解包成 Bayer16
     * @return 解包后的 FImageData，失败返回 nullptr
     */
    std::unique_ptr<FImageData> UnpackBayer(
        const std::vector<uint8_t>& Packed,
        EImageFormat SourceFormat,
        int32_t Width,
        int32_t Height,
        int32_t PackedStride)
    {
        const int32_t bitDepth = GetPackedBayerBitDepth(SourceFormat);

        if (bitDepth <= 0)
        {
            LOGE("UnpackBayer", "Unsupported packed Bayer format: %d", static_cast<int32_t>(SourceFormat));

            return nullptr;
        }

        if (!ValidateAndroidPackedGeometry(SourceFormat, Width, Height, "UnpackBayer"))
        {
            return nullptr;
        }

        const int32_t minPackedStride =
            FImageFormatDesc::ResolveBaseStride(SourceFormat, Width, 0);

        if (PackedStride < minPackedStride)
        {
            LOGE("UnpackBayer", "Packed stride too small, Stride: %d, Required: %d", PackedStride, minPackedStride);

            return nullptr;
        }

        size_t requiredPackedBytes = 0;
        size_t pixelCount = 0;
        size_t outputBytes = 0;

        if (!FImageLimits::TryMultiplySize(
                static_cast<size_t>(PackedStride),
                static_cast<size_t>(Height),
                requiredPackedBytes) ||
            requiredPackedBytes > Packed.size() ||
            !FImageLimits::TryGetPixelCount(Width, Height, pixelCount) ||
            !FImageLimits::TryMultiplySize(
                pixelCount,
                static_cast<size_t>(kRawWordBytes),
                outputBytes) ||
            !FImageLimits::IsFrameByteCountSupported(outputBytes))
        {
            LOGE(
                "UnpackBayer",
                "Packed/output size is invalid, Packed: %zu, Required: %zu, Size: %dx%d",
                Packed.size(),
                requiredPackedBytes,
                Width,
                Height);

            return nullptr;
        }

        auto imageData = std::make_unique<FImageData>();
        imageData->SetSize(Width, Height);
        imageData->SetFormat(EImageFormat::Bayer16);
        // 解包结果是紧凑的 16bit 容器
        imageData->SetStride(Width * kRawWordBytes);
        // 但真实位深仍是 10/12/14，且低位对齐。不告诉下游的话，
        // 按 16bit 归一化会让图像几乎全黑（1023/65535 ≈ 1.6%）
        imageData->SetSampleLayout(bitDepth, /*InSampleShift=*/0);
        imageData->AllocatePixelData(outputBytes);

        uint16_t* dst = reinterpret_cast<uint16_t*>(imageData->GetPixelData());

        for (int32_t row = 0; row < Height; ++row)
        {
            const uint8_t* src = Packed.data() + static_cast<size_t>(row) * PackedStride;
            uint16_t* dstRow = dst + static_cast<size_t>(row) * Width;

            switch (SourceFormat)
            {
            case EImageFormat::BayerPacked10:
                UnpackMipi10(src, dstRow, Width);
                break;

            case EImageFormat::BayerPacked12:
                UnpackMipi12(src, dstRow, Width);
                break;

            case EImageFormat::BayerPacked14:
                UnpackAndroid14(src, dstRow, Width);
                break;

            default:
                return nullptr;
            }
        }

        return imageData;
    }

    /**
     * 把所有 16bit 采样从大端转换为程序内部统一使用的小端。
     *
     * 按格式平面几何遍历有效采样，既覆盖 RGB/RGBA/Gray/Bayer，也覆盖将来可能
     * 开放字节序的多平面格式；行尾 padding 必须原样保留。
     */
    bool Swap16BitSampleByteOrder(
        FImageData& ImageData,
        const FFormatDesc& Desc,
        int32_t Width,
        int32_t Height,
        int32_t BaseStride)
    {
        uint8_t* data = ImageData.GetPixelData();

        for (int32_t planeIndex = 0; planeIndex < Desc.PlaneCount; ++planeIndex)
        {
            const FPlaneDesc& plane = Desc.Planes[planeIndex];

            if (plane.BytesPerSample != kRawWordBytes)
            {
                return false;
            }

            const int32_t planeWidth =
                FImageFormatDesc::GetPlaneWidth(Desc, planeIndex, Width);
            const int32_t planeHeight =
                FImageFormatDesc::GetPlaneHeight(Desc, planeIndex, Height);
            const int32_t planeStride =
                FImageFormatDesc::GetPlaneStrideBytes(
                    Desc,
                    planeIndex,
                    BaseStride);
            const size_t planeOffset =
                FImageFormatDesc::GetPlaneOffsetBytes(
                    Desc,
                    planeIndex,
                    Height,
                    BaseStride);
            const size_t planeSize =
                FImageFormatDesc::GetPlaneSizeBytes(
                    Desc,
                    planeIndex,
                    Height,
                    BaseStride);
            const size_t samplesPerRow =
                static_cast<size_t>(planeWidth) *
                static_cast<size_t>(plane.ChannelCount);
            const size_t activeRowBytes =
                samplesPerRow * static_cast<size_t>(kRawWordBytes);

            if (planeWidth <= 0 ||
                planeHeight <= 0 ||
                planeStride <= 0 ||
                activeRowBytes > static_cast<size_t>(planeStride) ||
                planeOffset > ImageData.GetPixelDataSize() ||
                planeSize >
                    ImageData.GetPixelDataSize() - planeOffset)
            {
                return false;
            }

            for (int32_t row = 0; row < planeHeight; ++row)
            {
                uint8_t* rowData =
                    data +
                    planeOffset +
                    static_cast<size_t>(row) *
                        static_cast<size_t>(planeStride);

                for (size_t sampleIndex = 0;
                     sampleIndex < samplesPerRow;
                     ++sampleIndex)
                {
                    uint8_t* sample =
                        rowData +
                        sampleIndex *
                            static_cast<size_t>(kRawWordBytes);
                    std::swap(sample[0], sample[1]);
                }
            }
        }

        return true;
    }
}

std::unique_ptr<FImageData> FRawImageLoader::LoadFromFile(const std::string& FilePath, const FImageLoadParams* Params, EImageLoadError* OutError)
try
{
    SetImageLoadError(OutError, EImageLoadError::InvalidParameters);
    if (!Params || Params->Format == EImageFormat::Unknown)
    {
        LOGE("LoadFromFile", "Raw formats require explicit load params (format/width/height)");

        return nullptr;
    }

    const EImageFormat format = Params->Format;
    const FFormatDesc& desc = FImageFormatDesc::Get(format);

    if (desc.PlaneCount <= 0)
    {
        LOGE("LoadFromFile", "Format has no plane description, Format: %s", desc.Name);

        return nullptr;
    }

    const int32_t width = Params->Width;
    const int32_t height = Params->Height;

    if (!FImageLimits::AreDimensionsSupported(width, height))
    {
        SetImageLoadError(OutError, EImageLoadError::ResourceLimit);
        LOGE(
            "LoadFromFile",
            "Dimensions exceed limits, Size: %dx%d, MaxDimension: %d, MaxPixels: %zu",
            width,
            height,
            FImageLimits::kMaximumDimension,
            FImageLimits::kMaximumPixelCount);

        return nullptr;
    }

    if (!ValidateAndroidPackedGeometry(format, width, height, "LoadFromFile"))
    {
        return nullptr;
    }

    const FBitDepthPropertyState bitDepth =
        FImageFormatDesc::ResolveBitDepth(
            format,
            Params->BitsPerPixel);

    if (bitDepth.Value < bitDepth.Minimum ||
        bitDepth.Value > bitDepth.Maximum)
    {
        LOGE(
            "LoadFromFile",
            "%s effective bit depth must be in [%d, %d], got: %d",
            desc.Name,
            bitDepth.Minimum,
            bitDepth.Maximum,
            bitDepth.Value);

        return nullptr;
    }

    const FByteOrderPropertyState byteOrder =
        FImageFormatDesc::ResolveByteOrder(format, Params->ByteOrder);
    const bool bSwap16BitBytes =
        byteOrder.Mode != EFormatPropertyMode::NotApplicable &&
        byteOrder.Value == EByteOrder::BigEndian;

    const int32_t baseStride = FImageFormatDesc::ResolveBaseStride(format, width, Params->Stride);
    const int32_t packedStride = FImageFormatDesc::ResolveBaseStride(format, width, 0);

    if (baseStride <= 0 || packedStride <= 0 || baseStride < packedStride)
    {
        LOGE(
            "LoadFromFile",
            "Invalid stride, Stride: %d, Required: %d, Maximum: %d",
            baseStride,
            packedStride,
            FImageLimits::kMaximumStrideBytes);

        return nullptr;
    }

    const size_t frameSize = FImageFormatDesc::CalculateFrameSize(format, width, height, baseStride);

    if (frameSize == 0)
    {
        SetImageLoadError(OutError, EImageLoadError::ResourceLimit);
        LOGE(
            "LoadFromFile",
            "Failed to compute frame size or frame exceeds %zu bytes, Format: %s",
            FImageLimits::kMaximumFrameBytes,
            desc.Name);

        return nullptr;
    }

    std::error_code ec;

    const std::filesystem::path nativePath = std::filesystem::u8path(FilePath);

    if (!std::filesystem::exists(nativePath, ec))
    {
        SetImageLoadError(OutError, EImageLoadError::FileAccess);
        LOGE("LoadFromFile", "%s", "File does not exist");

        return nullptr;
    }

    const auto fileSize = std::filesystem::file_size(nativePath, ec);

    if (ec)
    {
        SetImageLoadError(OutError, EImageLoadError::FileAccess);
        LOGE("LoadFromFile", "%s", "Failed to query file size");

        return nullptr;
    }

    // 一个文件就是一幅图，从头读满一帧即可。
    // 文件更大时多出来的字节直接忽略：那多半意味着参数填错了（属性面板会给出提示），
    // 但仍然把能解出来的这一幅显示出来，比直接报错更有助于用户把参数试对。
    if (fileSize < static_cast<uintmax_t>(frameSize))
    {
        SetImageLoadError(OutError, EImageLoadError::TruncatedData);
        LOGE(
            "LoadFromFile",
            "File too small, Expected at least: %zu, Actual: %llu",
            frameSize,
            static_cast<unsigned long long>(fileSize));

        return nullptr;
    }

    std::ifstream file(nativePath, std::ios::binary);

    if (!file.is_open())
    {
        SetImageLoadError(OutError, EImageLoadError::FileAccess);
        LOGE("LoadFromFile", "%s", "Failed to open file");

        return nullptr;
    }

    // Android packed Bayer 需要先读进临时缓冲再解包
    if (IsAndroidPackedBayer(format))
    {
        try
        {
            std::vector<uint8_t> packed(frameSize);
            file.read(reinterpret_cast<char*>(packed.data()), static_cast<std::streamsize>(frameSize));

            if (file.gcount() != static_cast<std::streamsize>(frameSize))
            {
                SetImageLoadError(OutError, EImageLoadError::TruncatedData);
                LOGE("LoadFromFile", "%s", "Short read on packed data");

                return nullptr;
            }

            auto unpacked = UnpackBayer(packed, format, width, height, baseStride);

            if (unpacked)
            {
                SetImageLoadError(OutError, EImageLoadError::None);
                LOGD("LoadFromFile", "Unpacked %s -> Bayer16, %dx%d", desc.Name, width, height);
            }

            return unpacked;
        }
        catch (const std::bad_alloc&)
        {
            SetImageLoadError(OutError, EImageLoadError::OutOfMemory);
            LOGE("LoadFromFile", "Out of memory while unpacking %zu-byte frame", frameSize);
            return nullptr;
        }
        catch (const std::length_error&)
        {
            SetImageLoadError(OutError, EImageLoadError::ResourceLimit);
            LOGE("LoadFromFile", "Allocation size rejected while unpacking %zu-byte frame", frameSize);
            return nullptr;
        }
    }

    std::unique_ptr<FImageData> imageData;

    try
    {
        imageData = std::make_unique<FImageData>();
        imageData->SetSize(width, height);
        imageData->SetStride(baseStride);
        imageData->SetFormat(format);

        if (desc.PlaneCount > 0 &&
            desc.Planes[0].BytesPerSample == kRawWordBytes)
        {
            const int32_t sourceBitDepth = bitDepth.Value;
            const int32_t sampleShift =
                FImageFormatDesc::CalculateSampleShift(
                    format,
                    sourceBitDepth,
                    Params->SampleAlignment);

            imageData->SetSampleLayout(sourceBitDepth, sampleShift);
        }

        imageData->AllocatePixelData(frameSize);
    }
    catch (const std::bad_alloc&)
    {
        SetImageLoadError(OutError, EImageLoadError::OutOfMemory);
        LOGE("LoadFromFile", "Out of memory while allocating %zu-byte frame", frameSize);
        return nullptr;
    }
    catch (const std::length_error&)
    {
        SetImageLoadError(OutError, EImageLoadError::ResourceLimit);
        LOGE("LoadFromFile", "Allocation size rejected for %zu-byte frame", frameSize);
        return nullptr;
    }

    file.read(reinterpret_cast<char*>(imageData->GetPixelData()), static_cast<std::streamsize>(frameSize));

    if (file.gcount() != static_cast<std::streamsize>(frameSize))
    {
        SetImageLoadError(OutError, EImageLoadError::TruncatedData);
        LOGE("LoadFromFile", "Short read, Expected: %zu, Read: %lld",
             frameSize, static_cast<long long>(file.gcount()));

        return nullptr;
    }

    if (bSwap16BitBytes)
    {
        if (!Swap16BitSampleByteOrder(
                *imageData,
                desc,
                width,
                height,
                baseStride))
        {
            LOGE(
                "LoadFromFile",
                "Failed to normalize big-endian samples, Format: %s",
                desc.Name);

            return nullptr;
        }

        LOGD(
            "LoadFromFile",
            "Converted %s from big-endian to little-endian, %dx%d stride=%d",
            desc.Name,
            width,
            height,
            baseStride);
    }

    if (!imageData->IsValid())
    {
        LOGE("LoadFromFile", "%s", "Image data invalid after loading");

        return nullptr;
    }

    LOGD("LoadFromFile", "Loaded %s %dx%d stride=%d (%zu bytes)",
         desc.Name, width, height, baseStride, frameSize);

    SetImageLoadError(OutError, EImageLoadError::None);
    return imageData;
}
catch (const std::bad_alloc&)
{
    SetImageLoadError(OutError, EImageLoadError::OutOfMemory);
    LOGE("RAW", "%s", "Out of memory while preparing the input file");
    return nullptr;
}
catch (const std::length_error&)
{
    SetImageLoadError(OutError, EImageLoadError::ResourceLimit);
    LOGE("RAW", "%s", "Input metadata exceeds container limits");
    return nullptr;
}

bool FRawImageLoader::SupportsFormat(const std::string& FilePath) const
{
    const std::filesystem::path path = std::filesystem::u8path(FilePath);
    std::string ext = path.extension().u8string();

    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const std::vector<std::string> supported = GetSupportedExtensions();

    return std::find(supported.begin(), supported.end(), ext) != supported.end();
}

bool FRawImageLoader::SupportsFormat(EImageFormat Format) const
{
    // 除 Unknown 外，格式描述表里的都由本加载器处理
    return Format != EImageFormat::Unknown && FImageFormatDesc::Get(Format).PlaneCount > 0;
}

std::vector<std::string> FRawImageLoader::GetSupportedExtensions() const
{
    return {
        ".yuv", ".raw", ".raw10", ".raw12", ".raw14", ".raw16",
        ".nv12", ".nv21", ".p010", ".p016", ".i420", ".yv12", ".bin", ".bayer"
    };
}
