#include "FImageFormatDesc.h"
#include "FImageLimits.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>

namespace
{
    constexpr size_t kBitsPerByte = 8u;
    constexpr int32_t kMinimumYuv420Sp16BitDepth = 8;
    constexpr int32_t kMaximumWordBitDepth = 16;

    /**
     * 构造一个平面描述的简写
     */
    constexpr FPlaneDesc MakePlane(
        int32_t WidthShift,
        int32_t HeightShift,
        int32_t ChannelCount,
        int32_t BytesPerSample,
        int32_t StrideDivisor)
    {
        return FPlaneDesc{ WidthShift, HeightShift, ChannelCount, BytesPerSample, StrideDivisor };
    }

    // 常用平面
    constexpr FPlaneDesc FullY8   = MakePlane(0, 0, 1, 1, 1);   // 全尺寸 8bit 单通道
    constexpr FPlaneDesc FullY16  = MakePlane(0, 0, 1, 2, 1);   // 全尺寸 16bit 单通道
    constexpr FPlaneDesc UV420_8  = MakePlane(1, 1, 2, 1, 1);   // 4:2:0 交织色度 8bit
    constexpr FPlaneDesc UV420_16 = MakePlane(1, 1, 2, 2, 1);   // 4:2:0 交织色度 16bit
    constexpr FPlaneDesc UV422_8  = MakePlane(1, 0, 2, 1, 1);   // 4:2:2 交织色度 8bit
    constexpr FPlaneDesc UV422_16 = MakePlane(1, 0, 2, 2, 1);   // 4:2:2 交织色度 16bit
    constexpr FPlaneDesc C420_8   = MakePlane(1, 1, 1, 1, 2);   // 4:2:0 独立色度平面
    constexpr FPlaneDesc C422_8   = MakePlane(1, 0, 1, 1, 2);   // 4:2:2 独立色度平面
    constexpr FPlaneDesc C444_8   = MakePlane(0, 0, 1, 1, 1);   // 4:4:4 独立色度平面

    constexpr FStorageLayoutDesc ConfigurableByteOrder()
    {
        return {
            EFormatPropertyMode::Configurable,
            EByteOrder::LittleEndian,
            EFormatPropertyMode::Fixed,
            ESampleAlignment::LeastSignificantBits,
        };
    }

    constexpr FStorageLayoutDesc FixedLittleHighAlignment()
    {
        return {
            EFormatPropertyMode::Fixed,
            EByteOrder::LittleEndian,
            EFormatPropertyMode::Fixed,
            ESampleAlignment::MostSignificantBits,
        };
    }

    constexpr FStorageLayoutDesc FixedLittlePackedLayout()
    {
        return {
            EFormatPropertyMode::Fixed,
            EByteOrder::LittleEndian,
            EFormatPropertyMode::NotApplicable,
            ESampleAlignment::LeastSignificantBits,
        };
    }

    constexpr FStorageLayoutDesc ConfigurableWordLayout()
    {
        return {
            EFormatPropertyMode::Configurable,
            EByteOrder::LittleEndian,
            EFormatPropertyMode::Configurable,
            ESampleAlignment::LeastSignificantBits,
        };
    }

    constexpr FEffectiveBitDepthDesc ConfigurableBitDepth(
        int32_t Minimum,
        int32_t Maximum)
    {
        return {
            EFormatPropertyMode::Configurable,
            Minimum,
            Maximum,
        };
    }

