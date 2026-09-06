#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "Image/FDisplaySettings.h"
#include "Image/FImageExporter.h"
#include "Image/FImageFormat.h"

/**
 * 一次导出请求的来源
 */
struct FExportRequest
{
    /// 源文件路径。批量导出时是多个，单张导出时只有一个
    std::vector<std::string> SourcePaths;

    /**
     * true = 直接用已经加载好的主图，不重新读盘
     *
     * 菜单"导出..."走这条路：用户此刻看到的可能是多帧文件里的第 N 帧，
     * 也可能刚在属性面板改过 stride，重新按文件名解析一遍会得到另一幅图。
     * 文件浏览器的批量导出则相反，必须逐个读盘。
     */
    bool bUseLoadedImage = false;
};

/**
 * 一次导出的结果，直接显示在面板里
 */
struct FExportResult
{
    /// 一行总结："导出完成" / "已导出 3 个文件" / 失败原因
    std::string Text;

    bool bHasError = false;

    /// 成功写出的文件。面板把它们列成可点击的链接，点了在资源管理器里定位该文件
    std::vector<std::string> OutputPaths;
};

/**
 * 导出面板（模态弹窗）
 *
 * 不是停靠面板 —— 导出是一次性动作，常驻一个标签页只会挤占布局，
 * 所以做成模态弹窗，用完即走，也不需要动 DockSpace 的版本号。
 */
class FExportPanel
{
public:
    /**
     * 点"开始导出"时触发
     *
     * 没有返回值：导出跑在后台线程上，结果稍后由 SetResult() 送回来。
     */
    using ExportCallback = std::function<void(const FExportSettings&, const FExportRequest&)>;

    FExportPanel();

    /**
     * 打开面板
     *
     * @param DefaultDirectory 默认保存目录，调用方传源文件所在目录
     * @param SourceFormat     源像素格式，决定色彩矩阵是否可用
     * @param SourceWidth      源尺寸，用于预览输出分辨率；批量时传第一张的尺寸
     * @param Display          源图当前的显示设置，作为色彩矩阵的初值
     */
    void Open(
        const FExportRequest& Request,
        const std::string& DefaultDirectory,
        EImageFormat SourceFormat,
        int32_t SourceWidth,
        int32_t SourceHeight,
        const FDisplaySettings& Display);

    void Render();

    /**
     * 后台导出结束后送回结果
     *
     * 弹窗此刻可能已经不在了（导出期间界面虽然整个置灰，Esc 仍然能关掉模态弹窗），
     * 那就把它重新弹出来 —— 结果，尤其是那几个可点的文件名，总得有地方显示。
     */
    void SetResult(const FExportResult& InResult);

    void SetOnExport(ExportCallback Callback) { OnExport = std::move(Callback); }

private:
    void RenderSourceSection();
    void RenderFormatSection();
    void RenderColorMatrixSection();
    void RenderResolutionSection();
    void RenderDirectorySection();

    /// 结果文字 + 可点击的输出文件名
    void RenderResultSection();

    FExportRequest Request;
    FExportSettings Settings;

    EImageFormat SourceFormat;
    int32_t SourceWidth;
    int32_t SourceHeight;

    /// 下一帧调用 OpenPopup。ImGui 要求 OpenPopup 与 BeginPopup 在同一 ID 栈里，
    /// 所以外部只置位，真正打开留到 Render 里做
    bool bRequestOpen;

    /// 弹窗本帧是否真的在显示。由 Render() 维护，SetResult() 据此决定要不要把它弹回来
    bool bIsOpen;

    /// 新导出请求打开时把设置正文恢复到顶部，避免沿用上一次查看结果时的滚动位置
    bool bResetBodyScroll;

    /// 后台结果回来后把正文滚到结果区，固定高度弹窗下不能让回执静默落在首屏之外
    bool bScrollToResult;

    FExportResult Result;

    ExportCallback OnExport;
};
