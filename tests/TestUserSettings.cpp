#include "Core/FUserSettings.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    constexpr size_t kAdditionalRecentFileCount = 3;
    constexpr size_t kTestImageConfigCacheCapacity = 37;
    constexpr bool kTestHistogramOverlayEnabled = false;
    constexpr bool kTestHistogramLogScaleEnabled = true;
    constexpr FUserSettings::ECompareMode kTestCompareMode =
        FUserSettings::ECompareMode::SideBySide;
    constexpr uint32_t kThemeColorChannelValueCount = 256;
    constexpr uint32_t kTestThemeRedBase = 19;
    constexpr uint32_t kTestThemeRedStep = 37;
    constexpr uint32_t kTestThemeGreenBase = 53;
    constexpr uint32_t kTestThemeGreenStep = 61;
    constexpr uint32_t kTestThemeBlueBase = 91;
    constexpr uint32_t kTestThemeBlueStep = 29;
    constexpr size_t kExpectedThemeColorRoleCount = 6;
    constexpr int32_t kTestWindowX = -720;
    constexpr int32_t kTestWindowY = 135;
    constexpr int32_t kTestWindowWidth = 1460;
    constexpr int32_t kTestWindowHeight = 920;
    constexpr const char* kVerifyPopulatedMode = "--verify-populated";
    constexpr const char* kVerifyEmptyMode = "--verify-empty";
    constexpr const char* kVerifyClearedMode = "--verify-cleared";
    constexpr const char* kVerifyCappedPresetsMode = "--verify-capped-presets";
    constexpr const char* kThemeSettingPrefix = "Theme";
    constexpr const char* kImageFormatPresetSettingPrefix =
        "ImageFormatPreset=";
    constexpr const char* kPrimaryPresetName = u8"相机=预设|一";
    constexpr const char* kSecondaryPresetName = "RGB16 big-endian";

    int GFailures = 0;

    FUserSettings::FThemePalette BuildTestThemePalette()
    {
        FUserSettings::FThemePalette result =
            FUserSettings::kDefaultThemePalette;

        for (size_t colorIndex = 0;
             colorIndex < FUserSettings::kThemeColorRoleCount;
             ++colorIndex)
        {
            result.Colors[colorIndex] = {
                static_cast<uint8_t>((
                    kTestThemeRedBase + colorIndex * kTestThemeRedStep)
                    % kThemeColorChannelValueCount),
                static_cast<uint8_t>((
                    kTestThemeGreenBase + colorIndex * kTestThemeGreenStep)
                    % kThemeColorChannelValueCount),
                static_cast<uint8_t>((
                    kTestThemeBlueBase + colorIndex * kTestThemeBlueStep)
                    % kThemeColorChannelValueCount),
            };
        }

        return result;
    }

    const FUserSettings::FThemePalette kTestThemePalette =
        BuildTestThemePalette();

    FUserSettings::FImageFormatPreset BuildPrimaryPreset(bool bUpdated)
    {
        FUserSettings::FImageFormatPreset preset;
        preset.Name = bUpdated
            ? kPrimaryPresetName
            : std::string("  ") + kPrimaryPresetName + "  ";

        if (!bUpdated)
        {
            preset.Params.Format = EImageFormat::NV21;
            preset.Params.Width = 1440;
            preset.Params.Height = 1920;
            preset.Params.Stride = 0;
            preset.Params.BitsPerPixel = 8;
            return preset;
        }

        preset.Params.Format = EImageFormat::YUV420SP16;
        preset.Params.Width = 4096;
        preset.Params.Height = 3072;
        preset.Params.Stride = 8192;
        preset.Params.BitsPerPixel = 10;
        preset.Params.BayerPattern = EBayerPattern::GBRG;
        preset.Params.ByteOrder = EByteOrder::BigEndian;
        preset.Params.SampleAlignment =
            ESampleAlignment::MostSignificantBits;
        return preset;
    }

    FUserSettings::FImageFormatPreset BuildSecondaryPreset()
    {
        FUserSettings::FImageFormatPreset preset;
        preset.Name = kSecondaryPresetName;
        preset.Params.Format = EImageFormat::RGB16;
        preset.Params.Width = 1920;
        preset.Params.Height = 1080;
        preset.Params.Stride = 11584;
        preset.Params.BitsPerPixel = 16;
        preset.Params.ByteOrder = EByteOrder::BigEndian;
        return preset;
    }

    std::vector<FUserSettings::FImageFormatPreset> BuildExpectedPresets()
    {
        return {
            BuildPrimaryPreset(true),
            BuildSecondaryPreset(),
        };
    }

    FUserSettings::FImageFormatPreset BuildCapacityPreset(size_t Index)
    {
        FUserSettings::FImageFormatPreset preset;
        preset.Name = "capacity_" + std::to_string(Index);
        preset.Params.Format = EImageFormat::RGB8;
        preset.Params.Width = 64;
        preset.Params.Height = 32;
        preset.Params.Stride = 192;
        preset.Params.BitsPerPixel = 8;
        return preset;
    }

    size_t CountPersistedThemeSettings(
        const std::filesystem::path& SettingsPath)
    {
        std::ifstream file(SettingsPath);
        std::string line;
        size_t count = 0;

        while (std::getline(file, line))
        {
            if (line.rfind(kThemeSettingPrefix, 0) == 0)
            {
                ++count;
            }
        }

        return count;
    }

    size_t CountPersistedPresetSettings(
        const std::filesystem::path& SettingsPath)
    {
        std::ifstream file(SettingsPath);
        std::string line;
        size_t count = 0;

        while (std::getline(file, line))
        {
            if (line.rfind(kImageFormatPresetSettingPrefix, 0) == 0)
            {
                ++count;
            }
        }

        return count;
    }

    std::string ReadFileText(const std::filesystem::path& Path)
    {
        std::ifstream file(Path, std::ios::binary);
        return std::string(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>());
    }

    void Check(bool bCondition, const char* Message)
    {
        std::printf("%-64s %s\n", Message, bCondition ? "OK" : "**FAIL**");

        if (!bCondition)
        {
            ++GFailures;
        }
    }

    std::vector<std::string> BuildExpectedRecentFiles()
    {
        std::vector<std::string> result;
        result.reserve(FUserSettings::kMaximumRecentFileCount);
        result.push_back(u8"C:/图片/带=号.raw");

        for (size_t index = 1;
             index < FUserSettings::kMaximumRecentFileCount;
             ++index)
        {
            result.push_back(
                "C:/images/recent_" + std::to_string(index) + ".raw");
        }

        return result;
    }

    std::vector<std::string> BuildOversizedRecentFiles()
    {
        const std::vector<std::string> expected = BuildExpectedRecentFiles();
        std::vector<std::string> result;
        result.reserve(
            expected.size() + kAdditionalRecentFileCount + 2);

        result.push_back(expected[0]);
        result.push_back(expected[1]);
        result.push_back(expected[0]);
        result.emplace_back();
        result.insert(result.end(), expected.begin() + 2, expected.end());

        for (size_t index = 0; index < kAdditionalRecentFileCount; ++index)
        {
            result.push_back(
                "C:/images/overflow_" + std::to_string(index) + ".raw");
        }

        return result;
    }

    int VerifyPopulatedSettings()
    {
        Check(FUserSettings::GetLanguage() == FLocalization::ELanguage::English,
            "English survives a fresh process and other preference changes");
        FUserSettings::FWindowPlacement placement;

        Check(
            FUserSettings::GetImageConfigCacheCapacity()
                == kTestImageConfigCacheCapacity,
            "saving recent files preserves other user settings");
        Check(
            FUserSettings::GetRecentFiles() == BuildExpectedRecentFiles(),
            "recent files survive a fresh process in MRU order");
        Check(
            FUserSettings::GetImageFormatPresets() == BuildExpectedPresets(),
            "format presets survive a fresh process with every load parameter");
        Check(
            FUserSettings::GetHistogramOverlayEnabled()
                == kTestHistogramOverlayEnabled,
            "histogram overlay preference survives a fresh process");
        Check(
            FUserSettings::GetHistogramLogScaleEnabled()
                == kTestHistogramLogScaleEnabled,
            "histogram log-scale preference survives a fresh process");
        Check(
            FUserSettings::GetCompareMode() == kTestCompareMode,
            "compare mode survives a fresh process");
        Check(
            FUserSettings::GetThemePalette() == kTestThemePalette,
            "all editable theme colors survive a fresh process");
        Check(
            FUserSettings::GetSingleImageCompareHintDisplayCount()
                == FUserSettings::kSingleImageCompareHintDisplayLimit,
            "single-image compare hint count survives a fresh process");
        Check(
            FUserSettings::TryGetWindowPlacement(placement) &&
                placement.X == kTestWindowX &&
                placement.Y == kTestWindowY &&
                placement.Width == kTestWindowWidth &&
                placement.Height == kTestWindowHeight &&
                placement.bMaximized,
            "window placement survives a fresh process");

        return GFailures == 0 ? 0 : 1;
    }

    int VerifyEmptySettings()
    {
        Check(FUserSettings::GetLanguage() == FLocalization::ELanguage::English,
            "clearing recent files preserves the language");
        FUserSettings::FWindowPlacement placement;

        Check(
            FUserSettings::GetRecentFiles().empty(),
            "cleared recent files stay empty in a fresh process");
        Check(
            FUserSettings::GetImageFormatPresets() == BuildExpectedPresets(),
            "clearing recent files preserves format presets");
        Check(
            FUserSettings::GetHistogramOverlayEnabled()
                == kTestHistogramOverlayEnabled,
            "clearing recent files preserves histogram overlay preference");
        Check(
            FUserSettings::GetHistogramLogScaleEnabled()
                == kTestHistogramLogScaleEnabled,
            "clearing recent files preserves histogram log-scale preference");
        Check(
            FUserSettings::GetCompareMode() == kTestCompareMode,
            "clearing recent files preserves compare mode");
        Check(
            FUserSettings::GetThemePalette() == kTestThemePalette,
            "clearing recent files preserves the six base theme colors");
        Check(
            FUserSettings::GetSingleImageCompareHintDisplayCount()
                == FUserSettings::kSingleImageCompareHintDisplayLimit,
            "clearing recent files preserves compare hint count");
        Check(
            FUserSettings::TryGetWindowPlacement(placement) &&
                placement.X == kTestWindowX &&
                placement.Y == kTestWindowY &&
                placement.Width == kTestWindowWidth &&
                placement.Height == kTestWindowHeight &&
                placement.bMaximized,
            "clearing recent files preserves window placement");

        return GFailures == 0 ? 0 : 1;
    }

    int VerifyClearedSettings()
    {
        Check(FUserSettings::GetLanguage() == FLocalization::kDefaultLanguage,
            "clear all restores the default language in a fresh process");
        FUserSettings::FWindowPlacement placement;

        Check(
            FUserSettings::GetRecentFiles().empty(),
            "clearing all data removes recent files in a fresh process");
        Check(
            FUserSettings::GetImageFormatPresets().empty(),
            "clearing all data removes format presets in a fresh process");
        Check(
            FUserSettings::GetImageConfigCacheCapacity() ==
                FUserSettings::kDefaultImageConfigCacheCapacity,
            "clearing all data restores the default cache capacity");
        Check(
            FUserSettings::GetCompareMode() ==
                FUserSettings::kDefaultCompareMode,
            "clearing all data restores the default compare mode");
        Check(
            FUserSettings::GetThemePalette() ==
                FUserSettings::kDefaultThemePalette,
            "clearing all data restores the default theme palette");
        Check(
            FUserSettings::GetSingleImageCompareHintDisplayCount() == 0,
            "clearing all data resets the compare hint count");
        Check(
            !FUserSettings::TryGetWindowPlacement(placement),
            "clearing all data removes window placement");
        Check(
            !std::filesystem::exists(
                FUserSettings::GetImageConfigCachePath()),
            "clearing all data removes the image property cache file");

        return GFailures == 0 ? 0 : 1;
    }

    int VerifyCappedPresets()
    {
        const std::vector<FUserSettings::FImageFormatPreset>& presets =
            FUserSettings::GetImageFormatPresets();

        Check(
            presets.size() == FUserSettings::kMaximumImageFormatPresetCount,
            "format preset cap survives a fresh process");
        Check(
            !presets.empty() && presets.front() ==
                BuildCapacityPreset(FUserSettings::kMaximumImageFormatPresetCount),
            "newest capped preset stays at the front after reload");
        Check(
            !presets.empty() && presets.back() == BuildCapacityPreset(1),
            "capped preset reload preserves newest-first eviction order");

        return GFailures == 0 ? 0 : 1;
    }

    int RunChild(const std::filesystem::path& Executable, const char* Mode)
    {
        std::wstring command = L"\"" + Executable.wstring() + L"\" ";
        command += std::filesystem::u8path(Mode).wstring();

        return _wsystem(command.c_str());
    }
}

