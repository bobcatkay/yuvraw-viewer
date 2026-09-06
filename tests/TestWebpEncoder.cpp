// 无损 WebP 编码器的往返自检。不依赖 OpenGL / ImGui，但需要 WIC（windowscodecs.lib）。
//
// Windows 只带 WebP 解码器不带编码器，所以这里反过来利用系统解码器给自己写的编码器把关：
// 编码 -> 落盘 -> 用 WIC 读回来 -> 逐像素比对。无损编码器只要有一位对不上就算失败。
#include "Image/FWebpEncoder.h"
#include "Image/FWicImageLoader.h"

#include <windows.h>
#include "WicDecodeTestHelper.h"

#include <cstdio>
#include <string>
#include <vector>

static int gFailures = 0;

static void Fail(const char* What, const char* Detail)
{
    std::printf("**FAIL** %s: %s\n", What, Detail);
    ++gFailures;
}

/** 用 WIC 解码成紧凑 RGB8；失败阶段与 HRESULT 由测试辅助记录。 */
static bool DecodeWithWic(const std::wstring& Path, int& OutWidth, int& OutHeight, std::vector<unsigned char>& OutRgb)
{
    return WicDecodeTestHelper::DecodeRgb8(Path, OutWidth, OutHeight, OutRgb);
}
/**
 * 编码 -> 落盘 -> 解码 -> 逐像素比对
 */
static void RoundTrip(const char* Label, const std::vector<unsigned char>& Rgb, int Width, int Height)
{
    std::vector<unsigned char> file;
    std::string error;

    if (!FWebpEncoder::EncodeLosslessRgb8(Rgb.data(), Width, Height, file, error))
    {
        Fail(Label, error.c_str());

        return;
    }

    wchar_t tempDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempDir);

    const std::wstring path = std::wstring(tempDir) + L"yuvraw_webp_test.webp";

    FILE* fp = nullptr;
    _wfopen_s(&fp, path.c_str(), L"wb");

    if (!fp)
    {
        Fail(Label, "无法写入临时文件");

        return;
    }

    std::fwrite(file.data(), 1, file.size(), fp);
    std::fclose(fp);

    int decodedWidth = 0;
    int decodedHeight = 0;
    std::vector<unsigned char> decoded;

    if (!DecodeWithWic(path, decodedWidth, decodedHeight, decoded))
    {
        Fail(Label, "WIC 无法解码生成的 .webp（本机可能缺少 WebP 解码器，或码流不合规）");

        return;
    }

    if (decodedWidth != Width || decodedHeight != Height)
    {
        char detail[128];
        std::snprintf(detail, sizeof(detail), "尺寸不符 %dx%d != %dx%d", decodedWidth, decodedHeight, Width, Height);
        Fail(Label, detail);

        return;
    }

    for (size_t i = 0; i < Rgb.size(); ++i)
    {
        if (Rgb[i] != decoded[i])
        {
            char detail[160];
            std::snprintf(detail, sizeof(detail), "第 %zu 字节不一致: 写入 %u 读回 %u",
                          i, static_cast<unsigned>(Rgb[i]), static_cast<unsigned>(decoded[i]));
            Fail(Label, detail);

            return;
        }
    }

    std::printf("%-46s %5d x %-5d  %8zu 字节 (%.2f bpp)  OK\n",
                Label, Width, Height, file.size(),
                file.size() * 8.0 / (static_cast<double>(Width) * Height));
}

int main()
{
    if (!WicDecodeTestHelper::CheckResult("CoInitializeEx", CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    {
        return 1;
    }

    std::printf("=== 无损 WebP 往返 ===\n");
    const auto codec = FWicImageLoader::GetCodecAvailability(".webp");
    std::printf("WIC WebP decoder registration: %s\n",
                codec == EWicCodecAvailability::Available ? "AVAILABLE" :
                codec == EWicCodecAvailability::Missing ? "MISSING" : "UNKNOWN");
    if (codec != EWicCodecAvailability::Available)
    {
        // 只有注册表明未安装才跳过；已安装但解码失败仍然是真正的回归失败。
        std::printf(codec == EWicCodecAvailability::Missing
            ? "SKIP: WebP round-trip requires an installed WIC WebP decoder\n"
            : "FAIL: cannot enumerate WIC decoders\n");
        CoUninitialize();
        constexpr int kSkippedExitCode = 77;
        return codec == EWicCodecAvailability::Missing ? kSkippedExitCode : 1;
    }

    // 单像素：最短的合法码流
    {
        std::vector<unsigned char> rgb = { 200, 30, 90 };
        RoundTrip("单像素", rgb, 1, 1);
    }

    // 纯色：每个通道只有一个取值，专门打"单符号前缀码"这条边界
    {
        const int w = 64;
        const int h = 48;
        std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);

        for (size_t i = 0; i < rgb.size(); i += 3)
        {
            rgb[i + 0] = 17;
            rgb[i + 1] = 17;
            rgb[i + 2] = 17;
        }

        RoundTrip("纯色", rgb, w, h);
    }

    // 渐变：码长分布平缓
    {
        const int w = 257;   // 故意用奇数宽，顺便验证没有隐含的对齐假设
        const int h = 129;
        std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                unsigned char* p = &rgb[(static_cast<size_t>(y) * w + x) * 3];
                p[0] = static_cast<unsigned char>(x & 0xFF);
                p[1] = static_cast<unsigned char>(y & 0xFF);
                p[2] = static_cast<unsigned char>((x + y) & 0xFF);
            }
        }

        RoundTrip("渐变", rgb, w, h);
    }

    // 伪随机：频次接近均匀，Huffman 退化成定长码
    {
        const int w = 200;
        const int h = 150;
        std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);

        unsigned int state = 12345u;

        for (size_t i = 0; i < rgb.size(); ++i)
        {
            state = state * 1664525u + 1013904223u;
            rgb[i] = static_cast<unsigned char>((state >> 16) & 0xFF);
        }

        RoundTrip("伪随机噪声", rgb, w, h);
    }

    // 高度倾斜的分布：绝大多数像素同色，少量异色，会产生很长的码字
    {
        const int w = 320;
        const int h = 240;
        std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3, 8);

        for (int i = 0; i < 300; ++i)
        {
            const size_t offset = (static_cast<size_t>(i) * 977) % (rgb.size() / 3);
            rgb[offset * 3 + 0] = static_cast<unsigned char>(i & 0xFF);
            rgb[offset * 3 + 1] = static_cast<unsigned char>((i * 7) & 0xFF);
            rgb[offset * 3 + 2] = static_cast<unsigned char>((i * 13) & 0xFF);
        }

        RoundTrip("稀疏异色点", rgb, w, h);
    }

    std::printf("\n");

    if (gFailures == 0)
    {
        std::printf("WebP 往返全部通过\n");
    }
    else
    {
        std::printf("%d 项失败\n", gFailures);
    }

    CoUninitialize();

    return gFailures == 0 ? 0 : 1;
}
