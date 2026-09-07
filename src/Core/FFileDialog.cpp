#include "FFileDialog.h"
#include "FLocalization.h"
#include "FLogger.h"
#include "Util.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <filesystem>
#include <system_error>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr const wchar_t* kProjectReleasesUrl =
        L"https://github.com/bobcatkay/yuvraw-viewer/releases";
    constexpr const wchar_t* kProjectIssueUrl =
        L"https://github.com/bobcatkay/yuvraw-viewer/issues/new";

    /**
     * 作用域内的 COM 初始化。
     * GLFW 已经初始化过 COM，所以这里通常拿到 S_FALSE 或 RPC_E_CHANGED_MODE，都不算失败。
     * 文件对话框需要套间线程模型，因此用 APARTMENTTHREADED。
     */
    class FScopedCoInitialize
    {
    public:
        FScopedCoInitialize()
        {
            const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            bShouldUninitialize = SUCCEEDED(hr);
        }

        ~FScopedCoInitialize()
        {
            if (bShouldUninitialize)
            {
                CoUninitialize();
            }
        }

        FScopedCoInitialize(const FScopedCoInitialize&) = delete;
        FScopedCoInitialize& operator=(const FScopedCoInitialize&) = delete;

    private:
        bool bShouldUninitialize = false;
    };

    std::wstring ToWide(const std::string& Utf8)
    {
        if (Utf8.empty())
        {
            return std::wstring();
        }

        const int required = MultiByteToWideChar(CP_UTF8, 0, Utf8.c_str(), static_cast<int>(Utf8.size()), nullptr, 0);

        if (required <= 0)
        {
            return std::wstring();
        }

        std::wstring wide(static_cast<size_t>(required), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, Utf8.c_str(), static_cast<int>(Utf8.size()), wide.data(), required);

        return wide;
    }

    std::string ToUtf8(const wchar_t* Wide)
    {
        if (!Wide || !*Wide)
        {
            return std::string();
        }

        const int required = WideCharToMultiByte(CP_UTF8, 0, Wide, -1, nullptr, 0, nullptr, nullptr);

        if (required <= 1)
        {
            return std::string();
        }

        // required 含结尾的 '\0'。先为终止符保留空间，转换成功后再从 std::string
        // 的逻辑长度中去掉它；否则把 required 个字节写进 required - 1 的缓冲区会越界。
        std::string utf8(static_cast<size_t>(required), '\0');
        const int written = WideCharToMultiByte(
            CP_UTF8,
            0,
            Wide,
            -1,
            utf8.data(),
            required,
            nullptr,
            nullptr);

        if (written != required)
        {
            return std::string();
        }

        utf8.pop_back();

        return utf8;
    }

    /**
     * 从对话框取出用户选中的路径
     */
    bool ExtractResult(IFileOpenDialog* Dialog, std::string& OutPath)
    {
        ComPtr<IShellItem> item;

        if (FAILED(Dialog->GetResult(&item)))
        {
            return false;
        }

        PWSTR widePath = nullptr;

        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &widePath)) || !widePath)
        {
            return false;
        }

        OutPath = ToUtf8(widePath);
        CoTaskMemFree(widePath);

        return !OutPath.empty();
    }

    bool OpenShellTarget(const wchar_t* Target, const char* LogTag)
    {
        FScopedCoInitialize comInit;

        // 直接传入宽字符路径或固定 HTTPS 地址，保留中文路径；失败由应用提示。
        SHELLEXECUTEINFOW executeInfo{};
        executeInfo.cbSize = sizeof(executeInfo);
        executeInfo.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        executeInfo.lpVerb = L"open";
        executeInfo.lpFile = Target;
        executeInfo.nShow = SW_SHOWNORMAL;

        if (!ShellExecuteExW(&executeInfo))
        {
            const DWORD error = GetLastError();
            LOGE(LogTag, "ShellExecuteExW failed: %lu", error);
            return false;
        }

        LOGI(LogTag, "Shell open request accepted");
        return true;
    }
}

namespace FFileDialog
{
    bool OpenProjectReleases()
    {
        LOGI("ProjectReleases", "Opening project releases in the default browser");
        return OpenShellTarget(kProjectReleasesUrl, "ProjectReleases");
    }

