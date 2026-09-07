#include "Core/FLocalization.h"
#include "FMainDockSpace.h"

#include "FAppVersion.h"
#include "FFileDialog.h"
#include "FThirdPartyNotices.h"
#include "FUserSettings.h"
#include "Image/FImageCompare.h"
#include "Image/FImageExporter.h"
#include "Image/FImageLoadParams.h"
#include "Image/FImageLoader.h"
#include "UI/FToast.h"
#include "UI/FUiIcons.h"
#include "UI/FUiScale.h"
#include "UI/FUiTheme.h"
#include "Util.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>

namespace
{
    /**
     * DockSpace 的标识字符串，兼作**布局版本号**。
     *
     * 布局只在 DockBuilderGetNode 返回空（即首次运行）时构建一次，之后由 imgui.ini 接管。
     * 新增面板时老用户的 ini 里没有它们的 DockId，面板会以浮动窗口出现在屏幕中央。
     * 改这个字符串会让旧 ini 中的节点失配，从而触发一次重新布局。
     *
     * **新增或移除面板后请递增末尾的版本号。**
    */
    // 内部持久化 ID 沿用旧名，迁移品牌时保留 imgui.ini 中用户已有的面板布局。
    constexpr const char* kDockSpaceId = "ImageDevToolDockSpace_v4";


    constexpr float kAboutPopupWidth = 480.0f;
    constexpr float kAboutPopupHeight = 360.0f;
    constexpr float kUsageGuidePopupWidth = 720.0f;
    constexpr float kUsageGuidePopupHeight = 600.0f;
    constexpr float kUsageGuideCloseButtonWidth = 88.0f;
    constexpr int32_t kUsageGuideStyleVarCount = 3;
    constexpr float kThirdPartyPopupWidth = 720.0f;
    constexpr float kThirdPartyPopupHeight = 560.0f;
    constexpr float kPopupViewportMargin = 24.0f;
    constexpr float kAboutHorizontalPadding = 28.0f;
    constexpr float kAboutVerticalPadding = 24.0f;
    constexpr float kAboutPopupRounding = 10.0f;
    constexpr float kAboutFrameHorizontalPadding = 16.0f;
    constexpr float kAboutFrameVerticalPadding = 7.0f;
    constexpr float kAboutItemSpacingY = 10.0f;
    constexpr float kAboutTitleScale = 1.4f;
    constexpr float kAboutSectionAdditionalGap = 8.0f;
    constexpr float kAboutBadgeHorizontalPadding = 14.0f;
    constexpr float kAboutBadgeVerticalPadding = 6.0f;
    constexpr float kAboutBadgeRounding = 12.0f;
    constexpr float kAboutLicensesButtonWidth = 132.0f;
    constexpr float kAboutConfirmButtonWidth = 88.0f;
    constexpr float kLicenseCloseButtonWidth = 80.0f;
    constexpr float kSettingsPopupWidth = 720.0f;
    constexpr float kSettingsPopupHeight = 720.0f;
    constexpr float kSettingsLanguageInputWidth = 180.0f;
    constexpr float kSettingsHorizontalPadding = 30.0f;
    constexpr float kSettingsVerticalPadding = 26.0f;
    constexpr float kSettingsPopupRounding = 10.0f;
    constexpr float kSettingsFrameHorizontalPadding = 12.0f;
    constexpr float kSettingsFrameVerticalPadding = 7.0f;
    constexpr float kSettingsItemSpacingY = 12.0f;
    constexpr float kSettingsCacheInputWidth = 180.0f;
    constexpr float kSettingsThemeEditorPreferredHeight = 340.0f;
    constexpr float kSettingsThemeEditorMinimumHeight = 220.0f;
    constexpr float kSettingsThemeEditorReservedHeight = 160.0f;
    constexpr float kSettingsThemeColorListWidth = 230.0f;
    constexpr float kSettingsThemeColorListWidthRatio = 0.5f;
    constexpr float kSettingsThemePickerMaximumWidth = 220.0f;
    constexpr float kSettingsThemeColorSwatchWidth = 30.0f;
    constexpr float kSettingsThemeColorSwatchHeight = 22.0f;
    constexpr float kSettingsThemeRowTextAlignment = 0.5f;
    constexpr int32_t kSettingsThemeColorTableColumnCount = 3;
    constexpr float kSettingsClearButtonWidth = 168.0f;
    constexpr float kSettingsApplyButtonWidth = 88.0f;
    constexpr float kSettingsCancelButtonWidth = 80.0f;
    constexpr int32_t kSettingsStyleVarCount = 4;
    constexpr int32_t kCacheCapacityInputStep = 10;
    constexpr int32_t kCacheCapacityInputFastStep = 100;
    constexpr float kImageLoadFailureToastSeconds = 3.5f;
    constexpr float kProjectReleasesFailureToastSeconds = 4.0f;
    constexpr int64_t kSlowHistogramLogMilliseconds = 16;
    constexpr int64_t kSlowHistogramLogIntervalMilliseconds = 1000;
    constexpr std::chrono::seconds kSingleImageCompareHintDuration(3);

    constexpr ImU32 kAboutAccentColor = IM_COL32(16, 87, 100, 255);
    constexpr ImU32 kAboutSubtitleColor = IM_COL32(92, 92, 92, 255);
    constexpr ImU32 kAboutSecondaryButton = IM_COL32(226, 232, 240, 255);
    constexpr ImU32 kAboutSecondaryButtonHovered = IM_COL32(203, 213, 225, 255);
    constexpr ImU32 kAboutSecondaryButtonActive = IM_COL32(183, 196, 211, 255);
    constexpr ImU32 kAboutSecondaryButtonBorder = IM_COL32(78, 158, 166, 255);
    constexpr ImU32 kAboutPrimaryButton = IM_COL32(30, 140, 148, 255);
    constexpr ImU32 kAboutPrimaryButtonHovered = IM_COL32(24, 113, 125, 255);
    constexpr ImU32 kAboutPrimaryButtonActive = IM_COL32(16, 87, 100, 255);
    constexpr ImU32 kAboutPrimaryButtonText = IM_COL32(255, 255, 255, 255);
    const ImVec4 kSettingsWarningTextColor(0.95f, 0.55f, 0.15f, 1.0f);
    const ImVec4 kSettingsDangerButtonColor(0.86f, 0.15f, 0.15f, 1.0f);
    const ImVec4 kSettingsDangerButtonHoveredColor(0.73f, 0.10f, 0.10f, 1.0f);
    const ImVec4 kSettingsDangerButtonActiveColor(0.60f, 0.07f, 0.07f, 1.0f);
    const ImVec4 kSettingsDangerButtonTextColor(1.0f, 1.0f, 1.0f, 1.0f);

    struct FUsageGuideSection
    {
        EUiText Title;
        EUiText Body;
    };

    constexpr FUsageGuideSection kUsageGuideSections[] = {
        { EUiText::UsageGuideShortcutsTitle, EUiText::UsageGuideShortcutsBody },
        { EUiText::UsageGuideViewTitle, EUiText::UsageGuideViewBody },
        { EUiText::UsageGuideCompareTitle, EUiText::UsageGuideCompareBody },
        { EUiText::UsageGuideExportTitle, EUiText::UsageGuideExportBody },
    };

    struct FThemeColorUiDefinition
    {
        FUserSettings::EThemeColorRole Role;
        EUiText Label;
    };

    constexpr std::array<
        FThemeColorUiDefinition,
        FUserSettings::kThemeColorRoleCount> kThemeColorUiDefinitions{{
        { FUserSettings::EThemeColorRole::Accent, EUiText::ThemeAccent },
        { FUserSettings::EThemeColorRole::InterfaceBackground, EUiText::ThemeBackground },
        { FUserSettings::EThemeColorRole::Text, EUiText::ThemeText },
        { FUserSettings::EThemeColorRole::Border, EUiText::ThemeBorder },
        { FUserSettings::EThemeColorRole::InputBackground, EUiText::ThemeInput },
        { FUserSettings::EThemeColorRole::Control, EUiText::ThemeControls },
    }};

    float CalculateGlyphCenteredTextY(
        const char* Text,
        float ContainerMinY,
        float ContainerMaxY)
    {
        ImFont* font = ImGui::GetFont();
        const float fontScale = ImGui::GetFontSize() / font->FontSize;
        const char* cursor = Text;
        const char* textEnd = Text + std::strlen(Text);
        float glyphMinY = 0.0f;
        float glyphMaxY = 0.0f;
        bool bHasVisibleGlyph = false;

        while (cursor < textEnd)
        {
            unsigned int codepoint = 0;
            const int32_t byteCount =
                ImTextCharFromUtf8(&codepoint, cursor, textEnd);

            if (byteCount <= 0)
            {
                break;
            }

            cursor += byteCount;
            const ImFontGlyph* glyph =
                font->FindGlyph(static_cast<ImWchar>(codepoint));

            if (!glyph || !glyph->Visible)
            {
                continue;
            }

            if (!bHasVisibleGlyph)
            {
                glyphMinY = glyph->Y0;
                glyphMaxY = glyph->Y1;
                bHasVisibleGlyph = true;
            }
            else
            {
                glyphMinY = std::min(glyphMinY, glyph->Y0);
                glyphMaxY = std::max(glyphMaxY, glyph->Y1);
            }
        }

        const float containerCenterY =
            (ContainerMinY + ContainerMaxY) / 2.0f;

        if (!bHasVisibleGlyph)
        {
            return containerCenterY - ImGui::GetFontSize() / 2.0f;
        }

        // 微软雅黑的中文字形在字体行框内偏下，按实际字形边界居中才能让按钮上下留白一致。
        const float glyphCenterY =
            (glyphMinY + glyphMaxY) * fontScale / 2.0f;
        return containerCenterY - glyphCenterY;
    }

    bool CenteredTextButton(const char* Label, const ImVec2& Size)
    {
        const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);

        // 让 ImGui 继续负责完整按钮交互和背景，只隐藏其行框对齐的文字并按字形边界重绘。
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        const bool bPressed = ImGui::Button(Label, Size);
        ImGui::PopStyleColor();

        if (ImGui::IsItemVisible())
        {
            const ImVec2 buttonMin = ImGui::GetItemRectMin();
            const ImVec2 buttonMax = ImGui::GetItemRectMax();
            const ImVec2 textSize = ImGui::CalcTextSize(Label);
            const ImVec2 textPos(
                buttonMin.x +
                    std::max(0.0f, (buttonMax.x - buttonMin.x - textSize.x) / 2.0f),
                CalculateGlyphCenteredTextY(
                    Label,
                    buttonMin.y,
                    buttonMax.y));
            ImGui::GetWindowDrawList()->AddText(textPos, textColor, Label);
        }

