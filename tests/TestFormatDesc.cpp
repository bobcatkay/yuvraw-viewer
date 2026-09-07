// 格式描述表的离线自检。不依赖 OpenGL / ImGui。
#include "Image/FImageFormatDesc.h"
#include "Image/FImageLimits.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <iterator>
#include <limits>
#include <string>

static int gFailures = 0;

namespace
{
    constexpr size_t kExpectedFormatCount = 29;
    constexpr int32_t kRgb10A2EnumValue = 27;
    constexpr int32_t kYuv420Sp16EnumValue = 28;
    constexpr int32_t kRgb10A2BytesPerPixel = 4;
    constexpr int32_t kRgb10A2PaddingBytes = 8;
    constexpr int32_t kTenBitSourceDepth = 10;
    constexpr int32_t kMinimumYuv420Sp16BitDepth = 8;
    constexpr int32_t kFullContainerBitDepth = 16;
    constexpr int32_t kTenBitHighAlignmentShift =
        kFullContainerBitDepth - kTenBitSourceDepth;
    constexpr size_t kExpectedRgbDisplayFormatCount = 5;
    constexpr std::array<
        EImageFormat,
        kExpectedRgbDisplayFormatCount> kExpectedRgbDisplayOrder = {
        EImageFormat::RGB8,
        EImageFormat::RGBA8,
        EImageFormat::RGB10A2,
        EImageFormat::RGB16,
        EImageFormat::RGBA16,
    };

    struct FExpectedStorageLayout
    {
        EImageFormat Format;
        EFormatPropertyMode ByteOrderMode;
        EByteOrder DefaultByteOrder;
        EFormatPropertyMode SampleAlignmentMode;
        ESampleAlignment DefaultSampleAlignment;
    };

    // 显式列出每个枚举，确保追加格式时必须同步决定两个属性的适用性与默认值。
    constexpr std::array<FExpectedStorageLayout, kExpectedFormatCount>
        kExpectedStorageLayouts = {{
            { EImageFormat::Unknown,       EFormatPropertyMode::NotApplicable, EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::RGB8,          EFormatPropertyMode::NotApplicable, EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::RGBA8,         EFormatPropertyMode::NotApplicable, EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::RGB16,         EFormatPropertyMode::Configurable,  EByteOrder::LittleEndian, EFormatPropertyMode::Fixed,         ESampleAlignment::LeastSignificantBits },
            { EImageFormat::RGBA16,        EFormatPropertyMode::Configurable,  EByteOrder::LittleEndian, EFormatPropertyMode::Fixed,         ESampleAlignment::LeastSignificantBits },
            { EImageFormat::YUV420P,       EFormatPropertyMode::NotApplicable, EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::YUV422P,       EFormatPropertyMode::NotApplicable, EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::YUV444P,       EFormatPropertyMode::NotApplicable, EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::NV12,          EFormatPropertyMode::NotApplicable, EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::NV21,          EFormatPropertyMode::NotApplicable, EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::P010,          EFormatPropertyMode::Fixed,         EByteOrder::LittleEndian, EFormatPropertyMode::Fixed,         ESampleAlignment::MostSignificantBits },
            { EImageFormat::Grayscale8,    EFormatPropertyMode::NotApplicable, EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::Grayscale16,   EFormatPropertyMode::Configurable,  EByteOrder::LittleEndian, EFormatPropertyMode::Fixed,         ESampleAlignment::LeastSignificantBits },
            { EImageFormat::Raw,           EFormatPropertyMode::NotApplicable, EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::YV12,          EFormatPropertyMode::NotApplicable, EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::NV16,          EFormatPropertyMode::NotApplicable, EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::P210,          EFormatPropertyMode::Fixed,         EByteOrder::LittleEndian, EFormatPropertyMode::Fixed,         ESampleAlignment::MostSignificantBits },
            { EImageFormat::YUY2,          EFormatPropertyMode::NotApplicable, EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::UYVY,          EFormatPropertyMode::NotApplicable, EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::Bayer8,        EFormatPropertyMode::NotApplicable, EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::Bayer16,       EFormatPropertyMode::Configurable,  EByteOrder::LittleEndian, EFormatPropertyMode::Configurable,  ESampleAlignment::LeastSignificantBits },
            { EImageFormat::BayerPacked10, EFormatPropertyMode::NotApplicable, EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::BayerPacked12, EFormatPropertyMode::NotApplicable, EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::Bayer10,       EFormatPropertyMode::Configurable,  EByteOrder::LittleEndian, EFormatPropertyMode::Fixed,         ESampleAlignment::LeastSignificantBits },
            { EImageFormat::Bayer12,       EFormatPropertyMode::Configurable,  EByteOrder::LittleEndian, EFormatPropertyMode::Fixed,         ESampleAlignment::LeastSignificantBits },
            { EImageFormat::Bayer14,       EFormatPropertyMode::Configurable,  EByteOrder::LittleEndian, EFormatPropertyMode::Fixed,         ESampleAlignment::LeastSignificantBits },
            { EImageFormat::BayerPacked14, EFormatPropertyMode::NotApplicable, EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::RGB10A2,       EFormatPropertyMode::Fixed,         EByteOrder::LittleEndian, EFormatPropertyMode::NotApplicable, ESampleAlignment::LeastSignificantBits },
            { EImageFormat::YUV420SP16,    EFormatPropertyMode::Configurable,  EByteOrder::LittleEndian, EFormatPropertyMode::Configurable,  ESampleAlignment::LeastSignificantBits },
        }};
}

