#pragma once

#include "Image/FDisplaySettings.h"
#include "Image/FImageLoadParams.h"

#include <cstdint>
#include <string>
#include <unordered_map>

/**
 * 同一目录、同一扩展名上一张主图可复用的属性。
 *
 * 视图状态不在这里保存：缩放、平移与朝向属于单张图片，只有按文件路径命中的
 * 持久缓存才应恢复它们。
 */
struct FDirectoryImageProperties
{
    FImageLoadParams LoadParams;
    FDisplaySettings DisplaySettings;
};

enum class EDirectoryImagePropertyLookup
{
    Compatible,
    NoHistoryForExtension,
    SelfDescribingTarget,
    FileSizeMismatch,
};

/**
 * 记录本次运行中每个目录内、各文件扩展名最近一张主图的加载与显示属性。
 *
 * 该历史只在切图时访问，不持久化，也不进入逐帧路径。目标文件只有在按上一张图
 * 的加载参数计算出的帧大小与文件大小完全一致时才会复用这些属性。扩展名按
 * ASCII 小写匹配，避免 .RAW 与 .raw 被拆成两份记录。
 */
class FDirectoryImagePropertyHistory
{
public:
    /// 清空本次运行中积累的目录继承记录。
    void Clear();

    /**
     * 记录一张即将被切出的主图。自描述文件只会清除同目录、同扩展名的旧记录，
     * 不影响该目录下其它扩展名已经记住的 RAW/YUV 属性。
     */
    void Remember(
        const std::string& FilePath,
        const FImageLoadParams& LoadParams,
        const FDisplaySettings& DisplaySettings,
        bool bSelfDescribing);

    /**
     * 查询目标文件能否复用同目录、同扩展名上一张主图的属性。
     *
     * @param FileSize 目标文件大小；读取失败或空文件传 0，按不匹配处理。
     */
    EDirectoryImagePropertyLookup TryGetCompatible(
        const std::string& FilePath,
        uint64_t FileSize,
        bool bSelfDescribing,
        FDirectoryImageProperties& OutProperties) const;

private:
    static bool BuildHistoryKeys(
        const std::string& FilePath,
        std::string& OutDirectoryKey,
        std::string& OutExtensionKey);

    using FExtensionEntries =
        std::unordered_map<std::string, FDirectoryImageProperties>;
    std::unordered_map<std::string, FExtensionEntries> Entries;
};
