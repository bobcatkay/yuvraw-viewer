// 导出流水线的离线自检：合成一帧带 padding 的 NV21，按四种目标格式导出，
// 再用 WIC 读回来验证尺寸与像素。不依赖 OpenGL / ImGui。
#include "Image/FImageData.h"
#include "Image/FImageExporter.h"
#include "Image/FImageFormatDesc.h"
#include "Image/FImageSampler.h"
#include "Image/FWicImageLoader.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "WicDecodeTestHelper.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

static int gFailures = 0;

static void Check(const char* What, bool bOk, const char* Detail = "")
{
    if (!bOk)
    {
        ++gFailures;
    }

    std::printf("%-52s %s %s\n", What, bOk ? "OK" : "**FAIL**", Detail);
}

static std::wstring ToWide(const std::string& Utf8)
{
    const int required = MultiByteToWideChar(CP_UTF8, 0, Utf8.c_str(), static_cast<int>(Utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, Utf8.c_str(), static_cast<int>(Utf8.size()), wide.data(), required);

    return wide;
}

/// 用 WIC 把文件解码成紧凑 RGB8；失败阶段与 HRESULT 由测试辅助记录。
static bool DecodeWithWic(const std::string& Path, int& OutWidth, int& OutHeight, std::vector<unsigned char>& OutRgb)
{
    return WicDecodeTestHelper::DecodeRgb8(ToWide(Path), OutWidth, OutHeight, OutRgb);
}
/**
 * 合成一帧 NV21，行尾带 padding —— 相机 dump 出来的就是这个样子，
 * 顺便验证导出路径没有把 stride 当成宽度用
 */
static std::unique_ptr<FImageData> MakeNv21(int32_t Width, int32_t Height, int32_t Stride)
{
    auto image = std::make_unique<FImageData>();
    image->SetSize(Width, Height);
    image->SetFormat(EImageFormat::NV21);
    image->SetStride(Stride);

    const size_t frameSize = FImageFormatDesc::CalculateFrameSize(EImageFormat::NV21, Width, Height, Stride);
    image->AllocatePixelData(frameSize);

    uint8_t* data = image->GetPixelData();

    // padding 填成一眼能看出来的值：万一被当成有效像素读进去，结果会明显不对
    std::fill(data, data + frameSize, static_cast<uint8_t>(0xA5));

    for (int32_t y = 0; y < Height; ++y)
    {
        uint8_t* row = data + static_cast<size_t>(y) * Stride;

        for (int32_t x = 0; x < Width; ++x)
        {
            row[x] = static_cast<uint8_t>(16 + (x * 219) / (Width - 1));
        }
    }

    uint8_t* chroma = data + static_cast<size_t>(Height) * Stride;

    for (int32_t y = 0; y < (Height + 1) / 2; ++y)
    {
        uint8_t* row = chroma + static_cast<size_t>(y) * Stride;

        for (int32_t x = 0; x < (Width + 1) / 2; ++x)
        {
            // NV21 是 V,U 顺序
            row[x * 2 + 0] = static_cast<uint8_t>(128 + (y % 5) * 8);
            row[x * 2 + 1] = static_cast<uint8_t>(128 - (x % 7) * 6);
        }
    }

    return image;
}

static std::string TempDirectory()
{
    wchar_t buffer[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, buffer);

    const std::filesystem::path directory = std::filesystem::path(buffer) / L"YUVRawExportTests";

    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    return directory.u8string();
}

static std::string UnicodeTempDirectory()
{
    const std::filesystem::path directory =
        std::filesystem::u8path(TempDirectory()) /
        std::filesystem::u8path(u8"中文路径");

    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    return directory.u8string();
}

static void TestUnicodeWicPath(const FImageData& Source)
{
    constexpr const char* kSourceFileName =
        u8"20250920-DSC08729-已增强-降噪.yuv";
    constexpr const char* kExpectedFileName =
        u8"20250920-DSC08729-已增强-降噪.png";

    FExportSettings settings;
    settings.Format = EExportFormat::PNG;
    settings.Display.ColorSpace = EColorSpace::BT601;
    settings.Display.ColorRange = EColorRange::Limited;
    settings.OutputDirectory = UnicodeTempDirectory();
    settings.bOverwrite = true;

    const std::string outputPath =
        FImageExporter::MakeOutputPath(settings, kSourceFileName);
    std::string error;
    const bool bExported = FImageExporter::Export(
        Source,
        EBayerPattern::RGGB,
        settings,
        outputPath,
        error);
    Check(u8"中文目录和文件名导出", bExported, error.c_str());

    if (!bExported)
    {
        return;
    }

    Check(
        u8"UTF-8 文件名保持不变",
        std::filesystem::u8path(outputPath).filename().u8string() ==
            kExpectedFileName,
        outputPath.c_str());

    FWicImageLoader loader;
    const std::unique_ptr<FImageData> loaded =
        loader.LoadFromFile(outputPath, nullptr);
    Check(
        u8"WIC 读取中文路径图像",
        loaded && loaded->IsValid() &&
            loaded->GetWidth() == Source.GetWidth() &&
            loaded->GetHeight() == Source.GetHeight(),
        outputPath.c_str());

    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(outputPath), ec);
    std::filesystem::remove(
        std::filesystem::u8path(settings.OutputDirectory),
        ec);
}

