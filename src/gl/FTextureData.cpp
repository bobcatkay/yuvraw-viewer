#include "FTextureData.h"
#include "FTexture.h"
#include "FSparseTexture.h"
#include "Image/FImageFormatDesc.h"
#include "Image/FImageLimits.h"
#include "Util.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <new>
#include <stdexcept>

namespace
{
    constexpr int32_t kTileCoreDimension = 2048;
    // 两侧保留采样邻域：线性过滤和 Bayer 插值必须能读取相邻块的原始像素。
    constexpr int32_t kTileHaloTexels = 2;
    constexpr size_t kSparseResidentBudget = 256u * FImageLimits::kUnitsPerMebi;
    constexpr size_t kSparseUploadBudgetPerDraw = 16u * FImageLimits::kUnitsPerMebi;
    constexpr int32_t kPreviewMaximumDimension = 4096;

    // 只有实际尝试稀疏路径时才生成预览。普通上传失败后的回退也可能进入这里，
    // 因此显式释放 Context，避免 Intel 驱动在长时间 CPU 缩采样期间阻塞主窗口。
    class FScopedCpuTexturePreparation
    {
    public:
        FScopedCpuTexturePreparation() : Context(glfwGetCurrentContext())
        {
            if (Context) glfwMakeContextCurrent(nullptr);
        }
        ~FScopedCpuTexturePreparation()
        {
            if (Context) glfwMakeContextCurrent(Context);
        }
    private:
        GLFWwindow* Context;
    };

    /**
     * 由图像格式及 (分量数, 每分量字节数) 决定 OpenGL 的三元组
     *
     * 16bit 一律用**归一化**的 GL_R16 / GL_RG16 / GL_RGB16 / GL_RGBA16，
     * 而不是整数纹理 GL_R16UI —— 整数纹理必须配 usampler2D 采样，
     * 用 sampler2D 是未定义行为，多数驱动直接返回 0。
     * RGB10_A2 则使用规范规定的 GL_RGB10_A2 + packed uint32 上传组合。
     *
     * @return 组合不受支持时返回 false
     */
    bool ResolveGLFormat(
        EImageFormat ImageFormat,
        int32_t ChannelCount,
        int32_t BytesPerSample,
        GLint& OutInternalFormat,
        GLint& OutFormat,
        GLenum& OutType)
    {
        if (ImageFormat == EImageFormat::RGB10A2)
        {
            // _REV 使第一个 RGBA 分量从最低有效位开始，与 Android RGBA_1010102
            // 的小端内存布局一致；非 REV 会把 R 放到高位，导致通道整体错位。
            OutInternalFormat = GL_RGB10_A2;
            OutFormat = GL_RGBA;
            OutType = GL_UNSIGNED_INT_2_10_10_10_REV;

            return true;
        }

        if (BytesPerSample != 1 && BytesPerSample != 2)
        {
            return false;
        }

        const bool b16 = (BytesPerSample == 2);

        OutType = b16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;

        switch (ChannelCount)
        {
        case 1:
            OutInternalFormat = b16 ? GL_R16 : GL_R8;
            OutFormat = GL_RED;
            return true;

        case 2:
            OutInternalFormat = b16 ? GL_RG16 : GL_RG8;
            OutFormat = GL_RG;
            return true;

        case 3:
            OutInternalFormat = b16 ? GL_RGB16 : GL_RGB8;
            OutFormat = GL_RGB;
            return true;

        case 4:
            OutInternalFormat = b16 ? GL_RGBA16 : GL_RGBA8;
            OutFormat = GL_RGBA;
            return true;

        default:
            return false;
        }
    }

