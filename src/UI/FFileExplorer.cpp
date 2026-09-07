#include "Core/FLocalization.h"
#include "FFileExplorer.h"

#include "Core/FFileDialog.h"
#include "Core/FUserSettings.h"
#include "FToast.h"
#include "FUiIcons.h"
#include "Util.h"

#include <imgui.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj_core.h>

// Win32 的 A/W 宏会与本类的同名方法冲突，只在本翻译单元保留类型和剪贴板 API。
#undef GetCurrentDirectory
#undef SetCurrentDirectory

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>

namespace
{
    /**
     * Windows 文件剪贴板要求绝对的 UTF-16 路径。项目内部路径保持 UTF-8，
     * 只在这里跨 Win32 边界时转换，并统一为系统偏好的反斜杠形式。
     */
    bool ResolveClipboardPaths(
        const std::vector<std::string>& FilePaths,
        std::vector<std::filesystem::path>& OutPaths)
    {
        OutPaths.clear();
        OutPaths.reserve(FilePaths.size());

        if (FilePaths.empty())
        {
            LOGW("ResolveClipboardPaths", "%s", "No files were provided");

            return false;
        }

        try
        {
            for (size_t i = 0; i < FilePaths.size(); ++i)
            {
                if (FilePaths[i].empty())
                {
                    LOGE("ResolveClipboardPaths", "File path at index %zu is empty", i);

                    return false;
                }

                std::error_code ec;
                std::filesystem::path path = std::filesystem::absolute(
                    std::filesystem::u8path(FilePaths[i]), ec);

                if (ec || path.empty())
                {
                    LOGE(
                        "ResolveClipboardPaths",
                        "Failed to resolve file path at index %zu: %s",
                        i,
                        ec ? ec.message().c_str() : "empty result");

                    return false;
                }

                path = path.lexically_normal();
                path.make_preferred();
                OutPaths.push_back(std::move(path));
            }
        }
        catch (const std::exception& exception)
        {
            LOGE(
                "ResolveClipboardPaths",
                "Failed to convert clipboard paths: %s",
                exception.what());

            return false;
        }

        return true;
    }

    HWND GetClipboardOwnerWindow()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();

        if (viewport && viewport->PlatformHandleRaw)
        {
            return static_cast<HWND>(viewport->PlatformHandleRaw);
        }

