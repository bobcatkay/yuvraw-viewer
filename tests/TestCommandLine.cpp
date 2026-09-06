// 命令行整数参数的离线自检：拒绝溢出、尾随字符与越界值。
#include "Core/FCommandLine.h"
#include "Image/FImageLimits.h"

#include <cstdio>
#include <string>

namespace
{
    constexpr int32_t kExistingWidth = 640;
    constexpr int32_t kExistingHeight = 480;
    constexpr int32_t kExistingStride = 640;
    constexpr int32_t kExistingBitsPerPixel = 10;
    constexpr int32_t kSampleWidthWithSuffix = 1920;

    int gFailures = 0;

    void Check(const char* Label, bool bCondition)
    {
        std::printf("%-62s %s\n", Label, bCondition ? "OK" : "**FAIL**");

        if (!bCondition)
        {
            ++gFailures;
        }
    }
}

int main()
{
    std::printf("=== 命令行有界整数解析 ===\n");

    const std::string validCommand =
        std::string("frame.raw --format RAW_SENSOR --width ") +
        std::to_string(FImageLimits::kMaximumDimension) +
        " --height " +
        std::to_string(FImageLimits::kMinimumDimension) +
        " --stride " +
        std::to_string(FImageLimits::kMinimumStrideBytes) +
        " --bits " +
        std::to_string(FImageLimits::kMaximumRawBitsPerSample);
    const FCommandLineOptions valid =
        FCommandLineOptions::Parse(validCommand.c_str());

    Check("合法边界 width 被接受",
          valid.bHasWidth &&
          valid.Params.Width == FImageLimits::kMaximumDimension);
    Check("合法最小 height 被接受",
          valid.bHasHeight &&
          valid.Params.Height == FImageLimits::kMinimumDimension);
    Check("stride=0 表示紧凑排列并被接受",
          valid.bHasStride &&
          valid.Params.Stride == FImageLimits::kMinimumStrideBytes);
    Check("RAW 16bit 边界被接受",
          valid.bHasBitsPerPixel &&
          valid.Params.BitsPerPixel == FImageLimits::kMaximumRawBitsPerSample);

    FImageLoadParams appliedValid;
    appliedValid.ByteOrder = EByteOrder::BigEndian;
    appliedValid.SampleAlignment =
        ESampleAlignment::MostSignificantBits;
    valid.ApplyTo(appliedValid);
    Check(
        "显式格式同步默认布局且 --bits 最后覆盖",
        appliedValid.Format == EImageFormat::Bayer16 &&
        appliedValid.BitsPerPixel == FImageLimits::kMaximumRawBitsPerSample &&
        appliedValid.ByteOrder == EByteOrder::LittleEndian &&
        appliedValid.SampleAlignment ==
            ESampleAlignment::LeastSignificantBits);

    const FCommandLineOptions packedFormatOnly =
        FCommandLineOptions::Parse("--format RAW14");
    appliedValid.Stride = kExistingStride;
    packedFormatOnly.ApplyTo(appliedValid);
    Check(
        "切换 packed RAW 恢复不适用属性的默认值",
        appliedValid.Format == EImageFormat::BayerPacked14 &&
        appliedValid.BitsPerPixel == FImageFormatDesc::Get(
            EImageFormat::BayerPacked14).BitDepth &&
        appliedValid.Stride == 0 &&
        appliedValid.ByteOrder == EByteOrder::LittleEndian &&
        appliedValid.SampleAlignment ==
            ESampleAlignment::LeastSignificantBits);

    const FCommandLineOptions rgb10A2FormatOnly =
        FCommandLineOptions::Parse("--format RGB10_A2");
    rgb10A2FormatOnly.ApplyTo(appliedValid);
    Check(
        "命令行识别 RGB10_A2 并约束为固定小端",
        appliedValid.Format == EImageFormat::RGB10A2 &&
        appliedValid.BitsPerPixel == FImageFormatDesc::Get(
            EImageFormat::RGB10A2).BitDepth &&
        appliedValid.Stride == 0 &&
        appliedValid.ByteOrder == EByteOrder::LittleEndian &&
        appliedValid.SampleAlignment ==
            ESampleAlignment::LeastSignificantBits);

    const FCommandLineOptions yuv420Sp16Options =
        FCommandLineOptions::Parse("--format YUV420SP16 --bits 10");
    yuv420Sp16Options.ApplyTo(appliedValid);
    Check(
        "命令行识别 YUV420SP16 并保留可配置有效位深",
        appliedValid.Format == EImageFormat::YUV420SP16 &&
        appliedValid.BitsPerPixel == kExistingBitsPerPixel &&
        appliedValid.ByteOrder == EByteOrder::LittleEndian &&
        appliedValid.SampleAlignment ==
            ESampleAlignment::LeastSignificantBits);

    FImageLoadParams dynamicAlignment;
    dynamicAlignment.SetDetectedFormat(EImageFormat::Bayer16);
    dynamicAlignment.BitsPerPixel = kExistingBitsPerPixel;
    dynamicAlignment.SampleAlignment =
        ESampleAlignment::MostSignificantBits;
    dynamicAlignment.ConstrainStorageLayout();
    const bool bHighAlignmentAccepted =
        dynamicAlignment.SampleAlignment ==
        ESampleAlignment::MostSignificantBits;
    dynamicAlignment.BitsPerPixel =
        FImageLimits::kMaximumRawBitsPerSample;
    dynamicAlignment.ConstrainStorageLayout();
    const bool bFullDepthFixedLow =
        dynamicAlignment.SampleAlignment ==
        ESampleAlignment::LeastSignificantBits;
    dynamicAlignment.BitsPerPixel = kExistingBitsPerPixel;
    dynamicAlignment.ConstrainStorageLayout();
    Check(
        "10bit 高位 -> 16bit 固定低位 -> 10bit 不恢复隐藏高位",
        bHighAlignmentAccepted &&
        bFullDepthFixedLow &&
        dynamicAlignment.SampleAlignment ==
            ESampleAlignment::LeastSignificantBits);

    const std::string trailingTextCommand =
        std::string("--width ") +
        std::to_string(kSampleWidthWithSuffix) +
        "px";
    const FCommandLineOptions trailingText =
        FCommandLineOptions::Parse(trailingTextCommand.c_str());
    Check("带尾随字符的 width 被拒绝", !trailingText.bHasWidth);

    const FCommandLineOptions negativeHeight =
        FCommandLineOptions::Parse("--height -1");
    Check("负 height 被拒绝", !negativeHeight.bHasHeight);

    const FCommandLineOptions zeroWidth =
        FCommandLineOptions::Parse("--width 0");
    Check("width=0 被拒绝", !zeroWidth.bHasWidth);

    const FCommandLineOptions overflowWidth =
        FCommandLineOptions::Parse("--width 999999999999999999999999");
    Check("整数溢出的 width 被拒绝", !overflowWidth.bHasWidth);

    const std::string oversizedStrideCommand =
        std::string("--stride ") +
        std::to_string(
            static_cast<long long>(FImageLimits::kMaximumStrideBytes) + 1);
    const FCommandLineOptions oversizedStride =
        FCommandLineOptions::Parse(oversizedStrideCommand.c_str());
    Check("超过内存边界的 stride 被拒绝", !oversizedStride.bHasStride);

    const std::string excessiveBitsCommand =
        std::string("--bits ") +
        std::to_string(FImageLimits::kMaximumRawBitsPerSample + 1);
    const FCommandLineOptions excessiveBits =
        FCommandLineOptions::Parse(excessiveBitsCommand.c_str());
    Check("超过容器位深的 bits 被拒绝", !excessiveBits.bHasBitsPerPixel);

    const FCommandLineOptions missingWidth =
        FCommandLineOptions::Parse("--width");
    Check("缺少值的 width 被拒绝", !missingWidth.bHasWidth);

    FImageLoadParams target;
    target.Width = kExistingWidth;
    target.Height = kExistingHeight;
    target.Stride = kExistingStride;
    target.BitsPerPixel = kExistingBitsPerPixel;

    const std::string allInvalidCommand =
        std::string("--width bad --height 0 --stride -1 --bits ") +
        std::to_string(FImageLimits::kMaximumRawBitsPerSample + 1);
    const FCommandLineOptions allInvalid =
        FCommandLineOptions::Parse(allInvalidCommand.c_str());
    allInvalid.ApplyTo(target);

    Check("无效参数不会覆盖既有加载参数",
          target.Width == kExistingWidth &&
          target.Height == kExistingHeight &&
          target.Stride == kExistingStride &&
          target.BitsPerPixel == kExistingBitsPerPixel);

    std::printf("\n==================================================\n");
    std::printf(gFailures == 0 ? "全部通过\n" : "%d 项失败\n", gFailures);

    return gFailures == 0 ? 0 : 1;
}
