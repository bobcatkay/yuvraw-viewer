#include "FImageConfigCache.h"

#include "Image/FImageLimits.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <system_error>
#include <type_traits>

namespace
{
    constexpr std::array<char, 8> kCacheFileMagic{
        'I', 'D', 'T', 'C', 'F', 'G', '0', '1'
    };
    constexpr uint32_t kCacheFileVersion = 1u;
    constexpr uint32_t kMaximumSerializedEntries = 10000u;
    constexpr uint32_t kMaximumSerializedKeyBytes = 32u * 1024u;
    constexpr uintmax_t kMaximumCacheFileBytes = 32u * 1024u * 1024u;
    constexpr wchar_t kTemporaryFileSeparator = L'.';
    constexpr const wchar_t* kTemporaryFileSuffix = L".tmp";
    constexpr int32_t kQuarterTurnsPerCircle = 4;

    template <typename TValue>
    bool WriteValue(std::ostream& Stream, const TValue& Value)
    {
        static_assert(std::is_trivially_copyable_v<TValue>);
        Stream.write(
            reinterpret_cast<const char*>(&Value),
            static_cast<std::streamsize>(sizeof(TValue)));

        return Stream.good();
    }

    template <typename TValue>
    bool ReadValue(std::istream& Stream, TValue& OutValue)
    {
        static_assert(std::is_trivially_copyable_v<TValue>);
        Stream.read(
            reinterpret_cast<char*>(&OutValue),
            static_cast<std::streamsize>(sizeof(TValue)));

        return Stream.good();
    }

    template <typename TEnum>
    bool WriteEnum(std::ostream& Stream, TEnum Value)
    {
        return WriteValue(Stream, static_cast<int32_t>(Value));
    }

    template <typename TEnum>
    bool ReadEnum(std::istream& Stream, TEnum& OutValue)
    {
        int32_t value = 0;

        if (!ReadValue(Stream, value))
        {
            return false;
        }

        OutValue = static_cast<TEnum>(value);
        return true;
    }

    bool WriteBool(std::ostream& Stream, bool bValue)
    {
        const uint8_t value = bValue ? 1u : 0u;
        return WriteValue(Stream, value);
    }

    bool ReadBool(std::istream& Stream, bool& OutValue)
    {
        uint8_t value = 0u;

        if (!ReadValue(Stream, value) || value > 1u)
        {
            return false;
        }

        OutValue = value != 0u;
        return true;
    }

    template <typename TEnum>
    bool IsEnumInRange(TEnum Value, TEnum First, TEnum Last)
    {
        const int32_t value = static_cast<int32_t>(Value);
        return value >= static_cast<int32_t>(First)
            && value <= static_cast<int32_t>(Last);
    }