    /**
     * 格式描述表
     *
     * 顺序必须与 EImageFormat 枚举一致 —— Get() 直接按下标取。
     */
    const std::vector<FFormatDesc>& BuildTable()
    {
        static const std::vector<FFormatDesc> Table = []
        {
            std::vector<FFormatDesc> t;

            auto Add = [&t](
                EImageFormat Format,
                const char* Name,
                const char* DisplayName,
                EColorModel ColorModel,
                int32_t BitDepth,
                std::initializer_list<FPlaneDesc> Planes,
                bool bSwapChroma = false,
                bool bIsPacked = false,
                int32_t BitsPerPixelPacked = 0,
                int32_t SampleShift = 0,
                FStorageLayoutDesc StorageLayout = {},
                FEffectiveBitDepthDesc EffectiveBitDepth = {})
            {
                FFormatDesc d;
                d.Format = Format;
                d.Name = Name;
                d.DisplayName = DisplayName;
                d.ColorModel = ColorModel;
                d.BitDepth = BitDepth;
                d.PlaneCount = static_cast<int32_t>(Planes.size());
                d.bSwapChroma = bSwapChroma;
                d.bIsPacked = bIsPacked;
                d.bNeedsExplicitSize = true;
                d.BitsPerPixelPacked = BitsPerPixelPacked;
                d.SampleShift = SampleShift;
                d.StorageLayout = StorageLayout;
                d.EffectiveBitDepth = EffectiveBitDepth;

                int32_t i = 0;

                for (const FPlaneDesc& p : Planes)
                {
                    if (i < 3)
                    {
                        d.Planes[i] = p;
                    }

                    ++i;
                }

                t.push_back(d);
            };

            // 顺序与 EImageFormat 严格对应
            Add(EImageFormat::Unknown,       "Unknown",       u8"未知",       EColorModel::RGB,  8, {});
            Add(EImageFormat::RGB8,          "RGB8",          u8"RGB8",       EColorModel::RGB,  8, { MakePlane(0, 0, 3, 1, 1) });
            Add(EImageFormat::RGBA8,         "RGBA8",         u8"RGBA8",      EColorModel::RGB,  8, { MakePlane(0, 0, 4, 1, 1) });
            Add(EImageFormat::RGB16,         "RGB16",         u8"RGB16",      EColorModel::RGB, 16, { MakePlane(0, 0, 3, 2, 1) }, false, false, 0, 0, ConfigurableByteOrder());
            Add(EImageFormat::RGBA16,        "RGBA16",        u8"RGBA16",     EColorModel::RGB, 16, { MakePlane(0, 0, 4, 2, 1) }, false, false, 0, 0, ConfigurableByteOrder());
            Add(EImageFormat::YUV420P,       "I420",          u8"I420 (YUV420P)", EColorModel::YUV, 8, { FullY8, C420_8, C420_8 });
            Add(EImageFormat::YUV422P,       "YUV422P",       u8"YUV422P",    EColorModel::YUV,  8, { FullY8, C422_8, C422_8 });
            Add(EImageFormat::YUV444P,       "YUV444P",       u8"YUV444P",    EColorModel::YUV,  8, { FullY8, C444_8, C444_8 });
            Add(EImageFormat::NV12,          "NV12",          u8"NV12",       EColorModel::YUV,  8, { FullY8, UV420_8 });
            Add(EImageFormat::NV21,          "NV21",          u8"NV21",       EColorModel::YUV,  8, { FullY8, UV420_8 }, /*bSwapChroma=*/true);
            Add(EImageFormat::P010,          "P010",          u8"P010 (10bit)", EColorModel::YUV, 10, { FullY16, UV420_16 }, false, false, 0, /*SampleShift=*/6, FixedLittleHighAlignment());
            Add(EImageFormat::Grayscale8,    "Gray8",         u8"灰度8",      EColorModel::Gray, 8, { FullY8 });
            Add(EImageFormat::Grayscale16,   "Gray16",        u8"灰度16",     EColorModel::Gray, 16, { FullY16 }, false, false, 0, 0, ConfigurableByteOrder());
            Add(EImageFormat::Raw,           "Raw",           u8"RAW (原始字节)", EColorModel::Gray, 8, { FullY8 });

            // --- Phase 2 追加 ---
            Add(EImageFormat::YV12,          "YV12",          u8"YV12",       EColorModel::YUV,  8, { FullY8, C420_8, C420_8 }, /*bSwapChroma=*/true);
            Add(EImageFormat::NV16,          "NV16",          u8"NV16 (4:2:2)", EColorModel::YUV, 8, { FullY8, UV422_8 });
            Add(EImageFormat::P210,          "P210",          u8"P210 (4:2:2 10bit)", EColorModel::YUV, 10, { FullY16, UV422_16 }, false, false, 0, /*SampleShift=*/6, FixedLittleHighAlignment());
            // packed 4:2:2：一个纹素打包 2 个像素（Y0 U Y1 V），因此纹理宽度取半、4 通道
            Add(EImageFormat::YUY2,          "YUY2",          u8"YUY2 (packed)", EColorModel::YUV, 8, { MakePlane(1, 0, 4, 1, 1) }, false, /*bIsPacked=*/true);
            Add(EImageFormat::UYVY,          "UYVY",          u8"UYVY (packed)", EColorModel::YUV, 8, { MakePlane(1, 0, 4, 1, 1) }, /*bSwapChroma=*/true, /*bIsPacked=*/true);
            Add(EImageFormat::Bayer8,        "Bayer8",        u8"Bayer 8bit", EColorModel::Bayer, 8, { FullY8 });
            Add(EImageFormat::Bayer16,       "Bayer16",       u8"Bayer 16bit", EColorModel::Bayer, 16, { FullY16 }, false, false, 0, 0, ConfigurableWordLayout(), ConfigurableBitDepth(FImageLimits::kMinimumRawBitsPerSample, FImageLimits::kMaximumRawBitsPerSample));
            // MIPI packed：行字节数不是整像素倍数，用 BitsPerPixelPacked 描述
            Add(EImageFormat::BayerPacked10, "BayerPacked10", u8"Bayer MIPI RAW10", EColorModel::Bayer, 10, { FullY8 }, false, /*bIsPacked=*/true, /*BitsPerPixelPacked=*/10);
            Add(EImageFormat::BayerPacked12, "BayerPacked12", u8"Bayer MIPI RAW12", EColorModel::Bayer, 12, { FullY8 }, false, /*bIsPacked=*/true, /*BitsPerPixelPacked=*/12);

            // --- Android Bayer RAW 扩展（顺序与追加的枚举严格对应）---
            Add(EImageFormat::Bayer10,        "Bayer10",        u8"Bayer 10bit (16bit 容器)", EColorModel::Bayer, 10, { FullY16 }, false, false, 0, 0, ConfigurableByteOrder());
            Add(EImageFormat::Bayer12,        "Bayer12",        u8"Bayer 12bit (16bit 容器)", EColorModel::Bayer, 12, { FullY16 }, false, false, 0, 0, ConfigurableByteOrder());
            Add(EImageFormat::Bayer14,        "Bayer14",        u8"Bayer 14bit (16bit 容器)", EColorModel::Bayer, 14, { FullY16 }, false, false, 0, 0, ConfigurableByteOrder());
            Add(EImageFormat::BayerPacked14, "BayerPacked14", u8"Android RAW14 (packed)", EColorModel::Bayer, 14, { FullY8 }, false, /*bIsPacked=*/true, /*BitsPerPixelPacked=*/14);

            // GL_UNSIGNED_INT_2_10_10_10_REV：一个小端 uint32_t 纹素，RGB 各 10bit、A 2bit。
            // 平面仍写成 4x1，使通用 stride / 帧大小 / 纹理行几何保持每像素 4 字节。
            Add(EImageFormat::RGB10A2,        "RGB10_A2",       u8"RGB10_A2 (packed)", EColorModel::RGB, 10, { MakePlane(0, 0, 4, 1, 1) }, false, /*bIsPacked=*/true, 0, 0, FixedLittlePackedLayout());

            // YUV420SP16 是通用 16bit 半平面容器：既覆盖标准 P016 数据，也用于
            // 描述相机 dump 中常见的 10/12/14bit 低位或高位对齐变体。
            Add(EImageFormat::YUV420SP16,     "YUV420SP16",    u8"YUV420SP 16bit", EColorModel::YUV, kMaximumWordBitDepth, { FullY16, UV420_16 }, false, false, 0, 0, ConfigurableWordLayout(), ConfigurableBitDepth(kMinimumYuv420Sp16BitDepth, kMaximumWordBitDepth));

            return t;
        }();

        return Table;
    }