static void Check(const char* What, long long Got, long long Want)
{
    const bool ok = (Got == Want);

    if (!ok)
    {
        ++gFailures;
    }

    std::printf("%-58s got=%-12lld want=%-12lld %s\n", What, Got, Want, ok ? "OK" : "**FAIL**");
}

static void CheckFrame(EImageFormat Fmt, int W, int H, int Stride, size_t Want)
{
    const FFormatDesc& d = FImageFormatDesc::Get(Fmt);
    std::string label = std::string(d.Name) + " frame " + std::to_string(W) + "x" + std::to_string(H)
                      + " stride=" + std::to_string(Stride);
    Check(label.c_str(), (long long)FImageFormatDesc::CalculateFrameSize(Fmt, W, H, Stride), (long long)Want);
}

static void CheckStorageLayoutPolicies()
{
    const auto& all = FImageFormatDesc::GetAll();
    Check(
        "存储布局矩阵覆盖 GetAll 全部格式",
        static_cast<long long>(all.size()),
        static_cast<long long>(kExpectedStorageLayouts.size()));

    const size_t testedCount =
        std::min(all.size(), kExpectedStorageLayouts.size());

    for (size_t index = 0; index < testedCount; ++index)
    {
        const FExpectedStorageLayout& expected =
            kExpectedStorageLayouts[index];
        const FFormatDesc& desc = all[index];
        const std::string prefix =
            std::string(desc.Name) + " 存储布局 ";

        Check(
            (prefix + "Format").c_str(),
            static_cast<long long>(desc.Format),
            static_cast<long long>(expected.Format));
        Check(
            (prefix + "字节序 mode").c_str(),
            static_cast<long long>(desc.StorageLayout.ByteOrderMode),
            static_cast<long long>(expected.ByteOrderMode));
        Check(
            (prefix + "字节序 default").c_str(),
            static_cast<long long>(desc.StorageLayout.DefaultByteOrder),
            static_cast<long long>(expected.DefaultByteOrder));
        Check(
            (prefix + "有效位对齐 mode").c_str(),
            static_cast<long long>(
                desc.StorageLayout.SampleAlignmentMode),
            static_cast<long long>(expected.SampleAlignmentMode));
        Check(
            (prefix + "有效位对齐 default").c_str(),
            static_cast<long long>(
                desc.StorageLayout.DefaultSampleAlignment),
            static_cast<long long>(expected.DefaultSampleAlignment));

        const FByteOrderPropertyState littleByteOrder =
            FImageFormatDesc::ResolveByteOrder(
                expected.Format,
                EByteOrder::LittleEndian);
        const FByteOrderPropertyState bigByteOrder =
            FImageFormatDesc::ResolveByteOrder(
                expected.Format,
                EByteOrder::BigEndian);
        const EByteOrder expectedLittleRequest =
            expected.ByteOrderMode == EFormatPropertyMode::Configurable
                ? EByteOrder::LittleEndian
                : expected.DefaultByteOrder;
        const EByteOrder expectedBigRequest =
            expected.ByteOrderMode == EFormatPropertyMode::Configurable
                ? EByteOrder::BigEndian
                : expected.DefaultByteOrder;

        Check(
            (prefix + "字节序 resolver mode").c_str(),
            static_cast<long long>(bigByteOrder.Mode),
            static_cast<long long>(expected.ByteOrderMode));
        Check(
            (prefix + "字节序 resolver 小端请求").c_str(),
            static_cast<long long>(littleByteOrder.Value),
            static_cast<long long>(expectedLittleRequest));
        Check(
            (prefix + "字节序 resolver 大端请求").c_str(),
            static_cast<long long>(bigByteOrder.Value),
            static_cast<long long>(expectedBigRequest));

        // 可变位深格式用 10bit 探测对齐可配置分支；16bit 动态固定行为在下方验证。
        const int32_t probeBitDepth =
            desc.EffectiveBitDepth.Mode == EFormatPropertyMode::Configurable
                ? kTenBitSourceDepth
                : desc.BitDepth;
        const FSampleAlignmentPropertyState lowAlignment =
            FImageFormatDesc::ResolveSampleAlignment(
                expected.Format,
                probeBitDepth,
                ESampleAlignment::LeastSignificantBits);
        const FSampleAlignmentPropertyState highAlignment =
            FImageFormatDesc::ResolveSampleAlignment(
                expected.Format,
                probeBitDepth,
                ESampleAlignment::MostSignificantBits);
        const ESampleAlignment expectedLowRequest =
            expected.SampleAlignmentMode ==
                    EFormatPropertyMode::Configurable
                ? ESampleAlignment::LeastSignificantBits
                : expected.DefaultSampleAlignment;
        const ESampleAlignment expectedHighRequest =
            expected.SampleAlignmentMode ==
                    EFormatPropertyMode::Configurable
                ? ESampleAlignment::MostSignificantBits
                : expected.DefaultSampleAlignment;

        Check(
            (prefix + "有效位 resolver mode").c_str(),
            static_cast<long long>(highAlignment.Mode),
            static_cast<long long>(expected.SampleAlignmentMode));
        Check(
            (prefix + "有效位 resolver 低位请求").c_str(),
            static_cast<long long>(lowAlignment.Value),
            static_cast<long long>(expectedLowRequest));
        Check(
            (prefix + "有效位 resolver 高位请求").c_str(),
            static_cast<long long>(highAlignment.Value),
            static_cast<long long>(expectedHighRequest));
    }
}

