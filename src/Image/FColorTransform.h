#pragma once

#include "FDisplaySettings.h"
#include "FImageFormat.h"
#include "FImageFormatDesc.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

/**
 * 色彩转换数学
 *
 * 本文件是**整条色彩管线唯一的真值来源**：着色器（gl/FShaders.h 里的 GLSL 前导块）
 * 与 CPU 侧（FImageSampler / 导出 / 直方图 / 差值）必须给出一致的结果，
 * 所以两边的公式与常数都以这里为准，改一处要同步改另一处。
 *
 * 完整链路（见 ApplyPipeline，GLSL 里对应 ApplyColorPipeline）：
 *
 *   YCbCr --BuildYuvToRgb--> R'G'B'(非线性)
 *         --EOTF-->           线性（相对参考白 或 绝对 cd/m^2）
 *         --归一化-->         1.0 = 参考白
 *         --原色 3x3-->       目标原色（线性域）
 *         --曝光-->
 *         --色调映射-->
 *         --OETF-->           显示编码值
 *
 * 第一步之后的部分只在 FColorPipeline::bEnabled 时执行；SDR 素材 + 原色一致时
 * 整条链路是恒等的，走直通路径，输出与本次改造之前**逐位一致**。
 */
namespace FColorTransform
{
    // =========================================================================
    // 矩阵系数（YCbCr -> R'G'B'）
    // =========================================================================

    /**
     * 各矩阵标准的亮度系数 Kr / Kb（Kg = 1 - Kr - Kb）
     */
    inline void GetLumaCoefficients(EColorSpace ColorSpace, float& OutKr, float& OutKb)
    {
        switch (ColorSpace)
        {
        case EColorSpace::BT709:
            OutKr = 0.2126f;
            OutKb = 0.0722f;
            break;

        case EColorSpace::BT2020:
            OutKr = 0.2627f;
            OutKb = 0.0593f;
            break;

        case EColorSpace::BT601:
        default:
            OutKr = 0.299f;
            OutKb = 0.114f;
            break;
        }
    }

    /**
     * 计算 YUV -> RGB 的 3x3 矩阵与采样偏移
     *
     * 着色器里的运算是：rgb = Matrix * (vec3(y, u, v) - Offset)
     *
     * 这一步**必须在非线性域做** —— YCbCr 承载的就是 R'G'B'（gamma/PQ 编码之后）的
     * 线性组合，先做 EOTF 再乘矩阵是错的。
     *
     * @param ColorSpace  矩阵标准
     * @param Range       数值范围。Limited 时按位深换算 16/235/128/224 的等比值
     * @param BitDepth    位深（8 / 10 / 12 / 16）。影响 Limited range 的黑电平与色度中心
     * @param OutMatrix   输出 9 个 float，**列主序**（可直接喂给 glUniformMatrix3fv 且 transpose=GL_FALSE）
     * @param OutOffset   输出 3 个 float，采样值在乘矩阵前要减去的偏移
     */
    inline void BuildYuvToRgb(
        EColorSpace ColorSpace,
        EColorRange Range,
        int32_t BitDepth,
        float OutMatrix[9],
        float OutOffset[3])
    {
        float Kr = 0.299f;
        float Kb = 0.114f;
        GetLumaCoefficients(ColorSpace, Kr, Kb);

        const float Kg = 1.0f - Kr - Kb;

        // 位深换算：8bit 的 16/235/128/224 在 10bit 下是 64/940/512/896
        const float multiplier = static_cast<float>(1 << (BitDepth - 8));
        const float maxValue = static_cast<float>((1 << BitDepth) - 1);

        const float black = 16.0f * multiplier;
        const float lumaRange = 219.0f * multiplier;
        const float chromaCenter = 128.0f * multiplier;
        const float chromaRange = 224.0f * multiplier;

        float lumaScale = 1.0f;
        float chromaScale = 1.0f;
        float lumaOffset = 0.0f;

        if (Range == EColorRange::Limited)
        {
            // 有效范围只占满量程的一部分，需要拉伸回 [0, 1]
            lumaScale = maxValue / lumaRange;
            chromaScale = maxValue / chromaRange;
            lumaOffset = black / maxValue;
        }

        OutOffset[0] = lumaOffset;
        OutOffset[1] = chromaCenter / maxValue;
        OutOffset[2] = chromaCenter / maxValue;

        // 标准的非恒定亮度 YCbCr 反变换：
        //   R = Y + 2(1-Kr) * V
        //   B = Y + 2(1-Kb) * U
        //   G = Y - 2Kb(1-Kb)/Kg * U - 2Kr(1-Kr)/Kg * V
        const float rV = chromaScale * 2.0f * (1.0f - Kr);
        const float bU = chromaScale * 2.0f * (1.0f - Kb);
        const float gU = -chromaScale * 2.0f * Kb * (1.0f - Kb) / Kg;
        const float gV = -chromaScale * 2.0f * Kr * (1.0f - Kr) / Kg;

        // 列主序：OutMatrix[col * 3 + row]
        // 第 0 列 = Y 的系数，第 1 列 = U 的系数，第 2 列 = V 的系数
        OutMatrix[0] = lumaScale;  OutMatrix[1] = lumaScale;  OutMatrix[2] = lumaScale;
        OutMatrix[3] = 0.0f;       OutMatrix[4] = gU;         OutMatrix[5] = bU;
        OutMatrix[6] = rV;         OutMatrix[7] = gV;         OutMatrix[8] = 0.0f;
    }

