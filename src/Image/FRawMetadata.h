#pragma once

#include "FDisplaySettings.h"
#include <string>

enum class ERawMetadataResult { Success, NotFound, Invalid, FrameNotFound, BitDepthMismatch };

namespace FRawMetadata
{
    inline constexpr int32_t kMaximumFrameIndex = 9999;
    int32_t GetFrameIndex(const std::string& RawPath);
    ERawMetadataResult LoadFile(
        const std::string& MetadataPath, int32_t FrameIndex, int32_t BitDepth,
        FRawDisplaySettings& OutSettings);
    /**
     * 读取 RAW 配套 TXT；优先同名，其次移除末尾帧编号后查找。
     * 导入黑电平、白平衡增益和 CCM，Bayer 排布由用户单独选择。
     * 失败不修改 OutSettings；参数解释方式保留用户的当前选择。
     */
    ERawMetadataResult LoadForRawFile(
        const std::string& RawPath, int32_t BitDepth, FRawDisplaySettings& OutSettings);
}