static void CheckDynamicSampleAlignment()
{
    const FSampleAlignmentPropertyState bayer10High =
        FImageFormatDesc::ResolveSampleAlignment(
            EImageFormat::Bayer16,
            kTenBitSourceDepth,
            ESampleAlignment::MostSignificantBits);
    Check(
        "Bayer16 10bit 高位对齐仍可配置",
        static_cast<long long>(bayer10High.Mode),
        static_cast<long long>(EFormatPropertyMode::Configurable));
    Check(
        "Bayer16 10bit 高位请求被保留",
        static_cast<long long>(bayer10High.Value),
        static_cast<long long>(
            ESampleAlignment::MostSignificantBits));
    Check(
        "Bayer16 10bit 高位对齐 shift",
        FImageFormatDesc::CalculateSampleShift(
            EImageFormat::Bayer16,
            kTenBitSourceDepth,
            ESampleAlignment::MostSignificantBits),
        kTenBitHighAlignmentShift);
    Check(
        "Bayer16 10bit 低位对齐 shift",
        FImageFormatDesc::CalculateSampleShift(
            EImageFormat::Bayer16,
            kTenBitSourceDepth,
            ESampleAlignment::LeastSignificantBits),
        0);

    const FSampleAlignmentPropertyState bayer16High =
        FImageFormatDesc::ResolveSampleAlignment(
            EImageFormat::Bayer16,
            kFullContainerBitDepth,
            ESampleAlignment::MostSignificantBits);
    Check(
        "Bayer16 16bit 对齐动态固定",
        static_cast<long long>(bayer16High.Mode),
        static_cast<long long>(EFormatPropertyMode::Fixed));
    Check(
        "Bayer16 16bit 动态固定为低位",
        static_cast<long long>(bayer16High.Value),
        static_cast<long long>(
            ESampleAlignment::LeastSignificantBits));
    Check(
        "Bayer16 16bit 无对齐 shift",
        FImageFormatDesc::CalculateSampleShift(
            EImageFormat::Bayer16,
            kFullContainerBitDepth,
            ESampleAlignment::MostSignificantBits),
        0);

    constexpr std::array<EImageFormat, 2> kFixedHighFormats = {
        EImageFormat::P010,
        EImageFormat::P210,
    };

    for (EImageFormat format : kFixedHighFormats)
    {
        const FFormatDesc& desc = FImageFormatDesc::Get(format);
        const std::string prefix =
            std::string(desc.Name) + " 固定存储布局 ";
        const FByteOrderPropertyState byteOrder =
            FImageFormatDesc::ResolveByteOrder(
                format,
                EByteOrder::BigEndian);
        const FSampleAlignmentPropertyState alignment =
            FImageFormatDesc::ResolveSampleAlignment(
                format,
                kTenBitSourceDepth,
                ESampleAlignment::LeastSignificantBits);

        Check(
            (prefix + "字节序 mode").c_str(),
            static_cast<long long>(byteOrder.Mode),
            static_cast<long long>(EFormatPropertyMode::Fixed));
        Check(
            (prefix + "忽略大端请求").c_str(),
            static_cast<long long>(byteOrder.Value),
            static_cast<long long>(EByteOrder::LittleEndian));
        Check(
            (prefix + "有效位 mode").c_str(),
            static_cast<long long>(alignment.Mode),
            static_cast<long long>(EFormatPropertyMode::Fixed));
        Check(
            (prefix + "忽略低位请求").c_str(),
            static_cast<long long>(alignment.Value),
            static_cast<long long>(
                ESampleAlignment::MostSignificantBits));
        Check(
            (prefix + "高位对齐 shift").c_str(),
            FImageFormatDesc::CalculateSampleShift(
                format,
                kTenBitSourceDepth,
                ESampleAlignment::LeastSignificantBits),
            kTenBitHighAlignmentShift);
    }
}

