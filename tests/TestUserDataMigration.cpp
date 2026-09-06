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
#include <stdexcept>
#include <string>

namespace
{
    constexpr size_t kLegacyCapacity = 37;
    constexpr size_t kCurrentCapacity = 73;
    constexpr const char* kLegacyCache = "legacy-cache-fixture";
    constexpr const char* kCurrentCache = "current-cache-fixture";
    constexpr const char* kLegacyPreset =
        "ImageFormatPreset=v1|6C6567616379|NV12|64|32|64|8|0|0|0\n";

    void Require(bool bCondition, const char* Message)
    {
        if (!bCondition)
        {
            throw std::runtime_error(Message);
        }
    }

    void WriteFile(const std::filesystem::path& Path, const std::string& Contents)
    {
        std::filesystem::create_directories(Path.parent_path());
        std::ofstream file(Path, std::ios::binary | std::ios::trunc);
        file << Contents;
        file.close();
        Require(static_cast<bool>(file), "fixture write failed");
    }

    std::string ReadFile(const std::filesystem::path& Path)
    {
        std::ifstream file(Path, std::ios::binary);
        Require(file.is_open(), "expected data file is missing");
        return std::string(std::istreambuf_iterator<char>(file), {});
    }

    void SeedLegacyData(const std::filesystem::path& Root)
    {
        WriteFile(Root / "Roaming/ImageDevTool/settings.ini",
            "ImageConfigCacheCapacity=" + std::to_string(kLegacyCapacity) + "\n" + kLegacyPreset);
        WriteFile(Root / "Local/ImageDevTool/image-properties.cache", kLegacyCache);
    }

    int Verify(const std::string& Mode, const std::filesystem::path& Root)
    {
        // 故意先请求缓存路径，确保导入不依赖应用恰好先读取用户偏好。
        const std::filesystem::path cache = FUserSettings::GetImageConfigCachePath();
        const std::filesystem::path settings = Root / "Roaming/YUVRaw/settings.ini";
        const std::filesystem::path marker = Root / "Roaming/YUVRaw/legacy-migration.complete";
        Require(cache == Root / "Local/YUVRaw/image-properties.cache", "cache uses YUVRaw directory");

        if (Mode == "locked")
        {
            Require(!std::filesystem::exists(settings), "failed copy must not publish partial settings");
            Require(!std::filesystem::exists(marker), "failed import remains retryable");
            Require(FUserSettings::GetImageConfigCacheCapacity() ==
                FUserSettings::kDefaultImageConfigCacheCapacity, "failed import keeps safe defaults");
            return 0;
        }

        if (Mode == "empty")
        {
            Require(FUserSettings::GetImageConfigCacheCapacity() ==
                FUserSettings::kDefaultImageConfigCacheCapacity, "cleared settings stay at defaults");
            Require(FUserSettings::GetImageFormatPresets().empty(), "cleared presets do not return");
            Require(!std::filesystem::exists(settings), "settings must not be reimported");
            Require(!std::filesystem::exists(cache), "cache must not be reimported");
        }
        else
        {
            const bool bCurrentSettings = Mode == "existing" || Mode == "mixed";
            Require(FUserSettings::GetImageConfigCacheCapacity() ==
                (bCurrentSettings ? kCurrentCapacity : kLegacyCapacity), "expected settings win");
            Require(ReadFile(cache) == (Mode == "existing" ? kCurrentCache : kLegacyCache),
                "cache import preserves the expected bytes");
            if (!bCurrentSettings)
            {
                const auto& presets = FUserSettings::GetImageFormatPresets();
                Require(presets.size() == 1 && presets.front().Name == "legacy",
                    "custom format presets survive migration");
                Require(ReadFile(settings) == ReadFile(Root / "Roaming/ImageDevTool/settings.ini"),
                    "legacy settings are copied without content changes");
            }
            if (Mode == "clear")
            {
                Require(FUserSettings::ClearAllData(), "clear all data succeeds");
                Require(!std::filesystem::exists(settings) && !std::filesystem::exists(cache),
                    "clear removes only the new data files");
            }
        }

        Require(std::filesystem::exists(marker), "migration decision is persisted");
        return 0;
    }

    void RunChild(const std::filesystem::path& Executable, const std::string& Mode,
        const std::filesystem::path& Root)
    {
        Require(_wputenv_s(L"APPDATA", (Root / "Roaming").c_str()) == 0, "redirect APPDATA");
        Require(_wputenv_s(L"LOCALAPPDATA", (Root / "Local").c_str()) == 0, "redirect LOCALAPPDATA");
        std::wstring command = L"\"" + Executable.wstring() + L"\" " +
            std::filesystem::u8path(Mode).wstring() + L" \"" + Root.wstring() + L"\"";
        // 直接创建进程，避免 cmd 对多段带引号路径再次解析。
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION process{};
        Require(CreateProcessW(Executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE,
            "migration test child process starts");
        const DWORD waitResult = WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exitCode = EXIT_FAILURE;
        const BOOL bExitCodeRead = GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        Require(waitResult == WAIT_OBJECT_0 && bExitCodeRead && exitCode == EXIT_SUCCESS,
            "fresh-process migration verification failed");
    }
}

int wmain(int ArgCount, wchar_t** Arguments)
{
    try
    {
        constexpr int kChildArgumentCount = 3;
        if (ArgCount == kChildArgumentCount)
        {
            return Verify(std::filesystem::path(Arguments[1]).u8string(), Arguments[2]);
        }

        const auto unique = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("YUVRaw_Migration_" + std::to_string(unique));
        const std::filesystem::path executable =
            std::filesystem::absolute(Arguments[0]);

        const auto legacy = root / "legacy";
        SeedLegacyData(legacy);
        RunChild(executable, "legacy", legacy);
        RunChild(executable, "clear", legacy);
        RunChild(executable, "empty", legacy);
        Require(ReadFile(legacy / "Local/ImageDevTool/image-properties.cache") == kLegacyCache,
            "clearing YUVRaw must preserve legacy cache");
        Require(ReadFile(legacy / "Roaming/ImageDevTool/settings.ini").find(kLegacyPreset) !=
            std::string::npos, "clearing YUVRaw must preserve legacy presets");

        const auto existing = root / "existing";
        SeedLegacyData(existing);
        WriteFile(existing / "Roaming/YUVRaw/settings.ini",
            "ImageConfigCacheCapacity=" + std::to_string(kCurrentCapacity) + "\n");
        RunChild(executable, "mixed", existing);
        WriteFile(existing / "Local/YUVRaw/image-properties.cache", kCurrentCache);
        RunChild(executable, "existing", existing);

        const auto locked = root / "locked";
        SeedLegacyData(locked);
        const HANDLE file = CreateFileW((locked / "Roaming/ImageDevTool/settings.ini").c_str(),
            GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(file != INVALID_HANDLE_VALUE, "lock legacy settings fixture");
        // 子进程读取失败后关闭锁，再用全新进程验证只补齐缺失文件。
        try
        {
            RunChild(executable, "locked", locked);
        }
        catch (...)
        {
            CloseHandle(file);
            throw;
        }
        CloseHandle(file);
        RunChild(executable, "legacy", locked);

        const auto clean = root / "clean";
        RunChild(executable, "empty", clean);
        SeedLegacyData(clean);
        RunChild(executable, "empty", clean);

        std::filesystem::remove_all(root);
        std::puts("TestUserDataMigration: all checks passed");
        return 0;
    }
    catch (const std::exception& Error)
    {
        std::fprintf(stderr, "TestUserDataMigration: %s\n", Error.what());
        return 1;
    }
}
