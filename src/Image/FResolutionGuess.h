#pragma once

#include "FImageFormat.h"

#include <cstdint>
#include <vector>

/**
 * 一条分辨率候选
 */
struct FResolutionCandidate
{
    int32_t Width = 0;
    int32_t Height = 0;

    /// 来自文件名解析（如 xxx_1472x1920.yuv），而不是由文件大小算出来的
    bool bFromFilename = false;

    /// 按该分辨率算出的图像字节数正好等于文件大小。
    /// 来自文件名的候选可能为 false —— 那正是最有价值的信号
    bool bExact = true;
};

/**
 * 由文件大小反推无头格式的分辨率
 *
 * 一个文件就是一幅图，所以图像字节数必须**正好等于**文件大小。再加上一个容易被忽略的
 * 事实 —— `CalculateFrameSize()` 里宽度**只**通过 `ResolveBaseStride()` 参与计算 ——
 * 约束就变成了对 (stride, 高) 的一次因数分解：
 *
 *     文件大小 = stride * 高 * M(格式)        // M 为有理数，NV21 = 3/2，RGBA8 = 4
 *     =>  stride * 高 = 文件大小 / M
 *
 * 枚举右式的因数对即得全部数学解，宽度再由 stride 在"紧凑排列"假设下反推。
 * 因此**带 padding 的排布无法与紧凑排布区分**：1440x1920 stride 1472 与 1472x1920 紧凑
 * 的文件大小完全相同。这里一律按紧凑给出，padding 的可能性由 UI 用文字提示。
 */
namespace FResolutionGuess
{
    /**
     * 按文件大小与格式枚举可能的分辨率，已按可信度排序并按宽度去重
     *
     * @param MaxCount 最多返回几条（0 表示不限制）
     */
    std::vector<FResolutionCandidate> Guess(EImageFormat Format, uint64_t FileSize, int32_t MaxCount = 10);

    /**
     * 给定参数算出的图像字节数是否正好等于文件大小
     */
    bool Matches(EImageFormat Format, int32_t Width, int32_t Height, int32_t Stride, uint64_t FileSize);

    /**
     * 找出"在该分辨率下图像字节数正好等于文件大小"的其它格式
     *
     * 用于当前格式对不上时提示换格式 —— texture_output_format1_384x2880.yuv 就是这个场景：
     * 384x2880 按 NV21 算是 1658880 字节，按 RGBA8 算正好 4423680 = 文件大小。
     *
     * Width/Height <= 0 时退化为扫一遍常见分辨率表。
     */
    std::vector<EImageFormat> GuessFormats(
        uint64_t FileSize,
        int32_t Width,
        int32_t Height,
        EImageFormat ExcludeFormat,
        int32_t MaxCount = 4);

    /**
     * 紧凑排列下由每行字节数反推宽度。没有整数解时返回 0
     *
     * packed 格式（YUY2 每纹素 2 像素、MIPI RAW10 每 4 像素 5 字节）的换算不是简单除法，
     * 所以这里对 `ResolveBaseStride()` 做二分反演，而不是另写一套每格式的公式。
     */
    int32_t WidthFromStride(EImageFormat Format, int32_t Stride);
}