    /**
     * 该格式的有效位深，用于 Limited range 的黑电平换算
     */
    inline int32_t GetColorBitDepth(EImageFormat Format)
    {
        return FImageFormatDesc::Get(Format).BitDepth;
    }

    /**
     * 该格式的色度顺序是否颠倒（NV21 的 UV 平面是 V,U；YV12 的平面顺序是 Y,V,U）
     */
    inline bool IsUVSwapped(EImageFormat Format)
    {
        return FImageFormatDesc::Get(Format).bSwapChroma;
    }

    // =========================================================================
    // 三原色（线性域的 3x3 转换）
    // =========================================================================

    /**
     * 各标准的三原色与白点 CIE xy 坐标
     *
     * 目前支持的五套原色**白点全是 D65**，所以不需要色度适应（Bradford）。
     * 将来若加入 DCI-P3 剧院白（约 x=0.314, y=0.351）或 D50，必须补上适应矩阵，
     * 否则白色会偏。
     */
    inline void GetPrimariesXY(
        EColorPrimaries Primaries,
        float OutRed[2],
        float OutGreen[2],
        float OutBlue[2],
        float OutWhite[2])
    {
        // D65
        OutWhite[0] = 0.3127f;
        OutWhite[1] = 0.3290f;

        switch (Primaries)
        {
        case EColorPrimaries::BT2020:
            OutRed[0]   = 0.708f; OutRed[1]   = 0.292f;
            OutGreen[0] = 0.170f; OutGreen[1] = 0.797f;
            OutBlue[0]  = 0.131f; OutBlue[1]  = 0.046f;
            break;

        case EColorPrimaries::DisplayP3:
            OutRed[0]   = 0.680f; OutRed[1]   = 0.320f;
            OutGreen[0] = 0.265f; OutGreen[1] = 0.690f;
            OutBlue[0]  = 0.150f; OutBlue[1]  = 0.060f;
            break;

        case EColorPrimaries::BT601_525:   // SMPTE 170M
            OutRed[0]   = 0.630f; OutRed[1]   = 0.340f;
            OutGreen[0] = 0.310f; OutGreen[1] = 0.595f;
            OutBlue[0]  = 0.155f; OutBlue[1]  = 0.070f;
            break;

        case EColorPrimaries::BT601_625:   // EBU 3213
            OutRed[0]   = 0.640f; OutRed[1]   = 0.330f;
            OutGreen[0] = 0.290f; OutGreen[1] = 0.600f;
            OutBlue[0]  = 0.150f; OutBlue[1]  = 0.060f;
            break;

        case EColorPrimaries::BT709:
        default:
            OutRed[0]   = 0.640f; OutRed[1]   = 0.330f;
            OutGreen[0] = 0.300f; OutGreen[1] = 0.600f;
            OutBlue[0]  = 0.150f; OutBlue[1]  = 0.060f;
            break;
        }
    }

