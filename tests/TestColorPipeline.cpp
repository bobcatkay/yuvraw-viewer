// 色彩管线自检
//
// 覆盖 Image/FColorTransform.h 里那条 EOTF -> 原色 -> 色调映射 -> OETF 的链路。
//
// 这些数学**必须能对上公开标准**，而不是"看起来差不多"：
// PQ 常数抄错一位、原色矩阵求逆写反一个分量，画面都还能出图，只是颜色悄悄偏掉，
// 肉眼根本发现不了。所以这里全部拿标准里的参考值做断言。
//
// 同样重要的是最后那组"直通恒等"断言：SDR 素材 + 原色一致时整条链路必须是
// 纯裁剪，与本次改造之前逐位一致 —— 否则每个已有的 .yuv 都会悄悄变色。

#include "Image/FColorTransform.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    int32_t gFailures = 0;

    void Check(const char* Label, bool bCondition, const char* Detail = "")
    {
        std::printf("%-52s %s %s\n", Label, bCondition ? "OK" : "FAIL", Detail);

        if (!bCondition)
        {
            ++gFailures;
        }
    }

    void CheckNear(const char* Label, double Actual, double Expected, double Tolerance)
    {
        const bool bOk = std::fabs(Actual - Expected) <= Tolerance;

        char detail[160];
        std::snprintf(detail, sizeof(detail), "got=%.6f expected=%.6f tol=%.6f", Actual, Expected, Tolerance);

        Check(Label, bOk, detail);
    }

    // =========================================================================

    void TestPq()
    {
        std::printf("\n=== PQ (SMPTE ST 2084) ===\n");

        // 标准里的锚点：码值 1.0 就是 10000 nit
        CheckNear(u8"码值 1.0 -> 10000 nit", FColorTransform::PqEotfNits(1.0f), 10000.0, 1.0);

        CheckNear(u8"码值 0.0 -> 0 nit", FColorTransform::PqEotfNits(0.0f), 0.0, 1e-4);

        // 广为引用的中点：PQ 0.5 约等于 92.2 nit。
        // 这个值对常数极其敏感 —— m1/m2/c1/c2/c3 任何一个抄错都过不了
        CheckNear(u8"码值 0.5 -> 约 92.2 nit", FColorTransform::PqEotfNits(0.5f), 92.245, 0.05);

        // 100 nit（标准漫反射白）对应的码值，常见文献给的是 0.5081
        CheckNear(u8"100 nit -> 码值 0.5081", FColorTransform::PqInverseEotf(100.0f), 0.50808, 0.0005);

        // 往返
        const float nits[] = { 0.1f, 1.0f, 10.0f, 100.0f, 1000.0f, 4000.0f, 10000.0f };

        bool bRoundTripOk = true;

        for (float n : nits)
        {
            const float back = FColorTransform::PqEotfNits(FColorTransform::PqInverseEotf(n));

            // PQ 在高亮端很陡，用相对误差判定
            if (std::fabs(back - n) > n * 0.001f + 1e-4f)
            {
                bRoundTripOk = false;
                std::printf("    往返失配: %.4f -> %.4f\n", n, back);
            }
        }

        Check(u8"EOTF / 逆 EOTF 往返", bRoundTripOk);

        // 单调递增：非单调会让渐变出现台阶
        bool bMonotonic = true;
        float previous = -1.0f;

        for (int32_t i = 0; i <= 1023; ++i)
        {
            const float value = FColorTransform::PqEotfNits(static_cast<float>(i) / 1023.0f);

            if (value < previous)
            {
                bMonotonic = false;
                break;
            }

            previous = value;
        }

        Check(u8"10bit 全码值单调递增", bMonotonic);
    }

    void TestHlg()
    {
        std::printf("\n=== HLG (BT.2100) ===\n");

        // OETF 逆变换的两个端点
        CheckNear(u8"码值 0.0 -> 场景线性 0", FColorTransform::HlgSceneLinear(0.0f), 0.0, 1e-6);
        CheckNear(u8"码值 1.0 -> 场景线性 1", FColorTransform::HlgSceneLinear(1.0f), 1.0, 1e-4);

        // 分段点 V=0.5 处两支必须接上，否则中灰附近会出现可见的折线
        const float lower = 0.5f * 0.5f / 3.0f;
        CheckNear(u8"分段点 V=0.5 两支连续", FColorTransform::HlgSceneLinear(0.5f), lower, 1e-5);

        const float justAbove = FColorTransform::HlgSceneLinear(0.5001f);
        CheckNear(u8"分段点右侧连续", justAbove, lower, 1e-3);

        // OOTF：白场 + Lw=1000 应当正好给出 1000 nit
        float lumaCoef[3];
        FColorTransform::GetPrimariesLumaCoef(EColorPrimaries::BT2020, lumaCoef);

        float white[3] = { 1.0f, 1.0f, 1.0f };
        FColorTransform::ApplyHlgOotf(1000.0f, lumaCoef, white);

        CheckNear(u8"白场 OOTF (Lw=1000) -> 1000 nit", white[0], 1000.0, 0.5);

        // 黑场不能变成 NaN / inf（pow(0, 负数) 是这里最容易踩的坑）
        float black[3] = { 0.0f, 0.0f, 0.0f };
        FColorTransform::ApplyHlgOotf(1000.0f, lumaCoef, black);

        Check(u8"黑场 OOTF 不产生 NaN/inf",
              std::isfinite(black[0]) && std::isfinite(black[1]) && std::isfinite(black[2]));
    }

    void TestSrgb()
    {
        std::printf("\n=== sRGB 传输函数 ===\n");

        CheckNear(u8"编码 0 -> 0", FColorTransform::SrgbEncode(0.0f), 0.0, 1e-6);
        CheckNear(u8"编码 1 -> 1", FColorTransform::SrgbEncode(1.0f), 1.0, 1e-5);

        // 中灰的经典值：线性 0.2140 附近对应编码 0.5
        CheckNear(u8"解码 0.5 -> 线性 0.2140", FColorTransform::SrgbDecode(0.5f), 0.21404, 0.0002);

        bool bRoundTripOk = true;

        for (int32_t i = 0; i <= 255; ++i)
        {
            const float v = static_cast<float>(i) / 255.0f;
            const float back = FColorTransform::SrgbEncode(FColorTransform::SrgbDecode(v));

            if (std::fabs(back - v) > 1e-4f)
            {
                bRoundTripOk = false;
                break;
            }
        }

        Check(u8"编码 / 解码往返（8bit 全码值）", bRoundTripOk);

        // 扩展版必须保号，且在 [0,1] 内与普通版一致 ——
        // HDR 帧缓冲靠负值表达广色域，符号丢了颜色就错了
        CheckNear(u8"扩展编码保号", FColorTransform::SrgbEncodeExtended(-0.5f),
                  -FColorTransform::SrgbEncode(0.5f), 1e-6);

        CheckNear(u8"扩展编码 / 解码往返 (2.5)",
                  FColorTransform::SrgbDecodeExtended(FColorTransform::SrgbEncodeExtended(2.5f)),
                  2.5, 1e-3);

        CheckNear(u8"扩展编码 / 解码往返 (-0.3)",
                  FColorTransform::SrgbDecodeExtended(FColorTransform::SrgbEncodeExtended(-0.3f)),
                  -0.3, 1e-4);
    }

    void TestPrimaries()
    {
        std::printf("\n=== 三原色矩阵 ===\n");

        // 同一套原色之间必须是单位阵
        float identity[9];
        FColorTransform::BuildPrimariesMatrix(EColorPrimaries::BT709, EColorPrimaries::BT709, identity);

        bool bIsIdentity = true;

        for (int32_t i = 0; i < 9; ++i)
        {
            const float expected = (i % 4 == 0) ? 1.0f : 0.0f;

            if (std::fabs(identity[i] - expected) > 1e-4f)
            {
                bIsIdentity = false;
                break;
            }
        }

        Check(u8"BT.709 -> BT.709 为单位阵", bIsIdentity);

        // BT.2020 -> BT.709 的公开值（D65，无色度适应）
        //   1.6605  -0.5876  -0.0728
        //  -0.1246   1.1329  -0.0083
        //  -0.0182  -0.1006   1.1187
        float m[9];
        FColorTransform::BuildPrimariesMatrix(EColorPrimaries::BT2020, EColorPrimaries::BT709, m);

        const float expected[9] = {
            // 列主序：第 0 列 = R 的贡献
             1.6605f, -0.1246f, -0.0182f,
            -0.5876f,  1.1329f, -0.1006f,
            -0.0728f, -0.0083f,  1.1187f
        };

        bool bMatches = true;

        for (int32_t i = 0; i < 9; ++i)
        {
            if (std::fabs(m[i] - expected[i]) > 0.002f)
            {
                bMatches = false;
                std::printf("    [%d] got=%.4f expected=%.4f\n", i, m[i], expected[i]);
            }
        }

        Check(u8"BT.2020 -> BT.709 匹配公开值", bMatches);

        // 白点必须守恒：任何一对同白点原色之间，(1,1,1) 都要映回 (1,1,1)。
        // 这条不成立就说明白点缩放那步写错了，画面会整体偏色
        const float r = m[0] + m[3] + m[6];
        const float g = m[1] + m[4] + m[7];
        const float b = m[2] + m[5] + m[8];

        CheckNear(u8"白点守恒 R", r, 1.0, 0.002);
        CheckNear(u8"白点守恒 G", g, 1.0, 0.002);
        CheckNear(u8"白点守恒 B", b, 1.0, 0.002);

        // 往返：2020 -> 709 -> 2020 应当还原
        float back[9];
        float roundTrip[9];
        FColorTransform::BuildPrimariesMatrix(EColorPrimaries::BT709, EColorPrimaries::BT2020, back);
        FColorTransform::Multiply3x3(back, m, roundTrip);

        bool bRoundTripIdentity = true;

        for (int32_t i = 0; i < 9; ++i)
        {
            const float want = (i % 4 == 0) ? 1.0f : 0.0f;

            if (std::fabs(roundTrip[i] - want) > 1e-3f)
            {
                bRoundTripIdentity = false;
                break;
            }
        }

        Check(u8"BT.2020 -> BT.709 -> BT.2020 还原", bRoundTripIdentity);

        // 亮度系数：BT.709 的 Y 行就是 0.2126 / 0.7152 / 0.0722
        float luma709[3];
        FColorTransform::GetPrimariesLumaCoef(EColorPrimaries::BT709, luma709);

        CheckNear(u8"BT.709 亮度系数 R", luma709[0], 0.2126, 0.001);
        CheckNear(u8"BT.709 亮度系数 G", luma709[1], 0.7152, 0.001);
        CheckNear(u8"BT.709 亮度系数 B", luma709[2], 0.0722, 0.001);

        float luma2020[3];
        FColorTransform::GetPrimariesLumaCoef(EColorPrimaries::BT2020, luma2020);

        CheckNear(u8"BT.2020 亮度系数 R", luma2020[0], 0.2627, 0.001);
        CheckNear(u8"BT.2020 亮度系数 G", luma2020[1], 0.6780, 0.001);
        CheckNear(u8"BT.2020 亮度系数 B", luma2020[2], 0.0593, 0.001);
    }

    void TestPassthrough()
    {
        std::printf("\n=== 直通恒等（最重要的一条回归保护）===\n");

        // 默认设置 = SDR + BT.709 原色 + 无曝光补偿，管线必须整体关闭
        FDisplaySettings display;
        FColorTransform::FColorPipeline pipeline = FColorTransform::BuildPipeline(display);

        Check(u8"默认设置下管线不启用", !pipeline.bEnabled);

        bool bIdentity = true;

        for (int32_t i = 0; i <= 255; ++i)
        {
            const float v = static_cast<float>(i) / 255.0f;

            float rgb[3] = { v, v, v };
            FColorTransform::ApplyPipeline(pipeline, rgb);

            if (std::fabs(rgb[0] - v) > 1e-6f || std::fabs(rgb[1] - v) > 1e-6f || std::fabs(rgb[2] - v) > 1e-6f)
            {
                bIdentity = false;
                break;
            }
        }

        Check(u8"直通路径逐值恒等", bIdentity);

        // 越界值仍然要被裁剪 —— 这是改造前的行为
        float over[3] = { 1.5f, -0.5f, 0.5f };
        FColorTransform::ApplyPipeline(pipeline, over);

        Check(u8"直通路径仍然裁剪到 [0,1]",
              over[0] == 1.0f && over[1] == 0.0f && std::fabs(over[2] - 0.5f) < 1e-6f);

        // 只改原色就必须进线性路径
        display.Primaries = EColorPrimaries::BT2020;
        pipeline = FColorTransform::BuildPipeline(display);

        Check(u8"原色不一致时启用管线", pipeline.bEnabled && pipeline.bApplyPrimaries != 0);

        // 只改曝光也要进
        FDisplaySettings exposed;
        exposed.ExposureStops = 1.0f;
        pipeline = FColorTransform::BuildPipeline(exposed);

        Check(u8"曝光非 0 时启用管线", pipeline.bEnabled);
    }

    void TestPassthroughOutOfRange()
    {
        std::printf("\n=== 直通路径的超范围高亮 ===\n");

        // limited range 的超白（Y > 235）与超黑（Y < 16）经矩阵拉伸后会跑出 [0,1]，
        // 而这类素材走的正是直通路径。高亮在这条路上不生效的话，
        // 最常见的一种"超范围"就永远看不见
        FDisplaySettings display;
        display.bShowOutOfRange = true;

        const FColorTransform::FColorPipeline pipeline = FColorTransform::BuildPipeline(display);

        Check(u8"开了高亮仍然走直通", !pipeline.bEnabled);
        Check(u8"高亮标志传进了管线", pipeline.bShowOutOfRange != 0);

        float over[3] = { 1.2f, 0.5f, 0.5f };
        FColorTransform::ApplyPipeline(pipeline, over);

        Check(u8"直通下超白标红",
              over[0] == 1.0f && over[1] == 0.0f && over[2] == 0.0f);

        float under[3] = { -0.05f, 0.5f, 0.5f };
        FColorTransform::ApplyPipeline(pipeline, under);

        Check(u8"直通下超黑标蓝",
              under[0] == 0.0f && std::fabs(under[1] - 0.4f) < 1e-6f && under[2] == 1.0f);

        // 范围内的像素一个字节都不能动 —— 高亮是叠加的诊断，不是换一条管线
        float inside[3] = { 0.25f, 0.5f, 0.75f };
        FColorTransform::ApplyPipeline(pipeline, inside);

        Check(u8"范围内像素保持恒等",
              std::fabs(inside[0] - 0.25f) < 1e-6f
              && std::fabs(inside[1] - 0.5f) < 1e-6f
              && std::fabs(inside[2] - 0.75f) < 1e-6f);

        // 探针的亮度读数按裁剪后的值算，不受高亮影响
        float probe[3] = { 1.2f, 1.2f, 1.2f };
        float nits = 0.0f;
        FColorTransform::ApplyPipeline(pipeline, probe, &nits);

        CheckNear(u8"标红像素仍然给出亮度读数", nits, 203.0, 0.5);

        // 没开高亮时必须回到纯裁剪（这是直通路径的原始行为）
        FDisplaySettings quiet;
        const FColorTransform::FColorPipeline quietPipeline = FColorTransform::BuildPipeline(quiet);

        float clipped[3] = { 1.2f, -0.05f, 0.5f };
        FColorTransform::ApplyPipeline(quietPipeline, clipped);

        Check(u8"未开高亮时仍是纯裁剪",
              clipped[0] == 1.0f && clipped[1] == 0.0f && std::fabs(clipped[2] - 0.5f) < 1e-6f);
    }

    void TestPqPipeline()
    {
        std::printf("\n=== PQ 完整管线 ===\n");

        // 目标文件的配置：BT.2020 原色 + PQ + 默认参考白，输出到 SDR
        FDisplaySettings display;
        display.ColorSpace = EColorSpace::BT2020;
        display.Primaries = EColorPrimaries::BT2020;
        display.Transfer = EColorTransfer::PQ;

        const FColorTransform::FColorPipeline pipeline = FColorTransform::BuildPipeline(display);

        Check(u8"PQ 启用管线", pipeline.bEnabled);
        Check(u8"PQ 标记为绝对传输函数", pipeline.bAbsoluteTransfer != 0);
        Check(u8"插入了原色转换", pipeline.bApplyPrimaries != 0);

        // 默认参考白是 BT.2408 的 203nit，不是"PQ = 100nit"那个来自 SDR
        // 参考监视器峰值的说法 —— 用 100 折算 HDR 素材会过曝约 1 档
        CheckNear(u8"归一化除数 = 默认参考白 203", pipeline.NormalizeNits, 203.0, 1e-4);

        // 参考白的 PQ 码值过完管线应当落在显示白附近。
        // BT.2020 白 -> BT.709 白仍是白，所以三个分量要一致
        const float whiteCode = FColorTransform::PqInverseEotf(203.0f);

        float white[3] = { whiteCode, whiteCode, whiteCode };
        float nits = 0.0f;
        FColorTransform::ApplyPipeline(pipeline, white, &nits);

        CheckNear(u8"203nit 白 -> 亮度读数 203", nits, 203.0, 0.5);
        CheckNear(u8"203nit 白 -> 编码 1.0 (R)", white[0], 1.0, 0.005);
        CheckNear(u8"203nit 白保持中性 (G)", white[1], white[0], 1e-4);
        CheckNear(u8"203nit 白保持中性 (B)", white[2], white[0], 1e-4);

        // 100nit 现在应当落在显示白**之下**（约 0.49 的线性值），
        // 这正是默认值从 100 改到 203 的可观测后果
        const float legacyWhiteCode = FColorTransform::PqInverseEotf(100.0f);

        float legacyWhite[3] = { legacyWhiteCode, legacyWhiteCode, legacyWhiteCode };
        FColorTransform::ApplyPipeline(pipeline, legacyWhite);

        Check(u8"100nit 不再顶到显示白", legacyWhite[0] < 0.99f);

        // 黑仍然是黑
        float black[3] = { 0.0f, 0.0f, 0.0f };
        FColorTransform::ApplyPipeline(pipeline, black);

        CheckNear(u8"黑 -> 0", black[0], 0.0, 1e-4);

        // 超过参考白的高光在 Clip 下应当被压到 1.0（而不是溢出成负数或 NaN）
        const float highlightCode = FColorTransform::PqInverseEotf(1000.0f);

        float highlight[3] = { highlightCode, highlightCode, highlightCode };
        float highlightNits = 0.0f;
        FColorTransform::ApplyPipeline(pipeline, highlight, &highlightNits);

        CheckNear(u8"1000nit 高光 -> 亮度读数 1000", highlightNits, 1000.0, 5.0);
        CheckNear(u8"1000nit 高光被裁剪到 1.0", highlight[0], 1.0, 1e-4);

        // 一整轮 10bit 码值都不能出 NaN
        bool bAllFinite = true;

        for (int32_t i = 0; i <= 1023; ++i)
        {
            const float v = static_cast<float>(i) / 1023.0f;

            float rgb[3] = { v, v * 0.5f, v * 0.25f };
            FColorTransform::ApplyPipeline(pipeline, rgb);

            if (!std::isfinite(rgb[0]) || !std::isfinite(rgb[1]) || !std::isfinite(rgb[2]))
            {
                bAllFinite = false;
                break;
            }
        }

        Check(u8"10bit 全码值无 NaN/inf", bAllFinite);
    }

    void TestHdrOutput()
    {
        std::printf("\n=== HDR 输出（scRGB 分支）===\n");

        FDisplaySettings display;
        display.Primaries = EColorPrimaries::BT2020;
        display.Transfer = EColorTransfer::PQ;

        FColorTransform::FDisplayOutput output;
        output.bHdr = true;
        output.SdrWhiteNits = 200.0f;
        output.MaxNits = 1000.0f;

        const FColorTransform::FColorPipeline pipeline = FColorTransform::BuildPipeline(display, output);

        Check(u8"输出模式为 HDR", pipeline.OutputMode == 1);

        // HDR 下归一化用的是显示器 SDR 白电平，不是内容参考白 ——
        // 这样 PQ 的绝对亮度才会被原样送到面板上
        CheckNear(u8"归一化除数 = 显示器 SDR 白", pipeline.NormalizeNits, 200.0, 1e-4);
        CheckNear(u8"上限 = 峰值 / SDR 白", pipeline.MaxOutputScale, 5.0, 1e-4);

        // 200nit（= 屏幕 SDR 白）的内容应当编码成 1.0
        const float code200 = FColorTransform::PqInverseEotf(200.0f);

        float sample[3] = { code200, code200, code200 };
        FColorTransform::ApplyPipeline(pipeline, sample);

        CheckNear(u8"200nit -> 编码 1.0", sample[0], 1.0, 0.005);

        // 800nit 应当编码成 ExtendedSrgbEncode(4.0)，解码回去必须还原
        const float code800 = FColorTransform::PqInverseEotf(800.0f);

        float highlight[3] = { code800, code800, code800 };
        FColorTransform::ApplyPipeline(pipeline, highlight);

        const float decoded = FColorTransform::SrgbDecodeExtended(highlight[0]);

        CheckNear(u8"800nit -> 解码回 4.0 倍 SDR 白", decoded, 4.0, 0.02);

        // 超过显示器峰值的部分被限到 MaxOutputScale
        const float code5000 = FColorTransform::PqInverseEotf(5000.0f);

        float overPeak[3] = { code5000, code5000, code5000 };
        FColorTransform::ApplyPipeline(pipeline, overPeak);

        CheckNear(u8"5000nit 限幅到显示器峰值", FColorTransform::SrgbDecodeExtended(overPeak[0]), 5.0, 0.02);

        // --- 限幅必须保色相 ---
        // 逐通道 min() 会把 (8, 2, 1) 裁成 (5, 2, 1)：红只剩绿的 2.5 倍，明显偏黄。
        // 整体缩放则只压亮度，比值一个都不变
        float peakClip[3] = { 8.0f, 2.0f, 1.0f };
        FColorTransform::LimitToPeakPreserveHue(peakClip, 5.0f);

        CheckNear(u8"最大分量落在上限", peakClip[0], 5.0, 1e-4);
        CheckNear(u8"R:G 比值保持 4", peakClip[0] / peakClip[1], 4.0, 1e-4);
        CheckNear(u8"R:B 比值保持 8", peakClip[0] / peakClip[2], 8.0, 1e-4);

        float underPeak[3] = { 2.0f, 1.0f, 0.5f };
        FColorTransform::LimitToPeakPreserveHue(underPeak, 5.0f);

        Check(u8"上限之内不缩放",
              underPeak[0] == 2.0f && underPeak[1] == 1.0f && underPeak[2] == 0.5f);

        // 负分量是 scRGB 表达色域外颜色的方式，只能等比缩放，不能被抬成 0
        float wideGamut[3] = { 10.0f, 3.0f, -1.0f };
        FColorTransform::LimitToPeakPreserveHue(wideGamut, 5.0f);

        CheckNear(u8"负分量等比缩放", wideGamut[2], -0.5, 1e-4);
        Check(u8"负分量未被抬起", wideGamut[2] < 0.0f);

        // --- 端到端：原色一致（不插矩阵）时，红色高光过完管线比值仍应保持 ---
        FDisplaySettings samePrimaries;
        samePrimaries.Transfer = EColorTransfer::PQ;   // Primaries 留在 BT.709 = 输出原色

        const FColorTransform::FColorPipeline noMatrix =
            FColorTransform::BuildPipeline(samePrimaries, output);

        Check(u8"原色一致时不插矩阵", noMatrix.bApplyPrimaries == 0);

        // 1600 / 400 / 200 nit，除以 SDR 白 200 后就是 8 : 2 : 1
        float redHighlight[3] = {
            FColorTransform::PqInverseEotf(1600.0f),
            FColorTransform::PqInverseEotf(400.0f),
            FColorTransform::PqInverseEotf(200.0f),
        };

        FColorTransform::ApplyPipeline(noMatrix, redHighlight);

        const float linR = FColorTransform::SrgbDecodeExtended(redHighlight[0]);
        const float linG = FColorTransform::SrgbDecodeExtended(redHighlight[1]);

        CheckNear(u8"饱和高光限到显示器峰值", linR, 5.0, 0.02);
        CheckNear(u8"限幅后 R:G 比值不变", linR / linG, 4.0, 0.05);
        Check(u8"红仍是最大分量（逐通道裁会让它掉到绿以下）", linR > linG);

        // SDR 素材在 HDR 输出下必须保持直通 —— 否则界面和图像会对不上
        FDisplaySettings sdr;
        const FColorTransform::FColorPipeline sdrPipeline = FColorTransform::BuildPipeline(sdr, output);

        Check(u8"SDR 素材在 HDR 输出下仍走直通", !sdrPipeline.bEnabled);

        float sdrSample[3] = { 0.5f, 0.25f, 0.75f };
        FColorTransform::ApplyPipeline(sdrPipeline, sdrSample);

        Check(u8"SDR 素材编码值不变",
              std::fabs(sdrSample[0] - 0.5f) < 1e-6f
              && std::fabs(sdrSample[1] - 0.25f) < 1e-6f
              && std::fabs(sdrSample[2] - 0.75f) < 1e-6f);
    }

    void TestOutOfGamut()
    {
        std::printf("\n=== 广色域与超范围 ===\n");

        // BT.2020 的纯绿在 BT.709 里表示不出来，转换后必然出现负分量
        float m[9];
        FColorTransform::BuildPrimariesMatrix(EColorPrimaries::BT2020, EColorPrimaries::BT709, m);

        // 列主序，第 1 列是 G 的贡献
        Check(u8"BT.2020 纯绿在 BT.709 下产生负分量", m[3] < 0.0f && m[5] < 0.0f);

        // 超范围高亮：超上限画红
        FDisplaySettings display;
        display.Transfer = EColorTransfer::PQ;
        display.Primaries = EColorPrimaries::BT2020;
        display.bShowOutOfRange = true;

        const FColorTransform::FColorPipeline pipeline = FColorTransform::BuildPipeline(display);

        const float bright = FColorTransform::PqInverseEotf(4000.0f);

        float overRange[3] = { bright, bright, bright };
        FColorTransform::ApplyPipeline(pipeline, overRange);

        Check(u8"超出显示上限标红",
              overRange[0] == 1.0f && overRange[1] == 0.0f && overRange[2] == 0.0f);
    }

    void TestToneMap()
    {
        std::printf("\n=== 色调映射算子 ===\n");

        // Clip 是恒等
        CheckNear(u8"Clip 恒等", FColorTransform::ApplyToneMap(EToneMapOperator::Clip, 0.7f, 4.0f), 0.7, 1e-6);

        // Reinhard 把 White 映射到 1.0
        CheckNear(u8"Reinhard 把白点映到 1.0",
                  FColorTransform::ToneMapReinhard(4.0f, 4.0f), 1.0, 1e-4);

        CheckNear(u8"Reinhard 0 -> 0", FColorTransform::ToneMapReinhard(0.0f, 4.0f), 0.0, 1e-6);

        // 单调性：非单调会让高光出现反转
        bool bMonotonic = true;
        float previous = -1.0f;

        for (int32_t i = 0; i <= 2000; ++i)
        {
            const float x = static_cast<float>(i) * 0.01f;
            const float y = FColorTransform::ToneMapACES(x);

            if (y < previous)
            {
                bMonotonic = false;
                break;
            }

            previous = y;
        }

        Check(u8"ACES 单调递增", bMonotonic);
        CheckNear(u8"ACES 0 -> 0", FColorTransform::ToneMapACES(0.0f), 0.0, 1e-6);
    }
}

int main()
{
    std::printf("=== 色彩管线自检 ===\n");

    TestPq();
    TestHlg();
    TestSrgb();
    TestPrimaries();
    TestPassthrough();
    TestPassthroughOutOfRange();
    TestPqPipeline();
    TestHdrOutput();
    TestOutOfGamut();
    TestToneMap();

    std::printf("\n");

    if (gFailures == 0)
    {
        std::printf("色彩管线自检全部通过\n");

        return 0;
    }

    std::printf("色彩管线自检失败：%d 项\n", gFailures);

    return 1;
}