    /**
     * 准备一个平面的纹理上传数据。
     *
     * GL_UNPACK_ROW_LENGTH 的单位是完整纹素，无法表示 RGB16 stride=有效字节+2
     * 这类非整纹素 padding。遇到这种布局时逐行复制有效区域到紧凑缓冲，避免第二行
     * 从 padding 中间开始；能整除时仍走零拷贝的 GL_UNPACK_ROW_LENGTH 路径。
     */
    bool PreparePlaneUpload(
        const uint8_t* PlaneData,
        size_t PlaneSize,
        int32_t PlaneWidth,
        int32_t PlaneHeight,
        int32_t PlaneStride,
        int32_t BytesPerTexel,
        std::vector<uint8_t>& Scratch,
        const void*& OutData,
        int32_t& OutRowLength,
        bool& bOutRepacked)
    {
        if (!PlaneData ||
            PlaneWidth <= 0 ||
            PlaneHeight <= 0 ||
            PlaneStride <= 0 ||
            BytesPerTexel <= 0)
        {
            return false;
        }

        const int32_t activeRowBytes =
            PlaneWidth * BytesPerTexel;

        if (activeRowBytes <= 0 ||
            activeRowBytes > PlaneStride)
        {
            return false;
        }

        if ((PlaneStride % BytesPerTexel) == 0)
        {
            const int32_t rowLength =
                PlaneStride / BytesPerTexel;

            if (rowLength < PlaneWidth)
            {
                return false;
            }

            Scratch.clear();
            OutData = PlaneData;
            OutRowLength =
                rowLength > PlaneWidth ? rowLength : 0;
            bOutRepacked = false;

            return true;
        }

        size_t tightSize = 0;

        if (!FImageLimits::TryMultiplySize(
                static_cast<size_t>(activeRowBytes),
                static_cast<size_t>(PlaneHeight),
                tightSize) ||
            tightSize == 0 ||
            PlaneSize <
                static_cast<size_t>(PlaneStride) *
                    static_cast<size_t>(PlaneHeight))
        {
            return false;
        }

        try
        {
            Scratch.resize(tightSize);
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        catch (const std::length_error&)
        {
            return false;
        }

        for (int32_t row = 0; row < PlaneHeight; ++row)
        {
            std::memcpy(
                Scratch.data() +
                    static_cast<size_t>(row) *
                        static_cast<size_t>(activeRowBytes),
                PlaneData +
                    static_cast<size_t>(row) *
                        static_cast<size_t>(PlaneStride),
                static_cast<size_t>(activeRowBytes));
        }

        OutData = Scratch.data();
        OutRowLength = 0;
        bOutRepacked = true;

        return true;
    }
}

// GL 类型留在实现文件，避免 FImageDocument 等公共头间接改变 Windows/GLAD 的包含顺序。
class FTextureData::FSparseDrawState
{
public:
    GLsync UseFence = nullptr;
    GLFWwindow* UseContext = nullptr;
    FTextureCreateError Error;
    int64_t FirstDetailCpuMicroseconds = 0;
    size_t FirstDetailUploadedBytes = 0;
    bool bFirstDetailReady = false;
    bool bFirstDetailLogged = false;
};

class FTextureData::FTiledDrawState
{
public:
    struct FPlane
    {
        int32_t OriginX = 0, OriginY = 0;
        std::unique_ptr<FTexture> Texture;
    };
    struct FTile
    {
        FTextureViewRegion Region;
        std::vector<FPlane> Planes;
    };
    std::vector<FTile> Tiles;
    FTextureViewRegion VisibleRegion;
};

FTextureData::FTextureData()
    : Format(EImageFormat::Unknown)
    , Width(0)
    , Height(0)
    , Stride(0)
{
}

FTextureData::~FTextureData()
{
    Destroy();
}

bool FTextureData::PrepareSparsePreview(const FImageData* ImageData, const std::function<bool()>& ShouldContinue)
{
    PreparedPreviews.clear();
    PreparedImage = nullptr;
    if (!ImageData || !ImageData->IsValid())
        return false;
    const FFormatDesc& desc = FImageFormatDesc::Get(ImageData->GetFormat());
    // CFA、打包 YUV 与 RGB10_A2 不能按独立 8/16 位分量做 box average。
    if (desc.PlaneCount <= 0 || desc.bIsPacked || desc.ColorModel == EColorModel::Bayer ||
        ImageData->GetFormat() == EImageFormat::RGB10A2)
        return false;
    const auto start = std::chrono::steady_clock::now();
    std::vector<FPreviewPlane> previews(static_cast<size_t>(desc.PlaneCount));
    for (int32_t i = 0; i < desc.PlaneCount; ++i)
    {
        const FPlaneDesc& plane = desc.Planes[i];
        if (plane.BytesPerSample != 1 && plane.BytesPerSample != static_cast<int32_t>(sizeof(uint16_t)))
            return false;
        const int32_t width = FImageFormatDesc::GetPlaneWidth(desc, i, ImageData->GetWidth());
        const int32_t height = FImageFormatDesc::GetPlaneHeight(desc, i, ImageData->GetHeight());
        const int32_t stride = FImageFormatDesc::GetPlaneStrideBytes(desc, i, ImageData->GetStride());
        const size_t offset = FImageFormatDesc::GetPlaneOffsetBytes(desc, i, ImageData->GetHeight(), ImageData->GetStride());
        const size_t planeBytes = FImageFormatDesc::GetPlaneSizeBytes(desc, i, ImageData->GetHeight(), ImageData->GetStride());
        const int32_t texelBytes = plane.ChannelCount * plane.BytesPerSample;
        if (width <= 0 || height <= 0 || stride < width * texelBytes || planeBytes == 0 ||
            offset > ImageData->GetPixelDataSize() || planeBytes > ImageData->GetPixelDataSize() - offset)
            return false;
        int32_t divisor = 2;
        while ((std::max(width, height) + divisor - 1) / divisor > kPreviewMaximumDimension)
            divisor *= 2;
        FPreviewPlane& preview = previews[static_cast<size_t>(i)];
        preview.Width = (width + divisor - 1) / divisor;
        preview.Height = (height + divisor - 1) / divisor;
        preview.Stride = preview.Width * texelBytes;
        preview.Pixels.resize(static_cast<size_t>(preview.Stride) * preview.Height);
        const uint8_t* source = ImageData->GetPixelData() + offset;
        for (int32_t y = 0; y < preview.Height; ++y)
        {
            if (ShouldContinue && !ShouldContinue()) return false;
            // 用均匀覆盖原图的整数区间，保证奇数尺寸不会丢掉最后一行/列。
            const int32_t top = static_cast<int32_t>(static_cast<int64_t>(y) * height / preview.Height);
            const int32_t bottom = static_cast<int32_t>(static_cast<int64_t>(y + 1) * height / preview.Height);
            for (int32_t x = 0; x < preview.Width; ++x)
            {
                const int32_t left = static_cast<int32_t>(static_cast<int64_t>(x) * width / preview.Width);
                const int32_t right = static_cast<int32_t>(static_cast<int64_t>(x + 1) * width / preview.Width);
                const uint64_t samples = static_cast<uint64_t>(right - left) * (bottom - top);
                for (int32_t c = 0; c < plane.ChannelCount; ++c)
                {
                    uint64_t sum = 0;
                    for (int32_t sy = top; sy < bottom; ++sy)
                    {
                        for (int32_t sx = left; sx < right; ++sx)
                        {
                            const uint8_t* sample = source + static_cast<size_t>(sy) * stride
                                + static_cast<size_t>(sx) * texelBytes + c * plane.BytesPerSample;
                            uint16_t value = *sample;
                            if (plane.BytesPerSample == static_cast<int32_t>(sizeof(uint16_t)))
                                std::memcpy(&value, sample, sizeof(value));
                            sum += value;
                        }
                    }
                    const uint16_t average = static_cast<uint16_t>((sum + samples / 2) / samples);
                    uint8_t* destination = preview.Pixels.data() + static_cast<size_t>(y) * preview.Stride
                        + static_cast<size_t>(x) * texelBytes + c * plane.BytesPerSample;
                    if (plane.BytesPerSample == 1) *destination = static_cast<uint8_t>(average);
                    else std::memcpy(destination, &average, sizeof(average));
                }
            }
        }
    }
    PreparedPreviews = std::move(previews);
    PreparedImage = ImageData;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    LOGD("SparseTexture", "CPU preview prepared for %s %dx%d in %lld ms", desc.Name,
        ImageData->GetWidth(), ImageData->GetHeight(), static_cast<long long>(elapsed));
    return true;
}

bool FTextureData::TryCreateSparseTextures(const std::vector<FPreviewPlane>& Previews)
{
    const FFormatDesc& desc = FImageFormatDesc::Get(Format);
    if (Previews.size() != static_cast<size_t>(desc.PlaneCount) || !FSparseTexture::IsSupported()) return false;
    auto sparseState = std::make_unique<FSparseDrawState>();
    std::vector<std::unique_ptr<FTexture>> textures;
    std::vector<std::unique_ptr<FSparseTexture>> sparseTextures;
    for (int32_t i = 0; i < desc.PlaneCount; ++i)
    {
        const FPlaneDesc& plane = desc.Planes[i];
        GLint internalFormat = GL_R8, format = GL_RED;
        GLenum type = GL_UNSIGNED_BYTE;
        if (!ResolveGLFormat(Format, plane.ChannelCount, plane.BytesPerSample, internalFormat, format, type))
            return false;
        auto sparse = std::make_unique<FSparseTexture>();
        if (!sparse->Create(FImageFormatDesc::GetPlaneWidth(desc, i, Width),
            FImageFormatDesc::GetPlaneHeight(desc, i, Height), internalFormat, format, type,
            plane.ChannelCount * plane.BytesPerSample))
        {
            LOGW("TextureLoad", "Sparse storage unavailable for %s plane %d", desc.Name, i);
            return false;
        }
        auto texture = std::make_unique<FTexture>();
        const FPreviewPlane& preview = Previews[static_cast<size_t>(i)];
        if (!texture->Create(preview.Width, preview.Height, preview.Pixels.data(), internalFormat, format, type))
            return false;
        textures.push_back(std::move(texture));
        sparseTextures.push_back(std::move(sparse));
    }
    Textures = std::move(textures);
    SparseTextures = std::move(sparseTextures);
    SparseDrawState = std::move(sparseState);
    LOGI("SparseTexture", "Using sparse textures for %s %dx%d; resident budget=%zu MiB upload budget=%zu MiB/draw",
        desc.Name, Width, Height, kSparseResidentBudget / FImageLimits::kUnitsPerMebi,
        kSparseUploadBudgetPerDraw / FImageLimits::kUnitsPerMebi);
    return true;
}

bool FTextureData::CreateFromImageData(const FImageData* ImageData, FTextureCreateError* OutError,
    FTextureLoadOptions Options, const std::function<bool()>& ShouldContinue)
{
    if (OutError) *OutError = {};
    if (!ImageData || !ImageData->IsValid())
    {
        if (OutError) OutError->Type = ETextureCreateError::InvalidDimensions;
        return false;
    }
    Options.Normalize();
    const auto start = std::chrono::steady_clock::now();
    std::vector<FPreviewPlane> previews;
    if (PreparedImage == ImageData) previews = std::move(PreparedPreviews);
    enum class EAttempt { Whole, Sparse, Tiled };
    const bool preferSparse = Options.PrefersSparse(ImageData->GetWidth(), ImageData->GetHeight());
    std::vector<EAttempt> attempts;
    if (!preferSparse) attempts.push_back(EAttempt::Whole);
    if (Options.bEnableSparseTextures) attempts.push_back(EAttempt::Sparse);
    attempts.push_back(EAttempt::Tiled);
    LOGI("TextureLoad", "Begin: format=%s size=%dx%d decodedBytes=%zu sparseEnabled=%d threshold=%d order=%s",
        FImageFormatDesc::Get(ImageData->GetFormat()).Name, ImageData->GetWidth(), ImageData->GetHeight(),
        ImageData->GetPixelDataSize(), Options.bEnableSparseTextures, Options.SparseDimensionThreshold,
        preferSparse ? "sparse->tiled" : (Options.bEnableSparseTextures ? "whole->sparse->tiled" : "whole->tiled"));
    int64_t previewMilliseconds = 0;
    FTextureCreateError error;
    for (const EAttempt attempt : attempts)
    {
        Destroy();
        if (ShouldContinue && !ShouldContinue())
        {
            LOGI("TextureLoad", "Cancelled before upload attempt");
            return false;
        }
        Format = ImageData->GetFormat();
        Width = ImageData->GetWidth();
        Height = ImageData->GetHeight();
        Stride = ImageData->GetStride();
        error = { ETextureCreateError::OpenGlFailure, Width, Height };
        const char* name = attempt == EAttempt::Whole ? "whole" : attempt == EAttempt::Sparse ? "sparse" : "tiled";
        const auto attemptStart = std::chrono::steady_clock::now();
        bool success = false;
        const char* reason = "creation failed (see preceding GL diagnostics)";
        try
        {
            if (attempt == EAttempt::Whole) success = TryCreateWholeTextures(ImageData, &error);
            else if (attempt == EAttempt::Tiled) success = TryCreateTiledTextures(ImageData, &error, ShouldContinue);
            else if (!FSparseTexture::IsSupported()) reason = "ARB_sparse_texture or required entry points unavailable";
            else
            {
                if (previews.empty())
                {
                    const auto previewStart = std::chrono::steady_clock::now();
                    {
                        const FScopedCpuTexturePreparation cpuPreparation;
                        PrepareSparsePreview(ImageData, ShouldContinue);
                    }
                    previewMilliseconds += std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - previewStart).count();
                    previews = std::move(PreparedPreviews);
                    PreparedImage = nullptr;
                }
                if (previews.empty()) reason = "preview unsupported for format, invalid layout, or cancelled";
                else success = TryCreateSparseTextures(previews);
                previews.clear();
            }
        }
        catch (const std::bad_alloc&)
        {
            error = { ETextureCreateError::OutOfMemory, ImageData->GetWidth(), ImageData->GetHeight() };
            reason = "CPU allocation failed";
        }
        catch (const std::exception& exception)
        {
            LOGW("TextureLoad", "Attempt=%s exception=%s", name, exception.what());
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - attemptStart).count();
        if (success)
        {
            PreviewPreparationMilliseconds = previewMilliseconds;
            LOGI("TextureLoad", "Complete: backend=%s attemptMs=%lld prepareAndSubmitMs=%lld inlinePreviewCpuMs=%lld draws=%u (GPU completion tracked by loader fence)",
                GetBackendName(), static_cast<long long>(elapsed), static_cast<long long>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()),
                static_cast<long long>(previewMilliseconds), GetDrawCount());
            if (OutError) *OutError = {};
            return true;
        }
        if (error.Type == ETextureCreateError::OpenGlFailure && error.OpenGlError == GL_NO_ERROR)
        {
            // 能力/格式跳过没有 GL 错误码，不输出误导性的“OpenGL error 0”。
            LOGW("TextureLoad", "Attempt=%s failed after %lld ms: %s", name, static_cast<long long>(elapsed), reason);
        }
        else
        {
            LOGW("TextureLoad", "Attempt=%s failed after %lld ms: %s; detail=%s", name,
                static_cast<long long>(elapsed), reason, error.GetText().c_str());
        }
    }
    Destroy();
    PreviewPreparationMilliseconds = previewMilliseconds;
    if (OutError) *OutError = error;
    LOGE("TextureLoad", "All upload paths failed for %dx%d", ImageData->GetWidth(), ImageData->GetHeight());
    return false;
}