        return bPressed;
    }

    FImageLoadAttempt MakeAutomaticLoadAttempt(const char* Source)
    {
        FImageLoadAttempt attempt;
        attempt.Mode = EImageLoadMode::Automatic;
        attempt.DiagnosticSource = Source ? Source : "automatic";
        return attempt;
    }

    FImageLoadAttempt MakeExplicitLoadAttempt(
        const FImageLoadParams& Params,
        const char* Source)
    {
        FImageLoadAttempt attempt;
        attempt.Mode = EImageLoadMode::Explicit;
        attempt.Params = Params;
        attempt.DiagnosticSource = Source ? Source : "explicit";
        return attempt;
    }

    FImageConfiguration MakeImageConfiguration(
        const FImageLoadParams& LoadParams,
        const FDisplaySettings& DisplaySettings = FDisplaySettings{},
        const FImageViewSettings& ViewSettings = FImageViewSettings{})
    {
        FImageConfiguration configuration;
        configuration.LoadParams = LoadParams;
        configuration.DisplaySettings = DisplaySettings;
        configuration.ViewSettings = ViewSettings;
        return configuration;
    }

    bool AreLoadParamsEqual(
        const FImageLoadParams& Left,
        const FImageLoadParams& Right)
    {
        return Left.Format == Right.Format
            && Left.Width == Right.Width
            && Left.Height == Right.Height
            && Left.Stride == Right.Stride
            && Left.BitsPerPixel == Right.BitsPerPixel
            && Left.BayerPattern == Right.BayerPattern
            && Left.ByteOrder == Right.ByteOrder
            && Left.SampleAlignment == Right.SampleAlignment;
    }

    const char* ImageLoadTargetName(EImageLoadTarget Target)
    {
        return Target == EImageLoadTarget::Main ? "main" : "compare";
    }

    const char* ViewTargetName(EViewTarget Target)
    {
        switch (Target)
        {
        case EViewTarget::Main:       return "main";
        case EViewTarget::Compare:    return "compare";
        case EViewTarget::Diff:       return "diff";
        case EViewTarget::SideBySide: return "side-by-side";
        default:                      return "unknown";
        }
    }

    /**
     * 弹窗优先使用设计尺寸；小窗口下收进工作区，避免标题栏或操作按钮落到屏幕外。
     */
    ImVec2 GetPopupSize(
        const ImGuiViewport* Viewport,
        float PreferredWidth,
        float PreferredHeight)
    {
        const float scaledPreferredWidth = FUiScale::Apply(PreferredWidth);
        const float scaledPreferredHeight = FUiScale::Apply(PreferredHeight);

        if (!Viewport)
        {
            return ImVec2(scaledPreferredWidth, scaledPreferredHeight);
        }

        const float viewportMargin = FUiScale::Apply(kPopupViewportMargin);
        const float maxWidth = std::max(
            1.0f,
            Viewport->WorkSize.x - viewportMargin * 2.0f);
        const float maxHeight = std::max(
            1.0f,
            Viewport->WorkSize.y - viewportMargin * 2.0f);

        return ImVec2(
            std::min(scaledPreferredWidth, maxWidth),
            std::min(scaledPreferredHeight, maxHeight));
    }

    void ConfigureNextPopup(
        const ImGuiViewport* Viewport,
        float PreferredWidth,
        float PreferredHeight)
    {
        const ImVec2 popupSize =
            GetPopupSize(Viewport, PreferredWidth, PreferredHeight);
        ImGui::SetNextWindowSize(popupSize, ImGuiCond_Appearing);

        if (!Viewport)
        {
            return;
        }

        // 强制附着主视口，避免启用 ImGui 多视口时模态弹窗成为独立系统窗口；
        // 否则主窗口最小化/恢复后可能只剩不可见模态层在拦截输入。
        ImGui::SetNextWindowViewport(Viewport->ID);
        ImGui::SetNextWindowPos(
            ImVec2(
                Viewport->WorkPos.x + Viewport->WorkSize.x * 0.5f,
                Viewport->WorkPos.y + Viewport->WorkSize.y * 0.5f),
            ImGuiCond_Appearing,
            ImVec2(0.5f, 0.5f));
    }

    /**
     * 完整许可文本独立滚动显示，让“关于”页保持简洁，同时确保单文件发布也能查看声明。
     */
    void RenderThirdPartyLicensesPopup(const ImGuiViewport* Viewport)
    {
        ConfigureNextPopup(
            Viewport,
            kThirdPartyPopupWidth,
            kThirdPartyPopupHeight);

        if (!ImGui::BeginPopupModal(
                FLocalization::WindowTitle(EUiText::ThirdPartyPopup),
                nullptr,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            return;
        }

        ImGui::TextWrapped(
            FLocalization::Text(EUiText::ThirdPartyIntro));
        ImGui::Separator();

        const ImGuiStyle& style = ImGui::GetStyle();
        const float footerHeight =
            ImGui::GetFrameHeight() + style.ItemSpacing.y;

        if (ImGui::BeginChild(
                "##ThirdPartyLicenseText",
                ImVec2(0.0f, -footerHeight),
                ImGuiChildFlags_Borders,
                ImGuiWindowFlags_HorizontalScrollbar))
        {
            ImGui::TextUnformatted(FThirdPartyNotices::Text);
        }
        ImGui::EndChild();

        ImGui::SetCursorPosX(std::max(
            style.WindowPadding.x,
            ImGui::GetWindowWidth()
                - style.WindowPadding.x
                - FUiScale::Apply(kLicenseCloseButtonWidth)));

        if (ImGui::Button(
                FLocalization::Text(EUiText::Close),
                ImVec2(FUiScale::Apply(kLicenseCloseButtonWidth), 0.0f)))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    /**
     * 只切换 Dock 标签，不改变 ImGui 的键盘导航窗口。
     * SetWindowFocus() 会把 NavWindow 改成目标面板，导致文件浏览器刚选中的文件
     * 无法继续响应上下键；NextSelectedTabId 会在下一帧布局时被消费且不会抢焦点。
     */
    void SelectDockedWindowTabWithoutFocus(const char* WindowName)
    {
        ImGuiWindow* window = ImGui::FindWindowByName(WindowName);

        if (!window || !window->DockNode)
        {
            return;
        }

        ImGuiDockNode* dockNode = window->DockNode;
        dockNode->SelectedTabId = window->TabId;

        if (dockNode->TabBar)
        {
            dockNode->TabBar->NextSelectedTabId = window->TabId;
        }
    }
}

FMainDockSpace::FMainDockSpace()
    : ImageConfigCache(FUserSettings::GetImageConfigCacheCapacity())
    , PendingDropX(0.0f)
    , PendingDropY(0.0f)
    , bIsInitialized(false)
    , bWantsToClose(false)
    , bRequestAboutPopup(false)
    , bRequestUsageGuidePopup(false)
    , bRequestSettingsPopup(false)
    , bSkipActiveImageConfigOnShutdown(false)
    , SettingsCacheCapacityDraft(static_cast<int32_t>(ImageConfigCache.GetCapacity()))
    , SettingsThemePaletteDraft(FUserSettings::kDefaultThemePalette)
    , SettingsThemeSelectedRole(FUserSettings::EThemeColorRole::Accent)
    , SettingsThemePickerState{}
    , bSettingsThemePreviewActive(false)
    , bSettingsOnlyClearImagePropertyCache(true)
    , bPendingSelectPropertyPanel(false)
    , bWasSingleImageCompareModeActive(false)
{
    FLocalization::SetLanguage(FUserSettings::GetLanguage());
    LOGI("Language", "UI language restored: %s",
        FLocalization::LanguageCode(FLocalization::GetLanguage()));
    const std::vector<std::string>& persistedRecentFiles =
        FUserSettings::GetRecentFiles();
    RecentFiles.assign(
        persistedRecentFiles.begin(),
        persistedRecentFiles.end());
    LOGI(
        "RecentFiles",
        "Restored %llu recent files",
        static_cast<unsigned long long>(RecentFiles.size()));

    const std::filesystem::path cachePath =
        FUserSettings::GetImageConfigCachePath();
    std::error_code cachePathError;

    if (std::filesystem::exists(cachePath, cachePathError))
    {
        const auto loadStart = std::chrono::steady_clock::now();
        const bool bLoaded = ImageConfigCache.LoadFromFile(cachePath);
        const auto loadMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - loadStart).count();

        if (bLoaded)
        {
            LOGI(
                "ImageConfigCache",
                "Loaded %llu persisted image configurations in %lld ms",
                static_cast<unsigned long long>(ImageConfigCache.GetSize()),
                static_cast<long long>(loadMilliseconds));
        }
        else
        {
            LOGW(
                "ImageConfigCache",
                "Ignoring invalid image configuration cache: %s",
                cachePath.u8string().c_str());
        }
    }
    else if (cachePathError)
    {
        LOGW(
            "ImageConfigCache",
            "Failed to inspect image configuration cache: %s (%s)",
            cachePath.u8string().c_str(),
            cachePathError.message().c_str());
    }

    Document = std::make_unique<FImageDocument>();
    CompareDocument = std::make_unique<FImageDocument>();
    DiffDocument = std::make_unique<FImageDocument>();

    MenuBar = std::make_unique<FMenuBar>();
    FileExplorer = std::make_unique<FFileExplorer>();
    ImageViewer = std::make_unique<FImageViewer>();
    PropertyPanel = std::make_unique<FPropertyPanel>();
    HistogramPanel = std::make_unique<FHistogramPanel>();
    ComparePanel = std::make_unique<FComparePanel>();
    ExportPanel = std::make_unique<FExportPanel>();

    ImageViewer->SetDocument(Document.get());
    ImageViewer->SetSelectedDocument(Document.get());
    ComparePanel->SetDocuments(Document.get(), CompareDocument.get(), DiffDocument.get());

    // 文件浏览器普通选图会根据当前查看目标替换主图或对比图。
    FileExplorer->SetOnFileSelected([this](const std::string& FilePath) {
        return OpenFileExplorerSelection(FilePath);
    });

    // 文件浏览器右键"添加为对比图"
    FileExplorer->SetOnFileCompareRequested([this](const std::string& FilePath) {
        OpenComparePath(FilePath);
    });

    // 文件浏览器右键"导出"，支持多选批量
    FileExplorer->SetOnFileExportRequested([this](const std::vector<std::string>& Paths) {
        RequestExportFiles(Paths);
    });

    // 文档内容变化 -> 回填属性面板并重算直方图
    Document->SetOnChanged([this]() {
        OnDocumentChanged(Document.get());
    });

    CompareDocument->SetOnChanged([this]() {
        OnDocumentChanged(CompareDocument.get());
    });
}

FMainDockSpace::~FMainDockSpace()
{
    const auto shutdownStart = std::chrono::steady_clock::now();
    LOGI("Shutdown", "Dock space shutdown started");

    // 后台线程可能正持有加载器产物和共享 GL 对象；必须在缓存访问、文档析构、
    // LoaderFactory 清理以及主 Context 销毁之前停止并回收。
    ClearPendingImageLoadVisual();
    AsyncImageLoader.Shutdown();

    if (!bSkipActiveImageConfigOnShutdown)
    {
        // 当前两个槽位尚未经历“切出”，退出前先归档，保证最后一次属性/视图改动也能恢复。
        CacheDocumentConfiguration(Document.get());
        CacheDocumentConfiguration(CompareDocument.get());
    }

    const auto cacheMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - shutdownStart).count();
    LOGI(
        "Shutdown",
        "Active document configurations archived in %lld ms",
        static_cast<long long>(cacheMilliseconds));

    PersistImageConfigCache("shutdown");
}

void FMainDockSpace::Initialize(GLFWwindow* MainWindow)
{
    if (bIsInitialized)
    {
        return;
    }

    if (!AsyncImageLoader.Initialize(MainWindow))
    {
        LOGW(
            "AsyncImageLoader",
            "%s",
            "Background loader unavailable; image loads will use the synchronous compatibility path");
    }

    MenuBar->SetOnOpenFile([this]() {
        std::string path;

        if (FFileDialog::OpenFile(FLocalization::Text(EUiText::OpenImage), FImageLoaderFactory::GetAllSupportedExtensions(), path))
        {
            OpenPath(path);
        }
    });

    MenuBar->SetOnOpenDirectory([this]() {
        std::string path;

        if (FFileDialog::OpenDirectory(FLocalization::Text(EUiText::OpenDirectory), path))
        {
            FileExplorer->SetCurrentDirectory(path);
        }
    });

    MenuBar->SetOnExport([this]() {
        RequestExportCurrentImage();
    });

    MenuBar->SetOnExit([this]() {
        bWantsToClose = true;
    });

    MenuBar->SetOnSettings([this]() {
        bRequestSettingsPopup = true;
    });

    MenuBar->SetOnUsageGuide([this]() {
        LOGI("UsageGuide", "Opening usage guide dialog");
        bRequestUsageGuidePopup = true;
    });

    MenuBar->SetOnAbout([this]() {
        LOGI("About", "Opening About dialog for YUVRaw %s", FAppVersion::String);
        bRequestAboutPopup = true;
    });

    ExportPanel->SetOnExport([this](const FExportSettings& Settings, const FExportRequest& Request) {
        PerformExport(Settings, Request);
    });

    // "最近打开"需要本类的状态，通过插槽自己画
    MenuBar->SetFileMenuExtras([this]() {
        RenderRecentFilesMenu();
    });

    // 属性面板的任意参数变更都走同一条提交路径
    PropertyPanel->SetOnFormatChanged([this](EImageFormat) { SubmitPanelToDocument(); });
    PropertyPanel->SetOnResolutionChanged([this](int32_t, int32_t) { SubmitPanelToDocument(); });
    PropertyPanel->SetOnBitsPerPixelChanged([this](int32_t) { SubmitPanelToDocument(); });
    PropertyPanel->SetOnStrideChanged([this](int32_t) { SubmitPanelToDocument(); });
    PropertyPanel->SetOnBayerPatternChanged([this](EBayerPattern) { SubmitPanelToDocument(); });
    PropertyPanel->SetOnByteOrderChanged([this](EByteOrder) { SubmitPanelToDocument(); });
    PropertyPanel->SetOnSampleAlignmentChanged(
        [this](ESampleAlignment) { SubmitPanelToDocument(); });
    PropertyPanel->SetOnFormatPresetApplied(
        [this]() { SubmitPanelToDocument(); });

    // 显示设置不参与解码，直接写进目标文档即可，但直方图统计要跟着色彩标准走
    PropertyPanel->SetOnDisplaySettingsChanged([this](const FDisplaySettings& Settings) {
        if (FImageDocument* target = GetTargetDocument())
        {
            target->SetDisplaySettings(Settings);
        }

        RebuildHistogram();
    });

    // 切换编辑对象 -> 把新对象的参数回填到面板
    PropertyPanel->SetOnTargetChanged([this](EPropertyTarget) {
        SyncPanelFromDocument();
    });

    // 平铺查看器点击任一侧 -> 属性面板、身份标签和直方图统一切到该文档。
    ImageViewer->SetOnDocumentSelected([this](FImageDocument* SelectedDocument) {
        EPropertyTarget selectedTarget;
        const char* selectedName = nullptr;

        if (SelectedDocument == Document.get())
        {
            selectedTarget = EPropertyTarget::Main;
            selectedName = "main";
        }
        else if (SelectedDocument == CompareDocument.get())
        {
            selectedTarget = EPropertyTarget::Compare;
            selectedName = "compare";
        }
        else if (SelectedDocument == DiffDocument.get())
        {
            // 差值图没有可编辑的源属性，但仍允许查看器独立缩放/旋转。
            return;
        }
        else
        {
            LOGW(
                "SelectEditTarget",
                "%s",
                "Ignoring unknown document selected by image viewer");
            return;
        }

        if (PropertyPanel->GetTarget() == selectedTarget)
        {
            return;
        }

        LOGD("SelectEditTarget", "Editing target changed from image viewer: %s", selectedName);
        PropertyPanel->SetTarget(selectedTarget);
        SyncPanelFromDocument();
    });

    ImageViewer->SetOnSingleImageSwitchRequested([this]() {
        SwitchSingleImageView();
    });

    ImageViewer->SetOnDocumentCloseRequested([this](FImageDocument* ClosingDocument) {
        PendingDocumentClose = ClosingDocument;
    });

    ComparePanel->SetOnPickCompareFile([this](const std::string& Path) {
        OpenPickedComparePath(Path);
    });

    ComparePanel->SetOnComputeDiff([this](float Gain) {
        ComputeDiff(Gain);
    });

    ComparePanel->SetOnClearMain([this]() {
        PendingDocumentClose = Document.get();
    });

    ComparePanel->SetOnClearCompare([this]() {
        PendingDocumentClose = CompareDocument.get();
    });

    bIsInitialized = true;
}

void FMainDockSpace::ProcessPendingDocumentClose()
{
    FImageDocument* closingDocument = PendingDocumentClose;
    PendingDocumentClose = nullptr;

    if (!closingDocument)
    {
        return;
    }

    if (closingDocument == Document.get())
    {
        LOGI("CloseImage", "%s", "Closing main image");
        ClearMainDocument();
    }
    else if (closingDocument == CompareDocument.get())
    {
        LOGI("CloseImage", "%s", "Closing comparison image");
        ClearCompareDocument();
    }
    else if (closingDocument == DiffDocument.get())
    {
        // 差值是独立的生成结果，关闭它只清除结果与统计，两个源图继续保留。
        LOGI("CloseImage", "%s", "Closing difference image");
        DiffDocument->Clear();
        ComparePanel->SetStats(FCompareStats());
        ComparePanel->SetViewTarget(EViewTarget::Main);
    }
    else
    {
        LOGW("CloseImage", "%s", "Ignoring unknown document close request");
        return;
    }

    // 即使关闭前后仍是 Main，也要重新回填空文档，清除属性与直方图的旧数据。
    AppliedViewTarget.reset();
    UpdateViewTarget();
}

void FMainDockSpace::ClearMainDocument()
{
    // 主图关闭后对比图会接管主图槽位，旧的任一加载请求都不能再按原槽位提交。
    if (PendingImageLoad)
    {
        AsyncImageLoader.Cancel();
        ClearPendingImageLoadVisual();
        PendingImageLoad.reset();
    }
    DeferredCompareLoad.reset();

    CacheDocumentConfiguration(Document.get());

    if (CompareDocument->IsValid())
    {
        const FImageViewSettings remainingView =
            ImageViewer->GetViewSettings(CompareDocument.get());
        Document->TakeContentFrom(*CompareDocument);
        ImageViewer->SetViewSettings(Document.get(), remainingView);
        ImageViewer->SetViewSettings(CompareDocument.get(), FImageViewSettings{});
        LOGI("CloseImage", "%s", "Promoted remaining comparison image to main image");
    }
    else
    {
        Document->Clear();
        CompareDocument->Clear();
    }

    FileExplorer->SetMainFilePath(Document->GetFilePath());
    FileExplorer->SetCompareFilePath("");
    FileExplorer->SelectMainFile();

    // 差值图是拿主图算出来的，主图没了它就失去意义
    DiffDocument->Clear();
    ComparePanel->SetStats(FCompareStats());
    ComparePanel->SetViewTarget(EViewTarget::Main);
}

void FMainDockSpace::ClearCompareDocument()
{
    DeferredCompareLoad.reset();

    if (PendingImageLoad &&
        PendingImageLoad->Target == EImageLoadTarget::Compare)
    {
        AsyncImageLoader.Cancel();
        ClearPendingImageLoadVisual();
        PendingImageLoad.reset();
    }

    CacheDocumentConfiguration(CompareDocument.get());
    CompareDocument->Clear();
    FileExplorer->SetCompareFilePath("");

    DiffDocument->Clear();
    ComparePanel->SetStats(FCompareStats());
    ComparePanel->SetViewTarget(EViewTarget::Main);
}

