#pragma once

#include "FDisplaySettings.h"
#include "FImageData.h"
#include "FImageFormat.h"

#include <memory>
#include <string>

/**
 * 两幅图的差异统计
 *
 * 调 ISP / 编码器时，"看起来差不多"没有意义，需要的是具体数字。
 */
struct FCompareStats
{
    bool bValid = false;

    int32_t Width = 0;
    int32_t Height = 0;

    /// 任意通道上的最大绝对差（0-255）
    int32_t MaxAbsDiff = 0;

    /// 全部通道的平均绝对差
    double MeanAbsDiff = 0.0;

    /// 存在任何通道差异的像素数及其占比
    int64_t DiffPixelCount = 0;
    double  DiffPixelRatio = 0.0;

    /// 峰值信噪比（dB）。两图完全一致时为无穷大，此处用 -1 表示
    double PSNR = -1.0;

    /// 无法比较时的原因，供 UI 显示
    std::string Error;
};

namespace FImageCompare
{
    /**
     * 计算两幅图的差值图与统计量
     *
     * 两幅图先各自转换成 RGB8 再逐像素相减，因此**允许格式不同**
     * （比如拿 NV21 的解码结果和 PNG 参考图对比），只要分辨率一致即可。
     *
     * @param A, B       待比较的两幅图
     * @param Gain       差值放大倍数。1 倍下细微差异几乎看不见，通常用 4-32 倍
     * @param Display, BayerPattern  转换到 RGB 时使用的设置。两幅图用**同一套**解读，
     *                   差值比的是"同一种看法下两张图差多少"
     * @param OutStats   输出统计量
     * @return 差值图（RGB8），失败返回 nullptr 且 OutStats.Error 说明原因
     */
    std::unique_ptr<FImageData> ComputeDiff(
        const FImageData& A,
        const FImageData& B,
        float Gain,
        const FDisplaySettings& Display,
        EBayerPattern BayerPattern,
        FCompareStats& OutStats);
}
