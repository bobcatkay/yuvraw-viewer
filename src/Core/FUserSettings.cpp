#include "FUserSettings.h"

#include "FLogger.h"
#include "Image/FImageLimits.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>

namespace
{
    constexpr const char* kLastDirectoryKey = "LastDirectory";
    constexpr const char* kLanguageKey = "Language";
    constexpr const char* kLanguageLogTag = "Language";
    constexpr const char* kRecentFileKey = "RecentFile";
    constexpr const char* kImageFormatPresetKey = "ImageFormatPreset";
    constexpr const char* kImageFormatPresetRecordVersion = "v1";
    constexpr char kImageFormatPresetFieldSeparator = '|';
    constexpr size_t kImageFormatPresetFieldCount = 10;
    constexpr size_t kMaximumImageFormatPresetRecordBytes = 1024;
    constexpr const char* kImageConfigCacheCapacityKey = "ImageConfigCacheCapacity";
    constexpr const char* kHistogramOverlayEnabledKey = "HistogramOverlayEnabled";
    constexpr const char* kHistogramLogScaleEnabledKey = "HistogramLogScaleEnabled";
    constexpr const char* kCompareModeKey = "CompareMode";
    constexpr const char* kSingleImageSwitchModeValue = "SingleImageSwitch";
    constexpr const char* kSideBySideModeValue = "SideBySide";
    constexpr const char* kSingleImageCompareHintDisplayCountKey =
        "SingleImageCompareHintDisplayCount";
    constexpr const char* kWindowPositionXKey = "WindowPositionX";
    constexpr const char* kWindowPositionYKey = "WindowPositionY";
    constexpr const char* kWindowWidthKey = "WindowWidth";
    constexpr const char* kWindowHeightKey = "WindowHeight";
    constexpr const char* kWindowMaximizedKey = "WindowMaximized";
    constexpr const wchar_t* kApplicationDirectoryName = L"YUVRaw";
    constexpr const wchar_t* kLegacyApplicationDirectoryName = L"ImageDevTool";
    constexpr const wchar_t* kLegacyMigrationMarkerName = L"legacy-migration.complete";
    constexpr const char* kMigrationLogTag = "LegacyMigration";
    constexpr const wchar_t* kSettingsFileName = L"settings.ini";
    constexpr wchar_t kTemporaryFileSeparator = L'.';
    constexpr const wchar_t* kTemporaryFileSuffix = L".tmp";
    constexpr const wchar_t* kImageConfigCacheFileName = L"image-properties.cache";
    constexpr size_t kThemeColorHexDigitCount = 6;
    constexpr size_t kThemeColorTextBufferSize = kThemeColorHexDigitCount + 1;
    constexpr uint32_t kThemeColorRedShift = 16;
    constexpr uint32_t kThemeColorGreenShift = 8;
    constexpr uint32_t kThemeColorChannelMask = 0xFFu;
    constexpr uint32_t kBitsPerHexDigit = 4;
    constexpr int32_t kHexAlphabetFirstValue = 10;
    constexpr int32_t kInvalidHexDigit = -1;

    struct FThemeColorSetting
    {
        FUserSettings::EThemeColorRole Role;
        const char* Key;
    };

    /**
     * 基础色沿用旧版完整调色板中对应角色的 key，升级后无需迁移设置文件。
     * 旧版的状态细项会被忽略，并在下次保存时自然移除。
     */
    constexpr std::array<
        FThemeColorSetting,
        FUserSettings::kThemeColorRoleCount> kThemeColorSettings{{
        { FUserSettings::EThemeColorRole::Accent, "ThemeAccentColor" },
        { FUserSettings::EThemeColorRole::InterfaceBackground, "ThemeWindowBackgroundColor" },
        { FUserSettings::EThemeColorRole::Text, "ThemeTextColor" },
        { FUserSettings::EThemeColorRole::Border, "ThemeBorderColor" },
        { FUserSettings::EThemeColorRole::InputBackground, "ThemeFrameBackgroundColor" },
        { FUserSettings::EThemeColorRole::Control, "ThemeButtonColor" },
    }};

    std::filesystem::path GetEnvironmentDirectory(
        const wchar_t* VariableName,
        const std::filesystem::path& Fallback)
    {
        wchar_t* value = nullptr;
        size_t length = 0;
        const errno_t error = _wdupenv_s(&value, &length, VariableName);

        const std::filesystem::path result =
            (error == 0 && value && *value)
                ? std::filesystem::path(value)
                : Fallback;

        free(value);

        return result;
    }

    /**
     * 配置文件路径。取不到 %APPDATA% 时退回当前工作目录，宁可存错地方也不丢功能。
     */
    std::filesystem::path GetSettingsPath()
    {
        const std::filesystem::path base = GetEnvironmentDirectory(
            L"APPDATA",
            std::filesystem::current_path());

        return base / kApplicationDirectoryName / kSettingsFileName;
    }

    std::filesystem::path MakeSettingsTemporaryPath(
        const std::filesystem::path& Path)
    {
        std::filesystem::path temporary = Path;
        temporary += kTemporaryFileSeparator;
        temporary += std::to_wstring(GetCurrentProcessId());
        temporary += kTemporaryFileSuffix;
        return temporary;
    }

    std::filesystem::path GetLocalCachePath()
    {
        // 绝对图片路径没有漫游价值；LOCALAPPDATA 避免在启用漫游配置时同步缓存。
        const std::filesystem::path base = GetEnvironmentDirectory(
            L"LOCALAPPDATA", GetSettingsPath().parent_path().parent_path());
        return base / kApplicationDirectoryName / kImageConfigCacheFileName;
    }