    /**
     * 构造面向用户的格式顺序，不改动枚举/描述表的追加顺序。
     *
     * RGB 格式按位深稳定排序，使后追加的 packed RGB 仍与 RGB8/RGB16 放在一起；
     * 非 RGB 格式通常保留描述表中的相对顺序；追加的 YUV420SP16 在 UI 中紧跟 P010，
     * 让相同平面布局的两种格式相邻，同时不改变持久化枚举值。
     */
    const std::vector<EImageFormat>& BuildDisplayOrder()
    {
        static const std::vector<EImageFormat> Order = []
        {
            const std::vector<FFormatDesc>& table = BuildTable();
            std::vector<EImageFormat> rgbFormats;
            std::vector<EImageFormat> otherFormats;
            rgbFormats.reserve(table.size());
            otherFormats.reserve(table.size());

            for (const FFormatDesc& desc : table)
            {
                if (desc.Format == EImageFormat::Unknown)
                {
                    continue;
                }

                if (desc.ColorModel == EColorModel::RGB)
                {
                    rgbFormats.push_back(desc.Format);
                }
                else if (desc.Format != EImageFormat::YUV420SP16)
                {
                    otherFormats.push_back(desc.Format);

                    if (desc.Format == EImageFormat::P010)
                    {
                        otherFormats.push_back(EImageFormat::YUV420SP16);
                    }
                }
            }

            std::stable_sort(
                rgbFormats.begin(),
                rgbFormats.end(),
                [&table](EImageFormat Left, EImageFormat Right)
                {
                    return table[static_cast<size_t>(Left)].BitDepth <
                        table[static_cast<size_t>(Right)].BitDepth;
                });

            std::vector<EImageFormat> order;
            order.reserve(table.size());
            order.push_back(EImageFormat::Unknown);
            order.insert(
                order.end(),
                rgbFormats.begin(),
                rgbFormats.end());
            order.insert(
                order.end(),
                otherFormats.begin(),
                otherFormats.end());

            return order;
        }();

        return Order;
    }

