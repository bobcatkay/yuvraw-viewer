#pragma once

#include <memory>
#include <vector>
#include <cstdint>
#include <functional>
#include "Image/FImageData.h"
#include "FTextureLoadOptions.h"

// Forward declaration
class FTexture;
class FSparseTexture;
struct FTextureCreateError;
struct FTextureViewRegion;

/**
 * 纹理数据类
 *
 * 根据图像格式创建和管理多个纹理对象（NV21/NV12/P010/YUV420SP16 需要 2 个，I420 需要 3 个，RGB 需要 1 个）。
 *
 * 平面数量、降采样倍率、每采样字节数、行跨距**全部来自格式描述表**（FImageFormatDesc），
 * 稀疏预览只支持可独立平均的普通分量布局；打包格式和 CFA 使用普通整图或分块纹理。
 */
class FTextureData
{
public:
    FTextureData();
    ~FTextureData();

    /** 纯 CPU 预览准备，调用方须解除上传 Context；取消时不留下半成品。 */
    bool PrepareSparsePreview(const FImageData* ImageData,
        const std::function<bool()>& ShouldContinue = {});

    /** 当前绘制 Context 内推进可见页面；页面完整之前统一显示预览，不采样未驻留内存。 */
    void PrepareForDraw(const FImageData* ImageData, const FTextureViewRegion& Region) noexcept;
    void FinishDraw();
    bool HasSparseTextures() const { return !SparseTextures.empty(); }
    bool IsShowingSparseDetail() const { return bUseSparseDetail; }
    bool HasSparseFailure() const { return bSparseFailed; }
    /** UI 构建阶段集中记录一次性性能/失败事件，禁止在绘制回调内调用。 */
    void LogPendingDrawEvents();
    void GetTextureCoordinateScale(uint32_t Plane, float& OutX, float& OutY, uint32_t DrawIndex = 0) const;
    void GetTextureCoordinateOffset(uint32_t Plane, float& OutX, float& OutY, uint32_t DrawIndex = 0) const;
    void GetTextureTexelOrigin(uint32_t Plane, float& OutX, float& OutY, uint32_t DrawIndex = 0) const;
    /** 先 PrepareForDraw，再逐块查询区域；返回 false 的块不需要绘制。 */
    uint32_t GetDrawCount() const;
    bool GetDrawRegion(uint32_t DrawIndex, FTextureViewRegion& OutRegion) const;
    bool HasTiledTextures() const;
    const char* GetBackendName() const;
    int64_t GetPreviewPreparationMilliseconds() const { return PreviewPreparationMilliseconds; }

    /**
     * 根据图像数据创建纹理
     * @param ImageData 图像数据
     * @param OutError 可选纹理创建失败详情；普通纹理在分配前统一检查 GPU 尺寸上限。
     * @return 是否创建成功
     */
    bool CreateFromImageData(const FImageData* ImageData, FTextureCreateError* OutError = nullptr,
        FTextureLoadOptions Options = {}, const std::function<bool()>& ShouldContinue = {});

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
    void BindTextures(uint32_t StartTextureUnit = 0, uint32_t DrawIndex = 0) const;

    /**
     * 解绑所有纹理
     */
    void UnbindTextures() const;

    /**
     * 获取纹理数量
     */
    uint32_t GetTextureCount() const;

    /**
     * 获取指定平面的普通纹理（稀疏路径为常驻预览，分块路径为第一块，仅供尺寸诊断）
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
    class FTiledDrawState;
    std::unique_ptr<FTiledDrawState> TiledDrawState;
    bool TryCreateWholeTextures(const FImageData* ImageData, FTextureCreateError* OutError);
    bool TryCreateTiledTextures(const FImageData* ImageData, FTextureCreateError* OutError,
        const std::function<bool()>& ShouldContinue);
    int64_t PreviewPreparationMilliseconds = 0;
    struct FPreviewPlane
    {
        int32_t Width = 0, Height = 0, Stride = 0;
        std::vector<uint8_t> Pixels;
    };
    bool TryCreateSparseTextures(const std::vector<FPreviewPlane>& Previews);
    std::vector<FPreviewPlane> PreparedPreviews;
    const FImageData* PreparedImage = nullptr;
    std::vector<std::unique_ptr<FSparseTexture>> SparseTextures;
    class FSparseDrawState;
    std::unique_ptr<FSparseDrawState> SparseDrawState;
    bool bUseSparseDetail = false;
    bool bSparseFailed = false;
    bool bSparseFailureLogged = false;
    mutable bool bMagNearest = false;
    std::vector<std::unique_ptr<FTexture>> Textures;
    /// GL_UNPACK_ROW_LENGTH 无法表达非整纹素 padding 时复用的逐平面紧凑缓冲。
    std::vector<std::vector<uint8_t>> UploadScratchBuffers;
    EImageFormat Format;
    int32_t Width;
    int32_t Height;
    int32_t Stride;   ///< 建纹理时的基准行跨距，决定各平面的上传布局
};