    bool MarkLegacyMigrationComplete()
    {
        const std::filesystem::path marker =
            GetSettingsPath().parent_path() / kLegacyMigrationMarkerName;
        std::error_code error;
        std::filesystem::create_directories(marker.parent_path(), error);
        if (!error)
        {
            std::ofstream file(marker, std::ios::binary | std::ios::trunc);
            file.close();
            if (file)
            {
                return true;
            }
        }

        FLogger::Write("WARN", "FUserSettings.cpp", kMigrationLogTag,
            "Unable to persist the legacy migration marker (error=%d)", error.value());
        return false;
    }

    bool ImportLegacyFile(const std::filesystem::path& Destination)
    {
        std::error_code error;
        if (std::filesystem::exists(Destination, error))
        {
            // 新版已经有自己的数据时始终以新版为准。
            return true;
        }
        if (error)
        {
            return false;
        }

        const std::filesystem::path source =
            Destination.parent_path().parent_path() /
            kLegacyApplicationDirectoryName / Destination.filename();
        const bool bSourceExists = std::filesystem::exists(source, error);
        if (error || !bSourceExists)
        {
            return !error;
        }

        std::filesystem::create_directories(Destination.parent_path(), error);
        if (error)
        {
            return false;
        }

        // 先完整复制到临时文件，再无覆盖地发布；中断或并发启动不能留下半份配置。
        const std::filesystem::path temporary = MakeSettingsTemporaryPath(Destination);
        bool bImported = std::filesystem::copy_file(
            source, temporary, std::filesystem::copy_options::overwrite_existing, error);
        if (bImported)
        {
            bImported = MoveFileExW(
                temporary.c_str(), Destination.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
            if (!bImported)
            {
                const DWORD moveError = GetLastError();
                bImported = moveError == ERROR_ALREADY_EXISTS || moveError == ERROR_FILE_EXISTS;
            }
        }

        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        FLogger::Write(bImported ? "INFO" : "WARN", "FUserSettings.cpp", kMigrationLogTag,
            "%s legacy %s; the original file is unchanged",
            bImported ? "Imported or kept existing" : "Unable to import",
            Destination.filename().u8string().c_str());
        return bImported;
    }

    void EnsureLegacyDataMigrated()
    {
        // 每个进程只尝试一次；失败可在下次启动重试，成功标记在清空设置后仍保留，
        // 防止用户清除全部数据后又从旧版目录恢复。标记本身不包含用户数据。
        static const bool bAttempted = []()
        {
            const std::filesystem::path marker =
                GetSettingsPath().parent_path() / kLegacyMigrationMarkerName;
            std::error_code error;
            if (std::filesystem::exists(marker, error))
            {
                return true;
            }
            if (error)
            {
                FLogger::Write("WARN", "FUserSettings.cpp", kMigrationLogTag,
                    "Unable to inspect the migration marker (error=%d)", error.value());
                return false;
            }

            const bool bSettingsImported = ImportLegacyFile(GetSettingsPath());
            const bool bCacheImported = ImportLegacyFile(GetLocalCachePath());
            if (!bSettingsImported || !bCacheImported)
            {
                FLogger::Write("WARN", "FUserSettings.cpp", kMigrationLogTag,
                    "Legacy migration incomplete; missing files will be retried next launch");
                return false;
            }
            return MarkLegacyMigrationComplete();
        }();
        (void)bAttempted;
    }

    struct FSettingsStore
    {
        FLocalization::ELanguage Language = FLocalization::kDefaultLanguage;
        std::string LastDirectory;
        std::vector<std::string> RecentFiles;
        std::vector<FUserSettings::FImageFormatPreset> ImageFormatPresets;
        size_t ImageConfigCacheCapacity = FUserSettings::kDefaultImageConfigCacheCapacity;
        FUserSettings::FThemePalette ThemePalette =
            FUserSettings::kDefaultThemePalette;
        bool bHistogramOverlayEnabled = FUserSettings::kDefaultHistogramOverlayEnabled;
        bool bHistogramLogScaleEnabled = FUserSettings::kDefaultHistogramLogScaleEnabled;
        FUserSettings::ECompareMode CompareMode = FUserSettings::kDefaultCompareMode;
        uint32_t SingleImageCompareHintDisplayCount = 0;
        FUserSettings::FWindowPlacement WindowPlacement;
        bool bHasWindowPlacement = false;
        bool bAllDataClearedThisSession = false;
        bool bLoaded = false;
    };

    std::vector<std::string> NormalizeRecentFiles(
        const std::vector<std::string>& Files)
    {
        std::vector<std::string> result;
        result.reserve(std::min(
            Files.size(),
            FUserSettings::kMaximumRecentFileCount));

        for (const std::string& path : Files)
        {
            if (path.empty()
                || std::find(result.begin(), result.end(), path) != result.end())
            {
                continue;
            }

            result.push_back(path);

            if (result.size() == FUserSettings::kMaximumRecentFileCount)
            {
                break;
            }
        }

        return result;
    }

    bool TryParseCacheCapacity(const std::string& Text, size_t& OutCapacity)
    {
        if (Text.empty() || Text.front() == '-')
        {
            return false;
        }

        char* end = nullptr;
        errno = 0;
        const unsigned long long parsed = std::strtoull(Text.c_str(), &end, 10);

        if (errno != 0 || end == Text.c_str() || (end && *end != '\0'))
        {
            return false;
        }

        OutCapacity = static_cast<size_t>(std::min<unsigned long long>(
            parsed,
            FUserSettings::kMaximumImageConfigCacheCapacity));

        return true;
    }

    bool TryParseBoolean(const std::string& Text, bool& OutValue)
    {
        if (Text == "1" || Text == "true")
        {
            OutValue = true;
            return true;
        }

        if (Text == "0" || Text == "false")
        {
            OutValue = false;
            return true;
        }

        return false;
    }

    int32_t GetHexDigitValue(char Character)
    {
        if (Character >= '0' && Character <= '9')
        {
            return Character - '0';
        }

        if (Character >= 'a' && Character <= 'f')
        {
            return Character - 'a' + kHexAlphabetFirstValue;
        }

        if (Character >= 'A' && Character <= 'F')
        {
            return Character - 'A' + kHexAlphabetFirstValue;
        }

        return kInvalidHexDigit;
    }

    bool TryParseThemeColor(
        const std::string& Text,
        FUserSettings::FThemeColor& OutColor)
    {
        const size_t digitOffset = !Text.empty() && Text.front() == '#'
            ? 1
            : 0;

        if (Text.size() != digitOffset + kThemeColorHexDigitCount)
        {
            return false;
        }

        uint32_t rgb = 0;

        for (size_t digitIndex = 0;
             digitIndex < kThemeColorHexDigitCount;
             ++digitIndex)
        {
            const int32_t digit = GetHexDigitValue(
                Text[digitOffset + digitIndex]);

            if (digit < 0)
            {
                return false;
            }

            rgb = (rgb << kBitsPerHexDigit) |
                static_cast<uint32_t>(digit);
        }

        OutColor.R = static_cast<uint8_t>(
            (rgb >> kThemeColorRedShift) & kThemeColorChannelMask);
        OutColor.G = static_cast<uint8_t>(
            (rgb >> kThemeColorGreenShift) & kThemeColorChannelMask);
        OutColor.B = static_cast<uint8_t>(rgb & kThemeColorChannelMask);
        return true;
    }

    bool TryApplyThemeColorSetting(
        const std::string& Key,
        const std::string& Value,
        FUserSettings::FThemePalette& OutPalette)
    {
        for (const FThemeColorSetting& setting : kThemeColorSettings)
        {
            if (Key != setting.Key)
            {
                continue;
            }

            FUserSettings::FThemeColor color;

            if (TryParseThemeColor(Value, color))
            {
                OutPalette.Get(setting.Role) = color;
            }

            // 已识别但值无效时同样返回 true，让调用方保留该项默认值且不误入其它 key 分支。
            return true;
        }

        return false;
    }

    std::string FormatThemeColor(
        const FUserSettings::FThemeColor& Color)
    {
        char text[kThemeColorTextBufferSize] = {};
        std::snprintf(
            text,
            sizeof(text),
            "%02X%02X%02X",
            static_cast<uint32_t>(Color.R),
            static_cast<uint32_t>(Color.G),
            static_cast<uint32_t>(Color.B));
        return text;
    }

    bool TryParseInt32(const std::string& Text, int32_t& OutValue)
    {
        if (Text.empty())
        {
            return false;
        }

        char* end = nullptr;
        errno = 0;
        const long long parsed = std::strtoll(Text.c_str(), &end, 10);

        if (errno != 0 || end == Text.c_str() || (end && *end != '\0') ||
            parsed < (std::numeric_limits<int32_t>::min)() ||
            parsed > (std::numeric_limits<int32_t>::max)())
        {
            return false;
        }

        OutValue = static_cast<int32_t>(parsed);
        return true;
    }

    bool IsAsciiWhitespace(char Character)
    {
        return Character == ' ' || Character == '\t' ||
            Character == '\r' || Character == '\n' ||
            Character == '\f' || Character == '\v';
    }

    std::string TrimImageFormatPresetName(const std::string& Name)
    {
        size_t begin = 0;
        size_t end = Name.size();

        while (begin < end && IsAsciiWhitespace(Name[begin]))
        {
            ++begin;
        }

        while (end > begin && IsAsciiWhitespace(Name[end - 1]))
        {
            --end;
        }

        return Name.substr(begin, end - begin);
    }

    bool IsValidUtf8(const std::string& Text)
    {
        size_t index = 0;

        while (index < Text.size())
        {
            const uint8_t first = static_cast<uint8_t>(Text[index]);

            if (first <= 0x7Fu)
            {
                ++index;
                continue;
            }

            size_t continuationCount = 0;

            if (first >= 0xC2u && first <= 0xDFu)
            {
                continuationCount = 1;
            }
            else if (first >= 0xE0u && first <= 0xEFu)
            {
                continuationCount = 2;
            }
            else if (first >= 0xF0u && first <= 0xF4u)
            {
                continuationCount = 3;
            }
            else
            {
                return false;
            }

            if (index + continuationCount >= Text.size())
            {
                return false;
            }

            const uint8_t second = static_cast<uint8_t>(Text[index + 1]);

            // 拒绝过长编码、UTF-16 代理项以及 U+10FFFF 之外的码点。
            if ((first == 0xE0u && second < 0xA0u) ||
                (first == 0xEDu && second > 0x9Fu) ||
                (first == 0xF0u && second < 0x90u) ||
                (first == 0xF4u && second > 0x8Fu))
            {
                return false;
            }

            for (size_t offset = 1; offset <= continuationCount; ++offset)
            {
                const uint8_t continuation =
                    static_cast<uint8_t>(Text[index + offset]);

                if (continuation < 0x80u || continuation > 0xBFu)
                {
                    return false;
                }
            }

            index += continuationCount + 1;
        }

        return true;
    }

    bool IsImageFormatPresetNameValid(const std::string& Name)
    {
        if (Name.empty() ||
            Name.size() > FUserSettings::kMaximumImageFormatPresetNameBytes ||
            Name.find("##") != std::string::npos ||
            !IsValidUtf8(Name))
        {
            return false;
        }

        for (const unsigned char character : Name)
        {
            if (character < 0x20u || character == 0x7Fu)
            {
                return false;
            }
        }

        return true;
    }

    template <typename TEnum>
    bool IsEnumInRange(TEnum Value, TEnum First, TEnum Last)
    {
        const int32_t value = static_cast<int32_t>(Value);
        return value >= static_cast<int32_t>(First) &&
            value <= static_cast<int32_t>(Last);
    }

    bool TryNormalizeImageFormatPreset(
        const FUserSettings::FImageFormatPreset& Preset,
        FUserSettings::FImageFormatPreset& OutPreset)
    {
        OutPreset = Preset;
        OutPreset.Name = TrimImageFormatPresetName(Preset.Name);

        if (!IsImageFormatPresetNameValid(OutPreset.Name))
        {
            return false;
        }

        FImageLoadParams& params = OutPreset.Params;
        const FFormatDesc& desc = FImageFormatDesc::Get(params.Format);

        if (params.Format == EImageFormat::Unknown ||
            desc.Format != params.Format ||
            desc.PlaneCount <= 0 ||
            !FImageLimits::AreDimensionsSupported(params.Width, params.Height) ||
            params.Stride < FImageLimits::kMinimumStrideBytes ||
            params.Stride > FImageLimits::kMaximumStrideBytes ||
            params.BitsPerPixel < FImageLimits::kMinimumRawBitsPerSample ||
            params.BitsPerPixel > FImageLimits::kMaximumRawBitsPerSample ||
            !IsEnumInRange(
                params.BayerPattern,
                EBayerPattern::RGGB,
                EBayerPattern::GBRG) ||
            !IsEnumInRange(
                params.ByteOrder,
                EByteOrder::LittleEndian,
                EByteOrder::BigEndian) ||
            !IsEnumInRange(
                params.SampleAlignment,
                ESampleAlignment::LeastSignificantBits,
                ESampleAlignment::MostSignificantBits))
        {
            return false;
        }

        const FBitDepthPropertyState bitDepth =
            FImageFormatDesc::ResolveBitDepth(
                params.Format,
                params.BitsPerPixel);

        // 固定格式必须等于描述表位深；可配置格式必须落在自身声明的范围内。
        if (params.BitsPerPixel != bitDepth.Value ||
            bitDepth.Value < bitDepth.Minimum ||
            bitDepth.Value > bitDepth.Maximum)
        {
            return false;
        }

        // Android packed Bayer 的解包器按 4 像素组读取，并要求偶数行。
        if (FImageFormatDesc::IsBayer(params.Format) &&
            desc.BitsPerPixelPacked > 0 &&
            ((params.Width % 4) != 0 || (params.Height % 2) != 0))
        {
            return false;
        }

        params.ConstrainStorageLayout();

        const int32_t baseStride = FImageFormatDesc::ResolveBaseStride(
            params.Format,
            params.Width,
            params.Stride);

        if (baseStride <= 0 ||
            FImageFormatDesc::CalculateFrameSize(
                params.Format,
                params.Width,
                params.Height,
                baseStride) == 0)
        {
            return false;
        }

        // UI 以具体 stride 判断当前参数是否仍匹配预设；把合法的 0（紧凑语义）
        // 规范化为实际字节数，避免应用后立刻因 0 != 具体值而失去预设名称。
        params.Stride = baseStride;
        return true;
    }

    std::string EncodeHex(const std::string& Text)
    {
        static constexpr char kHexDigits[] = "0123456789ABCDEF";
        std::string result;
        result.reserve(Text.size() * 2);

        for (const unsigned char character : Text)
        {
            result.push_back(kHexDigits[character >> 4]);
            result.push_back(kHexDigits[character & 0x0Fu]);
        }

        return result;
    }

    bool TryDecodeHex(const std::string& Text, std::string& OutText)
    {
        if ((Text.size() % 2) != 0 ||
            Text.size() / 2 > FUserSettings::kMaximumImageFormatPresetNameBytes)
        {
            return false;
        }

        std::string result;
        result.reserve(Text.size() / 2);

        for (size_t index = 0; index < Text.size(); index += 2)
        {
            const int32_t high = GetHexDigitValue(Text[index]);
            const int32_t low = GetHexDigitValue(Text[index + 1]);

            if (high < 0 || low < 0)
            {
                return false;
            }

            result.push_back(static_cast<char>((high << 4) | low));
        }

        OutText = std::move(result);
        return true;
    }

    bool TrySplitImageFormatPresetRecord(
        const std::string& Text,
        std::array<std::string, kImageFormatPresetFieldCount>& OutFields)
    {
        if (Text.size() > kMaximumImageFormatPresetRecordBytes)
        {
            return false;
        }

        size_t begin = 0;

        for (size_t fieldIndex = 0;
             fieldIndex < kImageFormatPresetFieldCount;
             ++fieldIndex)
        {
            const size_t separator = Text.find(
                kImageFormatPresetFieldSeparator,
                begin);
            const bool bLastField =
                fieldIndex + 1 == kImageFormatPresetFieldCount;

            if ((!bLastField && separator == std::string::npos) ||
                (bLastField && separator != std::string::npos))
            {
                return false;
            }

            const size_t end = bLastField ? Text.size() : separator;
            OutFields[fieldIndex] = Text.substr(begin, end - begin);
            begin = end + 1;
        }

        return true;
    }

    bool TryParseImageFormatPreset(
        const std::string& Text,
        FUserSettings::FImageFormatPreset& OutPreset)
    {
        std::array<std::string, kImageFormatPresetFieldCount> fields;

        if (!TrySplitImageFormatPresetRecord(Text, fields) ||
            fields[0] != kImageFormatPresetRecordVersion)
        {
            return false;
        }

        FUserSettings::FImageFormatPreset parsed;

        if (!TryDecodeHex(fields[1], parsed.Name))
        {
            return false;
        }

        parsed.Params.Format = FImageFormatDesc::FindByName(
            fields[2].c_str());

        int32_t bayerPattern = 0;
        int32_t byteOrder = 0;
        int32_t sampleAlignment = 0;

        if (!TryParseInt32(fields[3], parsed.Params.Width) ||
            !TryParseInt32(fields[4], parsed.Params.Height) ||
            !TryParseInt32(fields[5], parsed.Params.Stride) ||
            !TryParseInt32(fields[6], parsed.Params.BitsPerPixel) ||
            !TryParseInt32(fields[7], bayerPattern) ||
            !TryParseInt32(fields[8], byteOrder) ||
            !TryParseInt32(fields[9], sampleAlignment))
        {
            return false;
        }

        parsed.Params.BayerPattern =
            static_cast<EBayerPattern>(bayerPattern);
        parsed.Params.ByteOrder = static_cast<EByteOrder>(byteOrder);
        parsed.Params.SampleAlignment =
            static_cast<ESampleAlignment>(sampleAlignment);

        return TryNormalizeImageFormatPreset(parsed, OutPreset);
    }

    std::string FormatImageFormatPreset(
        const FUserSettings::FImageFormatPreset& Preset)
    {
        const FImageLoadParams& params = Preset.Params;
        std::string result = kImageFormatPresetRecordVersion;
        result += kImageFormatPresetFieldSeparator;
        result += EncodeHex(Preset.Name);
        result += kImageFormatPresetFieldSeparator;
        result += FImageFormatDesc::Get(params.Format).Name;
        result += kImageFormatPresetFieldSeparator;
        result += std::to_string(params.Width);
        result += kImageFormatPresetFieldSeparator;
        result += std::to_string(params.Height);
        result += kImageFormatPresetFieldSeparator;
        result += std::to_string(params.Stride);
        result += kImageFormatPresetFieldSeparator;
        result += std::to_string(params.BitsPerPixel);
        result += kImageFormatPresetFieldSeparator;
        result += std::to_string(static_cast<int32_t>(params.BayerPattern));
        result += kImageFormatPresetFieldSeparator;
        result += std::to_string(static_cast<int32_t>(params.ByteOrder));
        result += kImageFormatPresetFieldSeparator;
        result += std::to_string(static_cast<int32_t>(params.SampleAlignment));
        return result;
    }

    bool TryParseCompareMode(
        const std::string& Text,
        FUserSettings::ECompareMode& OutMode)
    {
        if (Text == kSingleImageSwitchModeValue)
        {
            OutMode = FUserSettings::ECompareMode::SingleImageSwitch;
            return true;
        }

        if (Text == kSideBySideModeValue)
        {
            OutMode = FUserSettings::ECompareMode::SideBySide;
            return true;
        }

        return false;
    }

    const char* GetCompareModeValue(FUserSettings::ECompareMode Mode)
    {
        return Mode == FUserSettings::ECompareMode::SideBySide
            ? kSideBySideModeValue
            : kSingleImageSwitchModeValue;
    }

    bool TryParseSingleImageCompareHintDisplayCount(
        const std::string& Text,
        uint32_t& OutCount)
    {
        if (Text.empty() || Text.front() == '-')
        {
            return false;
        }

        char* end = nullptr;
        errno = 0;
        const unsigned long long parsed = std::strtoull(Text.c_str(), &end, 10);

        if (errno != 0 || end == Text.c_str() || (end && *end != '\0'))
        {
            return false;
        }

        OutCount = static_cast<uint32_t>(std::min<unsigned long long>(
            parsed,
            FUserSettings::kSingleImageCompareHintDisplayLimit));
        return true;
    }

    FSettingsStore& Store()
    {
        static FSettingsStore Instance;

        return Instance;
    }

    void EnsureLoaded()
    {
        FSettingsStore& store = Store();

        if (store.bLoaded)
        {
            return;
        }

        EnsureLegacyDataMigrated();

        // 文件不存在是正常情况（首次运行），置位后不再重试
        store.bLoaded = true;

        std::ifstream file(GetSettingsPath());

        if (!file.is_open())
        {
            return;
        }

        std::string line;
        FUserSettings::FWindowPlacement parsedWindowPlacement;
        bool bHasWindowPositionX = false;
        bool bHasWindowPositionY = false;
        bool bHasWindowWidth = false;
        bool bHasWindowHeight = false;

        while (std::getline(file, line))
        {
            const size_t separator = line.find('=');

            if (separator == std::string::npos)
            {
                continue;
            }

            const std::string key = line.substr(0, separator);
            std::string value = line.substr(separator + 1);

            // 兼容被别的编辑器改成 CRLF 的情况
            while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
            {
                value.pop_back();
            }

            if (key == kLastDirectoryKey)
            {
                store.LastDirectory = value;
            }
            else if (key == kRecentFileKey)
            {
                // 重复 key 按文件中的先后顺序组成“最近优先”列表。
                std::vector<std::string> candidates = store.RecentFiles;
                candidates.push_back(value);
                store.RecentFiles = NormalizeRecentFiles(candidates);
            }
            else if (key == kImageFormatPresetKey)
            {
                FUserSettings::FImageFormatPreset preset;

                if (store.ImageFormatPresets.size() >=
                        FUserSettings::kMaximumImageFormatPresetCount ||
                    !TryParseImageFormatPreset(value, preset))
                {
                    continue;
                }

                const auto duplicate = std::find_if(
                    store.ImageFormatPresets.begin(),
                    store.ImageFormatPresets.end(),
                    [&preset](const FUserSettings::FImageFormatPreset& Existing)
                    {
                        return Existing.Name == preset.Name;
                    });

                // 文件按最近保存优先写入；手工制造重复记录时保留第一条。
                if (duplicate == store.ImageFormatPresets.end())
                {
                    store.ImageFormatPresets.push_back(std::move(preset));
                }
            }
            else if (key == kImageConfigCacheCapacityKey)
            {
                size_t capacity = 0;

                if (TryParseCacheCapacity(value, capacity))
                {
                    store.ImageConfigCacheCapacity = capacity;
                }
            }
            else if (TryApplyThemeColorSetting(
                    key,
                    value,
                    store.ThemePalette))
            {
                // 主题 key 已由稳定描述表处理。
            }
            else if (key == kHistogramOverlayEnabledKey)
            {
                bool bEnabled = FUserSettings::kDefaultHistogramOverlayEnabled;

                if (TryParseBoolean(value, bEnabled))
                {
                    store.bHistogramOverlayEnabled = bEnabled;
                }
            }
            else if (key == kHistogramLogScaleEnabledKey)
            {
                bool bEnabled = FUserSettings::kDefaultHistogramLogScaleEnabled;

                if (TryParseBoolean(value, bEnabled))
                {
                    store.bHistogramLogScaleEnabled = bEnabled;
                }
            }
            else if (key == kLanguageKey)
            {
                if (value == FLocalization::kEnglishLanguageCode)
                {
                    store.Language = FLocalization::ELanguage::English;
                }
                else if (value == FLocalization::kChineseLanguageCode)
                {
                    store.Language = FLocalization::ELanguage::SimplifiedChinese;
                }
                else
                {
                    FLogger::Write("WARN", "FUserSettings.cpp", kLanguageLogTag,
                        "Unsupported language setting ignored");
                }
            }
            else if (key == kCompareModeKey)
            {
                FUserSettings::ECompareMode mode =
                    FUserSettings::kDefaultCompareMode;

                if (TryParseCompareMode(value, mode))
                {
                    store.CompareMode = mode;
                }
            }
            else if (key == kSingleImageCompareHintDisplayCountKey)
            {
                uint32_t displayCount = 0;

                if (TryParseSingleImageCompareHintDisplayCount(
                        value,
                        displayCount))
                {
                    store.SingleImageCompareHintDisplayCount = displayCount;
                }
            }
            else if (key == kWindowPositionXKey)
            {
                bHasWindowPositionX = TryParseInt32(
                    value,
                    parsedWindowPlacement.X);
            }
            else if (key == kWindowPositionYKey)
            {
                bHasWindowPositionY = TryParseInt32(
                    value,
                    parsedWindowPlacement.Y);
            }
            else if (key == kWindowWidthKey)
            {
                bHasWindowWidth = TryParseInt32(
                    value,
                    parsedWindowPlacement.Width);
            }
            else if (key == kWindowHeightKey)
            {
                bHasWindowHeight = TryParseInt32(
                    value,
                    parsedWindowPlacement.Height);
            }
            else if (key == kWindowMaximizedKey)
            {
                TryParseBoolean(value, parsedWindowPlacement.bMaximized);
            }
        }

        // 窗口边界必须作为完整快照恢复；只读到一半通常意味着旧版本配置或手工编辑损坏。
        store.bHasWindowPlacement =
            bHasWindowPositionX && bHasWindowPositionY &&
            bHasWindowWidth && bHasWindowHeight &&
            parsedWindowPlacement.Width > 0 &&
            parsedWindowPlacement.Height > 0;

        if (store.bHasWindowPlacement)
        {
            store.WindowPlacement = parsedWindowPlacement;
        }
    }

    bool Save()
    {
        const std::filesystem::path path = GetSettingsPath();

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        if (ec)
        {
            return false;
        }

        const std::filesystem::path temporaryPath =
            MakeSettingsTemporaryPath(path);
        std::ofstream file(
            temporaryPath,
            std::ios::binary | std::ios::trunc);

        if (!file.is_open())
        {
            return false;
        }

        file << kLastDirectoryKey << "=" << Store().LastDirectory << "\n";
        file << kLanguageKey << "=" << FLocalization::LanguageCode(Store().Language) << "\n";
        file << kImageConfigCacheCapacityKey << "="
             << Store().ImageConfigCacheCapacity << "\n";
        for (const FThemeColorSetting& setting : kThemeColorSettings)
        {
            file << setting.Key << "="
                 << FormatThemeColor(Store().ThemePalette.Get(setting.Role))
                 << "\n";
        }
        file << kHistogramOverlayEnabledKey << "="
             << (Store().bHistogramOverlayEnabled ? 1 : 0) << "\n";
        file << kHistogramLogScaleEnabledKey << "="
             << (Store().bHistogramLogScaleEnabled ? 1 : 0) << "\n";
        file << kCompareModeKey << "="
             << GetCompareModeValue(Store().CompareMode) << "\n";
        file << kSingleImageCompareHintDisplayCountKey << "="
             << Store().SingleImageCompareHintDisplayCount << "\n";

        if (Store().bHasWindowPlacement)
        {
            const FUserSettings::FWindowPlacement& placement =
                Store().WindowPlacement;
            file << kWindowPositionXKey << "=" << placement.X << "\n";
            file << kWindowPositionYKey << "=" << placement.Y << "\n";
            file << kWindowWidthKey << "=" << placement.Width << "\n";
            file << kWindowHeightKey << "=" << placement.Height << "\n";
            file << kWindowMaximizedKey << "="
                 << (placement.bMaximized ? 1 : 0) << "\n";
        }

        for (const FUserSettings::FImageFormatPreset& preset :
             Store().ImageFormatPresets)
        {
            file << kImageFormatPresetKey << "="
                 << FormatImageFormatPreset(preset) << "\n";
        }

        for (const std::string& recentPath : Store().RecentFiles)
        {
            file << kRecentFileKey << "=" << recentPath << "\n";
        }

        file.flush();
        bool bSuccess = file.good();
        file.close();
        bSuccess = bSuccess && !file.fail();

        if (bSuccess)
        {
            // 临时文件与设置文件位于同一目录。直接替换不会先删除旧文件；
            // 若目标被占用或替换失败，旧 settings.ini 仍保持完整。
            bSuccess = MoveFileExW(
                temporaryPath.c_str(),
                path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
        }

        if (!bSuccess)
        {
            ec.clear();
            std::filesystem::remove(temporaryPath, ec);
            return false;
        }

        Store().bAllDataClearedThisSession = false;
        return true;
    }

    bool RemoveFileIfPresent(const std::filesystem::path& Path)
    {
        std::error_code ec;
        const bool bExists = std::filesystem::exists(Path, ec);

        if (ec)
        {
            return false;
        }

        if (!bExists)
        {
            return true;
        }

        const bool bRemoved = std::filesystem::remove(Path, ec);
        return bRemoved && !ec;
    }
}

namespace FUserSettings
{
    FLocalization::ELanguage GetLanguage()
    {
        EnsureLoaded();
        return Store().Language;
    }

    bool SetLanguage(FLocalization::ELanguage Language)
    {
        EnsureLoaded();
        if (Language != FLocalization::ELanguage::SimplifiedChinese &&
            Language != FLocalization::ELanguage::English)
        {
            FLogger::Write("WARN", "FUserSettings.cpp", kLanguageLogTag,
                "Invalid language selection rejected");
            return false;
        }
        const auto previous = Store().Language;
        if (previous == Language)
        {
            return true;
        }
        Store().Language = Language;
        if (!Save())
        {
            Store().Language = previous;
            FLogger::Write("ERROR", "FUserSettings.cpp", kLanguageLogTag,
                "Could not persist language; previous selection retained");
            return false;
        }
        FLogger::Write("INFO", "FUserSettings.cpp", kLanguageLogTag,
            "Language saved: %s", FLocalization::LanguageCode(Language));
        return true;
    }

    bool TryGetWindowPlacement(FWindowPlacement& OutPlacement)
    {
        EnsureLoaded();

        if (!Store().bHasWindowPlacement)
        {
            return false;
        }

        OutPlacement = Store().WindowPlacement;
        return true;
    }

    void SetWindowPlacement(const FWindowPlacement& Placement)
    {
        EnsureLoaded();

        if (Placement.Width <= 0 || Placement.Height <= 0)
        {
            return;
        }

        const FWindowPlacement& current = Store().WindowPlacement;
        const bool bUnchanged = Store().bHasWindowPlacement &&
            current.X == Placement.X &&
            current.Y == Placement.Y &&
            current.Width == Placement.Width &&
            current.Height == Placement.Height &&
            current.bMaximized == Placement.bMaximized;

        if (bUnchanged)
        {
            return;
        }

        Store().WindowPlacement = Placement;
        Store().bHasWindowPlacement = true;
        Save();
    }

    const std::string& GetLastDirectory()
    {
        EnsureLoaded();

        return Store().LastDirectory;
    }

    void SetLastDirectory(const std::string& Directory)
    {
        EnsureLoaded();

        if (Store().LastDirectory == Directory)
        {
            return;
        }

        Store().LastDirectory = Directory;

        Save();
    }

    const std::vector<std::string>& GetRecentFiles()
    {
        EnsureLoaded();

        return Store().RecentFiles;
    }

    void SetRecentFiles(const std::vector<std::string>& Files)
    {
        EnsureLoaded();

        std::vector<std::string> normalized = NormalizeRecentFiles(Files);

        if (Store().RecentFiles == normalized)
        {
            return;
        }

        Store().RecentFiles = std::move(normalized);
        Save();
    }

    const std::vector<FImageFormatPreset>& GetImageFormatPresets()
    {
        EnsureLoaded();
        return Store().ImageFormatPresets;
    }

    bool SaveImageFormatPreset(const FImageFormatPreset& Preset)
    {
        EnsureLoaded();

        FImageFormatPreset normalized;

        if (!TryNormalizeImageFormatPreset(Preset, normalized))
        {
            return false;
        }

        std::vector<FImageFormatPreset>& presets =
            Store().ImageFormatPresets;
        const std::vector<FImageFormatPreset> previous = presets;

        presets.erase(
            std::remove_if(
                presets.begin(),
                presets.end(),
                [&normalized](const FImageFormatPreset& Existing)
                {
                    return Existing.Name == normalized.Name;
                }),
            presets.end());
        presets.insert(presets.begin(), std::move(normalized));

        if (presets.size() > kMaximumImageFormatPresetCount)
        {
            presets.resize(kMaximumImageFormatPresetCount);
        }

        if (presets == previous)
        {
            // 即使内存内容未变，也要确保设置文件仍存在；用户或清理工具可能
            // 在程序运行期间删除了 settings.ini。
            return Save();
        }

        if (Save())
        {
            return true;
        }

        // 弹窗会把 false 当作保存失败；同时回滚内存，避免列表显示一个重启后会消失的预设。
        presets = previous;
        return false;
    }

    size_t GetImageConfigCacheCapacity()
    {
        EnsureLoaded();

        return Store().ImageConfigCacheCapacity;
    }

    void SetImageConfigCacheCapacity(size_t Capacity)
    {
        EnsureLoaded();

        const size_t constrained =
            std::min(Capacity, kMaximumImageConfigCacheCapacity);

        if (Store().ImageConfigCacheCapacity == constrained)
        {
            return;
        }

        Store().ImageConfigCacheCapacity = constrained;
        Save();
    }

    FThemePalette GetThemePalette()
    {
        EnsureLoaded();
        return Store().ThemePalette;
    }

    void SetThemePalette(const FThemePalette& Palette)
    {
        EnsureLoaded();

        if (Store().ThemePalette == Palette)
        {
            return;
        }

        Store().ThemePalette = Palette;
        Save();
    }

    FThemeColor GetThemeAccentColor()
    {
        return GetThemePalette().Get(EThemeColorRole::Accent);
    }

    void SetThemeAccentColor(const FThemeColor& Color)
    {
        FThemePalette palette = GetThemePalette();
        palette.Get(EThemeColorRole::Accent) = Color;
        SetThemePalette(palette);
    }

    bool GetHistogramOverlayEnabled()
    {
        EnsureLoaded();

        return Store().bHistogramOverlayEnabled;
    }

    void SetHistogramOverlayEnabled(bool bEnabled)
    {
        EnsureLoaded();

        if (Store().bHistogramOverlayEnabled == bEnabled)
        {
            return;
        }

        Store().bHistogramOverlayEnabled = bEnabled;
        Save();
    }

    bool GetHistogramLogScaleEnabled()
    {
        EnsureLoaded();

        return Store().bHistogramLogScaleEnabled;
    }

    void SetHistogramLogScaleEnabled(bool bEnabled)
    {
        EnsureLoaded();

        if (Store().bHistogramLogScaleEnabled == bEnabled)
        {
            return;
        }

        Store().bHistogramLogScaleEnabled = bEnabled;
        Save();
    }

    ECompareMode GetCompareMode()
    {
        EnsureLoaded();

        return Store().CompareMode;
    }

    void SetCompareMode(ECompareMode Mode)
    {
        EnsureLoaded();

        if (Mode != ECompareMode::SingleImageSwitch &&
            Mode != ECompareMode::SideBySide)
        {
            return;
        }

        if (Store().CompareMode == Mode)
        {
            return;
        }

        Store().CompareMode = Mode;
        Save();
    }

    uint32_t GetSingleImageCompareHintDisplayCount()
    {
        EnsureLoaded();

        return Store().SingleImageCompareHintDisplayCount;
    }

    bool ConsumeSingleImageCompareHintDisplay()
    {
        EnsureLoaded();

        if (Store().SingleImageCompareHintDisplayCount >=
            kSingleImageCompareHintDisplayLimit)
        {
            return false;
        }

        ++Store().SingleImageCompareHintDisplayCount;
        Save();
        return true;
    }

    bool ClearAllData()
    {
        EnsureLoaded();

        // 即使首次导入失败，也必须先阻止后续启动重新导入，才能安全执行清空。
        if (!MarkLegacyMigrationComplete())
        {
            return false;
        }

        const std::filesystem::path settingsPath = GetSettingsPath();
        const std::filesystem::path cachePath = GetImageConfigCachePath();
        const bool bSettingsRemoved = RemoveFileIfPresent(settingsPath);
        const bool bCacheRemoved = RemoveFileIfPresent(cachePath);

        // 内存也必须同步复位，否则本次进程后续的 getter 仍会暴露已清除的数据。
        FSettingsStore clearedStore;
        clearedStore.bLoaded = true;
        clearedStore.bAllDataClearedThisSession =
            bSettingsRemoved && bCacheRemoved;
        Store() = std::move(clearedStore);

        return bSettingsRemoved && bCacheRemoved;
    }

    bool WasAllDataClearedThisSession()
    {
        EnsureLoaded();
        return Store().bAllDataClearedThisSession;
    }

    std::filesystem::path GetImageConfigCachePath()
    {
        EnsureLegacyDataMigrated();
        return GetLocalCachePath();
    }
}