bool FTextureData::TryCreateWholeTextures(const FImageData* ImageData, FTextureCreateError* OutError)
{
    const FFormatDesc& desc = FImageFormatDesc::Get(Format);
    if (desc.PlaneCount <= 0) return false;
    // 先检查全部平面，防止上传到后续平面才发现超限，或先为不可显示的大图分配重排缓冲。
    for (int32_t i = 0; i < desc.PlaneCount; ++i)
    {
        const int32_t planeWidth = FImageFormatDesc::GetPlaneWidth(desc, i, Width);
        const int32_t planeHeight = FImageFormatDesc::GetPlaneHeight(desc, i, Height);
        if (!FTexture::ValidateDimensions(planeWidth, planeHeight, OutError))
        {
            LOGE("CreateFromImageData", "GPU geometry rejected: %s %dx%d, plane=%d (%dx%d)",
                desc.Name, Width, Height, i, planeWidth, planeHeight);
            Destroy();
            return false;
        }
    }

    const int32_t baseStride = Stride;
    const uint8_t* data = ImageData->GetPixelData();
    const size_t dataSize = ImageData->GetPixelDataSize();
    UploadScratchBuffers.resize(
        static_cast<size_t>(desc.PlaneCount));

    for (int32_t i = 0; i < desc.PlaneCount; ++i)
    {
        const FPlaneDesc& plane = desc.Planes[i];

        GLint internalFormat = GL_RED;
        GLint glFormat = GL_RED;
        GLenum glType = GL_UNSIGNED_BYTE;

        if (!ResolveGLFormat(Format, plane.ChannelCount, plane.BytesPerSample, internalFormat, glFormat, glType))
        {
            LOGE("CreateFromImageData", "Unsupported plane layout, Format: %s, Plane: %d, Channels: %d, Bytes: %d",
                 desc.Name, i, plane.ChannelCount, plane.BytesPerSample);
            Destroy();

            return false;
        }

        const int32_t planeWidth = FImageFormatDesc::GetPlaneWidth(desc, i, Width);
        const int32_t planeHeight = FImageFormatDesc::GetPlaneHeight(desc, i, Height);
        const int32_t planeStride =
            FImageFormatDesc::GetPlaneStrideBytes(
                desc,
                i,
                baseStride);
        const size_t planeOffset = FImageFormatDesc::GetPlaneOffsetBytes(desc, i, Height, baseStride);
        const size_t planeSize = FImageFormatDesc::GetPlaneSizeBytes(desc, i, Height, baseStride);

        if (planeOffset + planeSize > dataSize)
        {
            LOGE("CreateFromImageData", "Plane %d exceeds buffer, Offset: %zu, Size: %zu, Buffer: %zu",
                 i, planeOffset, planeSize, dataSize);
            Destroy();

            return false;
        }

        const int32_t bytesPerTexel =
            plane.ChannelCount * plane.BytesPerSample;
        const void* uploadData = nullptr;
        int32_t rowLength = 0;
        bool bRepacked = false;

        if (!PreparePlaneUpload(
                data + planeOffset,
                planeSize,
                planeWidth,
                planeHeight,
                planeStride,
                bytesPerTexel,
                UploadScratchBuffers[static_cast<size_t>(i)],
                uploadData,
                rowLength,
                bRepacked))
        {
            LOGE(
                "CreateFromImageData",
                "Invalid upload layout for plane %d of %s, Width: %d, Height: %d, Stride: %d, BytesPerTexel: %d",
                i,
                desc.Name,
                planeWidth,
                planeHeight,
                planeStride,
                bytesPerTexel);
            Destroy();

            return false;
        }

        if (bRepacked)
        {
            LOGD(
                "CreateFromImageData",
                "Repacked plane %d of %s from %d-byte rows to %d-byte rows for OpenGL upload",
                i,
                desc.Name,
                planeStride,
                planeWidth * bytesPerTexel);
        }

        auto texture = std::make_unique<FTexture>();

        if (!texture->Create(
                planeWidth,
                planeHeight,
                uploadData,
                internalFormat,
                glFormat,
                glType,
                rowLength,
                OutError))
        {
            LOGE("CreateFromImageData", "Failed to create texture for plane %d of %s", i, desc.Name);
            Destroy();

            return false;
        }

        Textures.push_back(std::move(texture));
    }

    LOGD("CreateFromImageData", "Created %zu textures for %s, %dx%d, stride=%d",
         Textures.size(), desc.Name, Width, Height, baseStride);

    return true;
}