        // 正常运行时 GLFW 后端一定会填 PlatformHandleRaw；回退值只用于初始化边界。
        return GetActiveWindow();
    }

    /**
     * OpenClipboard 必须带有效窗口句柄：传 nullptr 后 EmptyClipboard 会把 owner 清空，
     * 随后的 SetClipboardData 可能失败。作用域结束时统一关闭，避免错误分支漏掉。
     */
    class FScopedClipboardWriter
    {
    public:
        explicit FScopedClipboardWriter(const char* InLogContext)
            : LogContext(InLogContext)
        {
            const HWND ownerWindow = GetClipboardOwnerWindow();

            if (!ownerWindow)
            {
                LOGE(LogContext, "%s", "No native window is available as clipboard owner");

                return;
            }

            bOpen = OpenClipboard(ownerWindow) != FALSE;

            if (!bOpen)
            {
                LOGE(
                    LogContext,
                    "OpenClipboard failed: %lu",
                    static_cast<unsigned long>(GetLastError()));
            }
        }

        ~FScopedClipboardWriter()
        {
            if (bOpen && !CloseClipboard())
            {
                LOGW(
                    LogContext,
                    "CloseClipboard failed: %lu",
                    static_cast<unsigned long>(GetLastError()));
            }
        }

        FScopedClipboardWriter(const FScopedClipboardWriter&) = delete;
        FScopedClipboardWriter& operator=(const FScopedClipboardWriter&) = delete;

        bool IsOpen() const
        {
            return bOpen;
        }

        bool Clear() const
        {
            if (!bOpen || !EmptyClipboard())
            {
                LOGE(
                    LogContext,
                    "EmptyClipboard failed: %lu",
                    static_cast<unsigned long>(GetLastError()));

                return false;
            }

            return true;
        }

    private:
        const char* LogContext;
        bool bOpen = false;
    };

    HGLOBAL CreateFileDropData(const std::vector<std::filesystem::path>& Paths)
    {
        // 每个路径各有一个终止符，列表末尾还要再补一个终止符。
        size_t characterCount = 1;

        for (const std::filesystem::path& path : Paths)
        {
            const std::wstring& nativePath = path.native();

            if (nativePath.size() > std::numeric_limits<size_t>::max() - characterCount - 1)
            {
                LOGE("CreateFileDropData", "%s", "Clipboard path list is too large");

                return nullptr;
            }

            characterCount += nativePath.size() + 1;
        }

        if (characterCount >
            (std::numeric_limits<size_t>::max() - sizeof(DROPFILES)) / sizeof(wchar_t))
        {
            LOGE("CreateFileDropData", "%s", "Clipboard allocation size overflowed");

            return nullptr;
        }

        const size_t byteCount = sizeof(DROPFILES) + characterCount * sizeof(wchar_t);
        HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, byteCount);

        if (!data)
        {
            LOGE(
                "CreateFileDropData",
                "GlobalAlloc failed: %lu",
                static_cast<unsigned long>(GetLastError()));

            return nullptr;
        }

        auto* dropFiles = static_cast<DROPFILES*>(GlobalLock(data));

        if (!dropFiles)
        {
            LOGE(
                "CreateFileDropData",
                "GlobalLock failed: %lu",
                static_cast<unsigned long>(GetLastError()));
            GlobalFree(data);

            return nullptr;
        }

        dropFiles->pFiles = sizeof(DROPFILES);
        dropFiles->fWide = TRUE;

        wchar_t* destination = reinterpret_cast<wchar_t*>(
            reinterpret_cast<unsigned char*>(dropFiles) + sizeof(DROPFILES));

        for (const std::filesystem::path& path : Paths)
        {
            const std::wstring& nativePath = path.native();
            destination = std::copy(nativePath.begin(), nativePath.end(), destination);
            *destination++ = L'\0';
        }

        *destination = L'\0';
        GlobalUnlock(data);

        return data;
    }

    HGLOBAL CreatePathTextData(const std::vector<std::filesystem::path>& Paths)
    {
        try
        {
            std::wstring text;

            for (size_t i = 0; i < Paths.size(); ++i)
            {
                if (i > 0)
                {
                    // CRLF 与 Windows 文本剪贴板惯例一致，多选路径粘贴后保持一行一个。
                    text += L"\r\n";
                }

                text += Paths[i].native();
            }

            if (text.size() >= std::numeric_limits<size_t>::max() / sizeof(wchar_t))
            {
                LOGE("CreatePathTextData", "%s", "Clipboard text is too large");

                return nullptr;
            }

            const size_t byteCount = (text.size() + 1) * sizeof(wchar_t);
            HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, byteCount);

            if (!data)
            {
                LOGE(
                    "CreatePathTextData",
                    "GlobalAlloc failed: %lu",
                    static_cast<unsigned long>(GetLastError()));

                return nullptr;
            }

            auto* destination = static_cast<wchar_t*>(GlobalLock(data));

            if (!destination)
            {
                LOGE(
                    "CreatePathTextData",
                    "GlobalLock failed: %lu",
                    static_cast<unsigned long>(GetLastError()));
                GlobalFree(data);

                return nullptr;
            }

            std::copy(text.c_str(), text.c_str() + text.size() + 1, destination);
            GlobalUnlock(data);

            return data;
        }
        catch (const std::exception& exception)
        {
            LOGE("CreatePathTextData", "Failed to build clipboard text: %s", exception.what());

            return nullptr;
        }
    }

    /**
     * CF_HDROP 让目标应用把剪贴板内容识别成真实文件；Preferred DropEffect 明确指定
     * COPY，避免 Shell 或第三方文件管理器把来源不明的拖放数据解释成移动。
     */
    void TrySetPreferredCopyDropEffect()
    {
        const UINT format = RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT);

        if (format == 0)
        {
            LOGW(
                "TrySetPreferredCopyDropEffect",
                "RegisterClipboardFormat failed: %lu",
                static_cast<unsigned long>(GetLastError()));

            return;
        }

        HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));

        if (!data)
        {
            LOGW(
                "TrySetPreferredCopyDropEffect",
                "GlobalAlloc failed: %lu",
                static_cast<unsigned long>(GetLastError()));

            return;
        }

        auto* effect = static_cast<DWORD*>(GlobalLock(data));

        if (!effect)
        {
            LOGW(
                "TrySetPreferredCopyDropEffect",
                "GlobalLock failed: %lu",
                static_cast<unsigned long>(GetLastError()));
            GlobalFree(data);

            return;
        }

        *effect = DROPEFFECT_COPY;
        GlobalUnlock(data);

        if (!SetClipboardData(format, data))
        {
            LOGW(
                "TrySetPreferredCopyDropEffect",
                "SetClipboardData failed: %lu",
                static_cast<unsigned long>(GetLastError()));
            GlobalFree(data);
        }
    }

    bool CopyFilesToClipboard(const std::vector<std::string>& FilePaths)
    {
        std::vector<std::filesystem::path> paths;

        if (!ResolveClipboardPaths(FilePaths, paths))
        {
            return false;
        }

        HGLOBAL data = CreateFileDropData(paths);

        if (!data)
        {
            return false;
        }

        FScopedClipboardWriter clipboard("CopyFilesToClipboard");

        if (!clipboard.IsOpen() || !clipboard.Clear())
        {
            GlobalFree(data);

            return false;
        }

        if (!SetClipboardData(CF_HDROP, data))
        {
            LOGE(
                "CopyFilesToClipboard",
                "SetClipboardData failed: %lu",
                static_cast<unsigned long>(GetLastError()));
            GlobalFree(data);

            return false;
        }

        // SetClipboardData 成功后，内存所有权已经交给系统，调用方不能再释放。
        data = nullptr;
        TrySetPreferredCopyDropEffect();

        LOGI("CopyFilesToClipboard", "Copied %zu file(s) to the clipboard", paths.size());

        return true;
    }

    bool CopyFilePathsToClipboard(const std::vector<std::string>& FilePaths)
    {
        std::vector<std::filesystem::path> paths;

        if (!ResolveClipboardPaths(FilePaths, paths))
        {
            return false;
        }

        HGLOBAL data = CreatePathTextData(paths);

        if (!data)
        {
            return false;
        }

        FScopedClipboardWriter clipboard("CopyFilePathsToClipboard");

        if (!clipboard.IsOpen() || !clipboard.Clear())
        {
            GlobalFree(data);

            return false;
        }

        if (!SetClipboardData(CF_UNICODETEXT, data))
        {
            LOGE(
                "CopyFilePathsToClipboard",
                "SetClipboardData failed: %lu",
                static_cast<unsigned long>(GetLastError()));
            GlobalFree(data);

            return false;
        }

        // SetClipboardData 成功后，内存所有权已经交给系统，调用方不能再释放。
        data = nullptr;

        LOGI("CopyFilePathsToClipboard", "Copied %zu path(s) to the clipboard", paths.size());

        return true;
    }

    /**
     * Windows 路径比较不区分大小写；统一相对路径和分隔符，避免文件对话框、
     * 命令行与目录枚举给出不同写法时丢失图片身份。
     */
    std::string MakeFilePathKey(const std::string& FilePath)
    {
        if (FilePath.empty())
        {
            return {};
        }

        std::filesystem::path path = std::filesystem::u8path(FilePath);

        if (path.is_relative())
        {
            std::error_code ec;
            const std::filesystem::path absolutePath = std::filesystem::absolute(path, ec);
            if (!ec)
            {
                path = absolutePath;
            }
        }

        std::string key = path.lexically_normal().generic_u8string();

        std::transform(
            key.begin(),
            key.end(),
            key.begin(),
            [](unsigned char Character)
            {
                return static_cast<char>(std::tolower(Character));
            });

        return key;
    }
}