    /// 列主序 3x3 求逆。行列式为 0 时返回 false 并写入单位阵
    inline bool Invert3x3(const float In[9], float Out[9])
    {
        // In[col * 3 + row]，按行展开成 a b c / d e f / g h i
        const float a = In[0], d = In[1], g = In[2];   // 第 0 列
        const float b = In[3], e = In[4], h = In[5];   // 第 1 列
        const float c = In[6], f = In[7], i = In[8];   // 第 2 列

        const float A =  (e * i - f * h);
        const float B = -(d * i - f * g);
        const float C =  (d * h - e * g);

        const float det = a * A + b * B + c * C;

        if (std::fabs(det) < 1e-12f)
        {
            for (int32_t k = 0; k < 9; ++k)
            {
                Out[k] = (k % 4 == 0) ? 1.0f : 0.0f;
            }

            return false;
        }

        const float invDet = 1.0f / det;

        Out[0] = A * invDet;
        Out[1] = B * invDet;
        Out[2] = C * invDet;
        Out[3] = -(b * i - c * h) * invDet;
        Out[4] =  (a * i - c * g) * invDet;
        Out[5] = -(a * h - b * g) * invDet;
        Out[6] =  (b * f - c * e) * invDet;
        Out[7] = -(a * f - c * d) * invDet;
        Out[8] =  (a * e - b * d) * invDet;

        return true;
    }

    /// 列主序 3x3 相乘：Out = A * B
    inline void Multiply3x3(const float A[9], const float B[9], float Out[9])
    {
        float tmp[9];

        for (int32_t col = 0; col < 3; ++col)
        {
            for (int32_t row = 0; row < 3; ++row)
            {
                float sum = 0.0f;

                for (int32_t k = 0; k < 3; ++k)
                {
                    sum += A[k * 3 + row] * B[col * 3 + k];
                }

                tmp[col * 3 + row] = sum;
            }
        }

        for (int32_t k = 0; k < 9; ++k)
        {
            Out[k] = tmp[k];
        }
    }

    /**
     * 由三原色 xy 坐标现算线性 RGB -> CIE XYZ 的矩阵（列主序）
     *
     * 和 BuildYuvToRgb 一样不硬编码 9 个数：给定 xy 就能推出来的东西没有必要抄进代码，
     * 抄错了还很难发现。
     */
    inline void BuildRgbToXyz(EColorPrimaries Primaries, float OutMatrix[9])
    {
        float r[2], g[2], b[2], w[2];
        GetPrimariesXY(Primaries, r, g, b, w);

        // 每个原色归一到 Y=1 时的 XYZ
        const float Xr = r[0] / r[1], Yr = 1.0f, Zr = (1.0f - r[0] - r[1]) / r[1];
        const float Xg = g[0] / g[1], Yg = 1.0f, Zg = (1.0f - g[0] - g[1]) / g[1];
        const float Xb = b[0] / b[1], Yb = 1.0f, Zb = (1.0f - b[0] - b[1]) / b[1];

        // 列主序：第 0 列是 R 原色
        const float M[9] = { Xr, Yr, Zr,  Xg, Yg, Zg,  Xb, Yb, Zb };

        float Minv[9];
        Invert3x3(M, Minv);

        // 白点 XYZ（Y = 1）
        const float Xw = w[0] / w[1];
        const float Yw = 1.0f;
        const float Zw = (1.0f - w[0] - w[1]) / w[1];

        // S = M^-1 * Wxyz，得到让 RGB(1,1,1) 恰好落在白点上的三个缩放因子
        const float Sr = Minv[0] * Xw + Minv[3] * Yw + Minv[6] * Zw;
        const float Sg = Minv[1] * Xw + Minv[4] * Yw + Minv[7] * Zw;
        const float Sb = Minv[2] * Xw + Minv[5] * Yw + Minv[8] * Zw;

        OutMatrix[0] = M[0] * Sr; OutMatrix[1] = M[1] * Sr; OutMatrix[2] = M[2] * Sr;
        OutMatrix[3] = M[3] * Sg; OutMatrix[4] = M[4] * Sg; OutMatrix[5] = M[5] * Sg;
        OutMatrix[6] = M[6] * Sb; OutMatrix[7] = M[7] * Sb; OutMatrix[8] = M[8] * Sb;
    }

