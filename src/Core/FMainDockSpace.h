#pragma once

#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "FAsyncJob.h"
#include "FAsyncImageLoader.h"
#include "FDirectoryImagePropertyHistory.h"
#include "FImageConfigCache.h"
#include "FImageDocument.h"
#include "UI/FMenuBar.h"
#include "UI/FExportPanel.h"
#include "UI/FFileExplorer.h"
#include "UI/FImageViewer.h"
#include "UI/FPropertyPanel.h"
#include "UI/FHistogramPanel.h"
#include "UI/FComparePanel.h"
#include "UI/FThemeColorPicker.h"

struct GLFWwindow;

/**
 * 主DockSpace管理类
 *
 * 负责布局，以及把"打开文件""切换显示对象""算差值"这些动作
 * 协调到异步加载服务或对应的 FImageDocument；解码和 GL 上传实现不在这里。
 */
class FMainDockSpace
{
public:
    FMainDockSpace();
    ~FMainDockSpace();

    /**
     * 初始化 DockSpace，并为图片后台上传创建与主窗口共享的隐藏 Context。
     */
    void Initialize(GLFWwindow* MainWindow);

    /**
     * 渲染主界面
     */
    void Render();

    /**
     * 打开一个文件或目录。目录会切换文件浏览器的当前路径。
     * 菜单、拖放、最近文件和命令行调用这条路径时都替换主图并显示主图。
     */
    void OpenPath(const std::string& Path);

    /**
     * 用给定参数打开文件，跳过文件名解析。供命令行启动使用。
     */
    void OpenPathWithParams(const std::string& Path, const FImageLoadParams& Params);

    /**
     * 加载对比图并恢复用户记忆的单图切换或平铺模式。对比图沿用主图当前的加载参数。
     * @return 路径有效且加载请求已受理；真正结果会在后续帧提交
     */
    bool OpenComparePath(const std::string& Path);

    /**
     * 覆盖主图的显示设置（色彩标准/原色/传输函数/色调映射…）
     *
     * 供命令行启动使用。这些项无法从文件里推断 —— P010 既可能是 PQ 也可能是普通 SDR ——
     * 所以只有调用方显式给出时才走这里。不触发重新加载，但要重算直方图。
     */
    void ApplyDisplaySettings(const FDisplaySettings& Display);

    /**
     * 接收一次系统拖放。
     *
     * GLFW 只把文件列表丢过来，落在哪个面板上要靠松手时的光标位置自己判断，
     * 因此这里只记下来，等本帧各面板渲染完（拿到它们的屏幕矩形）再派发。
     *
     * @param Paths    拖入的路径
     * @param CursorX  光标位置，相对窗口客户区左上角
     * @param CursorY  同上
     */
    void HandleDroppedPaths(const std::vector<std::string>& Paths, float CursorX, float CursorY);

    FFileExplorer* GetFileExplorer() { return FileExplorer.get(); }
    FImageViewer* GetImageViewer() { return ImageViewer.get(); }
    FPropertyPanel* GetPropertyPanel() { return PropertyPanel.get(); }
    FImageDocument* GetDocument() { return Document.get(); }

    /**
     * 请求退出应用。由菜单"退出"触发。
     */
    bool WantsToClose() const { return bWantsToClose; }

private:
    enum class ECompareOpenMode
    {
        InheritMainParameters,
        PreferSelectedFileMetadata,
    };

    void SetupDockSpace();

    /**
     * 文件浏览器普通选图按当前查看目标替换主图或对比图。
     * 差值图会回退到主图；平铺则保持布局并替换当前选中的一侧。
     * @return true 表示替换主图，文件浏览器应更新普通选中；false 表示替换对比图。
     */
    bool OpenFileExplorerSelection(const std::string& Path);

    /**
     * 替换主图，并在加载成功后切换到指定查看目标。
     */
    void OpenMainPath(const std::string& Path, EViewTarget SuccessViewTarget);