static void TestExternalWicFixture(const wchar_t* WidePath)
{
    if (!WidePath || !*WidePath)
    {
        return;
    }

    const std::string path = std::filesystem::path(WidePath).u8string();
    FWicImageLoader loader;
    const std::unique_ptr<FImageData> loaded =
        loader.LoadFromFile(path, nullptr);

    char detail[160] = {};

    if (loaded)
    {
        std::snprintf(
            detail,
            sizeof(detail),
            "%dx%d RGBA8",
            loaded->GetWidth(),
            loaded->GetHeight());
    }

    Check(
        u8"外部中文路径图像读取",
        loaded && loaded->IsValid(),
        loaded ? detail : path.c_str());
}

/**
 * 导出一次并把结果读回来
 * @param bLossless 无损格式要求逐像素与 CPU 转换结果一致
 */
static void RoundTrip(
    const char* Label,
    const FImageData& Source,
    EExportFormat Format,
    EExportResizeMode ResizeMode,
    float Percent,
    int32_t TargetWidth,
    int32_t ExpectedWidth,
    int32_t ExpectedHeight,
    bool bLossless)
{
    FExportSettings settings;
    settings.Format = Format;
    settings.Display.ColorSpace = EColorSpace::BT601;
    settings.Display.ColorRange = EColorRange::Limited;
    settings.ResizeMode = ResizeMode;
    settings.Percent = Percent;
    settings.TargetWidth = TargetWidth;
    settings.OutputDirectory = TempDirectory();
    settings.bOverwrite = true;

    const std::string outputPath = FImageExporter::MakeOutputPath(settings, "synthetic_nv21.yuv");

    std::string error;

    if (!FImageExporter::Export(Source, EBayerPattern::RGGB, settings, outputPath, error))
    {
        Check(Label, false, error.c_str());

        return;
    }

    if (Format == EExportFormat::WEBP)
    {
        const auto codec = FWicImageLoader::GetCodecAvailability(".webp");
        std::printf("WIC WebP decoder registration: %s\n",
                    codec == EWicCodecAvailability::Available ? "AVAILABLE" :
                    codec == EWicCodecAvailability::Missing ? "MISSING" : "UNKNOWN");
        if (codec == EWicCodecAvailability::Missing)
        {
            std::printf("%s: export OK; read-back SKIP (WIC WebP decoder missing)\n", Label);
            return;
        }
        if (codec == EWicCodecAvailability::Unknown)
        {
            Check(Label, false, "cannot enumerate WIC decoders");
            return;
        }
    }

    int32_t decodedWidth = 0;
    int32_t decodedHeight = 0;
    std::vector<unsigned char> decoded;

    if (!DecodeWithWic(outputPath, decodedWidth, decodedHeight, decoded))
    {
        Check(Label, false, "无法读回导出的文件");

        return;
    }

    if (decodedWidth != ExpectedWidth || decodedHeight != ExpectedHeight)
    {
        char detail[128];
        std::snprintf(detail, sizeof(detail), "尺寸 %dx%d，期望 %dx%d",
                      decodedWidth, decodedHeight, ExpectedWidth, ExpectedHeight);
        Check(Label, false, detail);

        return;
    }

    if (!bLossless)
    {
        Check(Label, true, "(有损，仅校验尺寸)");

        return;
    }

    // 无损路径：应当与 CPU 转换的结果逐字节一致（未缩放时）
    std::vector<unsigned char> expected;

    if (ResizeMode == EExportResizeMode::Original)
    {
        FDisplaySettings display;
        display.ColorSpace = EColorSpace::BT601;
        display.ColorRange = EColorRange::Limited;

        FImageSampler::ConvertToRgb8(Source, display, EBayerPattern::RGGB, expected);

        for (size_t i = 0; i < expected.size(); ++i)
        {
            if (expected[i] != decoded[i])
            {
                char detail[160];
                std::snprintf(detail, sizeof(detail), "第 %zu 字节 %u != %u",
                              i, static_cast<unsigned>(decoded[i]), static_cast<unsigned>(expected[i]));
                Check(Label, false, detail);

                return;
            }
        }
    }

    Check(Label, true);
}