    /**
     * 线性域的原色转换矩阵：源原色 RGB -> 目标原色 RGB（列主序）
     *
     * 结果可能带负分量 —— BT.2020 里有些颜色 BT.709 根本表示不出来。
     * SDR 输出时必须裁剪（可用“超范围高亮”把它标出来）；
     * scRGB HDR 输出时**要保留负值**，那正是广色域在 scRGB 里的表达方式。
     *
     * 效果方向：BT.709 原色在 BT.2020 色域内部，所以 2020 -> 709 的矩阵会把坐标**拉开**
     * （对角线上是 1.66 / 1.13 / 1.12，非对角是负数）。不做这一步，BT.2020 素材在
     * 普通屏上看起来是发闷、欠饱和的，而不是过饱和。
     */
    inline void BuildPrimariesMatrix(EColorPrimaries Src, EColorPrimaries Dst, float OutMatrix[9])
    {
        float srcToXyz[9];
        float dstToXyz[9];
        float xyzToDst[9];

        BuildRgbToXyz(Src, srcToXyz);
        BuildRgbToXyz(Dst, dstToXyz);
        Invert3x3(dstToXyz, xyzToDst);

        Multiply3x3(xyzToDst, srcToXyz, OutMatrix);
    }

    /**
     * 该原色下的亮度系数（RGB->XYZ 矩阵的 Y 行），HLG 的 OOTF 需要
     */
    inline void GetPrimariesLumaCoef(EColorPrimaries Primaries, float OutCoef[3])
    {
        float m[9];
        BuildRgbToXyz(Primaries, m);

        // 列主序，Y 是第 1 行
        OutCoef[0] = m[1];
        OutCoef[1] = m[4];
        OutCoef[2] = m[7];
    }

    // =========================================================================
    // 传输函数
    // =========================================================================

    /// PQ / HLG 携带绝对亮度，其余传输函数是相对的
    inline bool IsAbsoluteTransfer(EColorTransfer Transfer)
    {
        return Transfer == EColorTransfer::PQ || Transfer == EColorTransfer::HLG;
    }

    /**
     * SMPTE ST 2084 (PQ) EOTF：码值 -> 绝对亮度 cd/m^2
     *
     * 常数是标准里的精确有理数，别换成四舍五入过的版本。
     */
    inline float PqEotfNits(float V)
    {
        const float m1 = 0.1593017578125f;   // 2610 / 16384
        const float m2 = 78.84375f;          // 2523 / 4096 * 128
        const float c1 = 0.8359375f;         // 3424 / 4096
        const float c2 = 18.8515625f;        // 2413 / 4096 * 32
        const float c3 = 18.6875f;           // 2392 / 4096 * 32

        const float v = std::max(V, 0.0f);
        const float p = std::pow(v, 1.0f / m2);
        const float num = std::max(p - c1, 0.0f);
        const float den = c2 - c3 * p;

        if (den <= 1e-6f)
        {
            return 10000.0f;
        }

        return 10000.0f * std::pow(num / den, 1.0f / m1);
    }

    /// PQ 逆 EOTF：绝对亮度 cd/m^2 -> 码值
    inline float PqInverseEotf(float Nits)
    {
        const float m1 = 0.1593017578125f;
        const float m2 = 78.84375f;
        const float c1 = 0.8359375f;
        const float c2 = 18.8515625f;
        const float c3 = 18.6875f;

        const float y = std::min(std::max(Nits, 0.0f) / 10000.0f, 1.0f);
        const float ym = std::pow(y, m1);

        return std::pow((c1 + c2 * ym) / (1.0f + c3 * ym), m2);
    }

    /**
     * HLG 的 OETF 逆变换：码值 -> 场景线性 [0, 1]
     *
     * 这一步只到场景光，还要经过 OOTF 才是显示光，见 ApplyHlgOotf。
     */
    inline float HlgSceneLinear(float V)
    {
        const float a = 0.17883277f;
        const float b = 0.28466892f;   // 1 - 4a
        const float c = 0.55991073f;   // 0.5 - a * ln(4a)

        const float v = std::max(V, 0.0f);

        if (v <= 0.5f)
        {
            return (v * v) / 3.0f;
        }

        return (std::exp((v - c) / a) + b) / 12.0f;
    }