    /**
     * 打开新主图且当前没有对比图时，请求把右侧标签切回属性面板。
     */
    void RequestPropertyPanelSelectionIfNoCompareImage();

    /**
     * 对比面板的文件选择器允许跨目录挑选文件。跨目录时优先使用所选文件名/文件头
     * 推断参数；同目录仍沿用主图参数，方便比较同批无头 dump。
     */
    bool OpenPickedComparePath(const std::string& Path);

    bool OpenComparePathInternal(
        const std::string& Path,
        ECompareOpenMode Mode,
        EViewTarget SuccessViewTarget);
    bool IsFileInCurrentBrowserDirectory(const std::string& Path) const;

    /** 一次异步请求在主线程保留的 UI/配置提交信息。 */
    struct FPendingImageLoad
    {
        uint64_t RequestId = 0;
        EImageLoadTarget Target = EImageLoadTarget::Main;
        std::vector<FImageConfiguration> AttemptConfigurations;
        EViewTarget SuccessViewTarget = EViewTarget::Main;
        std::chrono::steady_clock::time_point SubmittedAt;
        bool bAddRecentFile = false;
        bool bCacheHit = false;
    };

    /** 主图尚未准备好时保留一个对比图意图，避免它取消主图请求。 */
    struct FDeferredCompareLoad
    {
        std::string Path;
        ECompareOpenMode Mode = ECompareOpenMode::InheritMainParameters;
        EViewTarget SuccessViewTarget = EViewTarget::Compare;
    };

    bool QueueImageLoad(
        FImageLoadRequest Request,
        std::vector<FImageConfiguration> AttemptConfigurations,
        EViewTarget SuccessViewTarget,
        bool bAddRecentFile,
        bool bCacheHit);
    void PollImageLoad();
    void CompleteImageLoad(FImageLoadResult Result);
    void ClearPendingImageLoadVisual();
    void StartDeferredCompareLoad();
    FImageDocument* GetDocumentForLoadTarget(EImageLoadTarget Target) const;

    /**
     * 属性面板当前正在编辑的文档
     */
    FImageDocument* GetTargetDocument() const;

    /**
     * 把文档的当前状态同步到属性面板与直方图
     */
    void SyncPanelFromDocument();

    /**
     * 把属性面板的当前值提交给文档（可能触发重新加载）
     */
    void SubmitPanelToDocument();

    /**
     * 某个文档内容变化后的统一处理：它正被属性面板编辑就回填面板，否则只重算直方图
     */
    void OnDocumentChanged(FImageDocument* ChangedDocument);

    /**
     * 移除主图 / 对比图。关闭主图时剩余对比图转为主图；差值图依赖旧的两图，一并作废。
     */
    void ClearMainDocument();
    void ClearCompareDocument();
    void ProcessPendingDocumentClose();

    /**
     * 派发上一次拖放。必须在各面板本帧渲染完之后调用，那时它们的屏幕矩形才是新的。
     */
    void ProcessPendingDrop();

    /**
     * 按对比面板的选择，决定查看器显示哪一个文档
     */
    void UpdateViewTarget();

    /**
     * 单图切换时在主图与对比图之间切换，并同步属性面板与直方图。
     */
    void SwitchSingleImageView();

    /**
     * 查看器当前显示的那个文档（平铺时为主图）。导出以它为准；
     * 平铺时直方图改为跟随属性面板当前编辑的文档。
     */
    FImageDocument* GetDisplayedDocument() const;

    /**
     * 菜单"导出..."：导出查看器当前显示的那幅图
     */
    void RequestExportCurrentImage();

    /**
     * 文件浏览器右键"导出"：批量导出选中的文件
     */
    void RequestExportFiles(const std::vector<std::string>& Paths);

    /**
     * 一次导出所需的全部输入的快照
     *
     * 导出跑在后台线程上，而"该导哪一幅图""基准参数是什么"这些判断依赖 UI 状态。
     * 所以在主线程一次性取好放进这里，工作线程只读它，不碰任何面板或文档对象。
     */
    struct FExportJob
    {
        FExportSettings Settings;
        std::vector<std::string> SourcePaths;