bool FTextureData::TryCreateTiledTextures(const FImageData* ImageData, FTextureCreateError* OutError,
    const std::function<bool()>& ShouldContinue)
{
    GLint maximumDimension = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumDimension);
    if (maximumDimension <= 2 * kTileHaloTexels)
    {
        *OutError = { ETextureCreateError::LimitUnavailable, Width, Height, maximumDimension };
        return false;
    }
    const int32_t coreSize = std::min(kTileCoreDimension, maximumDimension - 2 * kTileHaloTexels);
    const FFormatDesc& desc = FImageFormatDesc::Get(Format);
    if (desc.PlaneCount <= 0) return false;
    auto state = std::make_unique<FTiledDrawState>();
    std::vector<uint8_t> scratch;
    size_t uploadedBytes = 0;
    for (int32_t y = 0; y < Height; y += coreSize)
    {
        for (int32_t x = 0; x < Width; x += coreSize)
        {
            if (ShouldContinue && !ShouldContinue()) return false;
            const int32_t right = std::min(x + coreSize, Width);
            const int32_t bottom = std::min(y + coreSize, Height);
            FTiledDrawState::FTile tile;
            tile.Region = { static_cast<float>(x) / Width, static_cast<float>(y) / Height,
                static_cast<float>(right) / Width, static_cast<float>(bottom) / Height };
            for (int32_t i = 0; i < desc.PlaneCount; ++i)
            {
                const auto& plane = desc.Planes[i];
                const int32_t planeWidth = FImageFormatDesc::GetPlaneWidth(desc, i, Width);
                const int32_t planeHeight = FImageFormatDesc::GetPlaneHeight(desc, i, Height);
                const int32_t planeStride = FImageFormatDesc::GetPlaneStrideBytes(desc, i, Stride);
                const size_t offset = FImageFormatDesc::GetPlaneOffsetBytes(desc, i, Height, Stride);
                const size_t bytes = FImageFormatDesc::GetPlaneSizeBytes(desc, i, Height, Stride);
                const int32_t texelBytes = plane.ChannelCount * plane.BytesPerSample;
                if (planeWidth <= 0 || planeHeight <= 0 || texelBytes <= 0 ||
                    planeStride < planeWidth * texelBytes || offset > ImageData->GetPixelDataSize() ||
                    bytes > ImageData->GetPixelDataSize() - offset ||
                    bytes < static_cast<size_t>(planeStride) * planeHeight) return false;
                // 归一化坐标与整图采样一致，奇数宽高的色度平面不能直接按整数移位截断边界。
                const int32_t left = std::max(0, static_cast<int32_t>(static_cast<int64_t>(x) * planeWidth / Width) - kTileHaloTexels);
                const int32_t top = std::max(0, static_cast<int32_t>(static_cast<int64_t>(y) * planeHeight / Height) - kTileHaloTexels);
                const int32_t endX = std::min(planeWidth, static_cast<int32_t>(
                    (static_cast<int64_t>(right) * planeWidth + Width - 1) / Width) + kTileHaloTexels);
                const int32_t endY = std::min(planeHeight, static_cast<int32_t>(
                    (static_cast<int64_t>(bottom) * planeHeight + Height - 1) / Height) + kTileHaloTexels);
                const int32_t tileWidth = endX - left, tileHeight = endY - top;
                const size_t rowBytes = static_cast<size_t>(tileWidth) * texelBytes;
                scratch.resize(rowBytes * tileHeight);
                for (int32_t row = 0; row < tileHeight; ++row)
                {
                    std::memcpy(scratch.data() + static_cast<size_t>(row) * rowBytes,
                        ImageData->GetPixelData() + offset + static_cast<size_t>(top + row) * planeStride
                            + static_cast<size_t>(left) * texelBytes, rowBytes);
                }
                GLint internalFormat = GL_R8, format = GL_RED;
                GLenum type = GL_UNSIGNED_BYTE;
                if (!ResolveGLFormat(Format, plane.ChannelCount, plane.BytesPerSample, internalFormat, format, type)) return false;
                FTiledDrawState::FPlane storage;
                storage.OriginX = left;
                storage.OriginY = top;
                storage.Texture = std::make_unique<FTexture>();
                if (!storage.Texture->Create(tileWidth, tileHeight, scratch.data(), internalFormat, format, type, 0, OutError))
                {
                    LOGW("TextureLoad", "Tile upload failed: imageOrigin=(%d,%d) plane=%d tile=%dx%d", x, y, i, tileWidth, tileHeight);
                    return false;
                }
                uploadedBytes += scratch.size();
                tile.Planes.push_back(std::move(storage));
            }
            state->Tiles.push_back(std::move(tile));
        }
    }
    LOGI("TextureLoad", "Tiled upload ready: tiles=%zu planes=%d coreEdge=%d halo=%d uploadedBytes=%zu maxTextureSize=%d",
        state->Tiles.size(), desc.PlaneCount, coreSize, kTileHaloTexels, uploadedBytes, maximumDimension);
    TiledDrawState = std::move(state);
    return true;
}