    /**
     * HLG 的 OOTF：场景线性 -> 显示线性（cd/m^2）
     *
     * 系统 gamma 由母版峰值亮度决定；亮度用**源原色**的系数算，
     * 因此三个分量共用同一个缩放因子 —— 这正是 HLG 与 PQ 在结构上的区别。
     */
    inline void ApplyHlgOotf(float PeakNits, const float LumaCoef[3], float InOutRgb[3])
    {
        const float lw = std::max(PeakNits, 1.0f);

        // Lw 很小时 gamma 会掉到 1 以下，那样 pow(0, 负数) 会变成 inf
        const float gamma = std::min(std::max(1.2f + 0.42f * std::log10(lw / 1000.0f), 1.0f), 2.0f);

        const float ys = std::max(
            LumaCoef[0] * InOutRgb[0] + LumaCoef[1] * InOutRgb[1] + LumaCoef[2] * InOutRgb[2],
            0.0f);

        const float scale = lw * std::pow(ys, gamma - 1.0f);

        InOutRgb[0] *= scale;
        InOutRgb[1] *= scale;
        InOutRgb[2] *= scale;
    }

    /// sRGB OETF：线性 -> 编码
    inline float SrgbEncode(float L)
    {
        if (L <= 0.0031308f)
        {
            return 12.92f * L;
        }

        return 1.055f * std::pow(L, 1.0f / 2.4f) - 0.055f;
    }

    /// sRGB EOTF：编码 -> 线性
    inline float SrgbDecode(float V)
    {
        if (V <= 0.04045f)
        {
            return V / 12.92f;
        }

        return std::pow((V + 0.055f) / 1.055f, 2.4f);
    }

    /**
     * 扩展 sRGB 编码（保号，可超出 [0,1]）
     *
     * HDR 呈现路径上的 fp16 帧缓冲用的就是这个约定：
     * **扩展 sRGB 编码，1.0 = 显示器 SDR 白电平**。
     *
     * 这样 ImGui 的界面（本来就写 0-1 的 sRGB 值）一个字节都不用改就是对的，
     * 只有图像内容需要写出 >1 的值。详见 Core/FHdrPresenter.h 的说明。
     */
    inline float SrgbEncodeExtended(float L)
    {
        const float s = (L < 0.0f) ? -1.0f : 1.0f;

        return s * SrgbEncode(std::fabs(L));
    }

    /// 扩展 sRGB 解码（保号）
    inline float SrgbDecodeExtended(float V)
    {
        const float s = (V < 0.0f) ? -1.0f : 1.0f;

        return s * SrgbDecode(std::fabs(V));
    }

    // =========================================================================
    // 色调映射
    // =========================================================================

    /// 扩展 Reinhard：把 White 映射到 1.0，低光部分近似线性
    inline float ToneMapReinhard(float X, float White)
    {
        const float w = std::max(White, 1e-4f);

        return X * (1.0f + X / (w * w)) / (1.0f + X);
    }

    /// Narkowicz 2015 的 ACES filmic 拟合曲线
    inline float ToneMapACES(float X)
    {
        const float a = 2.51f;
        const float b = 0.03f;
        const float c = 2.43f;
        const float d = 0.59f;
        const float e = 0.14f;

        return (X * (a * X + b)) / (X * (c * X + d) + e);
    }

    inline float ApplyToneMap(EToneMapOperator Op, float X, float White)
    {
        switch (Op)
        {
        case EToneMapOperator::Reinhard:
            return ToneMapReinhard(X, White);

        case EToneMapOperator::ACES:
            return ToneMapACES(X);

        case EToneMapOperator::Clip:
        default:
            return X;
        }
    }

