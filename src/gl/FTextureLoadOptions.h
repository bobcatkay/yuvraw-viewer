#pragma once

#include <algorithm>
#include <cstdint>
#include "Image/FImageLimits.h"

/** 主线程保存、按请求复制的纹理策略；不依赖 GL 或 UI 全局状态。 */
struct FTextureLoadOptions
{
    static constexpr int32_t kDefaultSparseDimensionThreshold = 16384;
    static constexpr int32_t kMinimumSparseDimensionThreshold = 1;
    static constexpr int32_t kMaximumSparseDimensionThreshold = FImageLimits::kMaximumDimension;

    bool bEnableSparseTextures = true;
    int32_t SparseDimensionThreshold = kDefaultSparseDimensionThreshold;

    void Normalize()
    {
        SparseDimensionThreshold = std::clamp(SparseDimensionThreshold,
            kMinimumSparseDimensionThreshold, kMaximumSparseDimensionThreshold);
    }

    bool PrefersSparse(int32_t Width, int32_t Height) const
    {
        return bEnableSparseTextures && std::max(Width, Height) > SparseDimensionThreshold;
    }
};