FFileExplorer::FFileExplorer()
    : SelectionAnchor(-1)
    , bNeedsRefresh(true)
    , DropRectMin{ 0.0f, 0.0f }
    , DropRectMax{ 0.0f, 0.0f }
    , bDropRectValid(false)
{
    // 回到上次浏览的目录。记录失效（盘符拔了、目录被删）时退回工作目录。
    const std::string& lastDirectory = FUserSettings::GetLastDirectory();

    if (!lastDirectory.empty())
    {
        SetCurrentDirectory(lastDirectory);
    }

    // 旧版本可能按系统代码页写入过中文目录。SetCurrentDirectory 会拒绝这类
    // 非 UTF-8 记录，CurrentDirectory 仍为空时安全地退回工作目录。
    if (CurrentDirectory.empty())
    {
        SetCurrentDirectory(std::filesystem::current_path().u8string());
    }
}

FFileExplorer::~FFileExplorer()
{
}

void FFileExplorer::RenderCurrentDirectoryBar()
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    const bool bBrowseClicked = FUiIcons::FolderButton("##BrowseDirectory");
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(FLocalization::Text(EUiText::BrowseDirectory));
    }

    if (bBrowseClicked)
    {
        std::string path;

        if (FFileDialog::OpenDirectory(FLocalization::Text(EUiText::OpenDirectory), path))
        {
            SetCurrentDirectory(path);
        }
    }

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text(FLocalization::Text(EUiText::CurrentDirectory));

    // 路径通常比面板宽，单独一行换行显示
    ImGui::TextWrapped("%s", CurrentDirectory.c_str());
}