    /**
     * 保色相地把线性值限制到上限
     *
     * 逐通道 min() 会让饱和高光**偏色**：(6, 2, 1) 在上限 1.86 下被裁成 (1.86, 2, 1)，
     * 红分量反而低于绿，颜色直接翻掉。按最大分量整体缩放则只压亮度、不动色度。
     *
     * 负分量随之等比缩放而**不被抬起** —— 那是 scRGB 表达目标色域外颜色的方式，
     * 抬成 0 等于悄悄做了一次 gamut clip。
     */
    inline void LimitToPeakPreserveHue(float InOutRgb[3], float UpperLimit)
    {
        const float peak = std::max(InOutRgb[0], std::max(InOutRgb[1], InOutRgb[2]));

        if (peak <= UpperLimit)
        {
            return;
        }

        // UpperLimit >= 1，所以走到这里必有 peak > 1 > 0，除法安全
        const float scale = UpperLimit / peak;

        for (int32_t c = 0; c < 3; ++c)
        {
            InOutRgb[c] *= scale;
        }
    }

    // =========================================================================
    // 完整管线
    // =========================================================================

    /**
     * 呈现目标的能力描述
     *
     * CPU 侧（导出/直方图/差值/像素探针）永远用默认值 —— 那几条路都通向 8bit sRGB，
     * 与显示器是不是 HDR 无关。只有屏幕上的实时渲染会填入真实的显示器信息。
     */
    struct FDisplayOutput
    {
        /// true 表示输出到 scRGB fp16 的 HDR 帧缓冲
        bool bHdr = false;

        /// 显示器当前的 SDR 白电平（cd/m^2）。HDR 帧缓冲里 1.0 就是这个亮度
        float SdrWhiteNits = 200.0f;

        /// 显示器峰值亮度（cd/m^2），HDR 输出时的上限
        float MaxNits = 1000.0f;

        /// 显示器原色。scRGB 与 sRGB 都是 BT.709
        EColorPrimaries Primaries = EColorPrimaries::BT709;
    };

    /**
     * 展开好的管线参数
     *
     * 字段刻意全是 int/float/数组：这个结构体既喂 CPU 侧的 ApplyPipeline，
     * 也逐字段喂着色器 uniform，两边看到的是同一份数字。
     */
    struct FColorPipeline
    {
        /**
         * 整条链路是否需要执行
         *
         * false 时 ApplyPipeline 只做一次 [0,1] 裁剪，与本次改造之前的行为逐位一致。
         * SDR 素材 + 原色一致 + 无曝光补偿时就是这种情况，也就是绝大多数打开的文件。
         */
        bool bEnabled = false;

        int32_t Transfer = static_cast<int32_t>(EColorTransfer::SDR);
        int32_t bAbsoluteTransfer = 0;

        int32_t bApplyPrimaries = 0;
        float PrimariesMatrix[9] = { 1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f };

        /// 源原色下的亮度系数，HLG 的 OOTF 用
        float LumaCoef[3] = { 0.2126f, 0.7152f, 0.0722f };

        /// 绝对传输函数下把 cd/m^2 归一到 1.0 的除数
        float NormalizeNits = 100.0f;

        float HlgPeakNits = 1000.0f;
        float ExposureScale = 1.0f;

        int32_t ToneMap = static_cast<int32_t>(EToneMapOperator::Clip);
        float ToneMapWhite = 4.0f;

        /// 0 = SDR（裁剪到 [0,1] 后 sRGB 编码），1 = HDR（保留负值，扩展 sRGB 编码）
        int32_t OutputMode = 0;

        /// HDR 输出时的上限（显示器峰值 / SDR 白）。SDR 输出时恒为 1
        float MaxOutputScale = 1.0f;

        int32_t bShowOutOfRange = 0;
    };