    bool IsConfigurationStructurallyValid(
        const FImageConfiguration& Configuration)
    {
        const FImageLoadParams& load = Configuration.LoadParams;
        const FDisplaySettings& display = Configuration.DisplaySettings;
        const FImageViewSettings& view = Configuration.ViewSettings;

        const bool bLoadValid =
            IsEnumInRange(load.Format, EImageFormat::Unknown, EImageFormat::YUV420SP16)
            && load.Width >= 0
            && load.Width <= FImageLimits::kMaximumDimension
            && load.Height >= 0
            && load.Height <= FImageLimits::kMaximumDimension
            && load.Stride >= 0
            && load.Stride <= FImageLimits::kMaximumStrideBytes
            && load.BitsPerPixel >= 1
            && load.BitsPerPixel <= FImageLimits::kMaximumRawBitsPerSample
            && IsEnumInRange(
                load.BayerPattern,
                EBayerPattern::RGGB,
                EBayerPattern::GBRG)
            && IsEnumInRange(
                load.ByteOrder,
                EByteOrder::LittleEndian,
                EByteOrder::BigEndian)
            && IsEnumInRange(
                load.SampleAlignment,
                ESampleAlignment::LeastSignificantBits,
                ESampleAlignment::MostSignificantBits);

        const bool bDisplayValid =
            IsEnumInRange(
                display.ColorSpace,
                EColorSpace::BT601,
                EColorSpace::BT2020)
            && IsEnumInRange(
                display.Primaries,
                EColorPrimaries::BT709,
                EColorPrimaries::BT601_625)
            && IsEnumInRange(
                display.Transfer,
                EColorTransfer::SDR,
                EColorTransfer::Linear)
            && IsEnumInRange(
                display.ColorRange,
                EColorRange::Limited,
                EColorRange::Full)
            && std::isfinite(display.ReferenceWhiteNits)
            && display.ReferenceWhiteNits > 0.0f
            && std::isfinite(display.HlgPeakNits)
            && display.HlgPeakNits > 0.0f
            && std::isfinite(display.ExposureStops)
            && IsEnumInRange(
                display.ToneMap,
                EToneMapOperator::Clip,
                EToneMapOperator::ACES)
            && std::isfinite(display.ToneMapWhite)
            && display.ToneMapWhite > 0.0f
            && IsEnumInRange(
                display.ChannelView,
                EChannelView::Color,
                EChannelView::Channel4);

        const bool bViewValid =
            IsEnumInRange(
                view.DisplayMode,
                EDisplayMode::AutoFit,
                EDisplayMode::Manual)
            && std::isfinite(view.ManualScale)
            && view.ManualScale > 0.0f
            && std::isfinite(view.PanOffsetX)
            && std::isfinite(view.PanOffsetY)
            && view.RotationQuarters >= 0
            && view.RotationQuarters < kQuarterTurnsPerCircle;

        return bLoadValid && bDisplayValid && bViewValid;
    }

    bool IsConfigurationCacheable(
        const FImageConfiguration& Configuration)
    {
        const FImageLoadParams& load = Configuration.LoadParams;
        return IsConfigurationStructurallyValid(Configuration)
            && load.Format != EImageFormat::Unknown
            && load.Width > 0
            && load.Height > 0;
    }

    bool WriteConfiguration(
        std::ostream& Stream,
        const FImageConfiguration& Configuration)
    {
        const FImageLoadParams& load = Configuration.LoadParams;
        const FDisplaySettings& display = Configuration.DisplaySettings;
        const FImageViewSettings& view = Configuration.ViewSettings;

        return WriteEnum(Stream, load.Format)
            && WriteValue(Stream, load.Width)
            && WriteValue(Stream, load.Height)
            && WriteValue(Stream, load.Stride)
            && WriteValue(Stream, load.BitsPerPixel)
            && WriteEnum(Stream, load.BayerPattern)
            && WriteEnum(Stream, load.ByteOrder)
            && WriteEnum(Stream, load.SampleAlignment)
            && WriteEnum(Stream, display.ColorSpace)
            && WriteEnum(Stream, display.Primaries)
            && WriteEnum(Stream, display.Transfer)
            && WriteEnum(Stream, display.ColorRange)
            && WriteValue(Stream, display.ReferenceWhiteNits)
            && WriteValue(Stream, display.HlgPeakNits)
            && WriteValue(Stream, display.ExposureStops)
            && WriteEnum(Stream, display.ToneMap)
            && WriteValue(Stream, display.ToneMapWhite)
            && WriteBool(Stream, display.bShowOutOfRange)
            && WriteEnum(Stream, display.ChannelView)
            && WriteEnum(Stream, view.DisplayMode)
            && WriteValue(Stream, view.ManualScale)
            && WriteValue(Stream, view.PanOffsetX)
            && WriteValue(Stream, view.PanOffsetY)
            && WriteValue(Stream, view.RotationQuarters)
            && WriteBool(Stream, view.bFlipH)
            && WriteBool(Stream, view.bFlipV);
    }

