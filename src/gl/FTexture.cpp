#include "FTexture.h"
#include "Core/FLocalization.h"
#include "Util.h"
#include <cstdio>
#include <cstring>

namespace
{
    constexpr const char* kTextureUploadLogTag = "TextureUpload";
    constexpr size_t kTextureErrorBufferSize = 384;

    void ClearPriorErrors()
    {
        // GL 错误属于 Context；先记录并清理调用前的错误，避免把其他操作的失败报成显存不足。
        const GLenum firstError = glGetError();
        if (firstError != GL_NO_ERROR)
        {
            while (glGetError() != GL_NO_ERROR) {}
            LOGW(kTextureUploadLogTag, "Cleared prior OpenGL errors, first: 0x%04X",
                static_cast<unsigned>(firstError));
        }
    }

    void SetCreateError(
        FTextureCreateError* OutError,
        ETextureCreateError Type,
        int32_t Width,
        int32_t Height,
        int32_t MaximumDimension = 0,
        GLenum OpenGlError = GL_NO_ERROR)
    {
        if (OutError)
        {
            *OutError = { Type, Width, Height, MaximumDimension, OpenGlError };
        }
    }

    /**
     * 在上传期间设置像素解包状态，析构时恢复为 GL 默认值。
     *
     * GL_UNPACK_ALIGNMENT 默认是 4：单通道 GL_RED 纹理在宽度不是 4 的倍数时，
     * GL 会按 4 字节对齐去取每一行，导致整幅图逐行斜切。必须显式设为 1。
     *
     * GL_UNPACK_ROW_LENGTH 让 GL 自己跳过行尾 padding，无需在 CPU 侧逐行 memcpy。
     */
    struct FScopedUnpackState
    {
        explicit FScopedUnpackState(int32_t InRowLength)
        {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, InRowLength > 0 ? InRowLength : 0);
        }

        ~FScopedUnpackState()
        {
            // 恢复 GL 默认值，避免影响 ImGui 后端自己的纹理上传
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        }
    };
}

std::string FTextureCreateError::GetText() const
{
    char buffer[kTextureErrorBufferSize] = {};
    switch (Type)
    {
    case ETextureCreateError::InvalidDimensions:
        std::snprintf(buffer, sizeof(buffer), FLocalization::Text(EUiText::TextureInvalidDimensions),
            Width, Height);
        break;
    case ETextureCreateError::DimensionLimit:
        std::snprintf(buffer, sizeof(buffer), FLocalization::Text(EUiText::TextureDimensionLimit),
            Width, Height, MaximumDimension);
        break;
    case ETextureCreateError::LimitUnavailable:
        return FLocalization::Text(EUiText::TextureLimitUnavailable);
    case ETextureCreateError::OutOfMemory:
        std::snprintf(buffer, sizeof(buffer), FLocalization::Text(EUiText::TextureOutOfMemory),
            Width, Height);
        break;
    case ETextureCreateError::OpenGlFailure:
        std::snprintf(buffer, sizeof(buffer), FLocalization::Text(EUiText::TextureOpenGlError),
            Width, Height, static_cast<unsigned>(OpenGlError));
        break;
    default:
        return FLocalization::Text(EUiText::TextureCreationFailed);
    }
    return buffer;
}

FTexture::FTexture()
    : TextureID(0)
    , Width(0)
    , Height(0)
    , RowLength(0)
    , InternalFormat(GL_RGBA)
    , Format(GL_RGBA)
    , DataType(GL_UNSIGNED_BYTE)
{
}

FTexture::~FTexture()
{
    Destroy();
}