void FFileExplorer::Render()
{
    // 所有 tab 背景已经统一为深青, 因此标题文字始终使用白色 (避免焦点/悬停切换闪烁)
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    const bool bVisible = ImGui::Begin(FLocalization::WindowTitle(EUiText::FileExplorer));
    ImGui::PopStyleColor();

    // 折叠或处于未选中的标签页时不接收拖放
    bDropRectValid = bVisible;

    if (!bVisible)
    {
        // 隐藏面板仍需配对 End，但不必遍历目录、转换路径或生成图标。
        ImGui::End();
        return;
    }

    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    DropRectMin[0] = windowPos.x;
    DropRectMin[1] = windowPos.y;
    DropRectMax[0] = windowPos.x + windowSize.x;
    DropRectMax[1] = windowPos.y + windowSize.y;

    RenderCurrentDirectoryBar();

    ImGui::Separator();

    // 工具栏按钮统一使用白色文字（在深青背景上更清晰），范围限定在 Push/Pop 之间
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::Button(FLocalization::Text(EUiText::Refresh)))
    {
        Refresh();
    }
    ImGui::SameLine();
    if (ImGui::Button(FLocalization::Text(EUiText::ParentDirectory)))
    {
        const std::filesystem::path parent =
            std::filesystem::u8path(CurrentDirectory).parent_path();
        if (!parent.empty())
        {
            SetCurrentDirectory(parent.u8string());
        }
    }
    ImGui::PopStyleColor();

    ImGui::Separator();

    // 文件列表
    if (bNeedsRefresh)
    {
        Refresh();
    }

    // 显示目录。
    // 目录切换只置位 bNeedsRefresh，下一帧才真正重建列表，
    // 因此在遍历过程中调用它不会让下面的迭代失效。
    for (size_t i = 0; i < CurrentDirectories.size(); ++i)
    {
        const std::filesystem::path& dir = CurrentDirectories[i];
        const std::string dirName = dir.filename().u8string();
        const std::string dirPath = dir.u8string();
        const std::string id = "##dir" + std::to_string(i);

        if (FUiIcons::IconSelectable(id.c_str(), dirName.c_str(), false, true))
        {
            SetCurrentDirectory(dirPath);
        }
    }

    RenderFileList();

    ImGui::End();
}