void FMainDockSpace::HandleDroppedPaths(const std::vector<std::string>& Paths, float CursorX, float CursorY)
{
    // 后台任务运行期间界面整个置灰，拖放同样不受理：工作线程正读着当前文档里的像素，
    // 此刻打开新文件会把它换掉
    if (Paths.empty() || AsyncJob.IsRunning())
    {
        return;
    }

    PendingDropPaths = Paths;
    PendingDropX = CursorX;
    PendingDropY = CursorY;
}

void FMainDockSpace::ProcessPendingDrop()
{
    if (PendingDropPaths.empty())
    {
        return;
    }

    const std::vector<std::string> paths = PendingDropPaths;
    PendingDropPaths.clear();

    // GLFW 给的是窗口客户区坐标；开启多视口后 ImGui 的面板矩形是桌面坐标，
    // 两者正好差一个主视口原点（未开多视口时该原点为 0，公式同样成立）
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float x = PendingDropX + viewport->Pos.x;
    const float y = PendingDropY + viewport->Pos.y;

    const std::string& first = paths.front();

    std::error_code ec;

    // 拖到文件浏览器：目录直接切过去，文件则切到它所在的目录
    if (FileExplorer->ContainsScreenPoint(x, y))
    {
        if (std::filesystem::is_directory(std::filesystem::u8path(first), ec))
        {
            FileExplorer->SetCurrentDirectory(first);
        }
        else
        {
            const std::filesystem::path parent =
                std::filesystem::u8path(first).parent_path();

            if (!parent.empty())
            {
                FileExplorer->SetCurrentDirectory(parent.u8string());
            }
        }

        return;
    }

    // 拖到查看器或其它地方：按打开图像处理（目录仍然只是切换浏览目录）
    OpenPath(first);
}

FImageDocument* FMainDockSpace::GetTargetDocument() const
{
    return PropertyPanel->GetTarget() == EPropertyTarget::Compare
        ? CompareDocument.get()
        : Document.get();
}

void FMainDockSpace::OnDocumentChanged(FImageDocument* ChangedDocument)
{
    if (ChangedDocument == GetTargetDocument())
    {
        SyncPanelFromDocument();
    }
    else
    {
        RebuildHistogram();
    }
}

std::string FMainDockSpace::BuildImageConfigCacheKey(const std::string& Path)
{
    if (Path.empty())
    {
        return {};
    }

    std::error_code ec;
    std::filesystem::path normalized =
        std::filesystem::weakly_canonical(std::filesystem::u8path(Path), ec);

    if (ec)
    {
        ec.clear();
        normalized = std::filesystem::absolute(std::filesystem::u8path(Path), ec);
    }

    if (ec)
    {
        normalized = std::filesystem::u8path(Path);
    }

    return normalized.lexically_normal().generic_u8string();
}

void FMainDockSpace::CacheDocumentConfiguration(FImageDocument* Target)
{
    if (!Target || Target->GetFilePath().empty())
    {
        return;
    }

    std::error_code ec;

    // 差值图等计算产物使用“<差值图>”标签占位，不能进入按文件缓存。
    if (!std::filesystem::is_regular_file(
            std::filesystem::u8path(Target->GetFilePath()),
            ec))
    {
        return;
    }

    // CommitLoadResult 失败时仍保留上一幅图的像素用于显示，但路径和参数已经是
    // 本次失败请求。只有“当前路径的最近一次加载成功”才能进入文件缓存和目录历史。
    if (!Target->IsValid()
        || !Target->GetLastError().empty()
        || !Target->GetParams().IsValid())
    {
        return;
    }

    if (Target == Document.get())
    {
        // 目录历史独立于持久缓存容量：即使用户禁用了按文件缓存，连续查看同批
        // 同扩展名 RAW/YUV 时仍应能沿用上一张主图的属性。
        DirectoryImagePropertyHistory.Remember(
            Target->GetFilePath(),
            Target->GetParams(),
            Target->GetDisplaySettings(),
            Target->IsSelfDescribing());
    }

    if (ImageConfigCache.GetCapacity() == 0)
    {
        return;
    }

    FImageConfiguration configuration;
    configuration.LoadParams = Target->GetParams();
    configuration.DisplaySettings = Target->GetDisplaySettings();
    configuration.ViewSettings = ImageViewer->GetViewSettings(Target);

    ImageConfigCache.Put(
        BuildImageConfigCacheKey(Target->GetFilePath()),
        configuration);
    bSkipActiveImageConfigOnShutdown = false;
}

bool FMainDockSpace::PersistImageConfigCache(const char* Reason)
{
    const std::filesystem::path cachePath =
        FUserSettings::GetImageConfigCachePath();
    const auto saveStart = std::chrono::steady_clock::now();
    const bool bSaved = ImageConfigCache.SaveToFile(cachePath);
    const auto saveMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - saveStart).count();

    if (bSaved)
    {
        LOGI(
            "ImageConfigCache",
            "Persisted %llu image configurations in %lld ms (%s)",
            static_cast<unsigned long long>(ImageConfigCache.GetSize()),
            static_cast<long long>(saveMilliseconds),
            Reason ? Reason : "unspecified");
    }
    else
    {
        LOGE(
            "ImageConfigCache",
            "Failed to persist image configurations: %s (%s)",
            cachePath.u8string().c_str(),
            Reason ? Reason : "unspecified");
    }

    return bSaved;
}

bool FMainDockSpace::TryGetCachedConfiguration(
    const std::string& Path,
    FImageConfiguration& OutConfiguration)
{
    return ImageConfigCache.TryGet(
        BuildImageConfigCacheKey(Path),
        OutConfiguration);
}

void FMainDockSpace::ApplyDocumentConfiguration(
    FImageDocument* Target,
    const FImageConfiguration* Configuration)
{
    if (!Target)
    {
        return;
    }

    Target->SetDisplaySettings(
        Configuration ? Configuration->DisplaySettings : FDisplaySettings{});

    if (ImageViewer)
    {
        ImageViewer->SetViewSettings(
            Target,
            Configuration ? Configuration->ViewSettings : FImageViewSettings{});
    }
}

FImageDocument* FMainDockSpace::GetDocumentForLoadTarget(
    EImageLoadTarget Target) const
{
    return Target == EImageLoadTarget::Main
        ? Document.get()
        : CompareDocument.get();
}

void FMainDockSpace::ClearPendingImageLoadVisual()
{
    if (!PendingImageLoad || !ImageViewer)
    {
        return;
    }

    ImageViewer->SetDocumentLoading(
        GetDocumentForLoadTarget(PendingImageLoad->Target),
        false,
        {});
}

bool FMainDockSpace::QueueImageLoad(
    FImageLoadRequest Request,
    std::vector<FImageConfiguration> AttemptConfigurations,
    EViewTarget SuccessViewTarget,
    bool bAddRecentFile,
    bool bCacheHit)
{
    if (Request.FilePath.empty() || Request.Attempts.empty() ||
        Request.Attempts.size() != AttemptConfigurations.size())
    {
        LOGE(
            "AsyncImageLoader",
            "Invalid request snapshot: attempts=%llu configurations=%llu",
            static_cast<unsigned long long>(Request.Attempts.size()),
            static_cast<unsigned long long>(AttemptConfigurations.size()));
        return false;
    }

    const EViewTarget currentViewTarget = ComparePanel
        ? ComparePanel->GetViewTarget()
        : EViewTarget::Main;
    FImageDocument* oppositeDocument =
        Request.Target == EImageLoadTarget::Main
            ? CompareDocument.get()
            : Document.get();
    const bool bWillCompleteSingleImagePair =
        IsSingleImageViewTarget(currentViewTarget) &&
        IsSingleImageViewTarget(SuccessViewTarget) &&
        oppositeDocument && oppositeDocument->GetImageData() &&
        oppositeDocument->GetImageData()->IsValid();

    if (bWillCompleteSingleImagePair && ImageViewer)
    {
        // 加载期间视图交互会被禁用，因此排队时拍下当前可见图的缩放/平移即可。
        // 把它写进每个候选配置，成功提交新文件时便不会短暂恢复该文件自己的旧位置；
        // CopyPanZoomViewSettings 会保留候选配置中属于新文件自己的旋转与镜像。
        FImageDocument* displayedDocument = GetDisplayedDocument();

        if (!displayedDocument || !displayedDocument->GetImageData() ||
            !displayedDocument->GetImageData()->IsValid())
        {
            // 清空当前槽位后，单图目标仍会指向该空文档；此时唯一有效的另一张图
            // 才是用户实际可用于延续视口的基准。
            displayedDocument = oppositeDocument;
        }

        if (displayedDocument)
        {
            const FImageViewSettings displayedView =
                ImageViewer->GetViewSettings(displayedDocument);

            for (FImageConfiguration& configuration : AttemptConfigurations)
            {
                CopyPanZoomViewSettings(
                    displayedView,
                    configuration.ViewSettings);
            }
        }
    }

    // Submit 会使执行中旧代际失效并覆盖尚未开始的请求。同步清掉旧目标的动画，
    // 保证快速切换主图/对比图时不会留下孤立的 loading 状态。
    ClearPendingImageLoadVisual();
    PendingImageLoad.reset();

    const auto submittedAt = std::chrono::steady_clock::now();
    const uint64_t requestId = AsyncImageLoader.Submit(Request);

    FPendingImageLoad pending;
    pending.RequestId = requestId;
    pending.Target = Request.Target;
    pending.AttemptConfigurations = std::move(AttemptConfigurations);
    pending.SuccessViewTarget = SuccessViewTarget;
    pending.SubmittedAt = submittedAt;
    pending.bAddRecentFile = bAddRecentFile;
    pending.bCacheHit = bCacheHit;
    PendingImageLoad = std::move(pending);

    if (ImageViewer)
    {
        ImageViewer->SetDocumentLoading(
            GetDocumentForLoadTarget(Request.Target),
            true,
            Request.Target == EImageLoadTarget::Main
                ? FLocalization::Text(EUiText::LoadingMain)
                : FLocalization::Text(EUiText::LoadingComparison));
    }

    if (requestId != 0)
    {
        LOGD(
            "AsyncImageLoader",
            "Queued image request %llu (target=%s attempts=%llu)",
            static_cast<unsigned long long>(requestId),
            ImageLoadTargetName(Request.Target),
            static_cast<unsigned long long>(Request.Attempts.size()));
        return true;
    }

    // 线程创建失败是可恢复能力降级。仍复用同一解码/提交路径，功能不丢失；
    // 只有这一罕见路径会重新出现同步等待。
    LOGW(
        "AsyncImageLoader",
        "%s",
        "Executing image request synchronously because the background service is unavailable");
    Request.EnqueuedAt = submittedAt;
    FImageLoadResult result =
        FAsyncImageLoader::DecodeOnCallingThread(std::move(Request));

    if (result.HasDecodedImage())
    {
        FAsyncImageLoader::UploadTextureOnCallingThread(result);
    }

    CompleteImageLoad(std::move(result));
    return true;
}

void FMainDockSpace::PollImageLoad()
{
    // 即使 UI 元数据已因“清除图片”而重置，也要继续 Poll 一次来回收服务里
    // 恰好在取消前发布的旧纹理/fence，避免大图资源一直滞留到下次打开。
    FImageLoadResult result;

    if (AsyncImageLoader.Poll(result))
    {
        CompleteImageLoad(std::move(result));
    }
}

void FMainDockSpace::CompleteImageLoad(FImageLoadResult Result)
{
    if (!PendingImageLoad || Result.RequestId != PendingImageLoad->RequestId)
    {
        LOGW(
            "AsyncImageLoader",
            "Discarding unmatched result %llu",
            static_cast<unsigned long long>(Result.RequestId));
        return;
    }

    FPendingImageLoad pending = std::move(*PendingImageLoad);
    ClearPendingImageLoadVisual();
    PendingImageLoad.reset();

    if (Result.bNeedsMainThreadUpload && Result.HasDecodedImage())
    {
        FAsyncImageLoader::UploadTextureOnCallingThread(Result);
    }

    FImageDocument* target = GetDocumentForLoadTarget(pending.Target);

    if (!target)
    {
        return;
    }

    int32_t configurationIndex = Result.SuccessfulAttemptIndex;

    if (configurationIndex < 0 ||
        configurationIndex >= static_cast<int32_t>(pending.AttemptConfigurations.size()))
    {
        configurationIndex = static_cast<int32_t>(pending.AttemptConfigurations.size()) - 1;
    }

    if (configurationIndex >= 0)
    {
        ApplyDocumentConfiguration(
            target,
            &pending.AttemptConfigurations[static_cast<size_t>(configurationIndex)]);
    }

    const bool bPrepared =
        Result.HasDecodedImage() &&
        Result.TextureData &&
        Result.TextureData->IsValid();
    const int32_t width = Result.HasDecodedImage() ? Result.ImageData->GetWidth() : 0;
    const int32_t height = Result.HasDecodedImage() ? Result.ImageData->GetHeight() : 0;
    const std::string failureText = Result.LastError;
    const uint64_t completedRequestId = Result.RequestId;
    const uint64_t completedFileSize = Result.FileSize;
    const int32_t successfulAttemptIndex = Result.SuccessfulAttemptIndex;
    const int32_t attemptCount = Result.AttemptCount;
    const std::string parameterSource = Result.ParameterSource;
    const bool bSharedUploadUsed = Result.bSharedUploadUsed;
    const FImageLoadTimings timings = Result.Timings;
    if (bPrepared)
    {
        // 差值图绑定旧的两份像素；任一源图换代成功时立即作废，避免显示陈旧结果。
        DiffDocument->Clear();
        ComparePanel->SetStats(FCompareStats());

        if (pending.Target == EImageLoadTarget::Compare)
        {
            FileExplorer->SetCompareFilePath(Result.FilePath);
        }
        else
        {
            FileExplorer->SetMainFilePath(Result.FilePath);
        }

        // 请求入队时已拍下文件选择发生时的查看目标。主图在平铺模式下换代时
        // 不能再无条件退回单图，否则文件浏览器连续切图会破坏当前对比上下文。
        ComparePanel->SetViewTarget(pending.SuccessViewTarget);
    }

    const auto commitStart = std::chrono::steady_clock::now();
    const bool bCommitted = target->CommitLoadResult(std::move(Result));
    const auto commitMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - commitStart).count();
    const auto endToEndMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - pending.SubmittedAt).count();

    LOGI(
        "AsyncImageLoader",
        "Request %llu complete: target=%s success=%d size=%dx%d bytes=%llu "
        "attempt=%d/%d source=%s cache=%d shared=%d queue=%lldms inspect=%lldms "
        "decode=%lldms workerUpload=%lldms mainUpload=%lldms worker=%lldms "
        "commit=%lldms total=%lldms",
        static_cast<unsigned long long>(completedRequestId),
        ImageLoadTargetName(pending.Target),
        bCommitted ? 1 : 0,
        width,
        height,
        static_cast<unsigned long long>(completedFileSize),
        successfulAttemptIndex != kInvalidImageLoadAttemptIndex
            ? successfulAttemptIndex + 1
            : 0,
        attemptCount,
        parameterSource.empty() ? "unknown" : parameterSource.c_str(),
        pending.bCacheHit ? 1 : 0,
        bSharedUploadUsed ? 1 : 0,
        static_cast<long long>(timings.QueueWaitMilliseconds),
        static_cast<long long>(timings.InspectionMilliseconds),
        static_cast<long long>(timings.DecodeMilliseconds),
        static_cast<long long>(timings.WorkerUploadMilliseconds),
        static_cast<long long>(timings.MainUploadMilliseconds),
        static_cast<long long>(timings.WorkerTotalMilliseconds),
        static_cast<long long>(commitMilliseconds),
        static_cast<long long>(endToEndMilliseconds));

    if (bCommitted)
    {
        bSkipActiveImageConfigOnShutdown = false;

        if (pending.bAddRecentFile)
        {
            AddRecentFile(target->GetFilePath());
        }
    }
    else
    {
        std::string message = pending.Target == EImageLoadTarget::Main
            ? FLocalization::Text(EUiText::MainLoadFailedPrefix)
            : FLocalization::Text(EUiText::CompareLoadFailedPrefix);
        message += failureText.empty() ? FLocalization::Text(EUiText::UnknownError)
            : FLocalization::Translate(failureText.c_str());
        FToast::Show(message.c_str(), kImageLoadFailureToastSeconds);
    }

    if (pending.Target == EImageLoadTarget::Main)
    {
        StartDeferredCompareLoad();
    }
}