static void CheckYuv420Sp16Properties()
{
    const FBitDepthPropertyState yuv420Sp16TenBit =
        FImageFormatDesc::ResolveBitDepth(
            EImageFormat::YUV420SP16,
            kTenBitSourceDepth);
    Check(
        "YUV420SP16 位深可配置",
        static_cast<long long>(yuv420Sp16TenBit.Mode),
        static_cast<long long>(EFormatPropertyMode::Configurable));
    Check("YUV420SP16 保留 10bit 请求", yuv420Sp16TenBit.Value, kTenBitSourceDepth);
    Check("YUV420SP16 最小有效位深", yuv420Sp16TenBit.Minimum, kMinimumYuv420Sp16BitDepth);
    Check("YUV420SP16 最大有效位深", yuv420Sp16TenBit.Maximum, kFullContainerBitDepth);

    const FByteOrderPropertyState yuv420Sp16BigEndian =
        FImageFormatDesc::ResolveByteOrder(
            EImageFormat::YUV420SP16,
            EByteOrder::BigEndian);
    Check(
        "YUV420SP16 保留大端请求",
        static_cast<long long>(yuv420Sp16BigEndian.Value),
        static_cast<long long>(EByteOrder::BigEndian));

    const FSampleAlignmentPropertyState yuv420Sp16High =
        FImageFormatDesc::ResolveSampleAlignment(
            EImageFormat::YUV420SP16,
            kTenBitSourceDepth,
            ESampleAlignment::MostSignificantBits);
    Check(
        "YUV420SP16 10bit 高位对齐可配置",
        static_cast<long long>(yuv420Sp16High.Mode),
        static_cast<long long>(EFormatPropertyMode::Configurable));
    Check(
        "YUV420SP16 10bit 保留高位请求",
        static_cast<long long>(yuv420Sp16High.Value),
        static_cast<long long>(ESampleAlignment::MostSignificantBits));
    Check(
        "YUV420SP16 10bit 高位对齐 shift",
        FImageFormatDesc::CalculateSampleShift(
            EImageFormat::YUV420SP16,
            kTenBitSourceDepth,
            ESampleAlignment::MostSignificantBits),
        kTenBitHighAlignmentShift);

    const FSampleAlignmentPropertyState yuv420Sp16FullDepth =
        FImageFormatDesc::ResolveSampleAlignment(
            EImageFormat::YUV420SP16,
            kFullContainerBitDepth,
            ESampleAlignment::MostSignificantBits);
    Check(
        "YUV420SP16 16bit 对齐动态固定",
        static_cast<long long>(yuv420Sp16FullDepth.Mode),
        static_cast<long long>(EFormatPropertyMode::Fixed));
    Check(
        "YUV420SP16 16bit 固定为低位",
        static_cast<long long>(yuv420Sp16FullDepth.Value),
        static_cast<long long>(ESampleAlignment::LeastSignificantBits));

    const FBitDepthPropertyState p010Fixed =
        FImageFormatDesc::ResolveBitDepth(
            EImageFormat::P010,
            kFullContainerBitDepth);
    Check(
        "P010 位深仍为固定",
        static_cast<long long>(p010Fixed.Mode),
        static_cast<long long>(EFormatPropertyMode::Fixed));
    Check("P010 忽略 16bit 请求", p010Fixed.Value, kTenBitSourceDepth);
}

