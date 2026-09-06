// 文件名格式/分辨率解析的离线自检。不依赖 OpenGL / ImGui。
#include "Image/FImageFormat.h"
#include "Image/FImageLoadParams.h"
#include "Util.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    int gFailures = 0;

    void CheckCondition(const char* Label, bool Passed)
    {
        std::printf("%-36s %s\n", Label, Passed ? "OK" : "**FAIL**");

        if (!Passed)
        {
            ++gFailures;
        }
    }

    void Check(
        const char* Label,
        const char* Filename,
        EImageFormat ExpectedFormat,
        int32_t ExpectedWidth,
        int32_t ExpectedHeight)
    {
        int32_t width = 0;
        int32_t height = 0;
        const EImageFormat format = ParseImageInfoFromFilename(Filename, width, height);

        const bool passed =
            format == ExpectedFormat &&
            width == ExpectedWidth &&
            height == ExpectedHeight;

        std::printf(
            "%-36s fmt=%-3d size=%dx%d %s\n",
            Label,
            static_cast<int32_t>(format),
            width,
            height,
            passed ? "OK" : "**FAIL**");

        if (!passed)
        {
            ++gFailures;
            std::printf(
                "  want fmt=%d size=%dx%d, file=%s\n",
                static_cast<int32_t>(ExpectedFormat),
                ExpectedWidth,
                ExpectedHeight,
                Filename);
        }
    }
}

