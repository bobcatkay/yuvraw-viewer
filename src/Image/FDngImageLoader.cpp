#include "FDngImageLoader.h"

#include "FImageData.h"
#include "FImageLimits.h"
#include "Util.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <libraw/libraw.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <new>
#include <stdexcept>

namespace
{
    constexpr int32_t kOutputBitsPerSample = 8;
    constexpr int32_t kLibRawHighBitsPerSample = 16;
    constexpr int32_t kLibRawSrgbOutput = 1;
    constexpr int32_t kGrayscaleChannelCount = 1;
    constexpr int32_t kRgbChannelCount = 3;
    constexpr int32_t kRgbaChannelCount = 4;
    constexpr uint8_t kOpaqueAlpha = 255;
    constexpr uint32_t kUint16ToUint8RoundingBias = 128u;
    constexpr uint32_t kUint16ToUint8Divisor = 257u;
    constexpr unsigned kLibRawMaximumRawMemoryMb = static_cast<unsigned>(
        FImageLimits::kMaximumFrameBytes / FImageLimits::kUnitsPerMebi);

    EImageLoadError ClassifyLibRawFailure(int Result)
    {
        switch (Result)
        {
        case LIBRAW_UNSUFFICIENT_MEMORY: return EImageLoadError::OutOfMemory;
        case LIBRAW_TOO_BIG: return EImageLoadError::ResourceLimit;
        case LIBRAW_MEMPOOL_OVERFLOW: return EImageLoadError::ResourceLimit;
        case LIBRAW_IO_ERROR: return EImageLoadError::TruncatedData;
        default: return EImageLoadError::DecodeFailure;
        }
    }

    using FProcessedImagePtr =
        std::unique_ptr<libraw_processed_image_t, decltype(&LibRaw::dcraw_clear_mem)>;

    uint8_t ToByte(const uint8_t* Source, int32_t BitsPerSample)
    {
        if (BitsPerSample == kOutputBitsPerSample)
        {
            return Source[0];
        }

        uint16_t value = 0;
        std::memcpy(&value, Source, sizeof(value));

        // LibRaw's 16-bit bitmap is native-endian and covers the full 0..65535 range.
        return static_cast<uint8_t>(
            (static_cast<uint32_t>(value) + kUint16ToUint8RoundingBias) /
            kUint16ToUint8Divisor);
    }

    bool IsOpenedGeometrySupported(const LibRaw& Processor)
    {
        const libraw_image_sizes_t& sizes = Processor.imgdata.sizes;
        const int32_t rawWidth = static_cast<int32_t>(
            sizes.raw_width != 0 ? sizes.raw_width : sizes.width);
        const int32_t rawHeight = static_cast<int32_t>(
            sizes.raw_height != 0 ? sizes.raw_height : sizes.height);
        const int32_t outputWidth = static_cast<int32_t>(
            sizes.width != 0 ? sizes.width : sizes.raw_width);
        const int32_t outputHeight = static_cast<int32_t>(
            sizes.height != 0 ? sizes.height : sizes.raw_height);

        size_t rawPixels = 0;
        size_t outputPixels = 0;
        size_t outputBytes = 0;

        // open_file 已读到 TIFF/DNG 元数据，此时先拒绝异常几何，避免 unpack/dcraw_process
        // 按不可信尺寸分配巨额 RAW 与中间缓冲。
        if (!FImageLimits::TryGetPixelCount(rawWidth, rawHeight, rawPixels) ||
            !FImageLimits::TryGetPixelCount(outputWidth, outputHeight, outputPixels) ||
            !FImageLimits::TryMultiplySize(
                outputPixels,
                static_cast<size_t>(kRgbaChannelCount),
                outputBytes) ||
            !FImageLimits::IsFrameByteCountSupported(outputBytes))
        {
            LOGE(
                "DNG",
                "DNG geometry exceeds limits before unpack, Raw: %dx%d, Output: %dx%d",
                rawWidth,
                rawHeight,
                outputWidth,
                outputHeight);

            return false;
        }

        return true;
    }
}