    /** 向上取整的移位：ceil(Value / 2^Shift) */
    int32_t ShiftCeil(int32_t Value, int32_t Shift)
    {
        if (Value <= 0 || Shift < 0 || Shift >= std::numeric_limits<int32_t>::digits)
        {
            return 0;
        }

        if (Shift == 0)
        {
            return Value;
        }

        const int32_t divisor = 1 << Shift;

        // 避免 Value + divisor - 1 在接近 INT32_MAX 时溢出。
        return Value / divisor + ((Value % divisor) != 0 ? 1 : 0);
    }
}

namespace FImageFormatDesc
{
    const std::vector<FFormatDesc>& GetAll()
    {
        return BuildTable();
    }

    const std::vector<EImageFormat>& GetDisplayOrder()
    {
        return BuildDisplayOrder();
    }

    const FFormatDesc& Get(EImageFormat Format)
    {
        const std::vector<FFormatDesc>& table = BuildTable();
        const size_t index = static_cast<size_t>(Format);

        if (index < table.size())
        {
            return table[index];
        }

        return table[0];
    }

    EImageFormat FindByName(const char* Name)
    {
        if (!Name)
        {
            return EImageFormat::Unknown;
        }

        std::string needle(Name);
        std::transform(needle.begin(), needle.end(), needle.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        for (const FFormatDesc& d : BuildTable())
        {
            std::string candidate(d.Name);
            std::transform(candidate.begin(), candidate.end(), candidate.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (candidate == needle)
            {
                return d.Format;
            }
        }

        // Android ImageFormat 的公开名称映射到本工具的具体内存布局。
        // RAW_SENSOR 是 16bit 容器；RAW10/12/14 则是逐行紧密打包，不能混为同一格式。
        struct FFormatAlias
        {
            const char* Name;
            EImageFormat Format;
        };

        static constexpr FFormatAlias Aliases[] = {
            { "raw_sensor",  EImageFormat::Bayer16 },
            { "rawsensor",   EImageFormat::Bayer16 },
            { "raw16",       EImageFormat::Bayer16 },
            { "androidraw16", EImageFormat::Bayer16 },
            { "raw10",       EImageFormat::BayerPacked10 },
            { "androidraw10", EImageFormat::BayerPacked10 },
            { "raw12",       EImageFormat::BayerPacked12 },
            { "androidraw12", EImageFormat::BayerPacked12 },
            { "raw14",       EImageFormat::BayerPacked14 },
            { "androidraw14", EImageFormat::BayerPacked14 },
            { "rgb10a2",      EImageFormat::RGB10A2 },
            { "rgba1010102",  EImageFormat::RGB10A2 },
            { "rgba_1010102", EImageFormat::RGB10A2 },
            // 标准 P016 与该通用 16bit 半平面容器布局兼容；保留为输入别名。
            { "p016",         EImageFormat::YUV420SP16 },
            { "p016le",       EImageFormat::YUV420SP16 },
        };

        for (const FFormatAlias& alias : Aliases)
        {
            if (needle == alias.Name)
            {
                return alias.Format;
            }
        }

        return EImageFormat::Unknown;
    }

    int32_t GetPlane0BytesPerPixel(EImageFormat Format)
    {
        const FFormatDesc& d = Get(Format);

        if (d.PlaneCount <= 0 ||
            d.Planes[0].ChannelCount <= 0 ||
            d.Planes[0].BytesPerSample <= 0)
        {
            return 0;
        }

        size_t bytesPerPixel = 0;

        if (!FImageLimits::TryMultiplySize(
                static_cast<size_t>(d.Planes[0].ChannelCount),
                static_cast<size_t>(d.Planes[0].BytesPerSample),
                bytesPerPixel) ||
            bytesPerPixel > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
        {
            return 0;
        }

        return static_cast<int32_t>(bytesPerPixel);
    }

    int32_t GetPlaneWidth(const FFormatDesc& Desc, int32_t PlaneIndex, int32_t Width)
    {
        if (PlaneIndex < 0 ||
            PlaneIndex >= Desc.PlaneCount ||
            Width < FImageLimits::kMinimumDimension ||
            Width > FImageLimits::kMaximumDimension)
        {
            return 0;
        }

        return ShiftCeil(Width, Desc.Planes[PlaneIndex].WidthShift);
    }

    int32_t GetPlaneHeight(const FFormatDesc& Desc, int32_t PlaneIndex, int32_t Height)
    {
        if (PlaneIndex < 0 ||
            PlaneIndex >= Desc.PlaneCount ||
            Height < FImageLimits::kMinimumDimension ||
            Height > FImageLimits::kMaximumDimension)
        {
            return 0;
        }

        return ShiftCeil(Height, Desc.Planes[PlaneIndex].HeightShift);
    }

    int32_t ResolveBaseStride(EImageFormat Format, int32_t Width, int32_t InStride)
    {
        if (Width < FImageLimits::kMinimumDimension ||
            Width > FImageLimits::kMaximumDimension ||
            InStride < FImageLimits::kMinimumStrideBytes ||
            InStride > FImageLimits::kMaximumStrideBytes)
        {
            return 0;
        }

        const FFormatDesc& d = Get(Format);

        if (d.PlaneCount <= 0)
        {
            return 0;
        }

        size_t strideBytes = 0;

        if (d.BitsPerPixelPacked > 0)
        {
            // Android RAW10/12/14 每像素不足整字节，用位数算再向上取整。
            const int32_t planeWidth = GetPlaneWidth(d, 0, Width);
            size_t rowBits = 0;
            size_t roundedRowBits = 0;

            if (planeWidth <= 0 ||
                !FImageLimits::TryMultiplySize(
                    static_cast<size_t>(planeWidth),
                    static_cast<size_t>(d.BitsPerPixelPacked),
                    rowBits) ||
                !FImageLimits::TryAddSize(
                    rowBits,
                    kBitsPerByte - 1u,
                    roundedRowBits))
            {
                return 0;
            }

            strideBytes = roundedRowBits / kBitsPerByte;
        }
        else
        {
            // BaseStride 同时控制所有平面。奇数宽的 NV12/P010 色度行可能比 Y 行
            // 多一个样本，因此必须取所有平面对 base stride 的最大需求。
            for (int32_t planeIndex = 0; planeIndex < d.PlaneCount; ++planeIndex)
            {
                const FPlaneDesc& plane = d.Planes[planeIndex];
                const int32_t planeWidth = GetPlaneWidth(d, planeIndex, Width);
                const int32_t divisor =
                    plane.StrideDivisor > 0 ? plane.StrideDivisor : 1;
                size_t bytesPerPixel = 0;
                size_t planeRowBytes = 0;
                size_t scaledRowBytes = 0;
                size_t requiredBaseStride = 0;

                if (planeWidth <= 0 ||
                    plane.ChannelCount <= 0 ||
                    plane.BytesPerSample <= 0 ||
                    !FImageLimits::TryMultiplySize(
                        static_cast<size_t>(plane.ChannelCount),
                        static_cast<size_t>(plane.BytesPerSample),
                        bytesPerPixel) ||
                    !FImageLimits::TryMultiplySize(
                        static_cast<size_t>(planeWidth),
                        bytesPerPixel,
                        planeRowBytes) ||
                    planeRowBytes == 0 ||
                    !FImageLimits::TryMultiplySize(
                        planeRowBytes - 1u,
                        static_cast<size_t>(divisor),
                        scaledRowBytes) ||
                    !FImageLimits::TryAddSize(
                        scaledRowBytes,
                        1u,
                        requiredBaseStride))
                {
                    return 0;
                }

                strideBytes = std::max(strideBytes, requiredBaseStride);
            }
        }

        if (strideBytes == 0 ||
            strideBytes > static_cast<size_t>(FImageLimits::kMaximumStrideBytes))
        {
            return 0;
        }

        const int32_t compactStride = static_cast<int32_t>(strideBytes);

        // 显式 stride 也必须能容纳一整行；否则后续按宽度采样会越过行边界。
        if (InStride > 0)
        {
            return InStride >= compactStride ? InStride : 0;
        }

        return compactStride;
    }

    int32_t GetPlaneStrideBytes(const FFormatDesc& Desc, int32_t PlaneIndex, int32_t BaseStride)
    {
        if (PlaneIndex < 0 ||
            PlaneIndex >= Desc.PlaneCount ||
            BaseStride <= 0 ||
            BaseStride > FImageLimits::kMaximumStrideBytes)
        {
            return 0;
        }

        const int32_t divisor = Desc.Planes[PlaneIndex].StrideDivisor > 0
            ? Desc.Planes[PlaneIndex].StrideDivisor
            : 1;

        // 色度平面的基准 stride 可能是奇数，必须向上取整才能覆盖最后一个样本。
        const int32_t planeStride =
            BaseStride / divisor +
            ((BaseStride % divisor) != 0 ? 1 : 0);
        return planeStride > 0 ? planeStride : 0;
    }

    int32_t GetPlaneRowLength(const FFormatDesc& Desc, int32_t PlaneIndex, int32_t BaseStride)
    {
        if (PlaneIndex < 0 || PlaneIndex >= Desc.PlaneCount)
        {
            return 0;
        }

        const FPlaneDesc& p = Desc.Planes[PlaneIndex];
        size_t bytesPerPixel = 0;

        if (p.ChannelCount <= 0 ||
            p.BytesPerSample <= 0 ||
            !FImageLimits::TryMultiplySize(
                static_cast<size_t>(p.ChannelCount),
                static_cast<size_t>(p.BytesPerSample),
                bytesPerPixel) ||
            bytesPerPixel >
                static_cast<size_t>(std::numeric_limits<int32_t>::max()))
        {
            return 0;
        }

        return GetPlaneStrideBytes(Desc, PlaneIndex, BaseStride) /
               static_cast<int32_t>(bytesPerPixel);
    }

    size_t GetPlaneSizeBytes(const FFormatDesc& Desc, int32_t PlaneIndex, int32_t Height, int32_t BaseStride)
    {
        if (PlaneIndex < 0 || PlaneIndex >= Desc.PlaneCount)
        {
            return 0;
        }

        const int32_t rows = GetPlaneHeight(Desc, PlaneIndex, Height);
        const int32_t strideBytes = GetPlaneStrideBytes(Desc, PlaneIndex, BaseStride);

        if (rows <= 0 || strideBytes <= 0)
        {
            return 0;
        }

        size_t planeSize = 0;

        if (!FImageLimits::TryMultiplySize(
                static_cast<size_t>(strideBytes),
                static_cast<size_t>(rows),
                planeSize) ||
            planeSize > FImageLimits::kMaximumFrameBytes)
        {
            return 0;
        }

        return planeSize;
    }

    size_t GetPlaneOffsetBytes(const FFormatDesc& Desc, int32_t PlaneIndex, int32_t Height, int32_t BaseStride)
    {
        if (PlaneIndex < 0 || PlaneIndex >= Desc.PlaneCount)
        {
            return 0;
        }

        size_t offset = 0;

        for (int32_t i = 0; i < PlaneIndex && i < Desc.PlaneCount; ++i)
        {
            const size_t planeSize = GetPlaneSizeBytes(Desc, i, Height, BaseStride);
            size_t nextOffset = 0;

            if (planeSize == 0 ||
                !FImageLimits::TryAddSize(offset, planeSize, nextOffset) ||
                nextOffset > FImageLimits::kMaximumFrameBytes)
            {
                return 0;
            }

            offset = nextOffset;
        }

        return offset;
    }

    size_t CalculateFrameSize(EImageFormat Format, int32_t Width, int32_t Height, int32_t InStride)
    {
        if (!FImageLimits::AreDimensionsSupported(Width, Height))
        {
            return 0;
        }

        const FFormatDesc& d = Get(Format);

        if (d.PlaneCount <= 0)
        {
            return 0;
        }

        const int32_t baseStride = ResolveBaseStride(Format, Width, InStride);

        if (baseStride <= 0)
        {
            return 0;
        }

        size_t total = 0;

        for (int32_t i = 0; i < d.PlaneCount; ++i)
        {
            const size_t planeSize = GetPlaneSizeBytes(d, i, Height, baseStride);
            size_t nextTotal = 0;

            if (planeSize == 0 ||
                !FImageLimits::TryAddSize(total, planeSize, nextTotal) ||
                nextTotal > FImageLimits::kMaximumFrameBytes)
            {
                return 0;
            }

            total = nextTotal;
        }

        return FImageLimits::IsFrameByteCountSupported(total) ? total : 0;
    }

    bool IsYUV(EImageFormat Format)
    {
        return Get(Format).ColorModel == EColorModel::YUV;
    }

    bool IsBayer(EImageFormat Format)
    {
        return Get(Format).ColorModel == EColorModel::Bayer;
    }

    FBitDepthPropertyState ResolveBitDepth(
        EImageFormat Format,
        int32_t Requested)
    {
        const FFormatDesc& desc = Get(Format);
        FBitDepthPropertyState state;
        state.Mode = desc.EffectiveBitDepth.Mode;

        if (state.Mode == EFormatPropertyMode::Configurable)
        {
            state.Value = Requested;
            state.Minimum = desc.EffectiveBitDepth.Minimum;
            state.Maximum = desc.EffectiveBitDepth.Maximum;
        }
        else
        {
            state.Value = desc.BitDepth;
            state.Minimum = desc.BitDepth;
            state.Maximum = desc.BitDepth;
        }

        return state;
    }

    FByteOrderPropertyState ResolveByteOrder(
        EImageFormat Format,
        EByteOrder Requested)
    {
        const FStorageLayoutDesc& layout = Get(Format).StorageLayout;

        FByteOrderPropertyState state;
        state.Mode = layout.ByteOrderMode;
        state.Value =
            layout.ByteOrderMode == EFormatPropertyMode::Configurable
                ? Requested
                : layout.DefaultByteOrder;

        return state;
    }

    FSampleAlignmentPropertyState ResolveSampleAlignment(
        EImageFormat Format,
        int32_t SourceBitDepth,
        ESampleAlignment Requested)
    {
        const FFormatDesc& desc = Get(Format);
        const FStorageLayoutDesc& layout = desc.StorageLayout;

        FSampleAlignmentPropertyState state;
        state.Mode = layout.SampleAlignmentMode;
        state.Value =
            layout.SampleAlignmentMode == EFormatPropertyMode::Configurable
                ? Requested
                : layout.DefaultSampleAlignment;

        if (desc.PlaneCount <= 0)
        {
            state.Mode = EFormatPropertyMode::NotApplicable;
            state.Value = layout.DefaultSampleAlignment;

            return state;
        }

        const int32_t containerBitDepth =
            desc.Planes[0].BytesPerSample * static_cast<int32_t>(kBitsPerByte);

        // 有效位占满容器时两种对齐产生完全相同的存储值。固定成格式默认值，
        // 可避免 UI 暗示一个实际上不会影响画面的选择。
        if (state.Mode == EFormatPropertyMode::Configurable &&
            SourceBitDepth >= containerBitDepth)
        {
            state.Mode = EFormatPropertyMode::Fixed;
            state.Value = layout.DefaultSampleAlignment;
        }

        return state;
    }

    int32_t CalculateSampleShift(
        EImageFormat Format,
        int32_t SourceBitDepth,
        ESampleAlignment Requested)
    {
        const FFormatDesc& desc = Get(Format);

        if (desc.PlaneCount <= 0 || SourceBitDepth <= 0)
        {
            return 0;
        }

        const int32_t containerBitDepth =
            desc.Planes[0].BytesPerSample * static_cast<int32_t>(kBitsPerByte);

        if (SourceBitDepth >= containerBitDepth)
        {
            return 0;
        }

        const FSampleAlignmentPropertyState state =
            ResolveSampleAlignment(Format, SourceBitDepth, Requested);

        return state.Value == ESampleAlignment::MostSignificantBits
            ? containerBitDepth - SourceBitDepth
            : 0;
    }
}