    bool ReadConfiguration(
        std::istream& Stream,
        FImageConfiguration& OutConfiguration)
    {
        FImageLoadParams& load = OutConfiguration.LoadParams;
        FDisplaySettings& display = OutConfiguration.DisplaySettings;
        FImageViewSettings& view = OutConfiguration.ViewSettings;

        const bool bRead = ReadEnum(Stream, load.Format)
            && ReadValue(Stream, load.Width)
            && ReadValue(Stream, load.Height)
            && ReadValue(Stream, load.Stride)
            && ReadValue(Stream, load.BitsPerPixel)
            && ReadEnum(Stream, load.BayerPattern)
            && ReadEnum(Stream, load.ByteOrder)
            && ReadEnum(Stream, load.SampleAlignment)
            && ReadEnum(Stream, display.ColorSpace)
            && ReadEnum(Stream, display.Primaries)
            && ReadEnum(Stream, display.Transfer)
            && ReadEnum(Stream, display.ColorRange)
            && ReadValue(Stream, display.ReferenceWhiteNits)
            && ReadValue(Stream, display.HlgPeakNits)
            && ReadValue(Stream, display.ExposureStops)
            && ReadEnum(Stream, display.ToneMap)
            && ReadValue(Stream, display.ToneMapWhite)
            && ReadBool(Stream, display.bShowOutOfRange)
            && ReadEnum(Stream, display.ChannelView)
            && ReadEnum(Stream, view.DisplayMode)
            && ReadValue(Stream, view.ManualScale)
            && ReadValue(Stream, view.PanOffsetX)
            && ReadValue(Stream, view.PanOffsetY)
            && ReadValue(Stream, view.RotationQuarters)
            && ReadBool(Stream, view.bFlipH)
            && ReadBool(Stream, view.bFlipV);

        return bRead && IsConfigurationStructurallyValid(OutConfiguration);
    }

    std::filesystem::path MakeTemporaryPath(const std::filesystem::path& Path)
    {
        std::filesystem::path temporary = Path;
        temporary += kTemporaryFileSeparator;
        temporary += std::to_wstring(GetCurrentProcessId());
        temporary += kTemporaryFileSuffix;
        return temporary;
    }
}

FImageConfigCache::FImageConfigCache(size_t InCapacity)
    : Capacity(InCapacity)
{
    Index.reserve(Capacity);
}

void FImageConfigCache::Put(
    const std::string& Key,
    const FImageConfiguration& Configuration)
{
    if (Key.empty() || Capacity == 0 || !IsConfigurationCacheable(Configuration))
    {
        return;
    }

    const auto existing = Index.find(Key);

    if (existing != Index.end())
    {
        existing->second->Configuration = Configuration;
        Entries.splice(Entries.begin(), Entries, existing->second);

        return;
    }

    Entries.push_front({ Key, Configuration });
    Index.emplace(Entries.front().Key, Entries.begin());
    TrimToCapacity();
}

bool FImageConfigCache::TryGet(
    const std::string& Key,
    FImageConfiguration& OutConfiguration)
{
    const auto found = Index.find(Key);

    if (found == Index.end())
    {
        return false;
    }

    if (!IsConfigurationCacheable(found->second->Configuration))
    {
        Entries.erase(found->second);
        Index.erase(found);
        return false;
    }

    Entries.splice(Entries.begin(), Entries, found->second);
    OutConfiguration = Entries.front().Configuration;

    return true;
}

void FImageConfigCache::SetCapacity(size_t InCapacity)
{
    if (Capacity == InCapacity)
    {
        return;
    }

    Capacity = InCapacity;
    TrimToCapacity();

    if (Capacity > Index.bucket_count())
    {
        Index.reserve(Capacity);
    }
}

void FImageConfigCache::Clear()
{
    Index.clear();
    Entries.clear();
}