static void CheckFormatDisplayOrder()
{
    const std::vector<EImageFormat>& displayOrder =
        FImageFormatDesc::GetDisplayOrder();
    Check(
        "显示顺序覆盖全部格式",
        static_cast<long long>(displayOrder.size()),
        static_cast<long long>(kExpectedFormatCount));

    if (displayOrder.size() != kExpectedFormatCount)
    {
        return;
    }

    Check(
        "显示顺序首项为 Unknown",
        static_cast<long long>(displayOrder.front()),
        static_cast<long long>(EImageFormat::Unknown));

    std::array<bool, kExpectedFormatCount> seen = {};
    bool bAllFormatsValidAndUnique = true;

    for (EImageFormat format : displayOrder)
    {
        const size_t index = static_cast<size_t>(format);

        if (index >= seen.size() || seen[index])
        {
            bAllFormatsValidAndUnique = false;
            continue;
        }

        seen[index] = true;
    }

    bAllFormatsValidAndUnique =
        bAllFormatsValidAndUnique &&
        std::all_of(
            seen.begin(),
            seen.end(),
            [](bool bSeen) { return bSeen; });
    Check(
        "显示顺序无遗漏或重复",
        bAllFormatsValidAndUnique ? 1 : 0,
        1);

    for (size_t index = 0;
         index < kExpectedRgbDisplayOrder.size();
         ++index)
    {
        const std::string label =
            "RGB 显示分组第 " +
            std::to_string(index + 1) +
            " 项";
        Check(
            label.c_str(),
            static_cast<long long>(displayOrder[index + 1]),
            static_cast<long long>(kExpectedRgbDisplayOrder[index]));
    }

    const auto p010It =
        std::find(
            displayOrder.begin(),
            displayOrder.end(),
            EImageFormat::P010);
    const bool bYuv420Sp16FollowsP010 =
        p010It != displayOrder.end() &&
        std::next(p010It) != displayOrder.end() &&
        *std::next(p010It) == EImageFormat::YUV420SP16;
    Check(
        "YUV420SP16 在 UI 中紧跟 P010",
        bYuv420Sp16FollowsP010 ? 1 : 0,
        1);
}

