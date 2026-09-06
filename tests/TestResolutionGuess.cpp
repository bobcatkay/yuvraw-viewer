// 由文件大小反推分辨率的离线自检。不依赖 OpenGL / ImGui。
//
// 前提：一个文件就是一幅图，图像字节数必须**正好等于**文件大小。
#include "Image/FResolutionGuess.h"
#include "Image/FImageFormatDesc.h"

#include <cstdio>
#include <string>

static int gFailures = 0;

static void Fail(const std::string& What)
{
    ++gFailures;
    std::printf("%-64s **FAIL**\n", What.c_str());
}

static void Pass(const std::string& What)
{
    std::printf("%-64s OK\n", What.c_str());
}

static void Check(const std::string& What, long long Got, long long Want)
{
    if (Got == Want)
    {
        std::printf("%-52s got=%-10lld OK\n", What.c_str(), Got);
    }
    else
    {
        ++gFailures;
        std::printf("%-52s got=%-10lld want=%-10lld **FAIL**\n", What.c_str(), Got, Want);
    }
}

static uint64_t ImageSizeOf(EImageFormat Fmt, int32_t W, int32_t H)
{
    return FImageFormatDesc::CalculateFrameSize(Fmt, W, H, 0);
}

/**
 * 候选列表里 Want 排第几（从 0 起）；找不到返回 -1
 */
static int IndexOf(const std::vector<FResolutionCandidate>& List, int32_t W, int32_t H)
{
    for (size_t i = 0; i < List.size(); ++i)
    {
        if (List[i].Width == W && List[i].Height == H)
        {
            return static_cast<int>(i);
        }
    }

    return -1;
}

static void CheckTopHit(const char* Label, EImageFormat Fmt, int32_t W, int32_t H, int MaxRank = 3)
{
    const uint64_t size = ImageSizeOf(Fmt, W, H);
    const std::vector<FResolutionCandidate> list = FResolutionGuess::Guess(Fmt, size, 10);
    const int index = IndexOf(list, W, H);

    std::string label = std::string(Label) + " " + std::to_string(W) + "x" + std::to_string(H);

    if (index < 0)
    {
        Fail(label + " -> 未出现在候选里");

        return;
    }

    // 允许不排第一（同一文件大小下确实存在多个等价解）
    if (index >= MaxRank)
    {
        Fail(label + " -> 排在第 " + std::to_string(index + 1) + " 位，太靠后");

        return;
    }

    Pass(label + " -> 第 " + std::to_string(index + 1) + " 位（共 "
         + std::to_string(list.size()) + " 条）");
}

