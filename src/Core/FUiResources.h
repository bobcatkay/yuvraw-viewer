#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ShlObj.h>

#include <cstdlib>
#include <filesystem>
#include <string>

// UI 资源与布局不能跟随启动目录；ZIP 可以解压到任意盘符和中文路径。
namespace FUiResources
{
    inline constexpr const wchar_t* kBundledFontFileName = L"NotoSansCJKsc-Regular.otf";
    inline constexpr const wchar_t* kLayoutFileName = L"imgui.ini";
    inline constexpr DWORD kMaximumWindowsPathCharacters = 32768;

    inline std::filesystem::path GetKnownDirectory(REFKNOWNFOLDERID FolderId)
    {
        PWSTR directory = nullptr;
        const HRESULT result = SHGetKnownFolderPath(FolderId, KF_FLAG_DEFAULT, nullptr, &directory);
        const std::filesystem::path path = SUCCEEDED(result) && directory
            ? std::filesystem::path(directory) : std::filesystem::path();
        CoTaskMemFree(directory);
        return path;
    }

    inline std::filesystem::path GetExecutableDirectory()
    {
        std::wstring executable(kMaximumWindowsPathCharacters, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(),
            static_cast<DWORD>(executable.size()));
        if (length == 0 || length >= executable.size())
        {
            return {};
        }
        executable.resize(length);
        return std::filesystem::path(executable).parent_path();
    }

    inline std::filesystem::path GetBundledFontPath(const std::filesystem::path& ExecutableDirectory)
    {
        return ExecutableDirectory.empty() ? std::filesystem::path()
            : ExecutableDirectory / L"resources" / L"fonts" / kBundledFontFileName;
    }

    inline std::filesystem::path GetSystemChineseFontPath()
    {
        const std::filesystem::path directory = GetKnownDirectory(FOLDERID_Fonts);
        return directory.empty() ? std::filesystem::path() : directory / L"msyh.ttc";
    }

    inline std::filesystem::path GetLayoutSettingsPath()
    {
        wchar_t* localAppData = nullptr;
        size_t length = 0;
        const errno_t result = _wdupenv_s(&localAppData, &length, L"LOCALAPPDATA");
        std::filesystem::path directory = result == 0 && localAppData && *localAppData
            ? std::filesystem::path(localAppData) : std::filesystem::path();
        free(localAppData);

        // 支持启动器/测试设置的绝对用户目录；缺失或相对值走 Windows 已知目录，
        // 绝不回退到 EXE 或当前目录，以免从只读目录启动时静默丢失布局。
        if (!directory.is_absolute())
        {
            directory = GetKnownDirectory(FOLDERID_LocalAppData);
        }
        return directory.empty() ? std::filesystem::path()
            : directory / L"YUVRaw" / kLayoutFileName;
    }
}