void FMainDockSpace::StartDeferredCompareLoad()
{
    if (!DeferredCompareLoad || PendingImageLoad)
    {
        return;
    }

    FDeferredCompareLoad deferred = std::move(*DeferredCompareLoad);
    DeferredCompareLoad.reset();
    OpenComparePathInternal(
        deferred.Path,
        deferred.Mode,
        deferred.SuccessViewTarget);
}

void FMainDockSpace::OpenPath(const std::string& Path)
{
    OpenMainPath(Path, EViewTarget::Main);
}

void FMainDockSpace::RequestPropertyPanelSelectionIfNoCompareImage()
{
    if (!CompareDocument || !CompareDocument->GetImageData())
    {
        bPendingSelectPropertyPanel = true;
    }
}

bool FMainDockSpace::OpenFileExplorerSelection(const std::string& Path)
{
    const EViewTarget currentViewTarget = ComparePanel
        ? ComparePanel->GetViewTarget()
        : EViewTarget::Main;

    EImageLoadTarget loadTarget = EImageLoadTarget::Main;
    EViewTarget successViewTarget = EViewTarget::Main;

    switch (currentViewTarget)
    {
    case EViewTarget::Compare:
        loadTarget = EImageLoadTarget::Compare;
        successViewTarget = EViewTarget::Compare;
        break;

    case EViewTarget::Diff:
        // 差值依赖当前两份源图；换主图后旧差值立即失效，因此回退到主图。
        loadTarget = EImageLoadTarget::Main;
        successViewTarget = EViewTarget::Main;
        break;

    case EViewTarget::SideBySide:
        // 属性目标与查看器当前选中的 pane 保持同步，可直接作为平铺替换目标。
        loadTarget = GetTargetDocument() == CompareDocument.get()
            ? EImageLoadTarget::Compare
            : EImageLoadTarget::Main;
        successViewTarget = EViewTarget::SideBySide;
        break;

    case EViewTarget::Main:
    default:
        loadTarget = EImageLoadTarget::Main;
        successViewTarget = EViewTarget::Main;
        break;
    }

    LOGD(
        "FileExplorerSelection",
        "Routing selected file to %s (view=%s successView=%s)",
        ImageLoadTargetName(loadTarget),
        ViewTargetName(currentViewTarget),
        ViewTargetName(successViewTarget));

    if (loadTarget == EImageLoadTarget::Compare)
    {
        // 文件来自当前浏览目录，与右键“添加为对比图”一致地沿用主图参数。
        OpenComparePathInternal(
            Path,
            ECompareOpenMode::InheritMainParameters,
            successViewTarget);
        return false;
    }

    OpenMainPath(Path, successViewTarget);
    return true;
}

void FMainDockSpace::OpenMainPath(
    const std::string& Path,
    EViewTarget SuccessViewTarget)
{
    if (Path.empty())
    {
        return;
    }

    std::error_code ec;

    const std::filesystem::path nativePath = std::filesystem::u8path(Path);

    if (!std::filesystem::exists(nativePath, ec))
    {
        LOGE("OpenPath", "Path does not exist: %s", Path.c_str());

        return;
    }

    if (std::filesystem::is_directory(nativePath, ec))
    {
        FileExplorer->SetCurrentDirectory(Path);

        return;
    }

    if (!std::filesystem::is_regular_file(nativePath, ec))
    {
        LOGE("OpenPath", "Path is neither file nor directory: %s", Path.c_str());

        return;
    }

    RequestPropertyPanelSelectionIfNoCompareImage();

    // 文档只有在请求完全准备好后才换代。面板若正在编辑对比图，只切换编辑目标，
    // 不把任何 UI 状态或半成品写进仍在显示的主图。
    if (PropertyPanel->GetTarget() != EPropertyTarget::Main)
    {
        PropertyPanel->SetTarget(EPropertyTarget::Main);
    }

    // 文档对象会被复用于新文件；覆盖路径前先把旧文件的属性与视图状态归档。
    CacheDocumentConfiguration(Document.get());

    // 打开文件的同时把所在目录设为浏览器当前目录，方便接着看同批文件
    const std::filesystem::path parent = nativePath.parent_path();

    if (!parent.empty())
    {
        FileExplorer->SetCurrentDirectory(parent.u8string());
    }

    const bool bSelfDescribing =
        FImageLoaderFactory::IsSelfDescribingFile(Path);
    uint64_t fileSize = 0;

    if (!bSelfDescribing)
    {
        ec.clear();
        const uintmax_t nativeFileSize =
            std::filesystem::file_size(nativePath, ec);

        if (!ec)
        {
            fileSize = static_cast<uint64_t>(nativeFileSize);
        }
        else
        {
            LOGW(
                "OpenPath",
                "Failed to inspect file size for property reuse: %s (%s)",
                Path.c_str(),
                ec.message().c_str());
        }
    }

    FImageConfiguration cachedConfiguration;
    bool bCacheHit = TryGetCachedConfiguration(Path, cachedConfiguration);

    // 文件内容可能在缓存生成后被替换。RAW 的缓存参数若连当前文件大小都对不上，
    // 不值得先发起一次必败解码，直接降级到目录历史/自动识别。
    if (bCacheHit && !bSelfDescribing
        && !FResolutionGuess::Matches(
            cachedConfiguration.LoadParams.Format,
            cachedConfiguration.LoadParams.Width,
            cachedConfiguration.LoadParams.Height,
            cachedConfiguration.LoadParams.Stride,
            fileSize))
    {
        bCacheHit = false;
    }

    FDirectoryImageProperties previousProperties;
    const EDirectoryImagePropertyLookup lookup =
        DirectoryImagePropertyHistory.TryGetCompatible(
            Path,
            fileSize,
            bSelfDescribing,
            previousProperties);

    FImageLoadRequest request;
    request.Target = EImageLoadTarget::Main;
    request.FilePath = Path;
    std::vector<FImageConfiguration> configurations;

    if (bCacheHit)
    {
        // 当前文件自己的缓存优先级最高，即使它与目录上一张图的属性不同也不覆盖。
        request.Attempts.push_back(MakeExplicitLoadAttempt(
            cachedConfiguration.LoadParams,
            "file cache"));
        configurations.push_back(cachedConfiguration);
    }

    const bool bAddInheritedAttempt =
        lookup == EDirectoryImagePropertyLookup::Compatible
        && (!bCacheHit
            || !AreLoadParamsEqual(
                cachedConfiguration.LoadParams,
                previousProperties.LoadParams));

    if (bAddInheritedAttempt)
    {
        request.Attempts.push_back(MakeExplicitLoadAttempt(
            previousProperties.LoadParams,
            "previous image with matching extension in directory"));
        configurations.push_back(MakeImageConfiguration(
            previousProperties.LoadParams,
            previousProperties.DisplaySettings));
    }

    // 文件缓存或目录继承都只是候选。任何一层实际解码失败，都在同一个后台
    // 请求中继续自动识别，避免旧缓存把文件永久锁在失败状态。
    const char* automaticSource = nullptr;

    if (bCacheHit)
    {
        automaticSource = bAddInheritedAttempt
            ? "automatic after cached and inherited properties failed"
            : "automatic after cached properties failed";
    }
    else if (bAddInheritedAttempt)
    {
        automaticSource = "automatic after inherited properties failed";
    }
    else
    {
        switch (lookup)
        {
        case EDirectoryImagePropertyLookup::NoHistoryForExtension:
            automaticSource = "automatic because directory has no history for this extension";
            break;
        case EDirectoryImagePropertyLookup::SelfDescribingTarget:
            automaticSource = "automatic for self-describing file";
            break;
        case EDirectoryImagePropertyLookup::FileSizeMismatch:
            automaticSource = "automatic after inherited size mismatch";
            break;
        case EDirectoryImagePropertyLookup::Compatible:
            automaticSource = "automatic after inherited properties failed";
            break;
        }
    }

    request.Attempts.push_back(MakeAutomaticLoadAttempt(automaticSource));
    configurations.push_back(MakeImageConfiguration(
        FImageLoadParams::Default()));

    QueueImageLoad(
        std::move(request),
        std::move(configurations),
        SuccessViewTarget,
        true,
        bCacheHit);
}

void FMainDockSpace::OpenPathWithParams(const std::string& Path, const FImageLoadParams& Params)
{
    std::error_code ec;

    const std::filesystem::path nativePath = std::filesystem::u8path(Path);

    if (Path.empty() || !std::filesystem::exists(nativePath, ec))
    {
        LOGE("OpenPathWithParams", "Path does not exist: %s", Path.c_str());

        return;
    }

    if (std::filesystem::is_directory(nativePath, ec))
    {
        FileExplorer->SetCurrentDirectory(Path);

        return;
    }

    if (!std::filesystem::is_regular_file(nativePath, ec))
    {
        LOGE("OpenPathWithParams", "Path is not a regular file: %s", Path.c_str());
        return;
    }

    RequestPropertyPanelSelectionIfNoCompareImage();

    CacheDocumentConfiguration(Document.get());

    if (PropertyPanel->GetTarget() != EPropertyTarget::Main)
    {
        PropertyPanel->SetTarget(EPropertyTarget::Main);
    }

    // 打开文件的同时把所在目录设为浏览器当前目录，方便接着看同批文件
    const std::filesystem::path parent = nativePath.parent_path();

    if (!parent.empty())
    {
        FileExplorer->SetCurrentDirectory(parent.u8string());
    }

    FImageLoadRequest request;
    request.Target = EImageLoadTarget::Main;
    request.FilePath = Path;
    request.Attempts.push_back(MakeExplicitLoadAttempt(
        Params,
        "explicit command-line parameters"));

    std::vector<FImageConfiguration> configurations;
    configurations.push_back(MakeImageConfiguration(Params));
    QueueImageLoad(
        std::move(request),
        std::move(configurations),
        EViewTarget::Main,
        true,
        false);
}

bool FMainDockSpace::OpenComparePath(const std::string& Path)
{
    // 文件浏览器右键与 --compare 面向同批 dump，保持主图参数继承语义，
    // 显示方式则恢复用户最近主动选择的非差值模式。
    return OpenComparePathInternal(
        Path,
        ECompareOpenMode::InheritMainParameters,
        ComparePanel->GetRememberedCompareViewTarget());
}

bool FMainDockSpace::OpenPickedComparePath(const std::string& Path)
{
    const ECompareOpenMode mode = IsFileInCurrentBrowserDirectory(Path)
        ? ECompareOpenMode::InheritMainParameters
        : ECompareOpenMode::PreferSelectedFileMetadata;

    return OpenComparePathInternal(
        Path,
        mode,
        ComparePanel->GetRememberedCompareViewTarget());
}

bool FMainDockSpace::IsFileInCurrentBrowserDirectory(const std::string& Path) const
{
    if (!FileExplorer || Path.empty() || FileExplorer->GetCurrentDirectory().empty())
    {
        return false;
    }

    try
    {
        const std::filesystem::path selectedDirectory =
            std::filesystem::u8path(Path).parent_path();
        const std::filesystem::path browserDirectory =
            std::filesystem::u8path(FileExplorer->GetCurrentDirectory());

        if (selectedDirectory.empty() || browserDirectory.empty())
        {
            return false;
        }

        std::error_code ec;
        const bool bEquivalent =
            std::filesystem::equivalent(selectedDirectory, browserDirectory, ec);

        if (!ec)
        {
            return bEquivalent;
        }

        // equivalent 需要两边都能被文件系统解析。失败时用与配置缓存相同的
        // 绝对规范化规则兜底，避免仅因分隔符或相对路径写法不同而误判为跨目录。
        return BuildImageConfigCacheKey(selectedDirectory.u8string()) ==
            BuildImageConfigCacheKey(browserDirectory.u8string());
    }
    catch (const std::exception& exception)
    {
        LOGW(
            "OpenPickedComparePath",
            "Failed to compare selected and browser directories: %s",
            exception.what());
        return false;
    }
}

