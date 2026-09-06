#include "FTexture.h"
#include "Util.h"
#include <cstring>

namespace
{
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

FTexture::FTexture()
    : TextureID(0)
    , Width(0)
    , Height(0)
    , RowLength(0)
    , InternalFormat(GL_RGBA)
    , Format(GL_RGBA)
    , DataType(GL_UNSIGNED_BYTE)
{
    glGenTextures(1, &TextureID);
}

FTexture::~FTexture()
{
    Destroy();
}

bool FTexture::Create(int32_t InWidth, int32_t InHeight, const void* Data, GLint InInternalFormat, GLint InFormat, GLenum InType, int32_t InRowLength)
{
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
        LOGE("Create", "Failed to create texture, OpenGL error: %d", error);

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
}