bool FFileExplorer::IsFileSelected(const std::string& FilePath) const
{
    return std::find(SelectedFiles.begin(), SelectedFiles.end(), FilePath) != SelectedFiles.end();
}

bool FFileExplorer::IsMainFile(const std::string& FilePath) const
{
    return !MainFileKey.empty() && MakeFilePathKey(FilePath) == MainFileKey;
}

bool FFileExplorer::IsCompareFile(const std::string& FilePath) const
{
    return !CompareFileKey.empty() && MakeFilePathKey(FilePath) == CompareFileKey;
}

void FFileExplorer::SelectRange(int32_t From, int32_t To)
{
    const int32_t count = static_cast<int32_t>(CurrentFiles.size());
    const int32_t begin = std::max(0, std::min(From, To));
    const int32_t end = std::min(count - 1, std::max(From, To));

    SelectedFiles.clear();

    for (int32_t i = begin; i <= end; ++i)
    {
        SelectedFiles.push_back(CurrentFiles[i].u8string());
    }
}

void FFileExplorer::HandleFileClick(int32_t Index)
{
    const std::string filePath = CurrentFiles[Index].u8string();
    const ImGuiIO& io = ImGui::GetIO();

    // Shift：从锚点到这里整段选中。多选是为了批量操作，所以**不打开文件** ——
    // 选到第 20 个文件的路上挨个加载一遍显然不是用户想要的
    if (io.KeyShift && SelectionAnchor >= 0 && SelectionAnchor < static_cast<int32_t>(CurrentFiles.size()))
    {
        SelectRange(SelectionAnchor, Index);

        return;
    }

    // Ctrl：单个加减，锚点跟着走
    if (io.KeyCtrl)
    {
        const auto it = std::find(SelectedFiles.begin(), SelectedFiles.end(), filePath);

        if (it != SelectedFiles.end())
        {
            SelectedFiles.erase(it);
        }
        else
        {
            SelectedFiles.push_back(filePath);
        }

        SelectionAnchor = Index;

        return;
    }

    // 无修饰键：先由主界面决定本次激活替换哪个槽位。替换对比图时保留
    // SelectedFiles 与锚点，避免作为主图身份标记的普通选中被对比图覆盖。
    const bool bUpdatePrimarySelection =
        !OnFileSelected || OnFileSelected(filePath);

    if (bUpdatePrimarySelection)
    {
        SelectedFiles.assign(1, filePath);
        SelectionAnchor = Index;
    }
}