    /**
     * 由显示设置与输出目标展开管线参数
     */
    inline FColorPipeline BuildPipeline(
        const FDisplaySettings& Display,
        const FDisplayOutput& Output = FDisplayOutput())
    {
        FColorPipeline p;

        p.Transfer = static_cast<int32_t>(Display.Transfer);
        p.bAbsoluteTransfer = IsAbsoluteTransfer(Display.Transfer) ? 1 : 0;
        p.HlgPeakNits = std::max(Display.HlgPeakNits, 1.0f);
        p.ExposureScale = std::pow(2.0f, Display.ExposureStops);
        p.ToneMap = static_cast<int32_t>(Display.ToneMap);
        p.ToneMapWhite = std::max(Display.ToneMapWhite, 1.0f);
        p.bShowOutOfRange = Display.bShowOutOfRange ? 1 : 0;
        p.OutputMode = Output.bHdr ? 1 : 0;

        GetPrimariesLumaCoef(Display.Primaries, p.LumaCoef);

        // 绝对传输函数的归一化除数：
        //   SDR 输出 -> 用户设的参考白（内容的漫反射白，默认 BT.2408 的 203nit，
        //               把它显示成屏幕白）
        //   HDR 输出 -> 显示器的 SDR 白电平。这样 nits 被**原样**送到面板上，
        //               PQ 的绝对亮度语义才成立
        p.NormalizeNits = Output.bHdr
            ? std::max(Output.SdrWhiteNits, 1.0f)
            : std::max(Display.ReferenceWhiteNits, 1.0f);

        p.MaxOutputScale = Output.bHdr
            ? std::max(Output.MaxNits / std::max(Output.SdrWhiteNits, 1.0f), 1.0f)
            : 1.0f;

        const bool bPrimariesDiffer = (Display.Primaries != Output.Primaries);

        if (bPrimariesDiffer)
        {
            BuildPrimariesMatrix(Display.Primaries, Output.Primaries, p.PrimariesMatrix);
            p.bApplyPrimaries = 1;
        }

        // 这三个条件之外，整条链路恒等，直通即可
        p.bEnabled = (Display.Transfer != EColorTransfer::SDR)
                  || bPrimariesDiffer
                  || (Display.ExposureStops != 0.0f);

        return p;
    }