bool FTextureData::UpdateFromImageData(const FImageData* ImageData)
{
    // 预览、稀疏页和分块都基于上一份像素，只更新一个普通纹理不能完成整图刷新。
    if (HasSparseTextures() || HasTiledTextures()) return false;
    if (!ImageData || !ImageData->IsValid())
    {
        LOGE("UpdateFromImageData", "Invalid image data");

        return false;
    }

    // 格式、分辨率、行跨距三者都得对上才能复用。
    // stride 也必须比 —— 它同时决定 GL_UNPACK_ROW_LENGTH 与是否需要逐行重排。
    // 只改 stride（1440x1920 从紧凑改成 1472）时宽高格式全都没变，
    // 漏掉这一项就会沿用旧上传布局，画面逐行斜切。
    if (ImageData->GetFormat() != Format ||
        ImageData->GetWidth() != Width ||
        ImageData->GetHeight() != Height ||
        ImageData->GetStride() != Stride)
    {
        return false;
    }

    const FFormatDesc& desc = FImageFormatDesc::Get(Format);

    if (static_cast<int32_t>(Textures.size()) != desc.PlaneCount ||
        static_cast<int32_t>(UploadScratchBuffers.size()) !=
            desc.PlaneCount)
    {
        return false;
    }

    const int32_t baseStride = Stride;
    const uint8_t* data = ImageData->GetPixelData();
    const size_t dataSize = ImageData->GetPixelDataSize();

    for (int32_t i = 0; i < desc.PlaneCount; ++i)
    {
        if (!Textures[i])
        {
            return false;
        }

        const size_t planeOffset = FImageFormatDesc::GetPlaneOffsetBytes(desc, i, Height, baseStride);
        const size_t planeSize = FImageFormatDesc::GetPlaneSizeBytes(desc, i, Height, baseStride);

        if (planeOffset + planeSize > dataSize)
        {
            return false;
        }

        const FPlaneDesc& plane = desc.Planes[i];
        const int32_t planeWidth =
            FImageFormatDesc::GetPlaneWidth(
                desc,
                i,
                Width);
        const int32_t planeHeight =
            FImageFormatDesc::GetPlaneHeight(
                desc,
                i,
                Height);
        const int32_t planeStride =
            FImageFormatDesc::GetPlaneStrideBytes(
                desc,
                i,
                baseStride);
        const int32_t bytesPerTexel =
            plane.ChannelCount * plane.BytesPerSample;
        const void* uploadData = nullptr;
        int32_t rowLength = 0;
        bool bRepacked = false;

        if (!PreparePlaneUpload(
                data + planeOffset,
                planeSize,
                planeWidth,
                planeHeight,
                planeStride,
                bytesPerTexel,
                UploadScratchBuffers[static_cast<size_t>(i)],
                uploadData,
                rowLength,
                bRepacked))
        {
            return false;
        }

        // stride 与格式未变，因此 Create() 时记录的 RowLength 必然与本次路径一致。
        Textures[i]->UpdateData(uploadData);
    }

    return true;
}

