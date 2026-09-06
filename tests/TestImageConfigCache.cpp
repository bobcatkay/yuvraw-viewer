#include "Core/FImageConfigCache.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    int GFailures = 0;
    constexpr int32_t kPersistedMarker = 11;
    constexpr int32_t kNewEntryMarker = 12;

    void Check(bool bCondition, const char* Message)
    {
        std::printf("%-64s %s\n", Message, bCondition ? "OK" : "**FAIL**");

        if (!bCondition)
        {
            ++GFailures;
        }
    }

    FImageConfiguration MakeConfiguration(int32_t Marker)
    {
        FImageConfiguration configuration;
        // 使用当前追加在枚举末尾的格式，确保缓存结构校验同步接受新持久化值。
        configuration.LoadParams.Format = EImageFormat::YUV420SP16;
        configuration.LoadParams.Width = Marker;
        configuration.LoadParams.Height = Marker + 1;
        configuration.LoadParams.Stride = Marker + 2;
        configuration.LoadParams.BitsPerPixel = 10;
        configuration.LoadParams.BayerPattern = EBayerPattern::GBRG;
        configuration.LoadParams.ByteOrder = EByteOrder::BigEndian;
        configuration.LoadParams.SampleAlignment =
            ESampleAlignment::MostSignificantBits;

        configuration.DisplaySettings.ColorSpace = EColorSpace::BT2020;
        configuration.DisplaySettings.Primaries = EColorPrimaries::DisplayP3;
        configuration.DisplaySettings.Transfer = EColorTransfer::HLG;
        configuration.DisplaySettings.ColorRange = EColorRange::Full;
        // 必须是**非默认值**（默认是 BT.2408 的 203）：等于默认值的话，
        // 持久化漏掉这个字段也会读回同一个数，往返断言就失去了区分能力
        configuration.DisplaySettings.ReferenceWhiteNits = 300.0f;
        configuration.DisplaySettings.HlgPeakNits = 1200.0f;
        configuration.DisplaySettings.ExposureStops = static_cast<float>(Marker);
        configuration.DisplaySettings.ToneMap = EToneMapOperator::ACES;
        configuration.DisplaySettings.ToneMapWhite = 6.0f;
        configuration.DisplaySettings.bShowOutOfRange = true;
        configuration.DisplaySettings.ChannelView = EChannelView::Channel4;

        configuration.ViewSettings.DisplayMode = EDisplayMode::Manual;
        configuration.ViewSettings.ManualScale = static_cast<float>(Marker);
        configuration.ViewSettings.PanOffsetX = static_cast<float>(Marker + 3);
        configuration.ViewSettings.PanOffsetY = static_cast<float>(-Marker);
        configuration.ViewSettings.RotationQuarters = 3;
        configuration.ViewSettings.bFlipH = true;
        configuration.ViewSettings.bFlipV = false;

        return configuration;
    }

    bool ConfigurationsMatch(
        const FImageConfiguration& Left,
        const FImageConfiguration& Right)
    {
        return Left.LoadParams.Format == Right.LoadParams.Format
            && Left.LoadParams.Width == Right.LoadParams.Width
            && Left.LoadParams.Height == Right.LoadParams.Height
            && Left.LoadParams.Stride == Right.LoadParams.Stride
            && Left.LoadParams.BitsPerPixel == Right.LoadParams.BitsPerPixel
            && Left.LoadParams.BayerPattern == Right.LoadParams.BayerPattern
            && Left.LoadParams.ByteOrder == Right.LoadParams.ByteOrder
            && Left.LoadParams.SampleAlignment == Right.LoadParams.SampleAlignment
            && Left.DisplaySettings.ColorSpace == Right.DisplaySettings.ColorSpace
            && Left.DisplaySettings.Primaries == Right.DisplaySettings.Primaries
            && Left.DisplaySettings.Transfer == Right.DisplaySettings.Transfer
            && Left.DisplaySettings.ColorRange == Right.DisplaySettings.ColorRange
            && Left.DisplaySettings.ReferenceWhiteNits
                == Right.DisplaySettings.ReferenceWhiteNits
            && Left.DisplaySettings.HlgPeakNits
                == Right.DisplaySettings.HlgPeakNits
            && Left.DisplaySettings.ExposureStops
                == Right.DisplaySettings.ExposureStops
            && Left.DisplaySettings.ToneMap == Right.DisplaySettings.ToneMap
            && Left.DisplaySettings.ToneMapWhite
                == Right.DisplaySettings.ToneMapWhite
            && Left.DisplaySettings.bShowOutOfRange
                == Right.DisplaySettings.bShowOutOfRange
            && Left.DisplaySettings.ChannelView
                == Right.DisplaySettings.ChannelView
            && Left.ViewSettings.DisplayMode == Right.ViewSettings.DisplayMode
            && Left.ViewSettings.ManualScale == Right.ViewSettings.ManualScale
            && Left.ViewSettings.PanOffsetX == Right.ViewSettings.PanOffsetX
            && Left.ViewSettings.PanOffsetY == Right.ViewSettings.PanOffsetY
            && Left.ViewSettings.RotationQuarters
                == Right.ViewSettings.RotationQuarters
            && Left.ViewSettings.bFlipH == Right.ViewSettings.bFlipH
            && Left.ViewSettings.bFlipV == Right.ViewSettings.bFlipV;
    }

    std::filesystem::path MakeTemporaryCachePath()
    {
        const auto uniqueValue = std::chrono::high_resolution_clock::now()
            .time_since_epoch().count();
        return std::filesystem::temp_directory_path()
            / ("YUVRaw_ImageConfigCache_"
                + std::to_string(uniqueValue)
                + ".cache");
    }
}

