#include "FResolutionGuess.h"

#include "FImageFormatDesc.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace
{
    // 合理范围。放宽没有意义 —— 因数分解出来的解绝大多数是 8x353280 这种垃圾
    constexpr int32_t kMinWidth = 16;
    constexpr int32_t kMaxWidth = 16384;
    constexpr int32_t kMinHeight = 16;
    constexpr int32_t kMaxHeight = 16384;
    constexpr int32_t kMinStride = 16;
    constexpr int32_t kMaxStride = 1 << 18;

    /// 宽高比允许区间，超出的一律丢掉
    constexpr double kMinAspect = 0.2;
    constexpr double kMaxAspect = 5.0;

    /// 常见宽高比。竖版不必列出 —— 打分时同时比较 a 与 1/a
    ///
    /// 刻意**不含 21:9**：这个比例在相机/视频 dump 里几乎不出现，加进来反而成了假引力点 ——
    /// 4239360 字节的 NV21 文件会因此把 1104x2560（比值 0.431，离 9:21 只差 0.6%）
    /// 排到真值 1472x1920 前面。宽高比表宁缺毋滥。
    const double kCommonAspects[] = { 16.0 / 9.0, 4.0 / 3.0, 3.0 / 2.0, 1.0, 16.0 / 10.0, 5.0 / 4.0, 2.0 };

    struct FCommonRes
    {
        int32_t Width;
        int32_t Height;
    };

    /// 常见分辨率（只列横版，竖版在匹配时按转置一并考虑）
    const FCommonRes kCommonResolutions[] = {
        // 视频
        { 7680, 4320 }, { 4096, 2160 }, { 3840, 2160 }, { 2560, 1440 }, { 2048, 1080 },
        { 1920, 1080 }, { 1600,  900 }, { 1280,  720 }, { 1024,  576 }, {  960,  540 },
        {  854,  480 }, {  720,  576 }, {  720,  480 }, {  640,  360 }, {  352,  288 },
        {  320,  240 }, {  176,  144 },
        // 相机 / 传感器
        { 8000, 6000 }, { 6000, 4000 }, { 4624, 3472 }, { 4032, 3024 }, { 4000, 3000 },
        { 3840, 2880 }, { 3264, 2448 }, { 3072, 2304 }, { 2592, 1944 }, { 2560, 1920 },
        { 2048, 1536 }, { 1920, 1440 }, { 1600, 1200 }, { 1440, 1080 }, { 1280,  960 },
        { 1024,  768 }, {  800,  600 }, {  640,  480 }, {  320,  240 },
    };

    int64_t Lcm(int64_t A, int64_t B)
    {
        if (A <= 0 || B <= 0)
        {
            return 1;
        }

        int64_t a = A;
        int64_t b = B;

        while (b != 0)
        {
            const int64_t t = a % b;
            a = b;
            b = t;
        }

        return (A / a) * B;
    }

    /**
     * 图像大小相对 stride * 高 的比例：图像大小 = stride * 高 * Num / Den
     *
     * 只在高度对齐到 2^最大 HeightShift、stride 对齐到最大 StrideDivisor 时精确成立，
     * 所以枚举出的候选一律再用 CalculateFrameSize() 复核一遍。
     */
    bool GetImageSizeRatio(const FFormatDesc& Desc, int64_t& OutNum, int64_t& OutDen, int32_t& OutHeightAlign, int32_t& OutStrideAlign)
    {
        if (Desc.PlaneCount <= 0)
        {
            return false;
        }

        int64_t den = 1;
        int32_t heightAlign = 1;
        int32_t strideAlign = 1;

        for (int32_t i = 0; i < Desc.PlaneCount; ++i)
        {
            const int32_t divisor = Desc.Planes[i].StrideDivisor > 0 ? Desc.Planes[i].StrideDivisor : 1;
            const int32_t rows = 1 << Desc.Planes[i].HeightShift;

            den = Lcm(den, static_cast<int64_t>(divisor) * rows);
            heightAlign = std::max(heightAlign, rows);
            strideAlign = std::max(strideAlign, divisor);
        }

        int64_t num = 0;

        for (int32_t i = 0; i < Desc.PlaneCount; ++i)
        {
            const int32_t divisor = Desc.Planes[i].StrideDivisor > 0 ? Desc.Planes[i].StrideDivisor : 1;
            const int32_t rows = 1 << Desc.Planes[i].HeightShift;

            num += den / (static_cast<int64_t>(divisor) * rows);
        }

        if (num <= 0)
        {
            return false;
        }

        OutNum = num;
        OutDen = den;
        OutHeightAlign = heightAlign;
        OutStrideAlign = strideAlign;

        return true;
    }

    bool IsCommonResolution(int32_t Width, int32_t Height)
    {
        for (const FCommonRes& r : kCommonResolutions)
        {
            if ((r.Width == Width && r.Height == Height) || (r.Height == Width && r.Width == Height))
            {
                return true;
            }
        }

        return false;
    }

    /**
     * 与最接近的常见宽高比的对数距离。0 表示正好命中
     */
    double AspectPenalty(int32_t Width, int32_t Height)
    {
        const double ratio = std::log(static_cast<double>(Width) / static_cast<double>(Height));

        double best = 1e9;

        for (double a : kCommonAspects)
        {
            const double la = std::log(a);

            // 同时考虑竖版：log(1/a) = -log(a)
            best = std::min(best, std::min(std::abs(ratio - la), std::abs(ratio + la)));
        }

        return best;
    }

    /**
     * 越小越可信
     */
    double ScoreCandidate(int32_t Width, int32_t Height)
    {
        // 宽高比是最强的先验，其余项只用来打破平局
        double score = AspectPenalty(Width, Height) * 100.0;

        if (IsCommonResolution(Width, Height))
        {
            score -= 60.0;
        }

        if (Width % 16 == 0)
        {
            score -= 4.0;
        }
        else if (Width % 8 == 0)
        {
            score -= 2.0;
        }

        if (Height % 16 == 0)
        {
            score -= 2.0;
        }

        return score;
    }
}