void FFileExplorer::RenderFileList()
{
    for (size_t i = 0; i < CurrentFiles.size(); ++i)
    {
        const std::filesystem::path& file = CurrentFiles[i];
        const std::string fileName = file.filename().u8string();
        const std::string filePath = file.u8string();
        const std::string id = "##file" + std::to_string(i);

        if (FUiIcons::IconSelectable(
                id.c_str(),
                fileName.c_str(),
                IsFileSelected(filePath),
                false,
                IsCompareFile(filePath),
                true))
        {
            HandleFileClick(static_cast<int32_t>(i));
        }

        // 右键本身不改变选择：这样给 B 添加对比图时，作为主图的 A 仍保持高亮。
        // 真正依赖选择的动作各自决定目标，避免右键未选文件时把导出发给旧的多选集合。
        if (ImGui::BeginPopupContextItem(id.c_str()))
        {
            const bool bUseCurrentSelection = IsFileSelected(filePath);
            const std::vector<std::string> contextFiles = bUseCurrentSelection
                ? SelectedFiles
                : std::vector<std::string>{ filePath };

            if (ImGui::MenuItem(FLocalization::Text(EUiText::Open)))
            {
                const bool bUpdatePrimarySelection =
                    !OnFileSelected || OnFileSelected(filePath);

                if (bUpdatePrimarySelection)
                {
                    SelectedFiles.assign(1, filePath);
                    SelectionAnchor = static_cast<int32_t>(i);
                }
            }

            if (ImGui::MenuItem(FLocalization::Text(EUiText::OpenInExplorer)))
            {
                FFileDialog::RevealInExplorer(filePath);
            }

            // 判断实际打开的源图，而不是多选集合；当前主图或已有对比图都无需重复添加。
            if (!IsMainFile(filePath) && !IsCompareFile(filePath) &&
                ImGui::MenuItem(FLocalization::Text(EUiText::AddComparison)))
            {
                if (OnFileCompareRequested)
                {
                    OnFileCompareRequested(filePath);
                }
            }

            ImGui::Separator();

            if (ImGui::MenuItem(FLocalization::Text(EUiText::Copy)))
            {
                if (CopyFilesToClipboard(contextFiles))
                {
                    const std::string message = contextFiles.size() > 1
                        ? std::string(FLocalization::Text(EUiText::CopiedPrefix)) + std::to_string(contextFiles.size()) + FLocalization::Text(EUiText::FilesSuffix)
                        : std::string(FLocalization::Text(EUiText::FileCopied));
                    FToast::Show(message.c_str());
                }
                else
                {
                    FToast::Show(FLocalization::Text(EUiText::CopyFailed));
                }
            }

            if (ImGui::MenuItem(FLocalization::Text(EUiText::CopyPath)))
            {
                if (CopyFilePathsToClipboard(contextFiles))
                {
                    const std::string message = contextFiles.size() > 1
                        ? std::string(FLocalization::Text(EUiText::CopiedPrefix)) + std::to_string(contextFiles.size()) + FLocalization::Text(EUiText::FilePathsSuffix)
                        : std::string(FLocalization::Text(EUiText::FilePathCopied));
                    FToast::Show(message.c_str());
                }
                else
                {
                    FToast::Show(FLocalization::Text(EUiText::CopyPathFailed));
                }
            }

            ImGui::Separator();

            const std::string exportLabel = contextFiles.size() > 1
                ? std::string(FLocalization::Text(EUiText::ExportSelectedPrefix)) + std::to_string(contextFiles.size()) + FLocalization::Text(EUiText::FilesActionSuffix)
                : std::string(FLocalization::Text(EUiText::ExportAction));

            if (ImGui::MenuItem(exportLabel.c_str()))
            {
                if (OnFileExportRequested)
                {
                    OnFileExportRequested(contextFiles);
                }
            }

            ImGui::EndPopup();
        }
    }

    if (!CurrentFiles.empty())
    {
        ImGui::Spacing();
        ImGui::TextDisabled(FLocalization::Text(EUiText::MultiSelectHelp));
    }
}

bool FFileExplorer::ContainsScreenPoint(float X, float Y) const
{
    return bDropRectValid &&
           X >= DropRectMin[0] && X <= DropRectMax[0] &&
           Y >= DropRectMin[1] && Y <= DropRectMax[1];
}

void FFileExplorer::SetCurrentDirectory(const std::string& Directory)
{
    if (Directory == CurrentDirectory)
    {
        return;
    }

    try
    {
        std::error_code ec;
        const std::filesystem::path nativeDirectory =
            std::filesystem::u8path(Directory);

        if (std::filesystem::is_directory(nativeDirectory, ec))
        {
            CurrentDirectory = nativeDirectory.lexically_normal().u8string();
            bNeedsRefresh = true;

            FUserSettings::SetLastDirectory(CurrentDirectory);

            return;
        }

        LOGW(
            "SetCurrentDirectory",
            "Directory is not accessible: %s (%s)",
            Directory.c_str(),
            ec ? ec.message().c_str() : "not a directory");
    }
    catch (const std::exception& exception)
    {
        LOGW(
            "SetCurrentDirectory",
            "Invalid UTF-8 directory path: %s (%s)",
            Directory.c_str(),
            exception.what());
    }
}