int wmain(int ArgumentCount, wchar_t* Arguments[])
{
    if (!WicDecodeTestHelper::CheckResult("CoInitializeEx", CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    {
        return 1;
    }

    // 64 宽的 NV21，stride 80 —— 每行尾部 16 字节 padding
    const std::unique_ptr<FImageData> source = MakeNv21(64, 32, 80);

    std::printf("=== 导出往返（源：NV21 64x32，stride 80）===\n");

    RoundTrip("PNG 原始尺寸",  *source, EExportFormat::PNG,  EExportResizeMode::Original, 100.0f, 0, 64, 32, true);
    RoundTrip("BMP 原始尺寸",  *source, EExportFormat::BMP,  EExportResizeMode::Original, 100.0f, 0, 64, 32, true);
    RoundTrip("WebP 原始尺寸", *source, EExportFormat::WEBP, EExportResizeMode::Original, 100.0f, 0, 64, 32, true);
    RoundTrip("JPEG 原始尺寸", *source, EExportFormat::JPEG, EExportResizeMode::Original, 100.0f, 0, 64, 32, false);

    std::printf("\n=== 等比例缩放 ===\n");

    RoundTrip("PNG 缩小到 50%",   *source, EExportFormat::PNG,  EExportResizeMode::Percent, 50.0f,  0, 32, 16, true);
    RoundTrip("PNG 放大到 200%",  *source, EExportFormat::PNG,  EExportResizeMode::Percent, 200.0f, 0, 128, 64, true);
    RoundTrip("WebP 指定宽 128",  *source, EExportFormat::WEBP, EExportResizeMode::Width,   100.0f, 128, 128, 64, true);
    RoundTrip("BMP 指定宽 16",    *source, EExportFormat::BMP,  EExportResizeMode::Width,   100.0f, 16, 16, 8, true);

    std::printf("\n=== UTF-8 文件路径 ===\n");
    TestUnicodeWicPath(*source);

    if (ArgumentCount > 1)
    {
        TestExternalWicFixture(Arguments[1]);
    }

    std::printf("\n=== 输出路径 ===\n");

    {
        FExportSettings settings;
        settings.Format = EExportFormat::PNG;
        settings.OutputDirectory = TempDirectory();
        settings.bOverwrite = false;

        // 上面已经写过 synthetic_nv21.png，不覆盖时应当让开
        const std::string path = FImageExporter::MakeOutputPath(settings, "synthetic_nv21.yuv");

        std::error_code ec;
        Check("不覆盖时自动改名", !std::filesystem::exists(std::filesystem::u8path(path), ec), path.c_str());

        settings.bOverwrite = true;
        const std::string overwritePath = FImageExporter::MakeOutputPath(settings, "synthetic_nv21.yuv");
        Check("覆盖时保持原名",
              std::filesystem::u8path(overwritePath).filename().u8string() == "synthetic_nv21.png");
    }

    std::printf("\n=== 色彩矩阵可用性 ===\n");

    Check("NV21 需要色彩矩阵", FImageExporter::NeedsColorMatrix(EImageFormat::NV21, EExportFormat::PNG));
    Check("RGBA8 不需要色彩矩阵", !FImageExporter::NeedsColorMatrix(EImageFormat::RGBA8, EExportFormat::PNG));
    Check("RGB10_A2 不需要色彩矩阵", !FImageExporter::NeedsColorMatrix(EImageFormat::RGB10A2, EExportFormat::PNG));
    Check("Bayer16 不需要色彩矩阵", !FImageExporter::NeedsColorMatrix(EImageFormat::Bayer16, EExportFormat::JPEG));

    std::printf("\n");

    if (gFailures == 0)
    {
        std::printf("导出自检全部通过\n");
    }
    else
    {
        std::printf("%d 项失败\n", gFailures);
    }

    CoUninitialize();

    return gFailures == 0 ? 0 : 1;
}