int main()
{
    std::printf("=== 表与枚举的对应关系 ===\n");
    const auto& all = FImageFormatDesc::GetAll();

    for (size_t i = 0; i < all.size(); ++i)
    {
        if (static_cast<size_t>(all[i].Format) != i)
        {
            std::printf("**FAIL** 下标 %zu 的 Format 是 %d，表顺序与枚举不一致\n", i, (int)all[i].Format);
            ++gFailures;
        }
    }

    std::printf("表内 %zu 项，顺序校验完成\n\n", all.size());
    Check(
        "RGB10_A2 枚举持久化值保持不变",
        static_cast<long long>(EImageFormat::RGB10A2),
        kRgb10A2EnumValue);
    Check(
        "YUV420SP16 枚举持久化值保持不变",
        static_cast<long long>(EImageFormat::YUV420SP16),
        kYuv420Sp16EnumValue);

    const FFormatDesc& yuv420Sp16Desc =
        FImageFormatDesc::Get(EImageFormat::YUV420SP16);
    Check(
        "YUV420SP16 内建格式名",
        std::string(yuv420Sp16Desc.Name) == "YUV420SP16" ? 1 : 0,
        1);
    Check(
        "YUV420SP16 UI 显示名",
        std::string(yuv420Sp16Desc.DisplayName) == "YUV420SP 16bit" ? 1 : 0,
        1);

    std::printf("\n=== 用户可见格式顺序 ===\n");
    CheckFormatDisplayOrder();

    std::printf("=== 字节序 / 有效位对齐策略矩阵 ===\n");
    CheckStorageLayoutPolicies();

    std::printf("\n=== 有效位对齐动态行为 ===\n");
    CheckDynamicSampleAlignment();
    CheckYuv420Sp16Properties();

    std::printf("=== 帧大小（紧凑排列） ===\n");
    CheckFrame(EImageFormat::NV21,    1920, 1080, 0, 1920ull * 1080 * 3 / 2);
    CheckFrame(EImageFormat::NV12,    1920, 1080, 0, 1920ull * 1080 * 3 / 2);
    CheckFrame(EImageFormat::YUV420P, 1920, 1080, 0, 1920ull * 1080 * 3 / 2);
    CheckFrame(EImageFormat::YV12,    1920, 1080, 0, 1920ull * 1080 * 3 / 2);
    CheckFrame(EImageFormat::YUV422P, 1920, 1080, 0, 1920ull * 1080 * 2);
    CheckFrame(EImageFormat::YUV444P, 1920, 1080, 0, 1920ull * 1080 * 3);
    CheckFrame(EImageFormat::NV16,    1920, 1080, 0, 1920ull * 1080 * 2);
    CheckFrame(EImageFormat::P010,    1920, 1080, 0, 1920ull * 1080 * 3);       // 16bit 采样, 4:2:0
    CheckFrame(EImageFormat::YUV420SP16, 1920, 1080, 0, 1920ull * 1080 * 3);    // 16bit 容器, 4:2:0
    CheckFrame(EImageFormat::P210,    1920, 1080, 0, 1920ull * 1080 * 4);       // 16bit 采样, 4:2:2
    CheckFrame(EImageFormat::YUY2,    1920, 1080, 0, 1920ull * 1080 * 2);
    CheckFrame(EImageFormat::UYVY,    1920, 1080, 0, 1920ull * 1080 * 2);
    CheckFrame(EImageFormat::RGB8,    1920, 1080, 0, 1920ull * 1080 * 3);
    CheckFrame(EImageFormat::RGBA8,   1920, 1080, 0, 1920ull * 1080 * 4);
    CheckFrame(EImageFormat::RGB16,   1920, 1080, 0, 1920ull * 1080 * 6);
    CheckFrame(EImageFormat::RGB10A2, 1920, 1080, 0, 1920ull * 1080 * kRgb10A2BytesPerPixel);
    CheckFrame(EImageFormat::Grayscale8,  1920, 1080, 0, 1920ull * 1080);
    CheckFrame(EImageFormat::Grayscale16, 1920, 1080, 0, 1920ull * 1080 * 2);
    CheckFrame(EImageFormat::Bayer8,  1920, 1080, 0, 1920ull * 1080);
    CheckFrame(EImageFormat::Bayer16, 1920, 1080, 0, 1920ull * 1080 * 2);
    CheckFrame(EImageFormat::Bayer10, 1920, 1080, 0, 1920ull * 1080 * 2);
    CheckFrame(EImageFormat::Bayer12, 1920, 1080, 0, 1920ull * 1080 * 2);
    CheckFrame(EImageFormat::Bayer14, 1920, 1080, 0, 1920ull * 1080 * 2);
    // MIPI RAW10: 4 像素 5 字节 -> 每行 W*5/4
    CheckFrame(EImageFormat::BayerPacked10, 1920, 1080, 0, 1920ull * 5 / 4 * 1080);
    // MIPI RAW12: 2 像素 3 字节 -> 每行 W*3/2
    CheckFrame(EImageFormat::BayerPacked12, 1920, 1080, 0, 1920ull * 3 / 2 * 1080);
    // Android RAW14: 4 像素 7 字节 -> 每行 W*7/4
    CheckFrame(EImageFormat::BayerPacked14, 1920, 1080, 0, 1920ull * 7 / 4 * 1080);

    std::printf("\n=== 真实测试素材（带 stride padding） ===\n");
    // texture_video_format35_1472x1920.yuv 实际是 1440x1920, stride 1472
    CheckFrame(EImageFormat::NV21, 1440, 1920, 1472, 4239360ull);
    // input_000_frame_132_..._3840x2880.yuv 无 padding
    CheckFrame(EImageFormat::NV21, 3840, 2880, 0, 16588800ull);

    std::printf("\n=== 平面几何：NV21 1440x1920 stride 1472 ===\n");
    {
        const FFormatDesc& d = FImageFormatDesc::Get(EImageFormat::NV21);
        const int stride = 1472;
        Check("Y  平面宽",        FImageFormatDesc::GetPlaneWidth(d, 0, 1440), 1440);
        Check("Y  平面高",        FImageFormatDesc::GetPlaneHeight(d, 0, 1920), 1920);
        Check("Y  RowLength(像素)", FImageFormatDesc::GetPlaneRowLength(d, 0, stride), 1472);
        Check("UV 平面宽",        FImageFormatDesc::GetPlaneWidth(d, 1, 1440), 720);
        Check("UV 平面高",        FImageFormatDesc::GetPlaneHeight(d, 1, 1920), 960);
        Check("UV RowLength(像素)", FImageFormatDesc::GetPlaneRowLength(d, 1, stride), 736);
        Check("UV 字节偏移",       (long long)FImageFormatDesc::GetPlaneOffsetBytes(d, 1, 1920, stride), 1472ll * 1920);
    }

    std::printf("\n=== 平面几何：P010 1920x1080 紧凑 ===\n");
    {
        const FFormatDesc& d = FImageFormatDesc::Get(EImageFormat::P010);
        const int stride = FImageFormatDesc::ResolveBaseStride(EImageFormat::P010, 1920, 0);
        Check("基准 stride(字节)",  stride, 3840);
        Check("Y  RowLength(像素)", FImageFormatDesc::GetPlaneRowLength(d, 0, stride), 1920);
        Check("UV RowLength(像素)", FImageFormatDesc::GetPlaneRowLength(d, 1, stride), 960);
        Check("UV 字节偏移",        (long long)FImageFormatDesc::GetPlaneOffsetBytes(d, 1, 1080, stride), 3840ll * 1080);
    }

    std::printf("\n=== 平面几何：I420 1920x1080 紧凑（U/V 平面 stride 减半） ===\n");
    {
        const FFormatDesc& d = FImageFormatDesc::Get(EImageFormat::YUV420P);
        const int stride = 1920;
        Check("U 平面每行字节",  FImageFormatDesc::GetPlaneStrideBytes(d, 1, stride), 960);
        Check("U 字节偏移",      (long long)FImageFormatDesc::GetPlaneOffsetBytes(d, 1, 1080, stride), 1920ll * 1080);
        Check("V 字节偏移",      (long long)FImageFormatDesc::GetPlaneOffsetBytes(d, 2, 1080, stride), 1920ll * 1080 + 960ll * 540);
    }

    std::printf("\n=== 奇数宽高：色度平面向上取整 ===\n");
    {
        constexpr int32_t kOddWidth = 1441;
        constexpr int32_t kOddHeight = 1081;
        constexpr int32_t kExpectedChromaWidth = 721;
        constexpr int32_t kExpectedChromaHeight = 541;
        const FFormatDesc& d = FImageFormatDesc::Get(EImageFormat::NV21);
        Check(
            "UV 平面宽 (W=1441)",
            FImageFormatDesc::GetPlaneWidth(d, 1, kOddWidth),
            kExpectedChromaWidth);
        Check(
            "UV 平面高 (H=1081)",
            FImageFormatDesc::GetPlaneHeight(d, 1, kOddHeight),
            kExpectedChromaHeight);
        Check(
            "NV21 奇数宽为交错 UV 多留一个字节",
            FImageFormatDesc::ResolveBaseStride(
                EImageFormat::NV21,
                kOddWidth,
                0),
            kExpectedChromaWidth * 2);
        Check(
            "P010 奇数宽为 16bit 交错 UV 留足空间",
            FImageFormatDesc::ResolveBaseStride(
                EImageFormat::P010,
                kOddWidth,
                0),
            kExpectedChromaWidth * 4);
        Check(
            "YUV420SP16 奇数宽为 16bit 交错 UV 留足空间",
            FImageFormatDesc::ResolveBaseStride(
                EImageFormat::YUV420SP16,
                kOddWidth,
                0),
            kExpectedChromaWidth * 4);

        const FFormatDesc& planar =
            FImageFormatDesc::Get(EImageFormat::YUV420P);
        const int32_t oddBaseStride =
            FImageFormatDesc::ResolveBaseStride(
                EImageFormat::YUV420P,
                kOddWidth,
                0);
        Check(
            "I420 奇数基准 stride 的色度 stride 向上取整",
            FImageFormatDesc::GetPlaneStrideBytes(planar, 1, oddBaseStride),
            kExpectedChromaWidth);
    }

    std::printf("\n=== 平面几何：RGB10_A2 1920x1080 ===\n");
    {
        constexpr int32_t kWidth = 1920;
        constexpr int32_t kHeight = 1080;
        const FFormatDesc& d = FImageFormatDesc::Get(EImageFormat::RGB10A2);
        const int32_t compactStride =
            FImageFormatDesc::ResolveBaseStride(
                EImageFormat::RGB10A2,
                kWidth,
                0);
        const int32_t paddedStride =
            compactStride + kRgb10A2PaddingBytes;

        Check("RGB10_A2 平面数", d.PlaneCount, 1);
        Check(
            "RGB10_A2 紧凑 stride",
            compactStride,
            kWidth * kRgb10A2BytesPerPixel);
        Check(
            "RGB10_A2 RowLength(像素)",
            FImageFormatDesc::GetPlaneRowLength(d, 0, compactStride),
            kWidth);
        CheckFrame(
            EImageFormat::RGB10A2,
            kWidth,
            kHeight,
            paddedStride,
            static_cast<size_t>(paddedStride) * kHeight);
    }

    std::printf("\n=== 尺寸与内存安全边界 ===\n");
    {
        constexpr int32_t kBoundaryWidth = 16384;
        constexpr int32_t kBoundaryHeight = static_cast<int32_t>(
            FImageLimits::kMaximumPixelCount / static_cast<size_t>(kBoundaryWidth));
        constexpr int32_t kAboveBoundaryHeight = kBoundaryHeight + 1;
        constexpr int32_t kSmallWidth = 4;
        constexpr int32_t kSmallHeight = 2;
        const int32_t oversizedStride =
            FImageLimits::kMaximumStrideBytes + 1;
        const int32_t compactRgbStride =
            FImageFormatDesc::ResolveBaseStride(
                EImageFormat::RGB8,
                kSmallWidth,
                0);
        const int32_t frameOverflowStride =
            static_cast<int32_t>(
                FImageLimits::kMaximumFrameBytes /
                static_cast<size_t>(kSmallHeight)) + 1;

        Check(
            "边界 RGBA8 帧等于最大帧内存",
            static_cast<long long>(FImageFormatDesc::CalculateFrameSize(
                EImageFormat::RGBA8,
                kBoundaryWidth,
                kBoundaryHeight,
                0)),
            static_cast<long long>(FImageLimits::kMaximumFrameBytes));
        Check(
            "超过最大像素数返回 0",
            static_cast<long long>(FImageFormatDesc::CalculateFrameSize(
                EImageFormat::Grayscale8,
                kBoundaryWidth,
                kAboveBoundaryHeight,
                0)),
            0);
        Check(
            "负 stride 返回 0",
            FImageFormatDesc::ResolveBaseStride(
                EImageFormat::RGB8,
                kSmallWidth,
                -1),
            0);
        Check(
            "不足一行的显式 stride 返回 0",
            FImageFormatDesc::ResolveBaseStride(
                EImageFormat::RGB8,
                kSmallWidth,
                compactRgbStride - 1),
            0);
        Check(
            "超过最大单边尺寸的 stride 返回 0",
            FImageFormatDesc::ResolveBaseStride(
                EImageFormat::BayerPacked10,
                FImageLimits::kMaximumDimension + 1,
                0),
            0);
        Check(
            "超过最大 stride 返回 0",
            FImageFormatDesc::ResolveBaseStride(
                EImageFormat::Grayscale8,
                kSmallWidth,
                oversizedStride),
            0);
        Check(
            "小图异常 stride 导致超大帧时返回 0",
            static_cast<long long>(FImageFormatDesc::CalculateFrameSize(
                EImageFormat::Grayscale8,
                kSmallWidth,
                kSmallHeight,
                frameOverflowStride)),
            0);

        size_t checkedValue = 0;
        Check(
            "size_t 乘法回绕被拒绝",
            FImageLimits::TryMultiplySize(
                std::numeric_limits<size_t>::max(),
                2u,
                checkedValue),
            0);
        Check(
            "size_t 加法回绕被拒绝",
            FImageLimits::TryAddSize(
                std::numeric_limits<size_t>::max(),
                1u,
                checkedValue),
            0);
    }

    std::printf("\n=== 名字查找 ===\n");
    Check("FindByName(\"nv21\")",  (long long)FImageFormatDesc::FindByName("nv21"),  (long long)EImageFormat::NV21);
    Check("FindByName(\"P010\")",  (long long)FImageFormatDesc::FindByName("P010"),  (long long)EImageFormat::P010);
    Check("FindByName(\"yuv420sp16\")", (long long)FImageFormatDesc::FindByName("yuv420sp16"), (long long)EImageFormat::YUV420SP16);
    Check("FindByName(\"p016\") alias", (long long)FImageFormatDesc::FindByName("p016"), (long long)EImageFormat::YUV420SP16);
    Check("FindByName(\"I420\")",  (long long)FImageFormatDesc::FindByName("I420"),  (long long)EImageFormat::YUV420P);
    Check("FindByName(\"Bayer10\")", (long long)FImageFormatDesc::FindByName("Bayer10"), (long long)EImageFormat::Bayer10);
    Check("FindByName(\"RAW10\")", (long long)FImageFormatDesc::FindByName("RAW10"), (long long)EImageFormat::BayerPacked10);
    Check("FindByName(\"RAW12\")", (long long)FImageFormatDesc::FindByName("RAW12"), (long long)EImageFormat::BayerPacked12);
    Check("FindByName(\"RAW14\")", (long long)FImageFormatDesc::FindByName("RAW14"), (long long)EImageFormat::BayerPacked14);
    Check("FindByName(\"RAW_SENSOR\")", (long long)FImageFormatDesc::FindByName("RAW_SENSOR"), (long long)EImageFormat::Bayer16);
    Check("FindByName(\"RGB10_A2\")", (long long)FImageFormatDesc::FindByName("RGB10_A2"), (long long)EImageFormat::RGB10A2);
    Check("FindByName(\"rgba_1010102\")", (long long)FImageFormatDesc::FindByName("rgba_1010102"), (long long)EImageFormat::RGB10A2);
    Check("FindByName(\"nope\")",  (long long)FImageFormatDesc::FindByName("nope"),  (long long)EImageFormat::Unknown);

    std::printf("\n==================================================\n");

    if (gFailures == 0)
    {
        std::printf("全部通过\n");
    }
    else
    {
        std::printf("%d 项失败\n", gFailures);
    }

    return gFailures == 0 ? 0 : 1;
}