        /// true 时直接用 LoadedImage（菜单"导出..."），false 时逐个读盘（批量导出）
        bool bUseLoadedImage = false;

        /// 指向文档里的像素，不拷贝：导出期间整个界面置灰，这块内存不会被换掉
        const FImageData* LoadedImage = nullptr;

        EBayerPattern BayerPattern = EBayerPattern::RGGB;

        /// 批量导出逐个读盘时的基准参数
        FImageLoadParams BaseParams;
    };

    /**
     * 发起一次导出。真正的活交给后台线程，这里只组装快照并启动任务。
     */
    void PerformExport(const FExportSettings& Settings, const FExportRequest& Request);

    /**
     * 在**工作线程**上跑完一次导出
     *
     * 有意做成静态的：它不该也不能访问任何成员，Job 之外的东西一律不碰。
     *
     * @param OnProgress 每导完一个文件回调一次，参数是已完成的个数
     */
    static FExportResult RunExportJob(const FExportJob& Job, const std::function<void(int32_t)>& OnProgress);

    /**
     * 为导出加载一个文件（不建纹理，不改变任何文档状态）
     *
     * 批量导出的文件通常没被打开过，参数只能靠猜：先沿用主图的加载参数，
     * 再让文件名解析出来的分辨率/格式覆盖它 —— 与 FImageDocument::Open 同一套规则。
     * 自带文件头的格式（PNG/JPEG…）则一律不套用参数，交给按扩展名分发的加载器。
     */
    static std::unique_ptr<FImageData> LoadForExport(const std::string& Path, const FImageLoadParams& BaseParams);

    /**
     * 导出面板的默认保存目录：源文件所在目录，取不到时退回浏览器当前目录
     */
    std::string GetDefaultExportDirectory(const std::string& SourcePath) const;

    /**
     * 重算直方图。图像或色彩设置变化后调用。
     */
    void RebuildHistogram();

    /**
     * 计算主图与对比图的差值。同样交给后台线程，收尾（建纹理）回主线程做。
     */
    void ComputeDiff(float Gain);

    /**
     * 后台任务运行期间盖在整个界面上的忙碌遮罩
     */
    void RenderBusyOverlay();

    /**
     * 渲染“关于”模态弹窗，版本号来自统一的编译期版本常量。
     */
    void RenderAboutPopup();

    /**
     * 渲染设置模态弹窗（主题色、图片配置缓存容量与数据清理）。
     */
    void RenderSettingsPopup();

    /// 从持久设置重载主题色草稿，同时结束未保存的实时预览。
    void ReloadThemePaletteDraft();
    void SelectThemeColorRole(FUserSettings::EThemeColorRole Role);
    void RenderThemeSettings();

    /// 执行设置页的清理动作；false 表示连同用户设置与 UI 布局一起清除。
    void ClearStoredData(bool bOnlyImagePropertyCache);

    /// 删除 ImGui 布局文件，并禁止当前会话在退出时把旧布局重新写回。
    bool ClearUiLayoutSettingsFile();

    /**
     * 把文档当前的属性与视图配置写入按路径 LRU 缓存。主图还会同步记为
     * 该目录本次运行中的“上一张图片”，供后续缓存未命中的文件安全继承属性。
     * 只在切图/关闭图时调用，不进入逐帧渲染路径。
     */
    void CacheDocumentConfiguration(FImageDocument* Target);

    /**
     * 把内存缓存原子写入本机缓存目录。只在退出或显式设置操作时调用，
     * 避免切图与逐帧渲染承担磁盘 I/O。
     */
    bool PersistImageConfigCache(const char* Reason);

    /**
     * 查找一张图片的缓存配置。命中会提升其 LRU 热度。
     */
    bool TryGetCachedConfiguration(
        const std::string& Path,
        FImageConfiguration& OutConfiguration);

