#pragma once

#include <string>
#include <vector>

enum class EImageMetadataBlock
{
    Other,
    Ifd,
    Exif,
};

/// 原始元数据与显示字段解耦；记录实际块类型，避免依赖 codec 的路径别名。
struct FImageMetadataEntry
{
    std::string Path;
    std::string Value;
    EImageMetadataBlock Block = EImageMetadataBlock::Other;
};

/// 后台加载完成后只读共享；不把 COM 对象或文件句柄带到 UI 线程。
struct FImageMetadata
{
    std::vector<FImageMetadataEntry> Entries;
    bool bIncomplete = false;
};