void FTextureData::BindTextures(uint32_t StartTextureUnit, uint32_t DrawIndex) const
{
    if (HasTiledTextures())
    {
        if (DrawIndex >= TiledDrawState->Tiles.size()) return;
        const auto& planes = TiledDrawState->Tiles[DrawIndex].Planes;
        for (size_t i = 0; i < planes.size(); ++i)
        {
            planes[i].Texture->Bind(StartTextureUnit + static_cast<uint32_t>(i));
            planes[i].Texture->SetMagFilterNearest(bMagNearest);
        }
        return;
    }
    for (size_t i = 0; i < Textures.size(); ++i)
    {
        if (bUseSparseDetail)
        {
            SparseTextures[i]->Bind(StartTextureUnit + static_cast<uint32_t>(i), bMagNearest);
            continue;
        }
        if (Textures[i] && Textures[i]->IsValid())
        {
            Textures[i]->Bind(StartTextureUnit + static_cast<uint32_t>(i));
        }
    }
}

void FTextureData::UnbindTextures() const
{
    if (HasTiledTextures())
    {
        for (uint32_t i = 0; i < GetTextureCount(); ++i)
        {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        return;
    }
    for (const auto& texture : Textures)
    {
        if (texture)
        {
            texture->Unbind();
        }
    }
}

void FTextureData::SetMagFilterNearest(bool bNearest) const
{
    bMagNearest = bNearest;
    for (const auto& texture : Textures)
    {
        if (texture)
        {
            texture->SetMagFilterNearest(bNearest);
        }
    }
}

FTexture* FTextureData::GetTexture(uint32_t Index) const
{
    if (HasTiledTextures())
    {
        const auto& planes = TiledDrawState->Tiles.front().Planes;
        return Index < planes.size() ? planes[Index].Texture.get() : nullptr;
    }
    if (Index >= Textures.size())
    {
        return nullptr;
    }

    return Textures[Index].get();
}

void FTextureData::PrepareForDraw(const FImageData* ImageData, const FTextureViewRegion& Region) noexcept
{
    if (HasTiledTextures()) TiledDrawState->VisibleRegion = Region;
    bUseSparseDetail = false;
    if (!HasSparseTextures() || bSparseFailed || !ImageData) return;
    FSparseDrawState& state = *SparseDrawState;
    try
    {
        // 副视口可以在另一个 Context 读这些页。修改驻留状态前，先在 GPU 上等待上次使用完成。
        // glWaitSync 不阻塞 UI 线程；相同 Context 的命令本身有序。
        if (state.UseFence && state.UseContext != glfwGetCurrentContext())
        {
            while (glGetError() != GL_NO_ERROR) {}
            glWaitSync(state.UseFence, 0, GL_TIMEOUT_IGNORED);
            const GLenum error = glGetError();
            if (error != GL_NO_ERROR)
            {
                state.Error = { ETextureCreateError::OpenGlFailure, Width, Height, 0, error };
                bSparseFailed = true;
                return;
            }
        }
        if (state.UseFence)
        {
            glDeleteSync(state.UseFence);
            state.UseFence = nullptr;
        }
        const FFormatDesc& desc = FImageFormatDesc::Get(Format);
        if (!ImageData->IsValid() || ImageData->GetFormat() != Format ||
            ImageData->GetWidth() != Width || ImageData->GetHeight() != Height || ImageData->GetStride() != Stride)
        {
            state.Error = { ETextureCreateError::InvalidDimensions, Width, Height };
            bSparseFailed = true;
            return;
        }
        size_t needed = 0;
        for (const auto& sparse : SparseTextures)
        {
            const size_t planeBytes = sparse->EstimateResidentBytes(Region);
            // Win32 的虚拟存储估算也可能超过 size_t；超预算就停止累计，不允许回绕后误判可驻留。
            if (planeBytes > kSparseResidentBudget - needed) { needed = kSparseResidentBudget + 1; break; }
            needed += planeBytes;
        }
        if (needed == 0 || needed > kSparseResidentBudget)
        {
            // 缩小到全图时只需常驻预览，不能为一幅缩略显示的图提交整个原分辨率纹理。
            for (auto& sparse : SparseTextures)
            {
                if (!sparse->Evict(state.Error)) { bSparseFailed = true; break; }
            }
            return;
        }
        // 多平面必须先统一淘汰，再开始提交；否则新 Y 页与旧 UV 页会短暂突破总预算。
        for (auto& sparse : SparseTextures)
        {
            if (!sparse->Trim(Region, state.Error)) { bSparseFailed = true; return; }
        }
        const auto streamStart = std::chrono::steady_clock::now();
        size_t uploadBudget = kSparseUploadBudgetPerDraw;
        bool ready = true;
        for (size_t i = 0; i < SparseTextures.size(); ++i)
        {
            const int32_t plane = static_cast<int32_t>(i);
            const size_t offset = FImageFormatDesc::GetPlaneOffsetBytes(desc, plane, Height, Stride);
            const size_t bytes = FImageFormatDesc::GetPlaneSizeBytes(desc, plane, Height, Stride);
            if (offset > ImageData->GetPixelDataSize() || bytes > ImageData->GetPixelDataSize() - offset)
            {
                state.Error = { ETextureCreateError::InvalidDimensions, Width, Height };
                bSparseFailed = true;
                return;
            }
            const bool planeReady = SparseTextures[i]->Update(Region, ImageData->GetPixelData() + offset,
                FImageFormatDesc::GetPlaneStrideBytes(desc, plane, Stride), uploadBudget, state.Error);
            if (state.Error.Type != ETextureCreateError::None)
            {
                bSparseFailed = true;
                return;
            }
            ready = ready && planeReady;
        }
        bUseSparseDetail = ready;
        if (!state.bFirstDetailReady)
        {
            state.FirstDetailUploadedBytes += kSparseUploadBudgetPerDraw - uploadBudget;
            state.FirstDetailCpuMicroseconds += std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - streamStart).count();
            state.bFirstDetailReady = ready;
        }
    }
    catch (const std::bad_alloc&)
    {
        state.Error = { ETextureCreateError::OutOfMemory, Width, Height };
        bSparseFailed = true;
    }
    catch (...)
    {
        // 渲染回调不能向 ImGui/驱动栈抛异常，也不能逐帧重试失败的分配。
        bSparseFailed = true;
    }
}