std::string FFileExplorer::GetCurrentDirectory() const
{
    return CurrentDirectory;
}

void FFileExplorer::SetOnFileSelected(FileSelectedCallback Callback)
{
    OnFileSelected = Callback;
}

void FFileExplorer::SetOnFileCompareRequested(FileCompareCallback Callback)
{
    OnFileCompareRequested = Callback;
}

void FFileExplorer::SetCompareFilePath(const std::string& FilePath)
{
    CompareFileKey = MakeFilePathKey(FilePath);
}

void FFileExplorer::SetMainFilePath(const std::string& FilePath)
{
    MainFileKey = MakeFilePathKey(FilePath);
}

void FFileExplorer::SelectMainFile()
{
    ClearSelection();

    if (MainFileKey.empty())
    {
        return;
    }

    // 关闭请求在本帧文件浏览器渲染前处理，目录切换后的待刷新列表不能用于定位选中行。
    if (bNeedsRefresh)
    {
        Refresh();
    }

    for (size_t index = 0; index < CurrentFiles.size(); ++index)
    {
        const std::string filePath = CurrentFiles[index].u8string();
        if (IsMainFile(filePath))
        {
            // 使用目录枚举的原始写法，保证普通选中与后续 Shift 选择指向同一行。
            SelectedFiles.assign(1, filePath);
            SelectionAnchor = static_cast<int32_t>(index);
            break;
        }
    }

    LOGD("FileExplorerSelection", "Main image selection synchronized: index=%d", SelectionAnchor);
}

void FFileExplorer::ClearSelection()
{
    SelectedFiles.clear();
    SelectionAnchor = -1;
}

void FFileExplorer::SetOnFileExportRequested(FileExportCallback Callback)
{
    OnFileExportRequested = Callback;
}

void FFileExplorer::Refresh()
{
    // 列表重建后下标全部作废，锚点跟着失效；已选中的文件按路径在下面重新对账
    SelectionAnchor = -1;

    CurrentFiles.clear();
    CurrentDirectories.clear();

    try
    {
        const std::filesystem::path currentPath =
            std::filesystem::u8path(CurrentDirectory);
        for (const auto& entry : std::filesystem::directory_iterator(currentPath))
        {
            if (entry.is_directory())
            {
                CurrentDirectories.push_back(entry.path());
            }
            // 相机与图像管线导出的裸数据经常没有扩展名。文件浏览器只负责枚举，
            // 是否能按当前参数解码留给文档加载链路判断，避免在这里重复维护格式白名单。
            else if (entry.is_regular_file())
            {
                CurrentFiles.push_back(entry.path());
            }
        }

        // 排序
        std::sort(CurrentDirectories.begin(), CurrentDirectories.end());
        std::sort(CurrentFiles.begin(), CurrentFiles.end());
    }
    catch (const std::exception& exception)
    {
        CurrentFiles.clear();
        CurrentDirectories.clear();
        // 权限不足 / 目录已被删除等，保持空列表，同时留下可诊断的失败原因。
        LOGE(
            "Refresh",
            "Failed to enumerate directory: %s (%s)",
            CurrentDirectory.c_str(),
            exception.what());
    }

    // CurrentFiles 已按原生路径排序；每个选中路径只转换一次并二分查找，
    // 避免反复扫描整份列表，同时保留 SelectedFiles 的批量导出顺序。
    SelectedFiles.erase(
        std::remove_if(
            SelectedFiles.begin(),
            SelectedFiles.end(),
            [this](const std::string& Path)
            {
                return !std::binary_search(
                    CurrentFiles.begin(),
                    CurrentFiles.end(),
                    std::filesystem::u8path(Path));
            }),
        SelectedFiles.end());

    bNeedsRefresh = false;
}