bool FMainDockSpace::OpenComparePathInternal(
    const std::string& Path,
    ECompareOpenMode Mode,
    EViewTarget SuccessViewTarget)
{
    std::error_code ec;

    if (Path.empty() ||
        !std::filesystem::is_regular_file(std::filesystem::u8path(Path), ec))
    {
        LOGE("OpenComparePath", "Not a regular file: %s", Path.c_str());

        return false;
    }

    if (PendingImageLoad &&
        PendingImageLoad->Target == EImageLoadTarget::Main)
    {
        // 服务只有一个执行槽；对比图不能把尚未完成的主图取消。只保留最新一次
        // 对比意图，主图提交后再用其最终参数构造请求。
        DeferredCompareLoad = FDeferredCompareLoad{
            Path,
            Mode,
            SuccessViewTarget,
        };
        LOGD(
            "AsyncImageLoader",
            "%s",
            "Deferred compare request until the main image request completes");
        return true;
    }

    CacheDocumentConfiguration(CompareDocument.get());

    FImageConfiguration cachedConfiguration;
    const bool bCacheHit =
        TryGetCachedConfiguration(Path, cachedConfiguration);

    FImageLoadRequest request;
    request.Target = EImageLoadTarget::Compare;
    request.FilePath = Path;
    std::vector<FImageConfiguration> configurations;

    if (bCacheHit)
    {
        request.Attempts.push_back(MakeExplicitLoadAttempt(
            cachedConfiguration.LoadParams,
            "compare file cache"));
        configurations.push_back(cachedConfiguration);

        if (Mode == ECompareOpenMode::PreferSelectedFileMetadata)
        {
            // 旧版本可能已经把一次失败的跨目录加载参数写进缓存。文件选择器显式
            // 选中外部文件时，缓存打不开就按文件自身信息和主图参数依次兜底。
            request.Attempts.push_back(MakeAutomaticLoadAttempt(
                "selected-file metadata after cache failure"));
            configurations.push_back(cachedConfiguration);

            FImageConfiguration mainFallback = cachedConfiguration;
            mainFallback.LoadParams = Document->GetParams();
            request.Attempts.push_back(MakeExplicitLoadAttempt(
                mainFallback.LoadParams,
                "main-image fallback after cache failure"));
            configurations.push_back(mainFallback);
        }
    }
    else if (Mode == ECompareOpenMode::PreferSelectedFileMetadata)
    {
        int32_t parsedWidth = 0;
        int32_t parsedHeight = 0;
        const EImageFormat parsedFormat =
            ParseImageInfoFromFilename(Path, parsedWidth, parsedHeight);
        const bool bCanInferSelectedFile =
            FImageLoaderFactory::IsSelfDescribingFile(Path) ||
            parsedFormat != EImageFormat::Unknown;

        if (bCanInferSelectedFile)
        {
            // 跨目录文件往往来自另一批输出，格式与尺寸不应继续套用主图。
            request.Attempts.push_back(MakeAutomaticLoadAttempt(
                "selected-file metadata"));
            configurations.push_back(MakeImageConfiguration(
                FImageLoadParams::Default()));
        }

        // 无后缀且文件名也不带格式/尺寸时无法自行推断，仍允许用主图参数；
        // 若上面的自动推断失败，这也是同一个后台请求里的兼容兜底。
        request.Attempts.push_back(MakeExplicitLoadAttempt(
            Document->GetParams(),
            "main-image fallback"));
        configurations.push_back(MakeImageConfiguration(
            Document->GetParams()));
    }
    else
    {
        request.Attempts.push_back(MakeExplicitLoadAttempt(
            Document->GetParams(),
            "main image"));
        configurations.push_back(MakeImageConfiguration(
            Document->GetParams()));
    }

    return QueueImageLoad(
        std::move(request),
        std::move(configurations),
        SuccessViewTarget,
        false,
        bCacheHit);
}

void FMainDockSpace::AddRecentFile(const std::string& Path)
{
    // 已存在就提到最前
    const auto it = std::find(RecentFiles.begin(), RecentFiles.end(), Path);

    if (it != RecentFiles.end())
    {
        RecentFiles.erase(it);
    }

    RecentFiles.push_front(Path);

    while (RecentFiles.size() > FUserSettings::kMaximumRecentFileCount)
    {
        RecentFiles.pop_back();
    }

    PersistRecentFiles();
}

void FMainDockSpace::PersistRecentFiles()
{
    FUserSettings::SetRecentFiles(std::vector<std::string>(
        RecentFiles.begin(),
        RecentFiles.end()));
}

void FMainDockSpace::SyncPanelFromDocument()
{
    FImageDocument* target = GetTargetDocument();

    if (!PropertyPanel || !target)
    {
        return;
    }

    const FImageLoadParams& params = target->GetParams();

    PropertyPanel->SetLoadParams(params);
    PropertyPanel->SetDisplaySettings(target->GetDisplaySettings());
    PropertyPanel->SetImageData(target->GetImageData());

    if (ImageViewer)
    {
        ImageViewer->SetSelectedDocument(target);
    }

    // 自带文件头的格式参数不可改；失败原因与文件/图像大小则用来解释"为什么改了没生效"
    PropertyPanel->SetParamsEditable(!target->IsSelfDescribing());
    PropertyPanel->SetLoadError(target->GetLastError());
    PropertyPanel->SetFileInfo(
        target->GetFileSize(),
        target->GetFilePath().empty() ? 0 : target->GetImageSize());
    PropertyPanel->SetSourceFile(target->GetFilePath());

    RebuildHistogram();
}

void FMainDockSpace::ApplyDisplaySettings(const FDisplaySettings& Display)
{
    if (!Document)
    {
        return;
    }

    if (PendingImageLoad &&
        PendingImageLoad->Target == EImageLoadTarget::Main)
    {
        // 命令行在排队打开主图后立刻调用这里。更新请求快照，而不是把新色彩解释
        // 套到加载期间仍在屏幕上的旧图；提交时会与新像素一起生效。
        for (FImageConfiguration& configuration :
             PendingImageLoad->AttemptConfigurations)
        {
            configuration.DisplaySettings = Display;
        }
        return;
    }

    Document->SetDisplaySettings(Display);

    // 显示设置不影响解码，不必重新加载；但直方图是按当前解读统计的，必须重算
    SyncPanelFromDocument();
}

void FMainDockSpace::SubmitPanelToDocument()
{
    FImageDocument* target = GetTargetDocument();

    if (!PropertyPanel || !target)
    {
        return;
    }

    if (target->GetFilePath().empty())
    {
        return;
    }

    const FImageLoadParams loadParams = PropertyPanel->GetLoadParams();
    FImageConfiguration configuration = MakeImageConfiguration(
        loadParams,
        target->GetDisplaySettings(),
        ImageViewer->GetViewSettings(target));

    FImageLoadRequest request;
    request.Target = target == CompareDocument.get()
        ? EImageLoadTarget::Compare
        : EImageLoadTarget::Main;
    request.FilePath = target->GetFilePath();
    request.Attempts.push_back(MakeExplicitLoadAttempt(
        loadParams,
        "property panel"));

    EViewTarget successViewTarget = ComparePanel->GetViewTarget();

    if (request.Target == EImageLoadTarget::Main &&
        successViewTarget == EViewTarget::Diff)
    {
        successViewTarget = EViewTarget::Main;
    }
    else if (request.Target == EImageLoadTarget::Compare &&
             (successViewTarget == EViewTarget::Main ||
              successViewTarget == EViewTarget::Diff))
    {
        successViewTarget = EViewTarget::Compare;
    }

    std::vector<FImageConfiguration> configurations;
    configurations.push_back(std::move(configuration));
    QueueImageLoad(
        std::move(request),
        std::move(configurations),
        successViewTarget,
        false,
        false);
}

FImageDocument* FMainDockSpace::GetDisplayedDocument() const
{
    switch (ComparePanel->GetViewTarget())
    {
    case EViewTarget::Compare: return CompareDocument.get();
    case EViewTarget::Diff:    return DiffDocument.get();
    // 平铺时以主图为准
    case EViewTarget::SideBySide:
    case EViewTarget::Main:
    default:                   return Document.get();
    }
}

void FMainDockSpace::RebuildHistogram()
{
    if (!HistogramPanel || !ImageViewer)
    {
        return;
    }

    // 单图模式统计当前显示图；平铺模式没有唯一的“显示图”，因此跟随编辑对象，
    // 让点击任一侧后属性与直方图始终描述同一份数据。
    FImageDocument* active =
        ComparePanel && ComparePanel->GetViewTarget() == EViewTarget::SideBySide
        ? GetTargetDocument()
        : GetDisplayedDocument();

    // 色彩设置按图存放，统计用的是被统计那幅图自己的设置
    const FDisplaySettings& display = active ? active->GetDisplaySettings() : Document->GetDisplaySettings();

    const auto rebuildStart = std::chrono::steady_clock::now();
    HistogramPanel->Rebuild(
        active ? active->GetImageData() : nullptr,
        display,
        active ? active->GetParams().BayerPattern : Document->GetParams().BayerPattern);
    const auto rebuildMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - rebuildStart).count();

    // 只记录超过一帧预算的慢刷新，避免属性拖动等高频路径持续刷日志。
    static std::chrono::steady_clock::time_point lastSlowRebuildLog;
    const auto now = std::chrono::steady_clock::now();
    const bool bLogIntervalElapsed =
        lastSlowRebuildLog.time_since_epoch().count() == 0 ||
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastSlowRebuildLog).count() >=
            kSlowHistogramLogIntervalMilliseconds;

    if (rebuildMilliseconds >= kSlowHistogramLogMilliseconds &&
        bLogIntervalElapsed)
    {
        lastSlowRebuildLog = now;
        const FImageData* data = active ? active->GetImageData() : nullptr;
        LOGW(
            "Histogram",
            "Slow histogram rebuild: %lld ms (%dx%d)",
            static_cast<long long>(rebuildMilliseconds),
            data ? data->GetWidth() : 0,
            data ? data->GetHeight() : 0);
    }
}

void FMainDockSpace::ComputeDiff(float Gain)
{
    const FImageData* a = Document ? Document->GetImageData() : nullptr;
    const FImageData* b = CompareDocument ? CompareDocument->GetImageData() : nullptr;

    if (!a || !b || AsyncJob.IsRunning() || PendingImageLoad)
    {
        return;
    }

    // 差值在 CPU 侧算，两边先各自转 RGB8 再相减。
    // 这里统一用主图的色彩设置：差值比的是"同一套解读下两张图差多少"。
    const FDisplaySettings display = Document->GetDisplaySettings();
    const EBayerPattern bayerPattern = Document->GetParams().BayerPattern;

    // 结果由工作线程写、主线程读，用 shared_ptr 把两边的生命周期兜住
    auto stats = std::make_shared<FCompareStats>();
    auto diff = std::make_shared<std::unique_ptr<FImageData>>();

    AsyncJob.Start(
        FLocalization::Text(EUiText::ComputingDifference),
        0,
        [a, b, Gain, display, bayerPattern, stats, diff]() {
            *diff = FImageCompare::ComputeDiff(
                *a, *b, Gain,
                display,
                bayerPattern,
                *stats);
        },
        [this, stats, diff]() {
            // ComputeDiff 会把失败原因写进 Error；两者都空说明是工作线程抛了异常
            if (!stats->bValid && stats->Error.empty())
            {
                stats->Error = FLocalization::Text(EUiText::DifferenceFailed);
            }

            ComparePanel->SetStats(*stats);

            if (*diff)
            {
                // 建纹理要 GL 上下文，只能在主线程做，所以这一步留到收尾
                DiffDocument->SetImageData(std::move(*diff), FLocalization::Text(EUiText::GeneratedDifferenceName));
                ComparePanel->SetViewTarget(EViewTarget::Diff);
                RebuildHistogram();
            }
        });
}

void FMainDockSpace::RenderBusyOverlay()
{
    std::string detail;

    // 只有批量导出才报得出进度；单个文件就是一直转圈
    if (AsyncJob.GetTotal() > 1)
    {
        detail = std::to_string(AsyncJob.GetCompleted()) + " / " + std::to_string(AsyncJob.GetTotal());
    }

    FUiIcons::DrawBusyOverlay(AsyncJob.GetLabel().c_str(), detail.empty() ? nullptr : detail.c_str());
}

std::string FMainDockSpace::GetDefaultExportDirectory(const std::string& SourcePath) const
{
    const std::filesystem::path parent = std::filesystem::u8path(SourcePath).parent_path();

    std::error_code ec;

    if (!parent.empty() && std::filesystem::is_directory(parent, ec))
    {
        return parent.u8string();
    }

    return FileExplorer ? FileExplorer->GetCurrentDirectory() : std::string();
}

void FMainDockSpace::RequestExportCurrentImage()
{
    if (PendingImageLoad)
    {
        FToast::Show(FLocalization::Text(EUiText::WaitBeforeExport));
        return;
    }

    FImageDocument* active = GetDisplayedDocument();

    if (!active || !active->GetImageData())
    {
        return;
    }

    std::string sourcePath = active->GetFilePath();

    std::error_code ec;

    if (!std::filesystem::is_regular_file(std::filesystem::u8path(sourcePath), ec))
    {
        // 差值图这类计算产物没有源文件，FilePath 里放的是 "<差值图>" 这种标签，
        // 直接拿去拼路径会生成非法文件名，改由主图的目录与主名派生
        const std::filesystem::path mainPath = std::filesystem::u8path(Document->GetFilePath());
        const std::string stem = mainPath.stem().u8string();

        sourcePath = (mainPath.parent_path() / std::filesystem::u8path((stem.empty() ? "image" : stem) + "_export")).u8string();
    }

    FExportRequest request;
    request.SourcePaths.push_back(sourcePath);
    request.bUseLoadedImage = true;

    const FImageData* data = active->GetImageData();

    ExportPanel->Open(
        request,
        GetDefaultExportDirectory(sourcePath),
        data->GetFormat(),
        data->GetWidth(),
        data->GetHeight(),
        active->GetDisplaySettings());
}

void FMainDockSpace::RequestExportFiles(const std::vector<std::string>& Paths)
{
    if (Paths.empty() || PendingImageLoad)
    {
        if (PendingImageLoad)
        {
            FToast::Show(FLocalization::Text(EUiText::WaitBeforeExport));
        }
        return;
    }

    FExportRequest request;
    request.SourcePaths = Paths;
    request.bUseLoadedImage = false;

    // 面板上的尺寸预览按第一个文件估算：已经打开的那个直接读，
    // 其余靠文件名解析，都拿不到就退回主图的加载参数
    const FImageLoadParams& baseParams = Document->GetParams();

    EImageFormat previewFormat = baseParams.Format;
    int32_t previewWidth = baseParams.Width;
    int32_t previewHeight = baseParams.Height;

    const std::string& first = Paths.front();

    if (Document->GetImageData() && Document->GetFilePath() == first)
    {
        const FImageData* data = Document->GetImageData();

        previewFormat = data->GetFormat();
        previewWidth = data->GetWidth();
        previewHeight = data->GetHeight();
    }
    else
    {
        int32_t parsedWidth = 0;
        int32_t parsedHeight = 0;
        const EImageFormat parsedFormat = ParseImageInfoFromFilename(first, parsedWidth, parsedHeight);

        if (parsedWidth > 0 && parsedHeight > 0)
        {
            previewWidth = parsedWidth;
            previewHeight = parsedHeight;
        }

        if (parsedFormat != EImageFormat::Unknown)
        {
            previewFormat = parsedFormat;
        }

        std::error_code fileSizeError;
        const uint64_t fileSize =
            static_cast<uint64_t>(std::filesystem::file_size(
                std::filesystem::u8path(first),
                fileSizeError));
        int32_t visibleWidth = 0;
        int32_t detectedStride = 0;

        if (!fileSizeError &&
            TryResolveAndroidSemiplanarStride(
                first,
                previewFormat,
                previewWidth,
                previewHeight,
                fileSize,
                visibleWidth,
                detectedStride))
        {
            previewWidth = visibleWidth;
        }
    }

    ExportPanel->Open(
        request,
        GetDefaultExportDirectory(first),
        previewFormat,
        previewWidth,
        previewHeight,
        Document->GetDisplaySettings());
}