    bool OpenProjectIssue()
    {
        LOGI("Feedback", "Opening a new project issue in the default browser");
        return OpenShellTarget(kProjectIssueUrl, "Feedback");
    }

    bool OpenLogDirectory()
    {
        LOGI("Feedback", "Opening the current logs folder in File Explorer");
        const std::filesystem::path directory = FLogger::GetLogDirectory();
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error))
        {
            LOGE("Feedback", "Logs folder is missing or inaccessible (error=%d)", error.value());
            return false;
        }

        return OpenShellTarget(directory.c_str(), "Feedback");
    }

    bool OpenFile(const std::string& Title, const std::vector<std::string>& Extensions, std::string& OutPath)
    {
        FScopedCoInitialize comInit;

        ComPtr<IFileOpenDialog> dialog;

        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        {
            LOGE("OpenFile", "Failed to create IFileOpenDialog");

            return false;
        }

        const std::wstring wideTitle = ToWide(Title);
        dialog->SetTitle(wideTitle.c_str());

        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_NOCHANGEDIR);

        // 组装筛选器："*.yuv;*.raw;..." 加一个"所有文件"
        std::wstring pattern;

        for (const std::string& ext : Extensions)
        {
            if (!pattern.empty())
            {
                pattern += L";";
            }

            pattern += L"*" + ToWide(ext);
        }

        COMDLG_FILTERSPEC filters[2];
        const std::wstring supportedLabel = ToWide(FLocalization::Text(EUiText::SupportedImages));
        const std::wstring allFilesLabel = ToWide(FLocalization::Text(EUiText::AllFiles));
        int32_t filterCount = 0;

        if (!pattern.empty())
        {
            filters[filterCount].pszName = supportedLabel.c_str();
            filters[filterCount].pszSpec = pattern.c_str();
            ++filterCount;
        }

        filters[filterCount].pszName = allFilesLabel.c_str();
        filters[filterCount].pszSpec = L"*.*";
        ++filterCount;

        dialog->SetFileTypes(static_cast<UINT>(filterCount), filters);

        if (FAILED(dialog->Show(nullptr)))
        {
            // 用户取消也会走这里，不算错误
            return false;
        }

        return ExtractResult(dialog.Get(), OutPath);
    }

    bool OpenDirectory(const std::string& Title, std::string& OutPath)
    {
        FScopedCoInitialize comInit;

        ComPtr<IFileOpenDialog> dialog;

        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        {
            LOGE("OpenDirectory", "Failed to create IFileOpenDialog");

            return false;
        }

        const std::wstring wideTitle = ToWide(Title);
        dialog->SetTitle(wideTitle.c_str());

        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS);

        if (FAILED(dialog->Show(nullptr)))
        {
            return false;
        }

        return ExtractResult(dialog.Get(), OutPath);
    }

    bool RevealInExplorer(const std::string& Path)
    {
        if (Path.empty())
        {
            return false;
        }

        FScopedCoInitialize comInit;

        std::wstring widePath = ToWide(Path);

        // ILCreateFromPathW 只认反斜杠，而项目里的路径可能混着 '/'（命令行传进来的、
        // 或是拼接出来的）。统一一遍比在每个调用点小心要可靠。
        std::replace(widePath.begin(), widePath.end(), L'/', L'\\');

        // 用 SHOpenFolderAndSelectItems 而不是 explorer.exe /select,"..."：
        // 后者要把路径拼进命令行，逗号、空格、引号都得自己转义，中文路径还得看代码页
        PIDLIST_ABSOLUTE idl = ILCreateFromPathW(widePath.c_str());

        if (!idl)
        {
            LOGE("RevealInExplorer", "ILCreateFromPathW failed: %s", Path.c_str());

            return false;
        }

        const HRESULT hr = SHOpenFolderAndSelectItems(idl, 0, nullptr, 0);
        ILFree(idl);

        if (FAILED(hr))
        {
            LOGE("RevealInExplorer", "SHOpenFolderAndSelectItems failed: 0x%08X, %s",
                 static_cast<unsigned int>(hr), Path.c_str());

            return false;
        }

        return true;
    }
}