int main(int ArgCount, char** Arguments)
{
    constexpr const char* kVerifyChineseMode = "--verify-chinese-language";
    static_assert(FLocalization::kDefaultLanguage == FLocalization::ELanguage::English,
        "fresh settings must default to English");
    if (ArgCount > 1 && std::string(Arguments[1]) == kVerifyChineseMode)
    {
        Check(FUserSettings::GetLanguage() == FLocalization::ELanguage::SimplifiedChinese,
            "saved Chinese preference overrides English default in a fresh process");
        return GFailures == 0 ? 0 : 1;
    }
    static_assert(
        FUserSettings::kMaximumRecentFileCount == 15,
        "recent file retention must remain 15 entries");
    static_assert(
        FUserSettings::kMaximumImageFormatPresetCount == 64,
        "format preset retention must remain bounded");
    static_assert(
        FUserSettings::kMaximumImageFormatPresetNameBytes == 128,
        "format preset names must have a documented UTF-8 byte limit");
    static_assert(
        FUserSettings::kSingleImageCompareHintDisplayLimit == 3,
        "single-image compare hint must be limited to three uses");
    static_assert(
        FUserSettings::kDefaultThemeAccentColor.R == 0 &&
            FUserSettings::kDefaultThemeAccentColor.G == 118 &&
            FUserSettings::kDefaultThemeAccentColor.B == 161,
        "the default theme accent color must remain #0076A1");
    static_assert(
        FUserSettings::kThemeColorRoleCount == kExpectedThemeColorRoleCount,
        "only the six necessary theme colors should remain user-editable");

    if (ArgCount > 1 && std::string(Arguments[1]) == kVerifyPopulatedMode)
    {
        return VerifyPopulatedSettings();
    }

    if (ArgCount > 1 && std::string(Arguments[1]) == kVerifyEmptyMode)
    {
        return VerifyEmptySettings();
    }

    if (ArgCount > 1 && std::string(Arguments[1]) == kVerifyClearedMode)
    {
        return VerifyClearedSettings();
    }

    if (ArgCount > 1 && std::string(Arguments[1]) == kVerifyCappedPresetsMode)
    {
        return VerifyCappedPresets();
    }

    const auto uniqueValue = std::chrono::high_resolution_clock::now()
        .time_since_epoch().count();
    const std::filesystem::path testRoot =
        std::filesystem::temp_directory_path()
        / ("YUVRaw_UserSettings_" + std::to_string(uniqueValue));
    const std::filesystem::path testAppData = testRoot / "Roaming";
    const std::filesystem::path testLocalAppData = testRoot / "Local";
    const std::filesystem::path executable =
        std::filesystem::absolute(std::filesystem::u8path(Arguments[0]));

    Check(
        _wputenv_s(L"APPDATA", testAppData.c_str()) == 0,
        "test redirects APPDATA away from the real user profile");
    Check(
        _wputenv_s(L"LOCALAPPDATA", testLocalAppData.c_str()) == 0,
        "test redirects LOCALAPPDATA away from the real user profile");

    Check(FUserSettings::GetLanguage() == FLocalization::kDefaultLanguage,
        "missing language uses English");
    Check(FUserSettings::SetLanguage(FLocalization::ELanguage::English),
        "English language preference can be saved");
    Check(FUserSettings::SetLanguage(FLocalization::ELanguage::SimplifiedChinese) &&
        FUserSettings::GetLanguage() == FLocalization::ELanguage::SimplifiedChinese,
        "switching back to Chinese is supported");
    Check(RunChild(executable, kVerifyChineseMode) == 0,
        "Chinese language preference survives restart");
    Check(FUserSettings::SetLanguage(FLocalization::ELanguage::English),
        "English can be selected again");
    constexpr auto kInvalidLanguage = static_cast<FLocalization::ELanguage>(-1);
    Check(!FUserSettings::SetLanguage(kInvalidLanguage) &&
        FUserSettings::GetLanguage() == FLocalization::ELanguage::English,
        "invalid language selection is rejected without changing preferences");

    Check(
        !FUserSettings::GetHistogramOverlayEnabled(),
        "missing settings disable histogram overlay by default");
    Check(
        FUserSettings::GetHistogramLogScaleEnabled()
            == FUserSettings::kDefaultHistogramLogScaleEnabled,
        "missing settings use the default histogram log-scale preference");
    Check(
        FUserSettings::GetCompareMode() == FUserSettings::kDefaultCompareMode,
        "missing settings use single-image switching by default");
    Check(
        FUserSettings::GetThemePalette() ==
            FUserSettings::kDefaultThemePalette,
        "missing settings use the six default base theme colors");
    Check(
        FUserSettings::GetSingleImageCompareHintDisplayCount() == 0,
        "missing settings have not displayed the compare hint");
    Check(
        FUserSettings::GetImageFormatPresets().empty(),
        "missing settings have no format presets");
    FUserSettings::FWindowPlacement missingPlacement;
    Check(
        !FUserSettings::TryGetWindowPlacement(missingPlacement),
        "missing settings have no saved window placement");

    for (uint32_t displayIndex = 0;
         displayIndex < FUserSettings::kSingleImageCompareHintDisplayLimit;
         ++displayIndex)
    {
        Check(
            FUserSettings::ConsumeSingleImageCompareHintDisplay(),
            "compare hint is available within its display limit");
    }

    Check(
        !FUserSettings::ConsumeSingleImageCompareHintDisplay(),
        "compare hint is suppressed after three displays");

    const std::vector<FUserSettings::FImageFormatPreset> emptyPresets =
        FUserSettings::GetImageFormatPresets();
    FUserSettings::FImageFormatPreset invalidPreset = BuildSecondaryPreset();
    invalidPreset.Name = "   ";
    Check(
        !FUserSettings::SaveImageFormatPreset(invalidPreset),
        "format preset rejects an empty trimmed name");
    invalidPreset = BuildSecondaryPreset();
    invalidPreset.Name = "hidden##identifier";
    Check(
        !FUserSettings::SaveImageFormatPreset(invalidPreset),
        "format preset rejects ImGui hidden-label separators");
    invalidPreset = BuildSecondaryPreset();
    invalidPreset.Name = std::string{
        static_cast<char>(0xC0),
        static_cast<char>(0xAF),
    };
    Check(
        !FUserSettings::SaveImageFormatPreset(invalidPreset),
        "format preset rejects malformed UTF-8 names");
    invalidPreset = BuildSecondaryPreset();
    invalidPreset.Name.assign(
        FUserSettings::kMaximumImageFormatPresetNameBytes + 1,
        'x');
    Check(
        !FUserSettings::SaveImageFormatPreset(invalidPreset),
        "format preset rejects names beyond the UTF-8 byte limit");
    invalidPreset = BuildSecondaryPreset();
    invalidPreset.Params.Format = EImageFormat::Unknown;
    Check(
        !FUserSettings::SaveImageFormatPreset(invalidPreset),
        "format preset rejects Unknown as its underlying format");
    invalidPreset = BuildSecondaryPreset();
    invalidPreset.Params.Stride = 1;
    Check(
        !FUserSettings::SaveImageFormatPreset(invalidPreset),
        "format preset rejects a stride smaller than one packed row");
    Check(
        FUserSettings::GetImageFormatPresets() == emptyPresets,
        "rejected format presets do not mutate in-memory settings");

    Check(
        FUserSettings::SaveImageFormatPreset(BuildPrimaryPreset(false)),
        "format preset accepts UTF-8 and delimiter characters in its name");
    Check(
        !FUserSettings::GetImageFormatPresets().empty() &&
            FUserSettings::GetImageFormatPresets().front().Params.Stride == 1440,
        "format preset normalizes compact stride zero to concrete row bytes");
    Check(
        FUserSettings::SaveImageFormatPreset(BuildSecondaryPreset()),
        "format preset saves a second complete load-parameter snapshot");
    Check(
        FUserSettings::SaveImageFormatPreset(BuildPrimaryPreset(true)),
        "saving the same format preset name overwrites it");
    Check(
        FUserSettings::GetImageFormatPresets() == BuildExpectedPresets(),
        "overwritten format preset moves to the front with all fields updated");

    FUserSettings::SetImageConfigCacheCapacity(
        kTestImageConfigCacheCapacity);
    FUserSettings::SetHistogramOverlayEnabled(kTestHistogramOverlayEnabled);
    FUserSettings::SetHistogramLogScaleEnabled(kTestHistogramLogScaleEnabled);
    FUserSettings::SetCompareMode(kTestCompareMode);
    FUserSettings::SetThemePalette(kTestThemePalette);
    FUserSettings::FWindowPlacement placement;
    placement.X = kTestWindowX;
    placement.Y = kTestWindowY;
    placement.Width = kTestWindowWidth;
    placement.Height = kTestWindowHeight;
    placement.bMaximized = true;
    FUserSettings::SetWindowPlacement(placement);
    FUserSettings::SetRecentFiles(BuildOversizedRecentFiles());

    Check(
        FUserSettings::GetCompareMode() == kTestCompareMode,
        "compare mode updates in memory");
    Check(
        FUserSettings::GetThemePalette() == kTestThemePalette,
        "six base theme colors update in memory");
    Check(
        FUserSettings::GetThemeAccentColor() == kTestThemePalette.Get(
            FUserSettings::EThemeColorRole::Accent),
        "theme accent compatibility getter follows the palette");

    Check(
        FUserSettings::GetRecentFiles() == BuildExpectedRecentFiles(),
        "recent files are deduplicated and capped at 15 entries");
    const std::filesystem::path settingsPath =
        testAppData / "YUVRaw" / "settings.ini";

    const std::string settingsBeforeFailedReplace =
        ReadFileText(settingsPath);
    const HANDLE lockedSettings = CreateFileW(
        settingsPath.c_str(),
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    Check(
        lockedSettings != INVALID_HANDLE_VALUE,
        "test exclusively locks the existing settings file");

    FUserSettings::FImageFormatPreset changedPreset =
        BuildSecondaryPreset();
    changedPreset.Params.Stride += 64;
    bool bSavedWhileLocked = true;

    if (lockedSettings != INVALID_HANDLE_VALUE)
    {
        Check(!FUserSettings::SetLanguage(FLocalization::ELanguage::SimplifiedChinese) &&
            FUserSettings::GetLanguage() == FLocalization::ELanguage::English,
            "failed language save rolls back the in-memory preference");
        bSavedWhileLocked =
            FUserSettings::SaveImageFormatPreset(changedPreset);
        CloseHandle(lockedSettings);
    }

    Check(
        !bSavedWhileLocked,
        "atomic settings replacement reports a locked destination failure");
    Check(
        ReadFileText(settingsPath) == settingsBeforeFailedReplace,
        "failed atomic replacement preserves the previous settings file");
    Check(
        FUserSettings::GetImageFormatPresets() == BuildExpectedPresets(),
        "failed atomic replacement rolls back the in-memory preset update");

    std::error_code removeSettingsError;
    const bool bRemovedSettings = std::filesystem::remove(
        settingsPath,
        removeSettingsError);
    Check(
        bRemovedSettings && !removeSettingsError &&
            !std::filesystem::exists(settingsPath),
        "test simulates external deletion of settings.ini");
    Check(
        FUserSettings::SaveImageFormatPreset(BuildPrimaryPreset(true)),
        "saving an unchanged preset recreates a missing settings file");
    Check(
        std::filesystem::exists(settingsPath) &&
            RunChild(executable, kVerifyPopulatedMode) == 0,
        "recreated settings file contains the complete in-memory settings snapshot");

    Check(
        CountPersistedThemeSettings(settingsPath) ==
            FUserSettings::kThemeColorRoleCount,
        "settings file persists only the six editable theme colors");
    Check(
        CountPersistedPresetSettings(settingsPath) ==
            BuildExpectedPresets().size(),
        "settings file writes one versioned record per format preset");
    const std::string settingsText = ReadFileText(settingsPath);
    Check(
        settingsText.find("ImageFormatPreset=v1|") != std::string::npos,
        "format preset records carry an explicit schema version");
    Check(
        settingsText.find(kPrimaryPresetName) == std::string::npos,
        "format preset names are safely encoded instead of using raw delimiters");

    {
        std::ofstream malformedSettings(
            settingsPath,
            std::ios::binary | std::ios::app);
        malformedSettings
            << "Language=unsupported-language\n"
            << "ImageFormatPreset=v2|626164|RGB8|64|32|192|8|0|0|0\n"
            << "ImageFormatPreset=v1|626164|Unknown|64|32|192|8|0|0|0\n"
            << "ImageFormatPreset=v1|626164|RGB8|-1|32|192|8|0|0|0\n"
            << "ImageFormatPreset=v1|626164|RGB8|64\n";
    }
    Check(
        RunChild(executable, kVerifyPopulatedMode) == 0,
        "fresh process ignores malformed or incompatible preset records");

    FUserSettings::SetRecentFiles(std::vector<std::string>());
    Check(
        FUserSettings::GetRecentFiles().empty(),
        "clearing recent files updates in-memory settings");
    Check(
        RunChild(executable, kVerifyEmptyMode) == 0,
        "clearing recent files is persisted");
    Check(
        CountPersistedPresetSettings(settingsPath) ==
            BuildExpectedPresets().size(),
        "next settings save drops malformed preset records");

    FUserSettings::FImageFormatPreset maximumNamePreset =
        BuildSecondaryPreset();
    maximumNamePreset.Name.assign(
        FUserSettings::kMaximumImageFormatPresetNameBytes,
        'n');
    Check(
        FUserSettings::SaveImageFormatPreset(maximumNamePreset),
        "format preset accepts a name exactly at the byte limit");

    bool bSavedAllCapacityPresets = true;
    for (size_t index = 0;
         index <= FUserSettings::kMaximumImageFormatPresetCount;
         ++index)
    {
        bSavedAllCapacityPresets =
            FUserSettings::SaveImageFormatPreset(BuildCapacityPreset(index)) &&
            bSavedAllCapacityPresets;
    }
    Check(
        bSavedAllCapacityPresets,
        "format preset capacity fixtures all persist successfully");
    Check(
        FUserSettings::GetImageFormatPresets().size() ==
            FUserSettings::kMaximumImageFormatPresetCount,
        "format preset list evicts old entries at its configured cap");
    Check(
        RunChild(executable, kVerifyCappedPresetsMode) == 0,
        "capped format preset order survives a fresh process");

    const std::filesystem::path cachePath =
        FUserSettings::GetImageConfigCachePath();
    std::error_code directoryError;
    std::filesystem::create_directories(
        cachePath.parent_path(),
        directoryError);
    Check(
        !directoryError,
        "test creates the redirected local application data directory");
    {
        std::ofstream cacheFile(cachePath, std::ios::binary | std::ios::trunc);
        cacheFile << "cached image properties";
    }
    Check(
        std::filesystem::exists(cachePath),
        "test fixture creates an image property cache file");
    Check(
        FUserSettings::ClearAllData(),
        "clearing all data removes persisted settings and caches");
    Check(
        FUserSettings::WasAllDataClearedThisSession(),
        "clearing all data suppresses immediate shutdown persistence");
    Check(
        !FUserSettings::TryGetWindowPlacement(placement),
        "clearing all data resets window placement in memory");
    Check(
        FUserSettings::GetThemePalette() ==
            FUserSettings::kDefaultThemePalette,
        "clearing all data resets the theme palette in memory");
    Check(
        FUserSettings::GetImageFormatPresets().empty(),
        "clearing all data resets format presets in memory");
    Check(
        RunChild(executable, kVerifyClearedMode) == 0,
        "clearing all data survives a fresh process");

    {
        std::ofstream invalidLanguageFile(settingsPath);
        invalidLanguageFile << "Language=unsupported-language\n";
    }
    Check(RunChild(executable, kVerifyClearedMode) == 0,
        "unsupported language in an otherwise empty file falls back to English");

    std::error_code cleanupError;
    std::filesystem::remove_all(testRoot, cleanupError);

    std::printf("\n%s\n", GFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return GFailures == 0 ? 0 : 1;
}