void FTextureData::FinishDraw()
{
    if (!HasSparseTextures() || bSparseFailed) return;
    FSparseDrawState& state = *SparseDrawState;
    // 包括预览帧中的页面上传/淘汰；下一次换 Context 时也必须看见这些命令。
    if (state.UseFence) glDeleteSync(state.UseFence);
    state.UseFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    state.UseContext = glfwGetCurrentContext();
    if (!state.UseFence)
    {
        state.Error = { ETextureCreateError::OpenGlFailure, Width, Height, 0, glGetError() };
        bSparseFailed = true;
        bUseSparseDetail = false;
    }
    glFlush();
}

void FTextureData::GetTextureCoordinateScale(uint32_t Plane, float& OutX, float& OutY, uint32_t DrawIndex) const
{
    OutX = 1.0f;
    OutY = 1.0f;
    if (HasTiledTextures() && DrawIndex < TiledDrawState->Tiles.size())
    {
        const auto& planes = TiledDrawState->Tiles[DrawIndex].Planes;
        if (Plane < planes.size())
        {
            const auto& desc = FImageFormatDesc::Get(Format);
            OutX = static_cast<float>(FImageFormatDesc::GetPlaneWidth(desc, Plane, Width)) / planes[Plane].Texture->GetWidth();
            OutY = static_cast<float>(FImageFormatDesc::GetPlaneHeight(desc, Plane, Height)) / planes[Plane].Texture->GetHeight();
        }
        return;
    }
    if (bUseSparseDetail && Plane < SparseTextures.size())
    {
        OutX = SparseTextures[Plane]->GetScaleX();
        OutY = SparseTextures[Plane]->GetScaleY();
    }
}