std::unique_ptr<FImageData> FMainDockSpace::LoadForExport(const std::string& Path, const FImageLoadParams& BaseParams)
{
    // 自带文件头的格式不套用任何参数（工厂内部也会兜这一层，这里显式跳过还能省掉文件名解析）
    if (FImageLoaderFactory::IsSelfDescribingFile(Path))
    {
        return FImageLoaderFactory::LoadImage(Path, nullptr);
    }

    FImageLoadParams params = BaseParams;

    int32_t parsedWidth = 0;
    int32_t parsedHeight = 0;
    const EImageFormat parsedFormat = ParseImageInfoFromFilename(Path, parsedWidth, parsedHeight);

    if (parsedWidth > 0 && parsedHeight > 0)
    {
        params.Width = parsedWidth;
        params.Height = parsedHeight;
        params.Stride = 0;
    }

    if (parsedFormat != EImageFormat::Unknown)
    {
        params.SetDetectedFormat(parsedFormat);
    }

    std::error_code fileSizeError;
    const uint64_t fileSize =
        static_cast<uint64_t>(std::filesystem::file_size(
            std::filesystem::u8path(Path),
            fileSizeError));
    int32_t visibleWidth = 0;
    int32_t detectedStride = 0;

    if (!fileSizeError &&
        TryResolveAndroidSemiplanarStride(
            Path,
            params.Format,
            params.Width,
            params.Height,
            fileSize,
            visibleWidth,
            detectedStride))
    {
        params.Width = visibleWidth;
        params.Stride = detectedStride;
    }

    return FImageLoaderFactory::LoadImage(Path, params.IsValid() ? &params : nullptr);
}

void FMainDockSpace::PerformExport(const FExportSettings& Settings, const FExportRequest& Request)
{
    if (Request.SourcePaths.empty() || AsyncJob.IsRunning() || PendingImageLoad)
    {
        return;
    }

    auto job = std::make_shared<FExportJob>();

    job->Settings = Settings;
    job->SourcePaths = Request.SourcePaths;
    job->bUseLoadedImage = Request.bUseLoadedImage;
    job->BaseParams = Document->GetParams();
    job->BayerPattern = job->BaseParams.BayerPattern;

    if (Request.bUseLoadedImage)
    {
        // 用户看到的那一帧（可能是多帧文件的第 N 帧，也可能刚改过 stride），
        // 重新读盘会得到另一幅图
        FImageDocument* active = GetDisplayedDocument();

        if (active)
        {
            job->LoadedImage = active->GetImageData();
            job->BayerPattern = active->GetParams().BayerPattern;
        }
    }

    // 工作线程抛异常时也得有个能显示给用户的结果，否则点完"开始导出"什么都不会发生
    auto result = std::make_shared<FExportResult>();
    result->Text = FLocalization::Text(EUiText::ExportInternalError);
    result->bHasError = true;

    AsyncJob.Start(
        FLocalization::Text(EUiText::Exporting),
        static_cast<int32_t>(job->SourcePaths.size()),
        [this, job, result]() {
            *result = RunExportJob(*job, [this](int32_t Done) { AsyncJob.ReportProgress(Done); });
        },
        [this, result]() {
            ExportPanel->SetResult(*result);
        });
}

FExportResult FMainDockSpace::RunExportJob(const FExportJob& Job, const std::function<void(int32_t)>& OnProgress)
{
    FExportResult result;

    int32_t successCount = 0;
    int32_t failureCount = 0;
    std::string firstError;

    for (size_t i = 0; i < Job.SourcePaths.size(); ++i)
    {
        const std::string& sourcePath = Job.SourcePaths[i];
        const std::string fileName = std::filesystem::u8path(sourcePath).filename().u8string();

        const FImageData* data = nullptr;
        std::unique_ptr<FImageData> loaded;

        if (Job.bUseLoadedImage)
        {
            data = Job.LoadedImage;
        }
        else
        {
            loaded = LoadForExport(sourcePath, Job.BaseParams);
            data = loaded.get();
        }

        if (!data || !data->IsValid())
        {
            ++failureCount;

            if (firstError.empty())
            {
                firstError = fileName + FLocalization::Text(EUiText::FileLoadFailedSuffix);
            }

            LOGE("RunExportJob", "Failed to load for export: %s", sourcePath.c_str());
        }
        else
        {
            const std::string outputPath = FImageExporter::MakeOutputPath(Job.Settings, sourcePath);

            std::string error;

            if (FImageExporter::Export(*data, Job.BayerPattern, Job.Settings, outputPath, error))
            {
                ++successCount;
                result.OutputPaths.push_back(outputPath);

                LOGD("RunExportJob", "Exported %s -> %s", sourcePath.c_str(), outputPath.c_str());
            }
            else
            {
                ++failureCount;

                if (firstError.empty())
                {
                    firstError = fileName + FLocalization::Text(EUiText::ErrorSeparator)
                        + FLocalization::Translate(error.c_str());
                }

                LOGE("RunExportJob", "Export failed: %s, %s", sourcePath.c_str(), error.c_str());
            }
        }

        if (OnProgress)
        {
            OnProgress(static_cast<int32_t>(i + 1));
        }
    }

    if (failureCount == 0 && successCount == 1)
    {
        // 文件名不写进这句话：它就在下面一行，是个能点开资源管理器的链接
        result.Text = FLocalization::Text(EUiText::ExportComplete);
    }
    else if (failureCount == 0)
    {
        result.Text = FLocalization::Text(EUiText::ExportedPrefix) + std::to_string(successCount) + FLocalization::Text(EUiText::FilesSuffix);
    }
    else if (successCount > 0)
    {
        result.Text = FLocalization::Text(EUiText::SucceededPrefix) + std::to_string(successCount) + FLocalization::Text(EUiText::FailedCountPrefix)
                    + std::to_string(failureCount) + FLocalization::Text(EUiText::SentenceEnd) + firstError;
        result.bHasError = true;
    }
    else
    {
        result.Text = FLocalization::Text(EUiText::ExportFailedPrefix) + firstError;
        result.bHasError = true;
    }

    return result;
}

void FMainDockSpace::UpdateViewTarget()
{
    const bool bHasMain =
        Document && Document->GetImageData() && Document->GetImageData()->IsValid();
    const bool bHasCompare =
        CompareDocument && CompareDocument->GetImageData() &&
        CompareDocument->GetImageData()->IsValid();
    PropertyPanel->SetTargetSelectionAvailable(bHasMain && bHasCompare);

    if (!bHasCompare && PropertyPanel->GetTarget() == EPropertyTarget::Compare)
    {
        PropertyPanel->SetTarget(EPropertyTarget::Main);
        SyncPanelFromDocument();
    }

    FImageDocument* primary = Document.get();
    FImageDocument* secondary = nullptr;
    FImageDocument* singleImageComparePeer = nullptr;
    const EViewTarget viewTarget = ComparePanel->GetViewTarget();

    switch (viewTarget)
    {
    case EViewTarget::Compare:
        primary = CompareDocument.get();

        if (bHasMain && bHasCompare)
        {
            singleImageComparePeer = Document.get();
        }
        break;

    case EViewTarget::Diff:
        primary = DiffDocument.get();
        break;

    case EViewTarget::SideBySide:
        // 平铺：两侧各自保存完整视图状态，查看器按工具栏开关选择是否联动缩放/平移。
        primary = Document.get();
        secondary = CompareDocument.get();
        break;

    case EViewTarget::Main:
    default:
        primary = Document.get();

        if (bHasMain && bHasCompare)
        {
            singleImageComparePeer = CompareDocument.get();
        }
        break;
    }

    ImageViewer->SetDocument(primary);
    ImageViewer->SetSecondaryDocument(secondary);
    ImageViewer->SetSingleImageComparePeer(singleImageComparePeer);

    const bool bViewTargetChanged =
        !AppliedViewTarget || *AppliedViewTarget != viewTarget;

    if (bViewTargetChanged)
    {
        AppliedViewTarget = viewTarget;

        if (IsSingleImageViewTarget(viewTarget))
        {
            const EPropertyTarget propertyTarget =
                viewTarget == EViewTarget::Compare
                    ? EPropertyTarget::Compare
                    : EPropertyTarget::Main;
            PropertyPanel->SetTarget(propertyTarget);
            SyncPanelFromDocument();
        }
        else
        {
            RebuildHistogram();
        }
    }

    const bool bSingleImageCompareModeActive =
        IsSingleImageViewTarget(viewTarget) && bHasMain && bHasCompare;
    const auto now = std::chrono::steady_clock::now();

    if (bSingleImageCompareModeActive && !bWasSingleImageCompareModeActive)
    {
        const bool bShouldShowHint =
            FUserSettings::ConsumeSingleImageCompareHintDisplay();

        if (bShouldShowHint)
        {
            SingleImageCompareHintExpiresAt =
                now + kSingleImageCompareHintDuration;
            LOGI(
                "SingleImageCompareHint",
                "Showing single-image comparison hint for %lld seconds: use=%u/%u",
                static_cast<long long>(
                    kSingleImageCompareHintDuration.count()),
                static_cast<unsigned>(
                    FUserSettings::GetSingleImageCompareHintDisplayCount()),
                static_cast<unsigned>(
                    FUserSettings::kSingleImageCompareHintDisplayLimit));
        }
        else
        {
            SingleImageCompareHintExpiresAt.reset();
        }
    }
    else if (!bSingleImageCompareModeActive)
    {
        SingleImageCompareHintExpiresAt.reset();
    }
    else if (SingleImageCompareHintExpiresAt &&
             now >= *SingleImageCompareHintExpiresAt)
    {
        SingleImageCompareHintExpiresAt.reset();
    }

    const bool bSingleImageCompareHintVisible =
        bSingleImageCompareModeActive &&
        SingleImageCompareHintExpiresAt.has_value();
    bWasSingleImageCompareModeActive = bSingleImageCompareModeActive;
    ImageViewer->SetSingleImageSwitchState(
        bSingleImageCompareModeActive,
        bSingleImageCompareHintVisible,
        viewTarget == EViewTarget::Compare);
}

void FMainDockSpace::SwitchSingleImageView()
{
    if (!ComparePanel || !Document || !Document->GetImageData() ||
        !CompareDocument || !CompareDocument->GetImageData())
    {
        return;
    }

    const EViewTarget currentTarget = ComparePanel->GetViewTarget();

    if (!IsSingleImageViewTarget(currentTarget))
    {
        return;
    }

    const EViewTarget nextTarget =
        currentTarget == EViewTarget::Main
            ? EViewTarget::Compare
            : EViewTarget::Main;
    ComparePanel->SetViewTarget(nextTarget);

    LOGD(
        "SingleImageCompare",
        "Viewport left release switched single image: target=%s",
        ViewTargetName(nextTarget));

    UpdateViewTarget();
}

void FMainDockSpace::HandleShortcuts()
{
    ImGuiIO& io = ImGui::GetIO();

    // 正在输入文字时不响应快捷键
    if (io.WantTextInput)
    {
        return;
    }

    // Ctrl+O 打开文件
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false))
    {
        std::string path;

        if (FFileDialog::OpenFile(FLocalization::Text(EUiText::OpenImage), FImageLoaderFactory::GetAllSupportedExtensions(), path))
        {
            OpenPath(path);
        }
    }

    // 加载期间只保留“继续选文件”快捷键；其它操作会读写当前文档或视图。
    if (PendingImageLoad)
    {
        return;
    }

    // Ctrl+E 导出当前图像
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E, false))
    {
        RequestExportCurrentImage();
    }

    // Ctrl+0 自适应 / Ctrl+1 一比一
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0, false))
    {
        ImageViewer->SetDisplayMode(EDisplayMode::AutoFit);
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_1, false))
    {
        ImageViewer->SetDisplayMode(EDisplayMode::OneToOne);
    }
}

void FMainDockSpace::RenderRecentFilesMenu()
{
    if (!ImGui::BeginMenu(FLocalization::Text(EUiText::RecentFiles)))
    {
        return;
    }

    if (RecentFiles.empty())
    {
        ImGui::MenuItem(FLocalization::Text(EUiText::Empty), nullptr, false, false);
    }
    else
    {
        // 拷贝一份再遍历：点击会修改 RecentFiles
        const std::deque<std::string> snapshot = RecentFiles;

        for (const std::string& path : snapshot)
        {
            const std::string label =
                std::filesystem::u8path(path).filename().u8string();

            if (ImGui::MenuItem(label.c_str()))
            {
                OpenPath(path);
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", path.c_str());
            }
        }

        ImGui::Separator();

        if (ImGui::MenuItem(FLocalization::Text(EUiText::ClearRecentFiles)))
        {
            RecentFiles.clear();
            PersistRecentFiles();
        }
    }

    ImGui::EndMenu();
}

void FMainDockSpace::ReloadThemePaletteDraft()
{
    FUiTheme::ReloadPalette();
    SettingsThemePaletteDraft = FUiTheme::GetPalette();
    SelectThemeColorRole(SettingsThemeSelectedRole);
}

void FMainDockSpace::SelectThemeColorRole(
    FUserSettings::EThemeColorRole Role)
{
    SettingsThemeSelectedRole = Role;
    FThemeColorPicker::Synchronize(
        SettingsThemePaletteDraft.Get(Role),
        SettingsThemePickerState);
}