int main()
{
    std::printf("=== 文件名格式识别 ===\n");

    Check(
        "RGBA 样例不再误判 NV21",
        "output_025_frame_115_97568024564_3840x2880_RGBA.yuv",
        EImageFormat::RGBA8,
        3840,
        2880);
    Check("RGBA_8888 长词优先", "camera_1920x1080_RGBA_8888.raw", EImageFormat::RGBA8, 1920, 1080);
    Check("RGB10_A2 packed", "camera_1920x1080_RGB10_A2.raw", EImageFormat::RGB10A2, 1920, 1080);
    Check("RGBA_1010102 不误判 RGBA8", "camera_1920x1080_RGBA_1010102.raw", EImageFormat::RGB10A2, 1920, 1080);
    Check("RGB24", "frame_1280x720_rgb24.bin", EImageFormat::RGB8, 1280, 720);
    Check("ARGB 不误判 RGB", "frame_640x480_argb.bin", EImageFormat::NV21, 640, 480);

    Check("Gray_16 长词优先", "depth_640x480_gray_16.raw", EImageFormat::Grayscale16, 640, 480);
    Check("Grayscale8", "mono_800x600_grayscale8.raw", EImageFormat::Grayscale8, 800, 600);

    Check("P010LE", "movie_3840x2160_p010le.yuv", EImageFormat::P010, 3840, 2160);
    Check("YUV420SP16", "movie_4096x3072_yuv420sp16.yuv", EImageFormat::YUV420SP16, 4096, 3072);
    Check("P016LE 兼容别名", "movie_4096x3072_p016le.yuv", EImageFormat::YUV420SP16, 4096, 3072);
    Check("NV21", "camera_1440x1920_nv21.yuv", EImageFormat::NV21, 1440, 1920);
    Check("NV16", "camera_1920x1080_nv16.yuv", EImageFormat::NV16, 1920, 1080);
    Check("I420", "camera_1920x1080_i420.yuv", EImageFormat::YUV420P, 1920, 1080);
    Check("YV12", "camera_1920x1080_yv12.yuv", EImageFormat::YV12, 1920, 1080);
    Check("YUV422P", "camera_1920x1080_yuv422p.yuv", EImageFormat::YUV422P, 1920, 1080);
    Check("YUV444P", "camera_1920x1080_yuv444p.yuv", EImageFormat::YUV444P, 1920, 1080);
    Check("YUY2", "camera_1920x1080_yuy2.yuv", EImageFormat::YUY2, 1920, 1080);

    Check("Android RAW10", "android_4032x3024_RAW10.raw", EImageFormat::BayerPacked10, 4032, 3024);
    Check("Android RAW12", "android_4032x3024_RAW_12.raw", EImageFormat::BayerPacked12, 4032, 3024);
    Check("Android RAW14", "android_4032x3024_RAW14.raw", EImageFormat::BayerPacked14, 4032, 3024);
    Check("Bayer8 稳定名称", "sensor_4032x3024_Bayer8.raw", EImageFormat::Bayer8, 4032, 3024);
    Check("Bayer_14 未打包", "sensor_4032x3024_Bayer_14.raw", EImageFormat::Bayer14, 4032, 3024);
    Check("Bayer 通用名称", "sensor_4032x3024_Bayer_BGGR.raw", EImageFormat::Bayer16, 4032, 3024);

    Check("无格式 YUV 回退", "legacy_640x480.yuv", EImageFormat::NV21, 640, 480);
    Check("大写 YUV 扩展名回退", "legacy_640x480.YUV", EImageFormat::NV21, 640, 480);
    Check(
        "超长分辨率不会抛异常",
        "sensor_999999999999999999999x480.raw",
        EImageFormat::Raw,
        0,
        0);

    std::printf("\n=== 格式参数归一化 ===\n");
    FImageLoadParams detectedParams;
    detectedParams.ByteOrder = EByteOrder::BigEndian;
    detectedParams.SampleAlignment =
        ESampleAlignment::MostSignificantBits;
    detectedParams.SetDetectedFormat(EImageFormat::NV21);
    CheckCondition(
        "YUV 恢复不适用属性的默认值",
        detectedParams.ByteOrder == EByteOrder::LittleEndian &&
        detectedParams.SampleAlignment ==
            ESampleAlignment::LeastSignificantBits &&
        detectedParams.BitsPerPixel == 8);

    std::printf("\n=== Android NV21 stride 识别 ===\n");
    constexpr int32_t kEncodedWidth = 32;
    constexpr int32_t kVisibleWidth = 16;
    constexpr int32_t kHeight = 2;
    constexpr int32_t kRowCount = kHeight + kHeight / 2;
    constexpr uint8_t kActiveSample = 128;
    std::vector<uint8_t> paddedNv21(
        static_cast<size_t>(kEncodedWidth) * kRowCount,
        kActiveSample);

    for (int32_t row = 0; row < kRowCount; ++row)
    {
        const size_t paddingBegin =
            static_cast<size_t>(row) * kEncodedWidth + kVisibleWidth;
        std::fill(
            paddedNv21.begin() + paddingBegin,
            paddedNv21.begin() + static_cast<size_t>(row + 1) * kEncodedWidth,
            0);
    }

    const std::filesystem::path strideFixture =
        std::filesystem::temp_directory_path() /
        "YUVRaw_format35_32x2_nv21.yuv";
    {
        std::ofstream stream(strideFixture, std::ios::binary | std::ios::trunc);
        stream.write(
            reinterpret_cast<const char*>(paddedNv21.data()),
            static_cast<std::streamsize>(paddedNv21.size()));
    }

    int32_t resolvedWidth = 0;
    int32_t resolvedStride = 0;
    const bool bResolved = TryResolveAndroidSemiplanarStride(
        strideFixture.u8string(),
        EImageFormat::NV21,
        kEncodedWidth,
        kHeight,
        static_cast<uint64_t>(paddedNv21.size()),
        resolvedWidth,
        resolvedStride);
    CheckCondition(
        "format35 零后缀解析为可见宽度",
        bResolved &&
        resolvedWidth == kVisibleWidth &&
        resolvedStride == kEncodedWidth);

    std::error_code removeError;
    std::filesystem::remove(strideFixture, removeError);

    std::printf("\n%s\n", gFailures == 0 ? "全部通过" : "存在失败");

    return gFailures == 0 ? 0 : 1;
}