bool FTextureData::HasTiledTextures() const
{
    return TiledDrawState && !TiledDrawState->Tiles.empty();
}

const char* FTextureData::GetBackendName() const
{
    if (HasSparseTextures()) return "sparse";
    if (HasTiledTextures()) return "tiled";
    return Textures.empty() ? "none" : "whole";
}

uint32_t FTextureData::GetTextureCount() const
{
    return static_cast<uint32_t>(HasTiledTextures() ? TiledDrawState->Tiles.front().Planes.size() : Textures.size());
}

uint32_t FTextureData::GetDrawCount() const
{
    return HasTiledTextures() ? static_cast<uint32_t>(TiledDrawState->Tiles.size()) : (Textures.empty() ? 0 : 1);
}

bool FTextureData::GetDrawRegion(uint32_t DrawIndex, FTextureViewRegion& OutRegion) const
{
    OutRegion = {};
    if (!HasTiledTextures()) return DrawIndex == 0;
    if (DrawIndex >= TiledDrawState->Tiles.size()) return false;
    OutRegion = TiledDrawState->Tiles[DrawIndex].Region;
    const auto& visible = TiledDrawState->VisibleRegion;
    return visible.MinU < visible.MaxU && visible.MinV < visible.MaxV &&
        OutRegion.MaxU > visible.MinU && OutRegion.MinU < visible.MaxU &&
        OutRegion.MaxV > visible.MinV && OutRegion.MinV < visible.MaxV;
}

void FTextureData::GetTextureTexelOrigin(uint32_t Plane, float& OutX, float& OutY, uint32_t DrawIndex) const
{
    OutX = OutY = 0.0f;
    if (!HasTiledTextures() || DrawIndex >= TiledDrawState->Tiles.size()) return;
    const auto& planes = TiledDrawState->Tiles[DrawIndex].Planes;
    if (Plane >= planes.size()) return;
    OutX = static_cast<float>(planes[Plane].OriginX);
    OutY = static_cast<float>(planes[Plane].OriginY);
}

void FTextureData::GetTextureCoordinateOffset(uint32_t Plane, float& OutX, float& OutY, uint32_t DrawIndex) const
{
    GetTextureTexelOrigin(Plane, OutX, OutY, DrawIndex);
    if (!HasTiledTextures() || Plane >= GetTextureCount()) return;
    const auto& desc = FImageFormatDesc::Get(Format);
    OutX /= FImageFormatDesc::GetPlaneWidth(desc, Plane, Width);
    OutY /= FImageFormatDesc::GetPlaneHeight(desc, Plane, Height);
}

void FTextureData::LogPendingDrawEvents()
{
    // 在 UI 构建阶段只输出一次首个细节区域的统计，渲染回调和拖动期间不写日志文件。
    if (SparseDrawState && SparseDrawState->bFirstDetailReady && !SparseDrawState->bFirstDetailLogged)
    {
        SparseDrawState->bFirstDetailLogged = true;
        LOGI("TextureLoad", "Sparse first detail ready: size=%dx%d uploadedBytes=%zu submitCpuUs=%lld",
            Width, Height, SparseDrawState->FirstDetailUploadedBytes,
            static_cast<long long>(SparseDrawState->FirstDetailCpuMicroseconds));
    }
    if (!bSparseFailed || bSparseFailureLogged) return;
    bSparseFailureLogged = true;
    LOGW("SparseTexture", "Detail streaming disabled for %dx%d; keeping preview: %s",
        Width, Height, SparseDrawState->Error.GetText().c_str());
}

bool FTextureData::IsValid() const
{
    if (HasTiledTextures()) return Format != EImageFormat::Unknown && Width > 0 && Height > 0;
    if (Textures.empty() || Format == EImageFormat::Unknown || Width <= 0 || Height <= 0)
    {
        return false;
    }

    for (const auto& texture : Textures)
    {
        if (!texture || !texture->IsValid())
        {
            return false;
        }
    }

    return true;
}

void FTextureData::Destroy()
{
    TiledDrawState.reset();
    PreviewPreparationMilliseconds = 0;
    if (SparseDrawState && SparseDrawState->UseFence)
    {
        glDeleteSync(SparseDrawState->UseFence);
    }
    SparseDrawState.reset();
    SparseTextures.clear();
    PreparedPreviews.clear();
    PreparedImage = nullptr;
    bUseSparseDetail = false;
    bSparseFailed = false;
    bSparseFailureLogged = false;
    for (auto& texture : Textures)
    {
        if (texture)
        {
            texture->Destroy();
        }
    }

    Textures.clear();
    UploadScratchBuffers.clear();
    Format = EImageFormat::Unknown;
    Width = 0;
    Height = 0;
    Stride = 0;
}
