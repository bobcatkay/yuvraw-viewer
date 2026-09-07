#pragma once

#include "FTexture.h"
#include <cstddef>
#include <cstdint>
#include <memory>

/** 原图归一化坐标中的可见矩形，与窗口、DPI 和旋转无关。 */
struct FTextureViewRegion
{
    float MinU = 0.0f;
    float MinV = 0.0f;
    float MaxU = 1.0f;
    float MaxV = 1.0f;
};

/**
 * 单平面 ARB_sparse_texture 后端，只分配 level 0 的可见页面。
 * 预览由 FTextureData 持有；全部可见页面准备好前，不能采样此纹理。
 * 扩展函数按当前 Context 获取，不修改进程级 GLAD 函数表。
 */
class FSparseTexture
{
public:
    FSparseTexture();
    ~FSparseTexture();
    FSparseTexture(const FSparseTexture&) = delete;
    FSparseTexture& operator=(const FSparseTexture&) = delete;

    static bool IsSupported();
    bool Create(int32_t Width, int32_t Height, GLint InternalFormat,
        GLenum Format, GLenum Type, int32_t BytesPerTexel);
    size_t EstimateResidentBytes(const FTextureViewRegion& Region) const;
    /** 所有平面先回收视口外页面，再上传新页，避免平移时多平面的瞬时驻留量超出预算。 */
    bool Trim(const FTextureViewRegion& Region, FTextureCreateError& OutError);
    /** 在绘制 Context 中调用；按剩余上传预算推进，无日志，失败通过 OutError 返回。 */
    bool Update(const FTextureViewRegion& Region, const uint8_t* Pixels, int32_t StrideBytes,
        size_t& InOutUploadBudget, FTextureCreateError& OutError);
    bool Evict(FTextureCreateError& OutError);
    void Bind(uint32_t Unit, bool bNearest) const;
    float GetScaleX() const;
    float GetScaleY() const;
    size_t GetResidentBytes() const;

private:
    class FImpl;
    std::unique_ptr<FImpl> Impl;
};