bool FImageConfigCache::LoadFromFile(const std::filesystem::path& Path)
{
    std::error_code error;
    const uintmax_t fileSize = std::filesystem::file_size(Path, error);

    if (error || fileSize > kMaximumCacheFileBytes)
    {
        return false;
    }

    std::ifstream file(Path, std::ios::binary);

    if (!file.is_open())
    {
        return false;
    }

    std::array<char, kCacheFileMagic.size()> magic{};
    uint32_t version = 0u;
    uint32_t entryCount = 0u;

    file.read(magic.data(), static_cast<std::streamsize>(magic.size()));

    if (!file.good()
        || magic != kCacheFileMagic
        || !ReadValue(file, version)
        || version != kCacheFileVersion
        || !ReadValue(file, entryCount)
        || entryCount > kMaximumSerializedEntries)
    {
        return false;
    }

    FImageConfigCache loaded(Capacity);

    // 文件按最旧到最新写入；依次 Put 后既保留 LRU 次序，也自然按当前容量裁剪。
    for (uint32_t index = 0u; index < entryCount; ++index)
    {
        uint32_t keyLength = 0u;

        if (!ReadValue(file, keyLength)
            || keyLength == 0u
            || keyLength > kMaximumSerializedKeyBytes)
        {
            return false;
        }

        std::string key(keyLength, '\0');
        file.read(key.data(), static_cast<std::streamsize>(key.size()));

        FImageConfiguration configuration;

        if (!file.good()
            || key.find('\0') != std::string::npos
            || !ReadConfiguration(file, configuration))
        {
            return false;
        }

        loaded.Put(key, configuration);
    }

    // 固定版本格式不允许尾随数据，避免半覆盖或损坏文件被当成有效缓存。
    if (file.peek() != std::char_traits<char>::eof())
    {
        return false;
    }

    Entries.swap(loaded.Entries);
    Index.swap(loaded.Index);

    return true;
}

bool FImageConfigCache::SaveToFile(const std::filesystem::path& Path) const
{
    if (Path.empty() || Entries.size() > kMaximumSerializedEntries)
    {
        return false;
    }

    std::error_code error;

    if (!Path.parent_path().empty())
    {
        std::filesystem::create_directories(Path.parent_path(), error);

        if (error)
        {
            return false;
        }
    }

    const std::filesystem::path temporaryPath = MakeTemporaryPath(Path);
    std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);

    if (!file.is_open())
    {
        return false;
    }

    file.write(
        kCacheFileMagic.data(),
        static_cast<std::streamsize>(kCacheFileMagic.size()));

    const uint32_t entryCount = static_cast<uint32_t>(Entries.size());
    bool bSuccess = file.good()
        && WriteValue(file, kCacheFileVersion)
        && WriteValue(file, entryCount);

    // 内存中表头是 MRU；反向写成 LRU -> MRU，载入时逐项 Put 即可还原次序。
    for (auto entry = Entries.rbegin(); bSuccess && entry != Entries.rend(); ++entry)
    {
        if (entry->Key.empty()
            || entry->Key.size() > kMaximumSerializedKeyBytes
            || !IsConfigurationCacheable(entry->Configuration))
        {
            bSuccess = false;
            break;
        }

        const uint32_t keyLength = static_cast<uint32_t>(entry->Key.size());
        bSuccess = WriteValue(file, keyLength);

        if (bSuccess)
        {
            file.write(
                entry->Key.data(),
                static_cast<std::streamsize>(entry->Key.size()));
            bSuccess = file.good()
                && WriteConfiguration(file, entry->Configuration);
        }
    }

    file.flush();
    const std::streamoff cacheFileBytes = file.tellp();
    bSuccess = bSuccess
        && file.good()
        && cacheFileBytes >= 0
        && static_cast<uintmax_t>(cacheFileBytes) <= kMaximumCacheFileBytes;
    file.close();

    if (bSuccess)
    {
        bSuccess = MoveFileExW(
            temporaryPath.c_str(),
            Path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }

    if (!bSuccess)
    {
        error.clear();
        std::filesystem::remove(temporaryPath, error);
    }

    return bSuccess;
}

void FImageConfigCache::TrimToCapacity()
{
    while (Entries.size() > Capacity)
    {
        const FEntryIterator oldest = std::prev(Entries.end());
        Index.erase(oldest->Key);
        Entries.erase(oldest);
    }
}
