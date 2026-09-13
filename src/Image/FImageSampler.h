#pragma once

#include "FColorTransform.h"
#include "FDisplaySettings.h"
#include "FImageData.h"
#include "FImageFormat.h"

#include <cstdint>
#include <vector>

/**
 * 单个像素的采样结果，供像素探针、直方图和 CPU 图像转换使用
 */
struct FPixelSample
{
    static constexpr int32_t ComponentCapacity = 4;

    /// 有效分量数（灰度/Bayer=1，YUV=3，RGB=3 或 4）
    int32_t Count = 0;

    /// 分量名，如 {"Y","U","V"} 或 {"R","G","B","A"}
    const char* Labels[ComponentCapacity] = { "", "", "", "" };

    /// 分量的原始整数值。高位对齐格式已右移 SampleShift，还原成实际有效位数
    int32_t Values[ComponentCapacity] = { 0, 0, 0, 0 };

    /// 该格式主要分量的满量程值（8bit=255，10bit=1023，16bit=65535）
    int32_t MaxValue = 255;

    /// 各分量各自的满量程。RGB10_A2 的 A 只有 2bit，不能沿用 RGB 的 1023。
    int32_t ComponentMaxValues[ComponentCapacity] = { 255, 255, 255, 255 };

    int32_t GetComponentMaxValue(int32_t ComponentIndex) const
    {
        return ComponentIndex >= 0 && ComponentIndex < ComponentCapacity
            ? ComponentMaxValues[ComponentIndex]
            : MaxValue;
    }

    /**
     * 按有效位深归一化的源 RGB 码值，供直方图统计。
     * YUV 已完成范围还原与矩阵转换，但未经过 EOTF、曝光、色调映射或裁剪；
     * PQ/HLG 仍保留对应的非线性编码，超出 [0,1] 的分量也原样保留。
     * Bayer 使用源传感器码值域的双线性去马赛克结果，不套用传输函数。
     */
    float SourceRgb[3] = { 0.0f, 0.0f, 0.0f };

    /// SDR 预览输出 RGB（0-1），Bayer 时为双线性去马赛克结果；不代表 HDR 屏幕输出
    float Rgb[3] = { 0.0f, 0.0f, 0.0f };

    /**
     * 该点的线性亮度（cd/m^2）
     *
     * PQ/HLG 下是**绝对值**，这正是调 HDR 素材时最想知道的那个数；
     * 其余传输函数没有绝对亮度含义，这里按参考白折算出一个等效值。
     */
    float LinearNits = 0.0f;

    /// 色彩管线是否实际生效。false 时 LinearNits 只是按 sRGB 曲线反推的估计值
    bool bPipelineActive = false;
};

namespace FImageSampler
{
    /**
     * 一幅图 + 一套显示设置展开后的采样上下文
     *
     * YUV 矩阵与色彩管线（含原色矩阵的 3x3 求逆）在这里只算一次。
     * **批量采样一定要先 MakeContext 再复用**：直方图要取 25 万个点，
     * 每点重建一次矩阵会把这件本来只算一次的事变成实打实的卡顿。
     */
    struct FSampleContext
    {
        float YuvMatrix[9] = {};
        float YuvOffset[3] = {};

        FColorTransform::FColorPipeline Pipeline;

        EBayerPattern BayerPattern = EBayerPattern::RGGB;

        int32_t MaxValue = 255;
        int32_t SampleShift = 0;
    };

    /**
     * 展开采样上下文
     *
     * Rgb 固定为 SDR 预览输出（导出/差值/探针色块），与屏幕是不是 HDR 无关。
     * 直方图使用进入显示色彩管线前的 SourceRgb。
     */
    FSampleContext MakeContext(
        const FImageData& ImageData,
        const FDisplaySettings& Display,
        EBayerPattern BayerPattern);

    /**
     * 用展开好的上下文读取一个像素（批量采样走这条）
     */
    bool SamplePixel(
        const FImageData& ImageData,
        int32_t X,
        int32_t Y,
        const FSampleContext& Context,
        FPixelSample& OutSample);

    /**
     * 读取指定像素的原始分量值、源 RGB 码值及 SDR 预览 RGB
     *
     * 与着色器共用色彩数学（FColorTransform::ApplyPipeline），但 Rgb 的输出目标固定为 SDR。
     * SourceRgb 在该管线执行前取值，不受 HDR 显示能力或色调映射影响。
     *
     * 这个重载每次都会展开一遍上下文，只适合像素探针这种一次一个点的场景。
     *
     * @param ImageData     图像数据
     * @param X, Y          图像坐标（左上角为原点）
     * @param Display       显示设置（矩阵/原色/传输函数/范围/色调映射…）
     * @param BayerPattern  仅对 Bayer 家族有意义，决定该点属于 R/G/B 哪个滤片
     * @param OutSample     输出
     * @return 坐标越界或格式不支持时返回 false
     */
    bool SamplePixel(
        const FImageData& ImageData,
        int32_t X,
        int32_t Y,
        const FDisplaySettings& Display,
        EBayerPattern BayerPattern,
        FPixelSample& OutSample);

    /**
     * 把整幅图转换成紧凑的 RGB8 缓冲（每像素 3 字节）
     *
     * 供差值对比与导出使用，生成统一的 SDR 预览 RGB；直方图直接使用 SourceRgb。
     *
     * 色彩管线（含 YUV 矩阵、原色矩阵）在循环外只展开一次。
     * 传输函数不是 SDR 时每像素要算 9 次 pow()，4K 图会明显变慢 ——
     * 这条路径全都跑在 FAsyncJob 的工作线程上，界面有忙碌遮罩，
     * 所以这里选择**精确计算而不是查表**：查表会让探针与导出相差不到 1 LSB，
     * 但"两处结果不完全一致"这件事本身在调试工具里就是个坑。
     *
     * @param OutRgb 输出，大小为 Width * Height * 3
     * @return 格式不支持时返回 false
     */
    bool ConvertToRgb8(
        const FImageData& ImageData,
        const FDisplaySettings& Display,
        EBayerPattern BayerPattern,
        std::vector<uint8_t>& OutRgb);
}
