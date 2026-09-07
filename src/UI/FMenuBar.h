#pragma once

#include <functional>
#include <string>

/**
 * 菜单栏类
 * 负责显示和管理菜单栏
 */
class FMenuBar
{
public:
    using MenuCallback = std::function<void()>;

    FMenuBar();
    ~FMenuBar();

    /**
     * 渲染菜单栏
     */
    void Render();

    /**
     * 设置打开文件回调
     */
    void SetOnOpenFile(MenuCallback Callback);

    /**
     * 设置打开目录回调
     */
    void SetOnOpenDirectory(MenuCallback Callback);

    /**
     * 设置导出回调
     */
    void SetOnExport(MenuCallback Callback);

    /**
     * 没有图像时把"导出..."置灰。菜单栏自己不持有文档，由调用方每帧告知
     */
    void SetExportEnabled(bool bEnabled);

    /**
     * 设置退出回调
     */
    void SetOnExit(MenuCallback Callback);

    /**
     * 设置“设置”入口回调。
     */
    void SetOnSettings(MenuCallback Callback);

    /** 设置使用说明弹窗回调。 */
    void SetOnUsageGuide(MenuCallback Callback);

    /** 设置反馈弹窗回调。 */
    void SetOnFeedback(MenuCallback Callback);

    /**
     * 设置关于对话框显示回调
     */
    void SetOnAbout(MenuCallback Callback);

    /**
     * 设置"文件"菜单里额外条目的绘制回调
     *
     * "最近打开"需要访问 FMainDockSpace 的状态，与其把状态搬进菜单栏，
     * 不如让菜单栏留一个插槽让调用方自己画。
     */
    void SetFileMenuExtras(MenuCallback Callback);

private:
    void RenderFileMenu();
    void RenderHelpMenu();

    MenuCallback OnOpenFile;
    MenuCallback OnOpenDirectory;
    MenuCallback OnExport;
    MenuCallback OnExit;
    MenuCallback OnSettings;
    MenuCallback OnUsageGuide;
    MenuCallback OnFeedback;
    MenuCallback OnAbout;
    MenuCallback FileMenuExtras;

    bool bExportEnabled = false;
};
