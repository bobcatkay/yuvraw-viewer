#pragma once

#include <memory>
#include <vector>
#include <cstdint>
#include "Image/FImageData.h"

// Forward declaration
class FTexture;

/**
 * 纹理数据类
 *
 * 根据图像格式创建和管理多个纹理对象（NV21/NV12/P010/YUV420SP16 需要 2 个，I420 需要 3 个，RGB 需要 1 个）。
 *
 * 平面数量、降采样倍率、每采样字节数、行跨距**全部来自格式描述表**（FImageFormatDesc），
 * 这里没有任何按格式分支的代码 —— 新增一种格式不需要动这个文件。
 */
class FTextureData
{
public:
    FTextureData();
    ~FTextureData();

    /**
     * 根据图像数据创建纹理
     * @param ImageData 图像数据
     * @return 是否创建成功
     */
    bool CreateFromImageData(const FImageData* ImageData);

    /**
     * 更新纹理数据
     * @param ImageData 新的图像数据（格式、分辨率、行跨距必须与建纹理时一致）
     * @return 是否更新成功
     */
    bool UpdateFromImageData(const FImageData* ImageData);

    /**
     * 绑定所有纹理到对应的纹理单元
     * @param StartTextureUnit 起始纹理单元（默认0）
     */
    void BindTextures(uint32_t StartTextureUnit = 0) const;

    /**
     * 解绑所有纹理
     */
    void UnbindTextures() const;

    /**
     * 获取纹理数量
     */
    uint32_t GetTextureCount() const { return static_cast<uint32_t>(Textures.size()); }

    /**
     * 获取指定索引的纹理
     * @param Index 纹理索引
     * @return 纹理指针，无效索引返回nullptr
     */
    FTexture* GetTexture(uint32_t Index) const;

    /**
     * 获取图像格式
     */
    EImageFormat GetFormat() const { return Format; }

    /**
     * 获取图像宽度
     */
    int32_t GetWidth() const { return Width; }

    /**
     * 获取图像高度
     */
    int32_t GetHeight() const { return Height; }

    /**
     * 获取建纹理时使用的基准行跨距（第 0 平面每行字节数）
     *
     * stride 决定各平面的 GL_UNPACK_ROW_LENGTH；不能换算成整纹素的 padding
     * 会先逐行重排。因此 stride 变化时必须重建纹理与上传策略。
     */
    int32_t GetStride() const { return Stride; }

    /**
     * 检查是否有效
     */
    bool IsValid() const;

    /**
     * 销毁所有纹理
     */
    void Destroy();

    /**
     * 设置所有纹理放大时是否使用最近邻过滤（1:1 以上需要看到硬像素边界）
     */
    void SetMagFilterNearest(bool bNearest) const;

private:
    std::vector<std::unique_ptr<FTexture>> Textures;
    /// GL_UNPACK_ROW_LENGTH 无法表达非整纹素 padding 时复用的逐平面紧凑缓冲。
    std::vector<std::vector<uint8_t>> UploadScratchBuffers;
    EImageFormat Format;
    int32_t Width;
    int32_t Height;
    int32_t Stride;   ///< 建纹理时的基准行跨距，决定各平面的上传布局
};