bool FTexture::ValidateDimensions(int32_t InWidth, int32_t InHeight, FTextureCreateError* OutError)
{
    if (OutError)
    {
        *OutError = {};
    }
    if (InWidth <= 0 || InHeight <= 0)
    {
        SetCreateError(OutError, ETextureCreateError::InvalidDimensions, InWidth, InHeight);
        LOGE(kTextureUploadLogTag, "Invalid texture dimensions: %dx%d", InWidth, InHeight);
        return false;
    }

    GLint maximumDimension = 0;
    GLenum queryError = GL_NO_ERROR;
    if (glGetIntegerv && glGetError)
    {
        ClearPriorErrors();
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumDimension);
        queryError = glGetError();
    }
    if (maximumDimension <= 0 || queryError != GL_NO_ERROR)
    {
        SetCreateError(OutError, ETextureCreateError::LimitUnavailable,
            InWidth, InHeight, maximumDimension, queryError);
        LOGE(kTextureUploadLogTag, "Cannot query GL_MAX_TEXTURE_SIZE, value=%d error=0x%04X",
            maximumDimension, static_cast<unsigned>(queryError));
        return false;
    }

    // 比较实际平面的有效宽高，而非含 padding 的 row length；等于上限是合法的。
    if (InWidth > maximumDimension || InHeight > maximumDimension)
    {
        SetCreateError(OutError, ETextureCreateError::DimensionLimit,
            InWidth, InHeight, maximumDimension);
        LOGE(kTextureUploadLogTag, "Texture %dx%d exceeds GL_MAX_TEXTURE_SIZE=%d",
            InWidth, InHeight, maximumDimension);
        return false;
    }
    return true;
}

bool FTexture::Create(int32_t InWidth, int32_t InHeight, const void* Data, GLint InInternalFormat,
    GLint InFormat, GLenum InType, int32_t InRowLength, FTextureCreateError* OutError)
{
    if (!ValidateDimensions(InWidth, InHeight, OutError))
    {
        return false;
    }

    if (TextureID == 0)
    {
        glGenTextures(1, &TextureID);
    }

    Width = InWidth;
    Height = InHeight;
    RowLength = (InRowLength > InWidth) ? InRowLength : 0;
    InternalFormat = InInternalFormat;
    Format = InFormat;
    DataType = InType;

    glBindTexture(GL_TEXTURE_2D, TextureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    {
        FScopedUnpackState unpackState(RowLength);
        glTexImage2D(GL_TEXTURE_2D, 0, InternalFormat, Width, Height, 0, Format, DataType, Data);
    }

    GLenum error = glGetError();

    if (error != GL_NO_ERROR)
    {
        SetCreateError(OutError,
            error == GL_OUT_OF_MEMORY ? ETextureCreateError::OutOfMemory : ETextureCreateError::OpenGlFailure,
            Width, Height, 0, error);
        LOGE(kTextureUploadLogTag,
            "Failed to create %dx%d texture, internalFormat=0x%04X type=0x%04X error=0x%04X",
            Width, Height, static_cast<unsigned>(InternalFormat),
            static_cast<unsigned>(DataType), static_cast<unsigned>(error));
        // 纹理名称分配成功不代表存储分配成功，不能留下可被 IsValid() 接受的半成品。
        Destroy();

        return false;
    }

    return true;
}

void FTexture::UpdateData(const void* Data)
{

    if (TextureID == 0)
    {
        return;
    }

    glBindTexture(GL_TEXTURE_2D, TextureID);

    FScopedUnpackState unpackState(RowLength);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, Width, Height, Format, DataType, Data);
}

void FTexture::SetMagFilterNearest(bool bNearest) const
{
    if (TextureID == 0)
    {
        return;
    }

    glBindTexture(GL_TEXTURE_2D, TextureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, bNearest ? GL_NEAREST : GL_LINEAR);
}

void FTexture::Bind(uint32_t TextureUnit) const
{

    if (TextureID != 0)
    {
        glActiveTexture(GL_TEXTURE0 + TextureUnit);
        glBindTexture(GL_TEXTURE_2D, TextureID);
    }
}

void FTexture::Unbind() const
{
    glBindTexture(GL_TEXTURE_2D, 0);
}

void FTexture::Destroy()
{

    if (TextureID != 0)
    {
        glDeleteTextures(1, &TextureID);
        TextureID = 0;
    }
    Width = 0;
    Height = 0;
    RowLength = 0;
}