int main()
{
    std::printf("=== 常见场景：真值应排在候选前列 ===\n");
    CheckTopHit("NV21",    EImageFormat::NV21,    1920, 1080);
    CheckTopHit("NV21",    EImageFormat::NV21,    3840, 2160);
    CheckTopHit("NV21",    EImageFormat::NV21,    1440, 1920);   // 竖版相机 dump
    CheckTopHit("NV21",    EImageFormat::NV21,    3840, 2880);
    CheckTopHit("RGBA8",   EImageFormat::RGBA8,   1920, 1080);
    CheckTopHit("RGB10_A2", EImageFormat::RGB10A2, 1920, 1080);
    CheckTopHit("RGB8",    EImageFormat::RGB8,    1280,  720);
    CheckTopHit("I420",    EImageFormat::YUV420P,  352,  288);
    CheckTopHit("P010",    EImageFormat::P010,    1920, 1080);
    CheckTopHit("YUV420SP16", EImageFormat::YUV420SP16, 1920, 1080);
    CheckTopHit("YUV420SP16 实际样本", EImageFormat::YUV420SP16, 4096, 3072);
    CheckTopHit("YUY2",    EImageFormat::YUY2,    1920, 1080);
    CheckTopHit("Gray8",   EImageFormat::Grayscale8, 4032, 3024);
    CheckTopHit("Bayer16", EImageFormat::Bayer16, 4032, 3024);

    std::printf("\n=== 真实测试素材 ===\n");
    // texture_video_format35_1472x1920.yuv：NV21，文件大小与 1472x1920 紧凑排列一致
    //（真值是 1440x1920 stride 1472 —— 两者字节数完全相同，数学上不可分辨，
    //  所以这里只要求给出 1472x1920，padding 由 UI 用文字提示）
    {
        const uint64_t size = 1472ull * 1920 * 3 / 2;
        Check("素材文件大小", static_cast<long long>(size), 4239360);

        const std::vector<FResolutionCandidate> list = FResolutionGuess::Guess(EImageFormat::NV21, size, 10);
        const int index = IndexOf(list, 1472, 1920);

        if (index >= 0 && index <= 2)
        {
            Pass("NV21 4239360 字节 -> 1472x1920 第 " + std::to_string(index + 1) + " 位");
        }
        else
        {
            Fail("NV21 4239360 字节 -> 1472x1920 未进前三");
        }
    }

    // DSC07808.yuv：手动选中 I420 后，面板会直接采用首个精确候选。
    {
        constexpr uint64_t size = 49112064;
        const std::vector<FResolutionCandidate> list =
            FResolutionGuess::Guess(EImageFormat::YUV420P, size, 1);

        Check("I420 DSC07808 首选候选数", static_cast<long long>(list.size()), 1);

        if (!list.empty()
            && list.front().Width == 7008
            && list.front().Height == 4672
            && list.front().bExact)
        {
            Pass("I420 49112064 字节 -> 首选 7008x4672");
        }
        else
        {
            Fail("I420 49112064 字节 -> 首选不是 7008x4672");
        }
    }

    std::printf("\n=== 格式猜错的场景（texture_output_format1_384x2880.yuv） ===\n");
    // 真值是 RGBA8 384x2880 = 4423680 字节，但文件名 + .yuv 扩展名会被解析成 NV21
    {
        const uint64_t size = 4423680;

        Check("NV21 384x2880 是否对上（应为否）",
              FResolutionGuess::Matches(EImageFormat::NV21, 384, 2880, 0, size) ? 1 : 0, 0);

        Check("RGBA8 384x2880 是否对上",
              FResolutionGuess::Matches(EImageFormat::RGBA8, 384, 2880, 0, size) ? 1 : 0, 1);

        const std::vector<EImageFormat> alts =
            FResolutionGuess::GuessFormats(size, 384, 2880, EImageFormat::NV21, 4);

        if (!alts.empty() && alts[0] == EImageFormat::RGBA8)
        {
            Pass("按文件名分辨率反查格式 -> RGBA8 排第一（共 "
                 + std::to_string(alts.size()) + " 个）");
        }
        else
        {
            Fail("按文件名分辨率反查格式 -> RGBA8 未排第一");
        }
    }

    std::printf("\n=== 单帧语义：文件比一幅图大就不算数 ===\n");
    {
        // 以前 3110400*5 字节会被当成 1920x1080 的 5 帧，现在必须不认
        const uint64_t fiveFrames = ImageSizeOf(EImageFormat::NV21, 1920, 1080) * 5;

        Check("NV21 1920x1080 对 5 倍大的文件（应为否）",
              FResolutionGuess::Matches(EImageFormat::NV21, 1920, 1080, 0, fiveFrames) ? 1 : 0, 0);

        if (IndexOf(FResolutionGuess::Guess(EImageFormat::NV21, fiveFrames, 10), 1920, 1080) >= 0)
        {
            Fail("5 倍大的文件仍给出 1920x1080");
        }
        else
        {
            Pass("5 倍大的文件不再给出 1920x1080");
        }

        // 恰好装满则必须认
        Check("NV21 1920x1080 对刚好一幅图的文件",
              FResolutionGuess::Matches(EImageFormat::NV21, 1920, 1080, 0,
                                        ImageSizeOf(EImageFormat::NV21, 1920, 1080)) ? 1 : 0, 1);
    }

    std::printf("\n=== stride 参与校验（带 padding 的排布） ===\n");
    {
        // 1440x1920 stride 1472 的 NV21 与 1472x1920 紧凑同样大
        const uint64_t size = 4239360;

        Check("1440x1920 紧凑（应为否）",
              FResolutionGuess::Matches(EImageFormat::NV21, 1440, 1920, 0, size) ? 1 : 0, 0);

        Check("1440x1920 stride=1472",
              FResolutionGuess::Matches(EImageFormat::NV21, 1440, 1920, 1472, size) ? 1 : 0, 1);
    }

    std::printf("\n=== stride -> 宽度 的反演 ===\n");
    {
        const int32_t widths[] = { 16, 64, 640, 1920, 4032, 8192 };

        for (const FFormatDesc& d : FImageFormatDesc::GetAll())
        {
            if (d.Format == EImageFormat::Unknown)
            {
                continue;
            }

            for (int32_t w : widths)
            {
                const int32_t stride = FImageFormatDesc::ResolveBaseStride(d.Format, w, 0);
                const int32_t back = FResolutionGuess::WidthFromStride(d.Format, stride);

                if (back != w)
                {
                    Fail(std::string(d.Name) + " 宽 " + std::to_string(w) + " -> stride "
                         + std::to_string(stride) + " -> 宽 " + std::to_string(back));
                }
            }
        }

        Check("非法 stride 返回 0", FResolutionGuess::WidthFromStride(EImageFormat::RGBA8, 3), 0);
        Pass("全部格式的 stride 反演一致");
    }

    std::printf("\n=== 不变量：每条候选都必须正好装满文件 ===\n");
    {
        const uint64_t sizes[] = { 4239360, 4423680, 3110400, 6220800, 45619200, 1 };
        int checked = 0;

        for (uint64_t size : sizes)
        {
            for (const FFormatDesc& d : FImageFormatDesc::GetAll())
            {
                if (d.Format == EImageFormat::Unknown)
                {
                    continue;
                }

                for (const FResolutionCandidate& c : FResolutionGuess::Guess(d.Format, size, 10))
                {
                    ++checked;

                    if (!c.bExact || !FResolutionGuess::Matches(d.Format, c.Width, c.Height, 0, size))
                    {
                        Fail(std::string(d.Name) + " " + std::to_string(c.Width) + "x"
                             + std::to_string(c.Height) + " 并没有正好装满 "
                             + std::to_string(size) + " 字节");
                    }
                }
            }
        }

        Pass("校验了 " + std::to_string(checked) + " 条候选");
    }

    std::printf("\n=== 边界 ===\n");
    Check("文件大小为 0 时无候选",
          static_cast<long long>(FResolutionGuess::Guess(EImageFormat::NV21, 0, 10).size()), 0);
    Check("Unknown 格式无候选",
          static_cast<long long>(FResolutionGuess::Guess(EImageFormat::Unknown, 4239360, 10).size()), 0);
    Check("MaxCount 生效",
          static_cast<long long>(FResolutionGuess::Guess(EImageFormat::NV21, 4239360, 3).size()), 3);

    std::printf("\n%s\n", gFailures == 0 ? "全部通过" : "存在失败项");

    return gFailures == 0 ? 0 : 1;
}
