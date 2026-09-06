#pragma once

#include "FImageFormat.h"

/**
 * 通道隔离模式。数值直接传给着色器的 uChannelMode uniform。
 * YUV 格式下 1/2/3 = Y/U/V；RGB 格式下 1/2/3/4 = R/G/B/A。
 */
enum class EChannelView
{
    Color = 0,
    Channel1 = 1,
    Channel2 = 2,
    Channel3 = 3,
    Channel4 = 4,
};

/**
 * 显示设置
 *
 * 只影响着色器 uniform 与 CPU 侧的 RGB 换算，不参与解码，切换时不需要重新加载文件。
 *
 * 挂在 FImageDocument 上而不是查看器上：并排对比两张图时它们可能来自
 * 不同的流水线环节，色彩标准或数值范围未必一致（比如一张 limited 一张 full），
 * 属性面板因此可以按图分别设置。
 *
 * **色彩描述是三轴正交的**（对齐 ffmpeg 的 colorspace / color_primaries / color_trc）：
 *
 *   ColorSpace  矩阵系数  —— YCbCr 怎么变成 R'G'B'（在非线性域做）
 *   Primaries   三原色    —— 线性域里 RGB 各代表什么颜色
 *   Transfer    传输函数  —— 码值与光强之间的曲线
 *
 * 一个典型的 HDR 素材是 (BT2020, BT2020, PQ)，一个普通的相机 NV21 dump 是
 * (BT601, BT601_625, SDR)。三者独立设置、互不推断 —— 恰恰是把它们混为一谈，
 * 才会出现“选了 BT.2020 色彩标准但画面还是又灰又发闷”这种结果 ——
 * 那是只改了矩阵系数，原色与传输函数两轴根本没动。
 */
struct FDisplaySettings
{
    // --- 三轴色彩描述 ---

    /// 矩阵系数（Kr/Kb）。只在 YUV 源上有意义
    EColorSpace ColorSpace = EColorSpace::BT601;

    /// 三原色。与显示器原色不同时会在线性域插入一个 3x3 转换
    EColorPrimaries Primaries = EColorPrimaries::BT709;

    /// 传输函数。非 SDR 时整条链路进入线性域，见 FColorPipeline
    EColorTransfer Transfer = EColorTransfer::SDR;

    /// 数值范围（Y 16-235 还是 0-255）
    EColorRange ColorRange = EColorRange::Limited;

    // --- HDR -> SDR 的呈现参数 ---

    /**
     * 参考白：绝对传输函数（PQ/HLG）下，多少 cd/m^2 算作"1.0"
     *
     * 默认 **203 nit**，取自 ITU-R BT.2408（HDR 制作实践里的图形白 / 漫反射白），
     * 也是 HLG 在 Lw=1000 时 75% 信号经 OOTF 后的落点。
     * ST 2084 本身只定义曲线、不定义漫反射白，那个"PQ = 100 nit"的说法来自
     * SDR 参考监视器的峰值，用它折算 HDR 素材会**过曝约 1 档**。
     *
     * 调大相当于整体压暗，调小相当于提亮。相对传输函数（SDR/BT1886/Linear）下
     * 这个值不参与计算 —— 那些曲线本来就不携带绝对亮度含义。
     */
    float ReferenceWhiteNits = 203.0f;

    /// HLG 素材的母版峰值亮度 Lw，决定 OOTF 的系统 gamma（1.2 + 0.42*log10(Lw/1000)）
    float HlgPeakNits = 1000.0f;

    /// 曝光补偿（档）。在线性域上乘 2^stops，纯调试用
    float ExposureStops = 0.0f;

    /// HDR -> SDR 的色调映射算子
    EToneMapOperator ToneMap = EToneMapOperator::Clip;

    /// Reinhard 算子里被映射到 1.0 的输入值（相对参考白的倍数）
    float ToneMapWhite = 4.0f;

    /**
     * 超范围高亮（false color）
     *
     * 在裁剪前判断：任一分量 > 显示上限画红色，任一分量 < 0（原色转换后落在目标色域外）画蓝色。
     * 判断"这段素材到底有没有超出显示器能力"，比反复调曲线有用得多。
     */
    bool bShowOutOfRange = false;

    // --- 通道隔离 ---

    EChannelView ChannelView = EChannelView::Color;
};