namespace FResolutionGuess
{
    int32_t WidthFromStride(EImageFormat Format, int32_t Stride)
    {
        if (Stride <= 0)
        {
            return 0;
        }

        // ResolveBaseStride 对宽度单调不减，二分出"stride 不超过目标"的最大宽度。
        // 取最大是有意的：packed 格式里多个宽度可能映射到同一个 stride
        //（RAW10 的 W 与 W+1 都可能得到同样的字节数），宽的那个才用满了这一行。
        int32_t low = 1;
        int32_t high = kMaxWidth;
        int32_t best = 0;

        while (low <= high)
        {
            const int32_t mid = low + (high - low) / 2;
            const int32_t stride = FImageFormatDesc::ResolveBaseStride(Format, mid, 0);

            if (stride <= 0)
            {
                return 0;
            }

            if (stride <= Stride)
            {
                best = mid;
                low = mid + 1;
            }
            else
            {
                high = mid - 1;
            }
        }

        if (best <= 0 || FImageFormatDesc::ResolveBaseStride(Format, best, 0) != Stride)
        {
            return 0;
        }

        return best;
    }

    bool Matches(EImageFormat Format, int32_t Width, int32_t Height, int32_t Stride, uint64_t FileSize)
    {
        if (FileSize == 0)
        {
            return false;
        }

        const uint64_t imageSize = FImageFormatDesc::CalculateFrameSize(Format, Width, Height, Stride);

        return imageSize != 0 && imageSize == FileSize;
    }