    /**
     * 把缓存配置或默认配置应用到文档槽位。
     */
    void ApplyDocumentConfiguration(
        FImageDocument* Target,
        const FImageConfiguration* Configuration);

    static std::string BuildImageConfigCacheKey(const std::string& Path);

    /**
     * 处理全局快捷键
     */
    void HandleShortcuts();

    void AddRecentFile(const std::string& Path);
    void PersistRecentFiles();
    void RenderRecentFilesMenu();

    std::unique_ptr<FImageDocument> Document;         ///< 主图
    std::unique_ptr<FImageDocument> CompareDocument;  ///< 对比图
    std::unique_ptr<FImageDocument> DiffDocument;     ///< 差值结果

    /// 关闭请求在下一帧绘制前处理，保持本帧 OpenGL 回调与属性面板借用的数据有效。
    FImageDocument* PendingDocumentClose = nullptr;

    std::unique_ptr<FMenuBar> MenuBar;
    std::unique_ptr<FFileExplorer> FileExplorer;
    std::unique_ptr<FImageViewer> ImageViewer;
    std::unique_ptr<FPropertyPanel> PropertyPanel;
    std::unique_ptr<FHistogramPanel> HistogramPanel;
    std::unique_ptr<FComparePanel> ComparePanel;
    std::unique_ptr<FExportPanel> ExportPanel;

    FImageConfigCache ImageConfigCache;
    FDirectoryImagePropertyHistory DirectoryImagePropertyHistory;

    std::deque<std::string> RecentFiles;

    /// 待派发的拖放：路径 + 松手时的光标位置（窗口客户区坐标）
    std::vector<std::string> PendingDropPaths;
    float PendingDropX;
    float PendingDropY;

    bool bIsInitialized;
    bool bWantsToClose;
    bool bRequestAboutPopup;
    bool bRequestSettingsPopup;
    /// 清除缓存后若用户未再切图，退出时不要把当前两张图立即塞回刚清空的缓存。
    bool bSkipActiveImageConfigOnShutdown;

    int32_t SettingsCacheCapacityDraft;
    FLocalization::ELanguage SettingsLanguageDraft = FLocalization::kDefaultLanguage;
    FUserSettings::FThemePalette SettingsThemePaletteDraft;
    FUserSettings::EThemeColorRole SettingsThemeSelectedRole;
    FThemeColorPickerState SettingsThemePickerState;
    /// 设置弹窗打开期间允许取色盘实时预览；取消或 Esc 需恢复持久值。
    bool bSettingsThemePreviewActive;
    /// 设置弹窗每次打开均默认只清图片属性缓存。
    bool bSettingsOnlyClearImagePropertyCache;

    /// 布局重建，或在没有对比图时打开新主图后，需要把"属性面板"设为选中标签页。
    /// 必须等到本帧所有面板渲染完再选中，同时不能改变文件浏览器的键盘焦点。
    bool bPendingSelectPropertyPanel;

    /// 最近一次已经应用到查看器的显示目标，用于只在真正切换时同步属性与直方图。
    std::optional<EViewTarget> AppliedViewTarget;

    /// 单图对比模式的进入边沿与提示过期时间；避免逐帧重复计次或重新开始倒计时。
    bool bWasSingleImageCompareModeActive;
    std::optional<std::chrono::steady_clock::time_point>
        SingleImageCompareHintExpiresAt;

    FAsyncImageLoader AsyncImageLoader;
    std::optional<FPendingImageLoad> PendingImageLoad;
    std::optional<FDeferredCompareLoad> DeferredCompareLoad;

    /// 计算差值 / 导出用的后台任务。
    ///
    /// **必须是最后一个成员**：成员按声明的逆序析构，它得先于几个文档销毁 ——
    /// 析构函数会等工作线程结束，而工作线程读的正是那些文档里的像素。
    FAsyncJob AsyncJob;
};