void FMainDockSpace::RenderThemeSettings()
{
    ImGui::TextUnformatted(FLocalization::Text(EUiText::ThemeColors));

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float editorHeight = std::min(
        FUiScale::Apply(kSettingsThemeEditorPreferredHeight),
        std::max(
            FUiScale::Apply(kSettingsThemeEditorMinimumHeight),
            available.y - FUiScale::Apply(kSettingsThemeEditorReservedHeight)));
    float maximumLabelWidth = 0.0f;
    for (const auto& definition : kThemeColorUiDefinitions)
    {
        maximumLabelWidth = std::max(maximumLabelWidth,
            ImGui::CalcTextSize(FLocalization::Text(definition.Label)).x);
    }
    const auto& style = ImGui::GetStyle();
    // Include child padding and every table cell so neither language loses the end of a label.
    const float requiredListWidth = maximumLabelWidth
        + ImGui::CalcTextSize(FLocalization::Text(EUiText::Reset)).x
        + style.FramePadding.x * 2.0f
        + FUiScale::Apply(kSettingsThemeColorSwatchWidth)
        + style.WindowPadding.x * 2.0f
        + style.CellPadding.x * 2.0f * kSettingsThemeColorTableColumnCount;
    const float listWidth = std::min(
        std::max(FUiScale::Apply(kSettingsThemeColorListWidth), requiredListWidth),
        available.x * kSettingsThemeColorListWidthRatio);

    if (ImGui::BeginChild(
            "##ThemeColorList",
            ImVec2(listWidth, editorHeight),
            ImGuiChildFlags_Borders))
    {
        if (ImGui::BeginTable(
                "##ThemeColorRows",
                kSettingsThemeColorTableColumnCount,
                ImGuiTableFlags_SizingStretchProp |
                    ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn(
                "##ThemeColorName",
                ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(
                "##ThemeColorSwatch",
                ImGuiTableColumnFlags_WidthFixed,
                FUiScale::Apply(kSettingsThemeColorSwatchWidth));
            ImGui::TableSetupColumn(
                "##ThemeColorReset",
                ImGuiTableColumnFlags_WidthFixed);

            for (const FThemeColorUiDefinition& definition :
                 kThemeColorUiDefinitions)
            {
                const float rowHeight =
                    FUiScale::Apply(kSettingsThemeColorSwatchHeight);
                ImGui::PushID(static_cast<int32_t>(definition.Role));
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
                ImGui::TableSetColumnIndex(0);

                ImGui::PushStyleVar(
                    ImGuiStyleVar_SelectableTextAlign,
                    ImVec2(0.0f, kSettingsThemeRowTextAlignment));
                if (ImGui::Selectable(
                        FLocalization::Text(definition.Label),
                        SettingsThemeSelectedRole == definition.Role,
                        ImGuiSelectableFlags_None,
                        ImVec2(0.0f, rowHeight)))
                {
                    SelectThemeColorRole(definition.Role);
                }
                ImGui::PopStyleVar();

                ImGui::TableSetColumnIndex(1);
                const ImVec4 color = FUiTheme::ToImGuiColor(
                    SettingsThemePaletteDraft.Get(definition.Role));
                if (ImGui::ColorButton(
                        "##Color",
                        color,
                        ImGuiColorEditFlags_NoAlpha |
                            ImGuiColorEditFlags_NoTooltip |
                            ImGuiColorEditFlags_NoDragDrop,
                        ImVec2(
                            FUiScale::Apply(kSettingsThemeColorSwatchWidth),
                            rowHeight)))
                {
                    SelectThemeColorRole(definition.Role);
                }

                ImGui::TableSetColumnIndex(2);
                if (CenteredTextButton(
                        FLocalization::Text(EUiText::Reset),
                        ImVec2(0.0f, rowHeight)))
                {
                    SettingsThemePaletteDraft.Get(definition.Role) =
                        FUserSettings::kDefaultThemePalette.Get(
                            definition.Role);
                    SelectThemeColorRole(definition.Role);
                    FUiTheme::SetPalette(SettingsThemePaletteDraft);
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    if (ImGui::BeginChild(
            "##ThemeColorEditor",
            ImVec2(0.0f, editorHeight),
            ImGuiChildFlags_Borders))
    {
        const ImVec2 pickerAvailable = ImGui::GetContentRegionAvail();
        const float pickerWidth = FThemeColorPicker::CalculateFittingWidth(
            FUiScale::Apply(kSettingsThemePickerMaximumWidth),
            pickerAvailable.x,
            pickerAvailable.y);
        const float pickerOffset = std::max(
            0.0f,
            (pickerAvailable.x - pickerWidth) * 0.5f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pickerOffset);

        if (FThemeColorPicker::Render(
                "ThemeColorPicker",
                SettingsThemePaletteDraft.Get(SettingsThemeSelectedRole),
                SettingsThemePickerState,
                pickerWidth))
        {
            FUiTheme::SetPalette(SettingsThemePaletteDraft);
        }
    }
    ImGui::EndChild();
}

void FMainDockSpace::RenderSettingsPopup()
{
    if (bRequestSettingsPopup)
    {
        SettingsLanguageDraft = FUserSettings::GetLanguage();
        SettingsCacheCapacityDraft =
            static_cast<int32_t>(ImageConfigCache.GetCapacity());
        SettingsThemeSelectedRole = FUserSettings::EThemeColorRole::Accent;
        ReloadThemePaletteDraft();
        bSettingsThemePreviewActive = true;
        bSettingsOnlyClearImagePropertyCache = true;
        ImGui::OpenPopup(FLocalization::WindowTitle(EUiText::SettingsPopup));
        bRequestSettingsPopup = false;
        LOGI("Settings", "Opening settings dialog");
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ConfigureNextPopup(viewport, kSettingsPopupWidth, kSettingsPopupHeight);

    const ImGuiStyle& baseStyle = ImGui::GetStyle();
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        FUiScale::Apply(kSettingsHorizontalPadding, kSettingsVerticalPadding));
    ImGui::PushStyleVar(
        ImGuiStyleVar_PopupRounding,
        FUiScale::Apply(kSettingsPopupRounding));
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        FUiScale::Apply(kSettingsFrameHorizontalPadding, kSettingsFrameVerticalPadding));
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(baseStyle.ItemSpacing.x, FUiScale::Apply(kSettingsItemSpacingY)));

    const bool bSettingsOpen = ImGui::BeginPopupModal(
            FLocalization::WindowTitle(EUiText::SettingsPopup),
            nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);

    if (!bSettingsOpen)
    {
        if (bSettingsThemePreviewActive)
        {
            ReloadThemePaletteDraft();
            bSettingsThemePreviewActive = false;
        }

        ImGui::PopStyleVar(kSettingsStyleVarCount);
        return;
    }

    // Reserve the footer outside the scrolling body so Apply/Cancel stay reachable on small screens.
    const float footerReserve = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
    ImGui::BeginChild("##SettingsBody", ImVec2(0.0f, -footerReserve));
    ImGui::TextUnformatted(FLocalization::Text(EUiText::Language));
    ImGui::SetNextItemWidth(FUiScale::Apply(kSettingsLanguageInputWidth));
    const char* languageNames[] = {
        FLocalization::Text(EUiText::LanguageChinese),
        FLocalization::Text(EUiText::LanguageEnglish)
    };
    int languageIndex = SettingsLanguageDraft == FLocalization::ELanguage::English ? 1 : 0;
    if (ImGui::Combo("##UiLanguage", &languageIndex, languageNames, IM_ARRAYSIZE(languageNames)))
    {
        SettingsLanguageDraft = languageIndex == 1
            ? FLocalization::ELanguage::English : FLocalization::ELanguage::SimplifiedChinese;
    }
    ImGui::TextDisabled("%s", FLocalization::Text(EUiText::LanguageApplyHelp));

    RenderThemeSettings();

    ImGui::TextUnformatted(FLocalization::Text(EUiText::ImageCacheCapacity));
    ImGui::SetNextItemWidth(FUiScale::Apply(kSettingsCacheInputWidth));
    ImGui::InputInt(
        "##ImagePropertyCacheCapacity",
        &SettingsCacheCapacityDraft,
        kCacheCapacityInputStep,
        kCacheCapacityInputFastStep);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(FLocalization::Text(EUiText::ImagesUnit));

    ImGui::TextDisabled(
        FLocalization::Text(EUiText::ImageCacheSummary),
        static_cast<unsigned long long>(FUserSettings::kMaximumImageConfigCacheCapacity),
        static_cast<unsigned long long>(ImageConfigCache.GetSize()));

    if (SettingsCacheCapacityDraft < 0 ||
        static_cast<size_t>(SettingsCacheCapacityDraft) >
            FUserSettings::kMaximumImageConfigCacheCapacity)
    {
        ImGui::TextColored(
            kSettingsWarningTextColor,
            FLocalization::Text(EUiText::CacheCapacityClamped));
    }

    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button, kSettingsDangerButtonColor);
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        kSettingsDangerButtonHoveredColor);
    ImGui::PushStyleColor(
        ImGuiCol_ButtonActive,
        kSettingsDangerButtonActiveColor);
    ImGui::PushStyleColor(ImGuiCol_Text, kSettingsDangerButtonTextColor);
    const bool bClearData = ImGui::Button(
        FLocalization::Text(EUiText::ClearData),
        ImVec2(FUiScale::Apply(kSettingsClearButtonWidth), 0.0f));
    ImGui::PopStyleColor(4);

    ImGui::SameLine();
    ImGui::Checkbox(
        FLocalization::Text(EUiText::ClearImageCacheOnly),
        &bSettingsOnlyClearImagePropertyCache);

    if (bClearData)
    {
        ClearStoredData(bSettingsOnlyClearImagePropertyCache);
    }
    ImGui::EndChild();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float footerWidth = FUiScale::Apply(kSettingsApplyButtonWidth)
        + FUiScale::Apply(kSettingsCancelButtonWidth)
        + style.ItemSpacing.x;
    const float footerY = ImGui::GetWindowHeight()
        - style.WindowPadding.y
        - ImGui::GetFrameHeight();
    const float separatorY = footerY - style.ItemSpacing.y;

    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), separatorY));
    ImGui::Separator();
    ImGui::SetCursorPosY(footerY);
    ImGui::SetCursorPosX(std::max(
        style.WindowPadding.x,
        ImGui::GetWindowWidth() - style.WindowPadding.x - footerWidth));

    if (ImGui::Button(
            FLocalization::Text(EUiText::Apply),
            ImVec2(FUiScale::Apply(kSettingsApplyButtonWidth), 0.0f)))
    {
        if (!FUserSettings::SetLanguage(SettingsLanguageDraft))
        {
            FToast::Show(FLocalization::Text(EUiText::LanguageSaveFailed));
            ImGui::EndPopup();
            ImGui::PopStyleVar(kSettingsStyleVarCount);
            return;
        }
        FLocalization::SetLanguage(SettingsLanguageDraft);
        const int32_t nonNegativeDraft =
            std::max(0, SettingsCacheCapacityDraft);
        const size_t capacity = std::min(
            static_cast<size_t>(nonNegativeDraft),
            FUserSettings::kMaximumImageConfigCacheCapacity);

        ImageConfigCache.SetCapacity(capacity);
        FUserSettings::SetImageConfigCacheCapacity(capacity);
        SettingsCacheCapacityDraft = static_cast<int32_t>(capacity);

        FUserSettings::SetThemePalette(SettingsThemePaletteDraft);
        ReloadThemePaletteDraft();
        bSettingsThemePreviewActive = false;

        const bool bPersisted = PersistImageConfigCache("capacity change");

        LOGI(
            "Settings",
            "Settings applied: cacheCapacity=%llu themeColors=%llu accent=#%02X%02X%02X",
            static_cast<unsigned long long>(capacity),
            static_cast<unsigned long long>(FUserSettings::kThemeColorRoleCount),
            static_cast<uint32_t>(SettingsThemePaletteDraft.Get(
                FUserSettings::EThemeColorRole::Accent).R),
            static_cast<uint32_t>(SettingsThemePaletteDraft.Get(
                FUserSettings::EThemeColorRole::Accent).G),
            static_cast<uint32_t>(SettingsThemePaletteDraft.Get(
                FUserSettings::EThemeColorRole::Accent).B));
        FToast::Show(
            bPersisted
                ? FLocalization::Text(EUiText::SettingsSaved)
                : FLocalization::Text(EUiText::SettingsCacheSaveFailed));
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button(
            FLocalization::Text(EUiText::Cancel),
            ImVec2(FUiScale::Apply(kSettingsCancelButtonWidth), 0.0f)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        ReloadThemePaletteDraft();
        bSettingsThemePreviewActive = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    ImGui::PopStyleVar(kSettingsStyleVarCount);
}

void FMainDockSpace::ClearStoredData(bool bOnlyImagePropertyCache)
{
    const size_t clearedCount = ImageConfigCache.GetSize();
    ImageConfigCache.Clear();
    DirectoryImagePropertyHistory.Clear();
    bSkipActiveImageConfigOnShutdown = true;

    if (bOnlyImagePropertyCache)
    {
        const bool bPersisted = PersistImageConfigCache("clear image properties");
        const std::string message = !bPersisted
            ? std::string(FLocalization::Text(EUiText::CacheClearSaveFailed))
            : (clearedCount > 0
                ? FLocalization::Text(EUiText::ClearedPrefix) + std::to_string(clearedCount) + FLocalization::Text(EUiText::CachedImagesSuffix)
                : FLocalization::Text(EUiText::ImageCacheEmpty));
        FToast::Show(message.c_str());
        LOGI(
            "Settings",
            "Image configuration cache cleared: %llu entries",
            static_cast<unsigned long long>(clearedCount));
        return;
    }

    // “全部数据”同时清理内存态，避免本次进程仍能从最近文件或目录继承中读回旧信息。
    RecentFiles.clear();
    ImageConfigCache.SetCapacity(
        FUserSettings::kDefaultImageConfigCacheCapacity);
    SettingsCacheCapacityDraft = static_cast<int32_t>(
        FUserSettings::kDefaultImageConfigCacheCapacity);

    if (HistogramPanel)
    {
        HistogramPanel->ResetPreferences();
    }

    if (ComparePanel)
    {
        ComparePanel->SetViewTarget(EViewTarget::Main);
        AppliedViewTarget.reset();
        SingleImageCompareHintExpiresAt.reset();
        bWasSingleImageCompareModeActive =
            Document && Document->GetImageData() &&
            CompareDocument && CompareDocument->GetImageData();
        UpdateViewTarget();
    }

    const bool bPersistentDataCleared = FUserSettings::ClearAllData();
    SettingsLanguageDraft = FUserSettings::GetLanguage();
    FLocalization::SetLanguage(SettingsLanguageDraft);
    ReloadThemePaletteDraft();
    const bool bLayoutCleared = ClearUiLayoutSettingsFile();
    const bool bAllCleared = bPersistentDataCleared && bLayoutCleared;

    FToast::Show(
        bAllCleared
            ? FLocalization::Text(EUiText::AllDataCleared)
            : FLocalization::Text(EUiText::DataClearFailed));
    LOGI(
        "Settings",
        "All application data cleared: imageEntries=%llu persistent=%d layout=%d",
        static_cast<unsigned long long>(clearedCount),
        bPersistentDataCleared ? 1 : 0,
        bLayoutCleared ? 1 : 0);
}

bool FMainDockSpace::ClearUiLayoutSettingsFile()
{
    ImGuiIO& io = ImGui::GetIO();

    if (!io.IniFilename || !*io.IniFilename)
    {
        return true;
    }

    const std::string iniFilename = io.IniFilename;
    const std::filesystem::path iniPath =
        std::filesystem::u8path(iniFilename);
    std::error_code ec;
    const bool bExists = std::filesystem::exists(iniPath, ec);

    if (ec)
    {
        LOGE(
            "Settings",
            "Failed to inspect UI layout data: %s (%s)",
            iniFilename.c_str(),
            ec.message().c_str());
        return false;
    }

    if (bExists && !std::filesystem::remove(iniPath, ec))
    {
        LOGE(
            "Settings",
            "Failed to remove UI layout data: %s (%s)",
            iniFilename.c_str(),
            ec.message().c_str());
        return false;
    }

    // DestroyContext 会自动保存 IniFilename；置空才能避免退出时把刚删除的旧布局写回来。
    io.IniFilename = nullptr;
    io.WantSaveIniSettings = false;
    LOGI("Settings", "UI layout data cleared: %s", iniFilename.c_str());
    return true;
}

void FMainDockSpace::RenderUsageGuidePopup()
{
    const bool bOpening = bRequestUsageGuidePopup;
    if (bOpening)
    {
        // 菜单只记录请求，在此处以与 BeginPopupModal 相同的 ID 栈打开。
        ImGui::OpenPopup(FLocalization::WindowTitle(EUiText::UsageGuidePopup));
        bRequestUsageGuidePopup = false;
    }

    ConfigureNextPopup(
        ImGui::GetMainViewport(), kUsageGuidePopupWidth, kUsageGuidePopupHeight);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        FUiScale::Apply(kAboutHorizontalPadding, kAboutVerticalPadding));
    ImGui::PushStyleVar(
        ImGuiStyleVar_PopupRounding, FUiScale::Apply(kAboutPopupRounding));
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        FUiScale::Apply(kAboutFrameHorizontalPadding, kAboutFrameVerticalPadding));

    bool bOpen = true;
    if (!ImGui::BeginPopupModal(
            FLocalization::WindowTitle(EUiText::UsageGuidePopup),
            &bOpen,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::PopStyleVar(kUsageGuideStyleVarCount);
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float footerHeight = ImGui::GetFrameHeight() + style.ItemSpacing.y;
    // 长说明只在正文区域滚动，关闭按钮始终留在弹窗底部。
    if (ImGui::BeginChild("##UsageGuideBody", ImVec2(0.0f, -footerHeight)))
    {
        if (bOpening)
        {
            ImGui::SetScrollY(0.0f);
        }

        for (const FUsageGuideSection& section : kUsageGuideSections)
        {
            ImGui::SeparatorText(FLocalization::Text(section.Title));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(FLocalization::Text(section.Body));
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
        }
    }
    ImGui::EndChild();

    const float closeButtonWidth = std::max(
        FUiScale::Apply(kUsageGuideCloseButtonWidth),
        ImGui::CalcTextSize(FLocalization::Text(EUiText::Close)).x
            + style.FramePadding.x * 2.0f);
    ImGui::SetCursorPosX(std::max(
        style.WindowPadding.x,
        ImGui::GetWindowWidth() - style.WindowPadding.x - closeButtonWidth));
    if (CenteredTextButton(
            FLocalization::Text(EUiText::Close), ImVec2(closeButtonWidth, 0.0f))
        || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    ImGui::PopStyleVar(kUsageGuideStyleVarCount);
}

void FMainDockSpace::RenderAboutPopup()
{
    if (bRequestAboutPopup)
    {
        ImGui::OpenPopup(FLocalization::WindowTitle(EUiText::AboutPopup));
        bRequestAboutPopup = false;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ConfigureNextPopup(viewport, kAboutPopupWidth, kAboutPopupHeight);

    const ImGuiStyle& baseStyle = ImGui::GetStyle();
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        FUiScale::Apply(kAboutHorizontalPadding, kAboutVerticalPadding));
    ImGui::PushStyleVar(
        ImGuiStyleVar_PopupRounding,
        FUiScale::Apply(kAboutPopupRounding));
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        FUiScale::Apply(kAboutFrameHorizontalPadding, kAboutFrameVerticalPadding));
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(baseStyle.ItemSpacing.x, FUiScale::Apply(kAboutItemSpacingY)));

    const bool bAboutOpen = ImGui::BeginPopupModal(
            FLocalization::WindowTitle(EUiText::AboutPopup),
            nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);

    if (!bAboutOpen)
    {
        ImGui::PopStyleVar(4);
        return;
    }

    const char* productName = "YUVRaw";
    const float contentWidth = ImGui::GetContentRegionAvail().x;
    const float titleFontSize = ImGui::GetFontSize() * kAboutTitleScale;
    const ImVec2 titleSize = ImGui::GetFont()->CalcTextSizeA(
        titleFontSize,
        FLT_MAX,
        0.0f,
        productName);
    const ImVec2 titleCursor = ImGui::GetCursorScreenPos();
    const ImVec2 titlePos(
        titleCursor.x + std::max(0.0f, (contentWidth - titleSize.x) * 0.5f),
        titleCursor.y);

    ImGui::GetWindowDrawList()->AddText(
        ImGui::GetFont(),
        titleFontSize,
        titlePos,
        kAboutAccentColor,
        productName);
    ImGui::Dummy(ImVec2(contentWidth, titleSize.y));

    const char* subtitle = FLocalization::Text(EUiText::AppDescription);
    const ImVec2 subtitleSize = ImGui::CalcTextSize(subtitle);
    ImGui::SetCursorPosX(std::max(
        FUiScale::Apply(kAboutHorizontalPadding),
        (ImGui::GetWindowWidth() - subtitleSize.x) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, kAboutSubtitleColor);
    ImGui::TextUnformatted(subtitle);
    ImGui::PopStyleColor();

    ImGui::SetCursorPosY(
        ImGui::GetCursorPosY() + FUiScale::Apply(kAboutSectionAdditionalGap));

    char versionLabel[64];
    std::snprintf(
        versionLabel,
        sizeof(versionLabel),
        FLocalization::Text(EUiText::AppVersion),
        FAppVersion::String);

    const ImGuiStyle& style = ImGui::GetStyle();
    const char* latestVersionLabel = FLocalization::Text(EUiText::LatestVersion);
    const float releasesButtonWidth =
        ImGui::CalcTextSize(latestVersionLabel).x + style.FramePadding.x * 2.0f;
    const ImVec2 versionTextSize = ImGui::CalcTextSize(versionLabel);
    const float versionRowHeight = std::max(
        versionTextSize.y + FUiScale::Apply(kAboutBadgeVerticalPadding) * 2.0f,
        ImGui::GetFrameHeight());
    const ImVec2 badgeSize(
        versionTextSize.x + FUiScale::Apply(kAboutBadgeHorizontalPadding) * 2.0f,
        versionRowHeight);
    // 按两种语言的实际文字宽度将版本号与下载按钮作为一组居中，并保持同高。
    const float versionRowWidth = badgeSize.x + style.ItemSpacing.x + releasesButtonWidth;
    const ImVec2 badgeCursor = ImGui::GetCursorScreenPos();
    const ImVec2 badgeMin(
        badgeCursor.x + std::max(0.0f, (contentWidth - versionRowWidth) * 0.5f),
        badgeCursor.y);
    const ImVec2 badgeMax(
        badgeMin.x + badgeSize.x,
        badgeMin.y + badgeSize.y);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        badgeMin,
        badgeMax,
        ImGui::GetColorU32(ImGuiCol_FrameBg),
        FUiScale::Apply(kAboutBadgeRounding));
    drawList->AddRect(
        badgeMin,
        badgeMax,
        ImGui::GetColorU32(ImGuiCol_Border),
        FUiScale::Apply(kAboutBadgeRounding));
    drawList->AddText(
        ImVec2(
            badgeMin.x + FUiScale::Apply(kAboutBadgeHorizontalPadding),
            CalculateGlyphCenteredTextY(versionLabel, badgeMin.y, badgeMax.y)),
        ImGui::GetColorU32(ImGuiCol_Text),
        versionLabel);
    ImGui::SetCursorScreenPos(badgeMin);
    ImGui::Dummy(badgeSize);
    ImGui::SameLine();

    const bool bOpenReleases = CenteredTextButton(
        latestVersionLabel, ImVec2(releasesButtonWidth, versionRowHeight));
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", FLocalization::Text(EUiText::ProjectReleasesHelp));
    }
    if (bOpenReleases && !FFileDialog::OpenProjectReleases())
    {
        FToast::Show(
            FLocalization::Text(EUiText::ProjectReleasesOpenFailed),
            kProjectReleasesFailureToastSeconds);
    }

    ImGui::SetCursorPosY(
        ImGui::GetCursorPosY() + FUiScale::Apply(kAboutSectionAdditionalGap));
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextWrapped(
        FLocalization::Text(EUiText::OpenSourceNotice));

    const float footerY =
        ImGui::GetWindowHeight()
        - style.WindowPadding.y
        - ImGui::GetFrameHeight();
    const float footerSeparatorY =
        footerY - style.ItemSpacing.y;
    ImGui::SetCursorPosY(std::max(
        ImGui::GetCursorPosY(),
        footerSeparatorY));
    ImGui::Separator();
    ImGui::SetCursorPosY(std::max(
        ImGui::GetCursorPosY(),
        footerY));

    ImGui::PushStyleColor(
        ImGuiCol_Button,
        kAboutSecondaryButton);
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        kAboutSecondaryButtonHovered);
    ImGui::PushStyleColor(
        ImGuiCol_ButtonActive,
        kAboutSecondaryButtonActive);
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        kAboutSecondaryButtonBorder);
    ImGui::PushStyleColor(ImGuiCol_Text, kAboutAccentColor);
    ImGui::PushStyleVar(
        ImGuiStyleVar_FrameBorderSize,
        FUiScale::Apply(1.0f));

    const float licensesButtonWidth = std::max(FUiScale::Apply(kAboutLicensesButtonWidth),
        ImGui::CalcTextSize(FLocalization::Text(EUiText::ThirdPartyLicenses)).x + style.FramePadding.x * 2.0f);
    const float footerWidth =
        licensesButtonWidth
        + style.ItemSpacing.x
        + FUiScale::Apply(kAboutConfirmButtonWidth);
    ImGui::SetCursorPosX(std::max(
        style.WindowPadding.x,
        ImGui::GetWindowWidth() - style.WindowPadding.x - footerWidth));

    const bool bOpenLicenses = ImGui::Button(
        FLocalization::Text(EUiText::ThirdPartyLicenses),
        ImVec2(licensesButtonWidth, 0.0f));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);

    if (bOpenLicenses)
    {
        LOGI("About", "Opening third-party license notices");
        ImGui::OpenPopup(FLocalization::WindowTitle(EUiText::ThirdPartyPopup));
    }

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, kAboutPrimaryButton);
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        kAboutPrimaryButtonHovered);
    ImGui::PushStyleColor(
        ImGuiCol_ButtonActive,
        kAboutPrimaryButtonActive);
    ImGui::PushStyleColor(ImGuiCol_Text, kAboutPrimaryButtonText);
    const bool bConfirm = ImGui::Button(
            FLocalization::Text(EUiText::Confirm),
            ImVec2(FUiScale::Apply(kAboutConfirmButtonWidth), 0.0f));
    ImGui::PopStyleColor(4);

    if (bConfirm)
    {
        ImGui::CloseCurrentPopup();
    }

    ImGui::PopStyleVar(4);
    RenderThirdPartyLicensesPopup(viewport);
    ImGui::EndPopup();
}

