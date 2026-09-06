#include "FTextureData.h"
#include "FTexture.h"
#include "Image/FImageFormatDesc.h"
#include "Image/FImageLimits.h"
#include "Util.h"

#include <glad/glad.h>
#include <cstring>
#include <new>
#include <stdexcept>

namespace
{
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
            OutInternalFormat = b16 ? GL_R16 : GL_RED;
            OutFormat = GL_RED;
            return true;

        case 2:
            OutInternalFormat = b16 ? GL_RG16 : GL_RG;
            OutFormat = GL_RG;
            return true;

        case 3:
            OutInternalFormat = b16 ? GL_RGB16 : GL_RGB;
            OutFormat = GL_RGB;
            return true;

        case 4:
            OutInternalFormat = b16 ? GL_RGBA16 : GL_RGBA;
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

bool FTextureData::CreateFromImageData(const FImageData* ImageData)
{
    if (!ImageData || !ImageData->IsValid())
    {
        LOGE("CreateFromImageData", "Invalid image data");

        return false;
    }

    // 清除旧的纹理
    Destroy();

    Format = ImageData->GetFormat();
    Width = ImageData->GetWidth();
    Height = ImageData->GetHeight();
    Stride = ImageData->GetStride();

    const FFormatDesc& desc = FImageFormatDesc::Get(Format);

    if (desc.PlaneCount <= 0)
    {
        LOGE("CreateFromImageData", "Format has no plane description: %s", desc.Name);
        Destroy();

        return false;
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
                rowLength))
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

bool FTextureData::UpdateFromImageData(const FImageData* ImageData)
{
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

void FTextureData::BindTextures(uint32_t StartTextureUnit) const
{
    for (size_t i = 0; i < Textures.size(); ++i)
    {
        if (Textures[i] && Textures[i]->IsValid())
        {
            Textures[i]->Bind(StartTextureUnit + static_cast<uint32_t>(i));
        }
    }
}

void FTextureData::UnbindTextures() const
{
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
    if (Index >= Textures.size())
    {
        return nullptr;
    }

    return Textures[Index].get();
}

bool FTextureData::IsValid() const
{
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