    /**
     * 在 CPU 侧执行整条管线（着色器里的 ApplyColorPipeline 与此逐步对应）
     *
     * @param InOutRgb      输入非线性 R'G'B'，原地输出显示编码值
     * @param OutLinearNits 可选，输出归一化前的亮度（相对传输函数下按参考白折算一个等效值），
     *                      像素探针拿它显示 nits 读数
     */
    inline void ApplyPipeline(const FColorPipeline& P, float InOutRgb[3], float* OutLinearNits = nullptr)
    {
        if (!P.bEnabled)
        {
            // 直通路径下"超范围"就是超出显示编码值域 [0,1]。limited range 的超白
            // （Y > 235）与超黑（Y < 16）经矩阵拉伸后正落在这里，而那恰恰是这个功能
            // 最该抓的场景之一 —— 判定必须在裁剪之前，裁完信息就没了
            bool bOver = false;
            bool bUnder = false;

            if (P.bShowOutOfRange != 0)
            {
                for (int32_t c = 0; c < 3; ++c)
                {
                    bOver  = bOver  || (InOutRgb[c] >  1.0f + 1e-4f);
                    bUnder = bUnder || (InOutRgb[c] < -1e-4f);
                }
            }

            // 直通：非线性 R'G'B' 本来就是显示编码值，原样裁剪即可
            for (int32_t c = 0; c < 3; ++c)
            {
                InOutRgb[c] = std::min(std::max(InOutRgb[c], 0.0f), 1.0f);
            }

            // 亮度读数按裁剪后的值算，与引入高亮之前一致 —— 探针不受高亮影响
            if (OutLinearNits)
            {
                const float lum = P.LumaCoef[0] * SrgbDecode(InOutRgb[0])
                                + P.LumaCoef[1] * SrgbDecode(InOutRgb[1])
                                + P.LumaCoef[2] * SrgbDecode(InOutRgb[2]);

                *OutLinearNits = lum * P.NormalizeNits;
            }

            // 高亮色本身也是显示编码值，在 HDR 帧缓冲的约定下同样成立（1.0 = SDR 白）
            if (bOver)
            {
                InOutRgb[0] = 1.0f; InOutRgb[1] = 0.0f; InOutRgb[2] = 0.0f;
            }
            else if (bUnder)
            {
                InOutRgb[0] = 0.0f; InOutRgb[1] = 0.4f; InOutRgb[2] = 1.0f;
            }

            return;
        }

        const EColorTransfer transfer = static_cast<EColorTransfer>(P.Transfer);

        float lin[3];

        // --- 1. EOTF ---
        switch (transfer)
        {
        case EColorTransfer::PQ:
            for (int32_t c = 0; c < 3; ++c)
            {
                lin[c] = PqEotfNits(InOutRgb[c]);
            }
            break;

        case EColorTransfer::HLG:
            for (int32_t c = 0; c < 3; ++c)
            {
                lin[c] = HlgSceneLinear(InOutRgb[c]);
            }
            ApplyHlgOotf(P.HlgPeakNits, P.LumaCoef, lin);
            break;

        case EColorTransfer::BT1886:
            for (int32_t c = 0; c < 3; ++c)
            {
                lin[c] = std::pow(std::max(InOutRgb[c], 0.0f), 2.4f);
            }
            break;

        case EColorTransfer::Linear:
            for (int32_t c = 0; c < 3; ++c)
            {
                lin[c] = InOutRgb[c];
            }
            break;

        case EColorTransfer::SDR:
        default:
            for (int32_t c = 0; c < 3; ++c)
            {
                lin[c] = SrgbDecode(InOutRgb[c]);
            }
            break;
        }

        if (OutLinearNits)
        {
            const float lum = P.LumaCoef[0] * lin[0] + P.LumaCoef[1] * lin[1] + P.LumaCoef[2] * lin[2];

            // 相对传输函数没有绝对亮度含义，按参考白折算一个等效值
            *OutLinearNits = (P.bAbsoluteTransfer != 0) ? lum : (lum * P.NormalizeNits);
        }

        // --- 2. 归一化到 "1.0 = 参考白" ---
        if (P.bAbsoluteTransfer != 0)
        {
            const float inv = 1.0f / P.NormalizeNits;

            for (int32_t c = 0; c < 3; ++c)
            {
                lin[c] *= inv;
            }
        }

        // --- 3. 原色转换（线性域）---
        if (P.bApplyPrimaries != 0)
        {
            const float r = lin[0];
            const float g = lin[1];
            const float b = lin[2];

            for (int32_t row = 0; row < 3; ++row)
            {
                lin[row] = P.PrimariesMatrix[0 * 3 + row] * r
                         + P.PrimariesMatrix[1 * 3 + row] * g
                         + P.PrimariesMatrix[2 * 3 + row] * b;
            }
        }

        // --- 4. 曝光 ---
        for (int32_t c = 0; c < 3; ++c)
        {
            lin[c] *= P.ExposureScale;
        }

        // --- 5. 超范围判定（在裁剪之前）---
        const float upperLimit = (P.OutputMode != 0) ? P.MaxOutputScale : 1.0f;

        if (P.bShowOutOfRange != 0)
        {
            bool bOver = false;
            bool bUnder = false;

            for (int32_t c = 0; c < 3; ++c)
            {
                bOver  = bOver  || (lin[c] > upperLimit + 1e-4f);
                bUnder = bUnder || (lin[c] < -1e-4f);
            }

            if (bOver)
            {
                InOutRgb[0] = 1.0f; InOutRgb[1] = 0.0f; InOutRgb[2] = 0.0f;

                return;
            }

            if (bUnder)
            {
                InOutRgb[0] = 0.0f; InOutRgb[1] = 0.4f; InOutRgb[2] = 1.0f;

                return;
            }
        }

        // --- 6. 色调映射 + 输出编码 ---
        if (P.OutputMode != 0)
        {
            // HDR：不做 SDR 色调映射，只限制到显示器峰值；**负值保留**，那是广色域的表达。
            // 限幅保色相，逐通道裁会让饱和高光翻色，见 LimitToPeakPreserveHue
            LimitToPeakPreserveHue(lin, upperLimit);

            for (int32_t c = 0; c < 3; ++c)
            {
                InOutRgb[c] = SrgbEncodeExtended(lin[c]);
            }

            return;
        }

        const EToneMapOperator op = static_cast<EToneMapOperator>(P.ToneMap);

        for (int32_t c = 0; c < 3; ++c)
        {
            const float mapped = ApplyToneMap(op, std::max(lin[c], 0.0f), P.ToneMapWhite);

            InOutRgb[c] = SrgbEncode(std::min(std::max(mapped, 0.0f), 1.0f));
        }
    }
}