int main()
{
    FImageConfigCache cache(2);
    FImageConfiguration configuration;

    cache.Put("a", MakeConfiguration(1));
    cache.Put("b", MakeConfiguration(2));
    Check(cache.GetSize() == 2, "capacity accepts two entries");

    Check(
        cache.TryGet("a", configuration) && configuration.LoadParams.Width == 1,
        "lookup returns complete cached configuration");

    // 读取 a 后它成为最近使用项，因此插入 c 应淘汰 b。
    cache.Put("c", MakeConfiguration(3));
    Check(!cache.TryGet("b", configuration), "least recently used entry is evicted");
    Check(cache.TryGet("a", configuration), "recently read entry is retained");

    cache.Put("a", MakeConfiguration(4));
    Check(
        cache.TryGet("a", configuration)
            && configuration.LoadParams.Width == 4
            && configuration.DisplaySettings.ExposureStops == 4.0f
            && configuration.ViewSettings.ManualScale == 4.0f,
        "updating an entry replaces all property and view settings");

    cache.SetCapacity(1);
    Check(cache.GetSize() == 1, "shrinking capacity trims immediately");
    Check(cache.TryGet("a", configuration), "most recent entry survives capacity shrink");

    cache.SetCapacity(0);
    Check(cache.GetSize() == 0, "zero capacity clears and disables cache");
    cache.Put("disabled", MakeConfiguration(5));
    Check(cache.GetSize() == 0, "disabled cache ignores writes");

    cache.SetCapacity(2);
    cache.Put("enabled", MakeConfiguration(6));
    Check(cache.TryGet("enabled", configuration), "cache can be re-enabled");

    FImageConfiguration invalidConfiguration;
    cache.Put("invalid", invalidConfiguration);
    Check(
        cache.GetSize() == 1 && !cache.TryGet("invalid", configuration),
        "invalid load parameters are not cached");
    cache.Put("enabled", invalidConfiguration);
    Check(
        cache.TryGet("enabled", configuration)
            && configuration.LoadParams.Width == 6,
        "invalid update does not replace a successful configuration");

    cache.Clear();
    Check(cache.GetSize() == 0, "clear removes all configurations");

    const std::filesystem::path cachePath = MakeTemporaryCachePath();
    const std::string persistedKey = u8"C:/图片/属性缓存.raw";
    FImageConfigCache persistentCache(2);
    persistentCache.Put(persistedKey, MakeConfiguration(kPersistedMarker));
    persistentCache.Put("oldest", MakeConfiguration(7));

    // 再次读取中文路径，使 oldest 成为淘汰顺序里的最旧项。
    persistentCache.TryGet(persistedKey, configuration);
    Check(
        persistentCache.SaveToFile(cachePath),
        "complete image configuration is persisted to disk");

    FImageConfigCache restoredCache(2);
    Check(
        restoredCache.LoadFromFile(cachePath),
        "persisted cache can be loaded by a new cache instance");
    Check(restoredCache.GetSize() == 2, "persisted entry count is restored");

    const FImageConfiguration expected = MakeConfiguration(kPersistedMarker);
    Check(
        restoredCache.TryGet(persistedKey, configuration)
            && ConfigurationsMatch(configuration, expected),
        "all load, display and view fields survive persistence");

    // 载入后 LRU 次序也应保持，插入第三项会淘汰保存前最旧的 oldest。
    restoredCache.Put("new", MakeConfiguration(kNewEntryMarker));
    Check(
        !restoredCache.TryGet("oldest", configuration),
        "persisted LRU order controls eviction after restart");
    Check(
        restoredCache.TryGet(persistedKey, configuration),
        "most recent persisted entry survives post-restart eviction");

    // 损坏文件不能冲掉当前仍有效的内存缓存。
    {
        std::ofstream corruptFile(cachePath, std::ios::binary | std::ios::trunc);
        corruptFile << "invalid cache";
    }
    Check(
        !restoredCache.LoadFromFile(cachePath),
        "invalid cache file is rejected");
    Check(
        restoredCache.TryGet(persistedKey, configuration),
        "failed load leaves the existing memory cache unchanged");

    std::error_code cleanupError;
    std::filesystem::remove(cachePath, cleanupError);

    std::printf("\n%s\n", GFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return GFailures == 0 ? 0 : 1;
}