    std::vector<FResolutionCandidate> Guess(EImageFormat Format, uint64_t FileSize, int32_t MaxCount)
    {
        std::vector<FResolutionCandidate> result;

        if (Format == EImageFormat::Unknown || FileSize == 0)
        {
            return result;
        }

        const FFormatDesc& desc = FImageFormatDesc::Get(Format);

        int64_t num = 0;
        int64_t den = 0;
        int32_t heightAlign = 1;
        int32_t strideAlign = 1;

        if (!GetImageSizeRatio(desc, num, den, heightAlign, strideAlign))
        {
            return result;
        }

        // 同一宽度下 (W, H, k) 与 (W, H/2, 2k) 永远同时成立，不去重的话列表会被这种
        // 谐波灌满。每个宽度只留宽高比最像样的那一个。
        std::unordered_map<int32_t, FResolutionCandidate> bestByWidth;
        std::unordered_map<int32_t, double> scoreByWidth;

        auto tryPair = [&](uint64_t Stride, uint64_t Height)
        {
            if (Stride < static_cast<uint64_t>(kMinStride) || Stride > static_cast<uint64_t>(kMaxStride))
            {
                return;
            }

            if (Height < static_cast<uint64_t>(kMinHeight) || Height > static_cast<uint64_t>(kMaxHeight))
            {
                return;
            }

            const int32_t stride = static_cast<int32_t>(Stride);
            const int32_t height = static_cast<int32_t>(Height);

            if (stride % strideAlign != 0 || height % heightAlign != 0)
            {
                return;
            }

            const int32_t width = WidthFromStride(Format, stride);

            if (width < kMinWidth || width > kMaxWidth)
            {
                return;
            }

            const double aspect = static_cast<double>(width) / static_cast<double>(height);

            if (aspect < kMinAspect || aspect > kMaxAspect)
            {
                return;
            }

            // 有理数推导只是用来生成候选，字节数是否真的对得上以格式表的计算为准
            if (!Matches(Format, width, height, 0, FileSize))
            {
                return;
            }

            const double score = ScoreCandidate(width, height);
            const auto it = scoreByWidth.find(width);

            if (it != scoreByWidth.end() && it->second <= score)
            {
                return;
            }

            FResolutionCandidate c;
            c.Width = width;
            c.Height = height;
            c.bExact = true;

            bestByWidth[width] = c;
            scoreByWidth[width] = score;
        };

        // stride * 高 = 文件大小 * Den / Num。除不尽就说明这个格式根本装不满这个文件
        const uint64_t lhs = FileSize * static_cast<uint64_t>(den);
        const uint64_t rhs = static_cast<uint64_t>(num);

        if (lhs % rhs != 0)
        {
            return result;
        }

        const uint64_t product = lhs / rhs;

        if (product < static_cast<uint64_t>(kMinStride) * static_cast<uint64_t>(kMinHeight) ||
            product > static_cast<uint64_t>(kMaxStride) * static_cast<uint64_t>(kMaxHeight))
        {
            return result;
        }

        for (uint64_t d = 1; d * d <= product; ++d)
        {
            if (product % d != 0)
            {
                continue;
            }

            tryPair(d, product / d);
            tryPair(product / d, d);
        }

        result.reserve(bestByWidth.size());

        for (const auto& kv : bestByWidth)
        {
            result.push_back(kv.second);
        }

        std::sort(result.begin(), result.end(), [&](const FResolutionCandidate& A, const FResolutionCandidate& B)
        {
            const double sa = ScoreCandidate(A.Width, A.Height);
            const double sb = ScoreCandidate(B.Width, B.Height);

            if (sa != sb)
            {
                return sa < sb;
            }

            // 分数相同时给一个稳定顺序，避免哈希表遍历顺序泄漏到 UI 上
            return A.Width > B.Width;
        });

        if (MaxCount > 0 && static_cast<int32_t>(result.size()) > MaxCount)
        {
            result.resize(static_cast<size_t>(MaxCount));
        }

        return result;
    }

    std::vector<EImageFormat> GuessFormats(
        uint64_t FileSize,
        int32_t Width,
        int32_t Height,
        EImageFormat ExcludeFormat,
        int32_t MaxCount)
    {
        std::vector<EImageFormat> result;

        if (FileSize == 0)
        {
            return result;
        }

        for (const FFormatDesc& d : FImageFormatDesc::GetAll())
        {
            if (d.Format == EImageFormat::Unknown || d.Format == ExcludeFormat)
            {
                continue;
            }

            if (MaxCount > 0 && static_cast<int32_t>(result.size()) >= MaxCount)
            {
                break;
            }

            bool bHit = false;

            if (Width > 0 && Height > 0)
            {
                // 已知（多半来自文件名的）分辨率时直接验它，这比重新猜一遍分辨率精准得多
                bHit = Matches(d.Format, Width, Height, 0, FileSize);
            }
            else
            {
                // 一点线索都没有时退而求其次：该格式下有没有哪个常见分辨率正好装满文件
                for (const FCommonRes& r : kCommonResolutions)
                {
                    if (Matches(d.Format, r.Width, r.Height, 0, FileSize) ||
                        Matches(d.Format, r.Height, r.Width, 0, FileSize))
                    {
                        bHit = true;

                        break;
                    }
                }
            }

            if (bHit)
            {
                result.push_back(d.Format);
            }
        }

        return result;
    }
}
