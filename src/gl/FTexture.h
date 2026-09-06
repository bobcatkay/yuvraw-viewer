#pragma once

#include <glad/glad.h>
#include <cstdint>

/**
 * OpenGL纹理封装类
 * 虚幻引擎风格命名
 */
class FTexture
{
public:
    FTexture();
    ~FTexture();

    /**
     * 创建纹理
     * @param Width 宽度（有效像素数）
     * @param Height 高度
     * @param Data 像素数据
     * @param InternalFormat 内部格式
     * @param Format 格式
     * @param Type 数据类型
     * @param RowLength 源数据每行的像素数（含行尾 padding）。0 表示与 Width 相同（紧凑排列）。
     *                  用于跳过 stride padding，交由 GL 的 GL_UNPACK_ROW_LENGTH 处理。
     * @return 是否创建成功
     */
    bool Create(
        int32_t Width,
        int32_t Height,
        const void* Data,
        GLint InternalFormat = GL_RGBA,
        GLint Format = GL_RGBA,
        GLenum Type = GL_UNSIGNED_BYTE,
        int32_t RowLength = 0
    );

    /**
     * 更新纹理数据（沿用 Create 时的 RowLength）
     */
    void UpdateData(const void* Data);

    /**
     * 设置放大时的过滤方式。开发工具在 1:1 以上需要看到硬像素边界，
     * 因此放大用 GL_NEAREST，缩小仍用 GL_LINEAR 以避免摩尔纹。
     */
    void SetMagFilterNearest(bool bNearest) const;

    /**
     * 绑定纹理
     */
    void Bind(uint32_t TextureUnit = 0) const;

    /**
     * 解绑纹理
     */
    void Unbind() const;

    /**
     * 获取纹理ID
     */
    GLuint GetID() const { return TextureID; }

    /**
     * 获取宽度
     */
    int32_t GetWidth() const { return Width; }

    /**
     * 获取高度
     */
    int32_t GetHeight() const { return Height; }

    /**
     * 检查纹理是否有效
     */
    bool IsValid() const { return TextureID != 0; }

    /**
     * 销毁纹理
     */
    void Destroy();

private:
    GLuint TextureID;
    int32_t Width;
    int32_t Height;
    int32_t RowLength;   ///< 源数据每行像素数，0 = 紧凑
    GLint InternalFormat;
    GLint Format;
    GLenum DataType;
};