void FMainDockSpace::Render()
{
    // 后台任务的收尾必须在主线程做（建纹理要 GL 上下文），放在出帧的最前面
    AsyncJob.Poll();

    const bool bBusy = AsyncJob.IsRunning();

    // 差值/导出工作线程直接持有文档像素指针；只有它结束后才能原子替换文档。
    // 图片解码线程可以继续工作，准备好的结果在这里最多多等几帧，不会阻塞 UI。
    if (!bBusy)
    {
        // 上一帧的绘制已完成；先取消被关闭槽位的加载，再接受其它加载结果。
        ProcessPendingDocumentClose();
        PollImageLoad();
    }

    const bool bImageLoading = PendingImageLoad.has_value();

    // 创建主窗口（无边框，全屏）
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    window_flags |= ImGuiWindowFlags_MenuBar;
    window_flags |= ImGuiWindowFlags_NoBackground;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("MainDockSpaceWindow", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    // 后台任务运行期间整个界面置灰。这不只是"正在忙"的视觉反馈，也是一条安全边界：
    // 工作线程直接读着文档里的像素，此刻任何改参数 / 换帧 / 重新加载都会让它读到已释放的内存。
    // 置灰状态是 ImGui 的全局栈，跨 Begin/End 依然有效，所以一次就能罩住下面所有面板。
    ImGui::BeginDisabled(bBusy);

    // 快捷键不走控件，置灰拦不住它，只能显式跳过
    if (!bBusy)
    {
        HandleShortcuts();
    }

    // 没有图像时"导出..."置灰
    {
        const FImageDocument* displayed = GetDisplayedDocument();
        MenuBar->SetExportEnabled(
            !bImageLoading &&
            displayed != nullptr &&
            displayed->GetImageData() != nullptr);
    }

    MenuBar->Render();

    // 创建DockSpace
    ImGuiID dockspace_id = ImGui::GetID(kDockSpaceId);
    bool firstRun = ImGui::DockBuilderGetNode(dockspace_id) == nullptr;
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

    // 首次运行时设置布局
    if (firstRun)
    {
        SetupDockSpace();
    }

    UpdateViewTarget();

    // 渲染各个窗口。
    // 同一个 dock 节点里，标签页的排列顺序 = 窗口首次 Begin 的顺序，
    // 所以"属性面板"要排在"对比"前面就必须先渲染。
    // 默认选中哪个标签页则由下面的延后选择决定，两者互不冲突。
    FileExplorer->Render();

    // 图片加载只冻结会读写当前文档/视图的控件；文件浏览器、菜单与窗口本身保持
    // 可交互，用户可以继续快速切换，后台邮箱会自动合并成最新请求。
    ImGui::BeginDisabled(bImageLoading);
    ImageViewer->Render();
    PropertyPanel->Render();
    ComparePanel->Render();
    ImGui::EndDisabled();
    HistogramPanel->Render();

    // 模态弹窗，不参与 DockSpace 布局
    ExportPanel->Render();
    RenderSettingsPopup();
    RenderUsageGuidePopup();
    RenderAboutPopup();

    // 此时各面板窗口已经存在，可以只切换 Dock 标签而不抢走文件浏览器的键盘焦点。
    if (bPendingSelectPropertyPanel)
    {
        SelectDockedWindowTabWithoutFocus(FLocalization::WindowTitle(EUiText::Properties));
        bPendingSelectPropertyPanel = false;
    }

    // 各面板刚更新完自己的屏幕矩形，这时才能判断拖放落在了谁身上
    ProcessPendingDrop();

    ImGui::EndDisabled();

    // 预览图由自定义 OpenGL 回调绘制，浮层视觉必须等所有 Dock 窗口提交完再进前景列表；
    // 命中已在查看器内处理。忙碌或有弹窗时不画，避免控件盖住遮罩/菜单/模态框。
    const ImGuiPopupFlags anyPopupFlags =
        ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel;
    if (!bBusy && !ImGui::IsPopupOpen(nullptr, anyPopupFlags))
    {
        ImageViewer->RenderOverlayVisuals();
    }

    // 遮罩画在前景绘制列表上：不受上面那层置灰的透明度影响，也永远压在所有面板之上
    if (bBusy)
    {
        RenderBusyOverlay();
    }

    // 提示同样画在前景列表上，排在遮罩之后 —— 长任务结束时的回执不该被遮罩压住
    FToast::Render();

    ImGui::End();

}

void FMainDockSpace::SetupDockSpace()
{
    ImGuiID dockspace_id = ImGui::GetID(kDockSpaceId);
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

    // 分割：左侧15%，右侧15%，中间70%
    ImGuiID dock_left = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.15f, nullptr, &dockspace_id);
    ImGuiID dock_right = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, 0.15f / 0.85f, nullptr, &dockspace_id);

    // 右侧再从下方分出一块放直方图，属性/对比作为标签页共用上半部分
    ImGuiID dock_right_bottom = ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.4f, nullptr, &dock_right);

    // 分配窗口。这里的字符串必须与各面板 ImGui::Begin() 的标题完全一致
    ImGui::DockBuilderDockWindow(FLocalization::WindowTitle(EUiText::FileExplorer), dock_left);
    ImGui::DockBuilderDockWindow(FLocalization::WindowTitle(EUiText::ImageViewer), dockspace_id);
    ImGui::DockBuilderDockWindow(FLocalization::WindowTitle(EUiText::Properties), dock_right);
    ImGui::DockBuilderDockWindow(FLocalization::WindowTitle(EUiText::Compare), dock_right);
    ImGui::DockBuilderDockWindow(FLocalization::WindowTitle(EUiText::Histogram), dock_right_bottom);

    ImGui::DockBuilderFinish(dockspace_id);

    // 属性面板是最常用的，默认选中它而不是"对比"
    bPendingSelectPropertyPanel = true;
}