std::unique_ptr<FImageData> FDngImageLoader::LoadFromFile(
    const std::string& FilePath,
    const FImageLoadParams* /*Params*/,
    EImageLoadError* OutError)
try
{
    SetImageLoadError(OutError, EImageLoadError::DecodeFailure);
    const std::filesystem::path nativePath = std::filesystem::u8path(FilePath);
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(nativePath, fileError))
    {
        SetImageLoadError(OutError, EImageLoadError::FileAccess);
        LOGE("DNG", "%s", "Input file is missing, inaccessible or not a regular file");
        return nullptr;
    }
    // LibRaw 0.22 的实例约占 750 KiB；后台线程默认栈较小，放到堆上并统一处理分配失败。
    const auto processorOwner = std::make_unique<LibRaw>();
    LibRaw& processor = *processorOwner;

    libraw_output_params_t& output = processor.imgdata.params;
    output.output_bps = kOutputBitsPerSample;
    output.output_color = kLibRawSrgbOutput;
    output.use_camera_wb = 1;

    // 在任何文件解析前配置 RAW 缓冲上限。0.22.2 的 recycle() 保留 rawparams；
    // 仍在 open_file 后重申该策略，避免未来升级改变初始化行为。
    processor.imgdata.rawparams.max_raw_memory_mb = kLibRawMaximumRawMemoryMb;

    const std::wstring widePath = nativePath.wstring();
    int result = processor.open_file(widePath.c_str());

    if (result != LIBRAW_SUCCESS)
    {
        SetImageLoadError(OutError, ClassifyLibRawFailure(result));
        LOGE("DNG", "LibRaw cannot open file, Error: %s", libraw_strerror(result));

        return nullptr;
    }

    if (!IsOpenedGeometrySupported(processor))
    {
        SetImageLoadError(OutError, EImageLoadError::ResourceLimit);
        return nullptr;
    }

    // 此参数约束 RAW/解包缓冲，不是进程总内存或全部元数据分配的硬上限。
    processor.imgdata.rawparams.max_raw_memory_mb =
        kLibRawMaximumRawMemoryMb;

    result = processor.unpack();

    if (result != LIBRAW_SUCCESS)
    {
        SetImageLoadError(OutError, ClassifyLibRawFailure(result));
        LOGE("DNG", "LibRaw cannot unpack file, Error: %s", libraw_strerror(result));

        return nullptr;
    }

    result = processor.dcraw_process();

    if (result != LIBRAW_SUCCESS)
    {
        SetImageLoadError(OutError, ClassifyLibRawFailure(result));
        LOGE("DNG", "LibRaw post-processing failed, Error: %s", libraw_strerror(result));

        return nullptr;
    }

    int memoryError = LIBRAW_SUCCESS;
    FProcessedImagePtr processed(
        processor.dcraw_make_mem_image(&memoryError),
        &LibRaw::dcraw_clear_mem);

    if (!processed)
    {
        SetImageLoadError(OutError, ClassifyLibRawFailure(memoryError));
        LOGE("DNG", "LibRaw did not return a bitmap, Error: %s", libraw_strerror(memoryError));

        return nullptr;
    }

    if (processed->type != LIBRAW_IMAGE_BITMAP ||
        processed->width == 0 ||
        processed->height == 0 ||
        processed->width >
            static_cast<uint32_t>(FImageLimits::kMaximumDimension) ||
        processed->height >
            static_cast<uint32_t>(FImageLimits::kMaximumDimension) ||
        (processed->colors != kGrayscaleChannelCount &&
         processed->colors < kRgbChannelCount) ||
        (processed->bits != kOutputBitsPerSample &&
         processed->bits != kLibRawHighBitsPerSample))
    {
        LOGE("DNG",
             "Unsupported LibRaw bitmap layout, Type: %d, Size: %ux%u, Colors: %u, Bits: %u",
             static_cast<int>(processed->type),
             processed->width,
             processed->height,
             processed->colors,
             processed->bits);

        return nullptr;
    }

    const size_t bytesPerSample = static_cast<size_t>(processed->bits / 8);
    size_t pixelCount = 0;
    size_t sourcePixelBytes = 0;
    size_t requiredSourceBytes = 0;
    size_t rgbaBytes = 0;

    if (!FImageLimits::TryGetPixelCount(
            static_cast<int32_t>(processed->width),
            static_cast<int32_t>(processed->height),
            pixelCount) ||
        !FImageLimits::TryMultiplySize(
            static_cast<size_t>(processed->colors),
            bytesPerSample,
            sourcePixelBytes) ||
        sourcePixelBytes == 0 ||
        !FImageLimits::TryMultiplySize(
            pixelCount,
            sourcePixelBytes,
            requiredSourceBytes) ||
        requiredSourceBytes > processed->data_size ||
        !FImageLimits::TryMultiplySize(
            pixelCount,
            static_cast<size_t>(kRgbaChannelCount),
            rgbaBytes) ||
        !FImageLimits::IsFrameByteCountSupported(rgbaBytes))
    {
        LOGE(
            "DNG",
            "LibRaw bitmap size is inconsistent or exceeds %zu bytes",
            FImageLimits::kMaximumFrameBytes);

        return nullptr;
    }

    std::unique_ptr<FImageData> imageData;

    try
    {
        imageData = std::make_unique<FImageData>();
        imageData->SetSize(
            static_cast<int32_t>(processed->width),
            static_cast<int32_t>(processed->height));
        imageData->SetFormat(EImageFormat::RGBA8);
        imageData->SetStride(
            static_cast<int32_t>(processed->width * kRgbaChannelCount));
        imageData->AllocatePixelData(rgbaBytes);
    }
    catch (const std::bad_alloc&)
    {
        SetImageLoadError(OutError, EImageLoadError::OutOfMemory);
        LOGE("DNG", "Out of memory while allocating %zu-byte RGBA frame", rgbaBytes);
        return nullptr;
    }
    catch (const std::length_error&)
    {
        SetImageLoadError(OutError, EImageLoadError::ResourceLimit);
        LOGE("DNG", "Allocation size rejected for %zu-byte RGBA frame", rgbaBytes);
        return nullptr;
    }

    const uint8_t* source = processed->data;
    uint8_t* destination = imageData->GetPixelData();

    for (size_t pixel = 0; pixel < pixelCount; ++pixel)
    {
        const uint8_t* sourcePixel = source + pixel * sourcePixelBytes;
        uint8_t* destinationPixel = destination + pixel * kRgbaChannelCount;

        if (processed->colors == kGrayscaleChannelCount)
        {
            const uint8_t gray = ToByte(sourcePixel, processed->bits);
            destinationPixel[0] = gray;
            destinationPixel[1] = gray;
            destinationPixel[2] = gray;
        }
        else
        {
            for (int32_t channel = 0; channel < kRgbChannelCount; ++channel)
            {
                destinationPixel[channel] =
                    ToByte(sourcePixel + static_cast<size_t>(channel) * bytesPerSample,
                           processed->bits);
            }
        }

        destinationPixel[3] = kOpaqueAlpha;
    }

    LOGD("DNG", "Loaded DNG through LibRaw as RGBA8, %ux%u, SourceBits: %u",
         processed->width, processed->height, processed->bits);

    SetImageLoadError(OutError, EImageLoadError::None);
    return imageData;
}
catch (const std::bad_alloc&)
{
    SetImageLoadError(OutError, EImageLoadError::OutOfMemory);
    LOGE("DNG", "%s", "Out of memory while creating the decoder or loading metadata");
    return nullptr;
}
catch (const std::length_error&)
{
    SetImageLoadError(OutError, EImageLoadError::ResourceLimit);
    LOGE("DNG", "%s", "Decoder or metadata allocation exceeds container limits");
    return nullptr;
}

bool FDngImageLoader::SupportsFormat(const std::string& FilePath) const
{
    std::string extension = std::filesystem::u8path(FilePath).extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });

    return extension == ".dng";
}

bool FDngImageLoader::SupportsFormat(EImageFormat /*Format*/) const
{
    return false;
}

std::vector<std::string> FDngImageLoader::GetSupportedExtensions() const
{
    return { ".dng" };
}
