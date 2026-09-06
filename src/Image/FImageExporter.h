#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "FDisplaySettings.h"
#include "FImageData.h"
#include "FImageFormat.h"

/**
 * 导出的目标文件格式
 *
 * 与 EImageFormat 是两回事：那个描述的是内存里的像素排布，
 * 这个描述的是落盘的容器/编码方式。
 */
enum class EExportFormat
{
    PNG,
    JPEG,
    BMP,
    WEBP,
};

/**
 * 分辨率处理方式。只支持等比例缩放，不提供拉伸变形
 */
enum class EExportResizeMode
{
    Original,   ///< 保持原始尺寸
    Percent,    ///< 按百分比缩放
    Width,      ///< 指定输出宽度，高度按原始宽高比推算
};

/**
 * 导出设置
 */
struct FExportSettings
{
    EExportFormat Format = EExportFormat::PNG;

    /// JPEG 质量 1-100，其它格式无意义（PNG/BMP/WebP 在这里都是无损的）
    int32_t JpegQuality = 90;

    /**
     * 色彩解读方式：把源图换算成 RGB 时用的全套设置
     *
     * 不只是 YUV 矩阵 —— 传输函数与原色也在里面。导出一帧 PQ 素材时，
     * 落盘的 PNG 必须是**色调映射之后**的 SDR 图，否则导出的文件和屏幕上看到的
     * 完全是两回事（会是那张灰蒙蒙的原始 PQ 码值）。
     *
     * 初值取源文档当前的显示设置，见 FExportPanel::Open。
     */
    FDisplaySettings Display;

    EExportResizeMode ResizeMode = EExportResizeMode::Original;
    float Percent = 100.0f;
    int32_t TargetWidth = 0;

    /// 输出目录。默认取源文件所在目录
    std::string OutputDirectory;

    /// false 时遇到同名文件自动加 _1/_2 后缀，不覆盖已有文件
    bool bOverwrite = false;
};

namespace FImageExporter
{
    /// 扩展名（含点号，全小写）
    const char* GetExtension(EExportFormat Format);

    /// UI 上显示的名字
    const char* GetDisplayName(EExportFormat Format);

    /**
     * 色彩矩阵对这次导出是否有意义
     *
     * 只有"源是 YUV、目标是 RGB 容器"时才需要挑选转换矩阵。
     * 源本来就是 RGB / 灰度 / Bayer 时矩阵不参与任何计算，面板上应当置灰。
     * 目前四种导出格式都是 RGB 容器，所以实际由源格式决定 —— 参数仍然带上目标格式，
     * 将来若支持导出 YUV 就不必改调用点。
     */
    bool NeedsColorMatrix(EImageFormat SourceFormat, EExportFormat TargetFormat);

    /**
     * 按设置推算输出尺寸。等比例，最小 1x1
     */
    void GetTargetSize(
        const FExportSettings& Settings,
        int32_t SourceWidth,
        int32_t SourceHeight,
        int32_t& OutWidth,
        int32_t& OutHeight);

    /**
     * 拼出输出文件路径：<输出目录>/<源文件主名><扩展名>
     *
     * bOverwrite 为 false 且同名文件已存在时，依次尝试 _1、_2……
     */
    std::string MakeOutputPath(const FExportSettings& Settings, const std::string& SourcePath);

    /**
     * 导出一幅图像
     *
     * 内部流程：转 RGB8 -> 等比例重采样 -> 编码。
     * 转 RGB8 走 FImageSampler，与直方图/差值同一套解读，所见即所得。
     *
     * @param BayerPattern 仅 Bayer 源用得上
     * @return 失败时 OutError 里是可以直接显示给用户的原因
     */
    bool Export(
        const FImageData& Source,
        EBayerPattern BayerPattern,
        const FExportSettings& Settings,
        const std::string& OutputPath,
        std::string& OutError);
}
