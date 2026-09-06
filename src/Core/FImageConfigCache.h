#pragma once

#include "Image/FDisplaySettings.h"
#include "Image/FImageLoadParams.h"
#include "UI/FImageViewer.h"

#include <cstddef>
#include <filesystem>
#include <list>
#include <string>
#include <unordered_map>

/**
 * 一张图片需要在再次选中时恢复的完整配置。
 *
 * 像素与纹理不进入缓存；这里只保存体积固定的参数，因此即使缓存数百张图片，
 * 内存占用也保持很小。
 */
struct FImageConfiguration
{
    FImageLoadParams LoadParams;
    FDisplaySettings DisplaySettings;
    FImageViewSettings ViewSettings;
};

/**
 * 图片配置的有界 LRU 缓存。
 *
 * 查找、写入与提升热度均为均摊 O(1)，淘汰只发生在切图或修改容量时，
 * 不进入 ImGui/OpenGL 的逐帧热路径。该类只在主线程使用，不做额外加锁。
 */
class FImageConfigCache
{
public:
    explicit FImageConfigCache(size_t Capacity);

    /**
     * 写入或更新有效加载配置。容量为 0、或加载参数无效时忽略。
     */
    void Put(const std::string& Key, const FImageConfiguration& Configuration);

    /**
     * 查找配置并把它提升为最近使用项。
     */
    bool TryGet(const std::string& Key, FImageConfiguration& OutConfiguration);

    /**
     * 修改容量并立即淘汰超出的最久未使用项。
     */
    void SetCapacity(size_t Capacity);

    size_t GetCapacity() const { return Capacity; }
    size_t GetSize() const { return Entries.size(); }

    void Clear();

    /**
     * 从版本化缓存文件恢复配置。文件缺失、损坏或版本不兼容时返回 false，
     * 并保持当前内存缓存不变。
     */
    bool LoadFromFile(const std::filesystem::path& Path);

    /**
     * 原子写入版本化缓存文件。配置按 LRU 顺序保存，重启后仍能正确淘汰。
     */
    bool SaveToFile(const std::filesystem::path& Path) const;

private:
    struct FEntry
    {
        std::string Key;
        FImageConfiguration Configuration;
    };

    using FEntryList = std::list<FEntry>;
    using FEntryIterator = FEntryList::iterator;

    void TrimToCapacity();

    size_t Capacity;
    FEntryList Entries;
    std::unordered_map<std::string, FEntryIterator> Index;
};
