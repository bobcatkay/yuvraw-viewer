#pragma once

#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <cstdint>

/**
 * 文件浏览器类
 * 负责显示文件树和文件选择
 */
class FFileExplorer
{
public:
    /**
     * @return true 表示本次激活替换主图，文件浏览器应同步更新普通选中；
     *         false 表示替换对比图，应保留当前主图选中标记。
     */
    using FileSelectedCallback = std::function<bool(const std::string&)>;
    using FileCompareCallback = std::function<void(const std::string&)>;
    using FileExportCallback = std::function<void(const std::vector<std::string>&)>;

    FFileExplorer();
    ~FFileExplorer();

    /**
     * 渲染文件浏览器
     */
    void Render();

    /**
     * 设置当前目录。会同时记入用户偏好，下次启动时恢复到这里。
     */
    void SetCurrentDirectory(const std::string& Directory);

    /**
     * 获取当前目录
     */
    std::string GetCurrentDirectory() const;

    /**
     * 设置文件激活回调。回调返回值决定是否更新普通选中状态。
     */
    void SetOnFileSelected(FileSelectedCallback Callback);

    /**
     * 设置"添加为对比图"回调（文件右键菜单）
     */
    void SetOnFileCompareRequested(FileCompareCallback Callback);

    /**
     * 设置当前对比图路径。传空字符串清除对比图高亮。
     */
    void SetCompareFilePath(const std::string& FilePath);

    /// 记录实际显示的主图路径，供右键菜单判断；不修改用户的多选集合。
    void SetMainFilePath(const std::string& FilePath);

    /// 对比图转为主图后，在当前目录中同步普通选中与 Shift 锚点。
    void SelectMainFile();

    /// 清除普通选中与 Shift 锚点，不修改打开图片的身份标记。
    void ClearSelection();

    /**
     * 设置"导出"回调（文件右键菜单）。参数是当前选中的全部文件，支持批量
     */
    void SetOnFileExportRequested(FileExportCallback Callback);

    /**
     * 刷新文件列表
     */
    void Refresh();

    /**
     * 判断一个屏幕坐标是否落在本面板内，用于把系统拖放派发到正确的面板。
     * 面板被折叠或位于未选中的标签页时恒为 false。
     */
    bool ContainsScreenPoint(float X, float Y) const;

private:
    /**
     * 顶部的"当前目录"一行：文件夹按钮 + 路径
     */
    void RenderCurrentDirectoryBar();

    /**
     * 文件列表。选中状态、多选与右键菜单都在这里
     */
    void RenderFileList();

    bool IsFileSelected(const std::string& FilePath) const;
    bool IsMainFile(const std::string& FilePath) const;
    bool IsCompareFile(const std::string& FilePath) const;

    /**
     * 处理一次文件行的点击，按修饰键决定是"打开""区间多选"还是"加减单个"
     * @param Index 该文件在 CurrentFiles 中的下标
     */
    void HandleFileClick(int32_t Index);

    /**
     * 把 [From, To] 区间（含两端，顺序无所谓）设为选中
     */
    void SelectRange(int32_t From, int32_t To);

    std::string CurrentDirectory;
    std::vector<std::filesystem::path> CurrentFiles;
    std::vector<std::filesystem::path> CurrentDirectories;

    FileSelectedCallback OnFileSelected;
    FileCompareCallback OnFileCompareRequested;
    FileExportCallback OnFileExportRequested;

    /// 当前选中的文件（可多选）。按点击顺序/区间顺序排列，批量导出直接用它
    std::vector<std::string> SelectedFiles;

    /// 实际显示的主图路径键；多选和待加载文件不能影响当前图片的菜单判断。
    std::string MainFileKey;

    /// 规范化后的对比图路径键；独立于普通选中项，保证主图和对比图可同时高亮
    std::string CompareFileKey;

    /// Shift 区间选择的锚点，指向 CurrentFiles 的下标；-1 表示还没有锚点
    int32_t SelectionAnchor;

    bool bNeedsRefresh;

    /// 本帧窗口在屏幕坐标下的矩形，供拖放命中判定；不可见时 bDropRectValid 为 false
    float DropRectMin[2];
    float DropRectMax[2];
    bool bDropRectValid;
};
