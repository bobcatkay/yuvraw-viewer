#pragma once

#include "FImageFormat.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * 单个平面的几何描述
 *
 * 所有尺寸都相对于图像的完整宽高，用移位表示降采样：
 *   平面宽 = ceil(Width  >> WidthShift)
 *   平面高 = ceil(Height >> HeightShift)
 */
struct FPlaneDesc
{
    int32_t WidthShift = 0;      ///< 0 = 全宽, 1 = 半宽
    int32_t HeightShift = 0;     ///< 0 = 全高, 1 = 半高
    int32_t ChannelCount = 1;    ///< 该平面每个采样点的分量数（Y=1, UV 交织=2, RGBA=4）
    int32_t BytesPerSample = 1;  ///< 每个分量的字节数（8bit=1, 10/16bit=2）
                                     ///< packed RGB10_A2 用 4x1 仅表达“每纹素 4 字节”，分量按位解包

    /// 该平面每行字节数 = 基准 stride / StrideDivisor
    /// 半平面格式的 UV 平面与 Y 平面等宽（除数 1），平面格式的 U/V 平面减半（除数 2）
    int32_t StrideDivisor = 1;
};

/**
 * 无文件头格式的存储布局策略。
 *
 * 策略跟随像素格式而不是 UI：加载器与属性面板共同读取这里，避免出现控件可选但
 * 解码器不消费，或格式已规定固定值但仍允许用户修改的分叉行为。
 */
struct FStorageLayoutDesc
{
    EFormatPropertyMode ByteOrderMode = EFormatPropertyMode::NotApplicable;
    EByteOrder DefaultByteOrder = EByteOrder::LittleEndian;

    EFormatPropertyMode SampleAlignmentMode = EFormatPropertyMode::NotApplicable;
    ESampleAlignment DefaultSampleAlignment = ESampleAlignment::LeastSignificantBits;
};

/**
 * 有效位深策略。
 *
 * Fixed 格式始终使用 FFormatDesc::BitDepth；Configurable 格式允许用户在给定范围内
 * 描述 16bit 容器里实际有效的位数。
 */
struct FEffectiveBitDepthDesc
{
    EFormatPropertyMode Mode = EFormatPropertyMode::Fixed;
    int32_t Minimum = 0;
    int32_t Maximum = 0;
};

struct FBitDepthPropertyState
{
    EFormatPropertyMode Mode = EFormatPropertyMode::Fixed;
    int32_t Value = 0;
    int32_t Minimum = 0;
    int32_t Maximum = 0;
};

struct FByteOrderPropertyState
{
    EFormatPropertyMode Mode = EFormatPropertyMode::NotApplicable;
    EByteOrder Value = EByteOrder::LittleEndian;
};

struct FSampleAlignmentPropertyState
{
    EFormatPropertyMode Mode = EFormatPropertyMode::NotApplicable;
    ESampleAlignment Value = ESampleAlignment::LeastSignificantBits;
};

/**
 * 格式描述。一张表驱动五处：
 *   1. 加载器的帧大小计算与平面偏移
 *   2. FTextureData 建纹理
 *   3. 着色器变体选择
 *   4. 属性面板下拉框
 *   5. 文件大小校验 / 多帧数量推算
 */
struct FFormatDesc
{
    EImageFormat Format = EImageFormat::Unknown;
    const char*  Name = "Unknown";          ///< 稳定的 ASCII 标识，用于文件名解析与持久化
    const char*  DisplayName = "Unknown";   ///< UI 显示名（可含中文）
    EColorModel  ColorModel = EColorModel::RGB;
    int32_t      BitDepth = 8;              ///< 有效位深，用于 limited range 换算
    int32_t      PlaneCount = 1;
    FPlaneDesc   Planes[3];

    /// 色度顺序颠倒：NV21 的 UV 平面是 V,U；YV12 的平面顺序是 Y,V,U
    bool bSwapChroma = false;

    /// 无文件头，必须由用户提供分辨率
    bool bNeedsExplicitSize = true;

    /// packed 格式（YUY2/UYVY/MIPI RAW）需要着色器或 CPU 侧特殊处理
    bool bIsPacked = false;

    /// 每像素位数，仅用于每像素不足整字节的 packed 格式（Android RAW10/12/14）。
    /// 0 表示按 ChannelCount * BytesPerSample * 8 计算
    int32_t BitsPerPixelPacked = 0;

    /// 格式固定的默认采样左移位数。
    /// P010/P210 把 10bit 放在 16bit 的**高位**（word = value << 6），故为 6；
    /// Bayer16 的可选对齐由实际加载参数动态计算，其余格式默认为 0。
    int32_t SampleShift = 0;

    /// 字节序与有效位对齐的默认值及可编辑策略
    FStorageLayoutDesc StorageLayout;

    /// 有效位深的可编辑策略；固定格式的范围由 BitDepth 自动给出
    FEffectiveBitDepthDesc EffectiveBitDepth;
};

namespace FImageFormatDesc
{
    /**
     * 取格式描述。未知格式返回一个 Format 为 Unknown 的静态描述，不会返回空指针
     */
    const FFormatDesc& Get(EImageFormat Format);

    /**
     * 全部格式描述（含 Unknown，顺序与 EImageFormat 枚举一致）
     */
    const std::vector<FFormatDesc>& GetAll();

    /**
     * 用户可见格式列表顺序。
     *
     * Unknown 固定在首项，RGB 格式按位深排在一起，其余格式保持枚举相对顺序。
     * 该顺序与枚举值解耦，不能用作持久化或 Get() 下标。
     */
    const std::vector<EImageFormat>& GetDisplayOrder();

    /**
     * 按 ASCII 名查找（大小写不敏感），找不到返回 Unknown
     */
    EImageFormat FindByName(const char* Name);

    /**
     * 第 0 平面每像素字节数 = Planes[0].ChannelCount * Planes[0].BytesPerSample
     * 用于把"紧凑排列"的宽度换算成 stride 字节数
     */
    int32_t GetPlane0BytesPerPixel(EImageFormat Format);

    /**
     * 基准 stride（第 0 平面每行字节数）。InStride <= 0 时按紧凑排列计算
     */
    int32_t ResolveBaseStride(EImageFormat Format, int32_t Width, int32_t InStride);

    /** 指定平面的纹理宽度（有效像素数） */
    int32_t GetPlaneWidth(const FFormatDesc& Desc, int32_t PlaneIndex, int32_t Width);

    /** 指定平面的行数 */
    int32_t GetPlaneHeight(const FFormatDesc& Desc, int32_t PlaneIndex, int32_t Height);

    /** 指定平面每行字节数 */
    int32_t GetPlaneStrideBytes(const FFormatDesc& Desc, int32_t PlaneIndex, int32_t BaseStride);

    /**
     * 指定平面每行的像素数（喂给 GL_UNPACK_ROW_LENGTH）
     * = 该平面行字节数 / 该平面每像素字节数
     */
    int32_t GetPlaneRowLength(const FFormatDesc& Desc, int32_t PlaneIndex, int32_t BaseStride);

    /** 指定平面的字节数 */
    size_t GetPlaneSizeBytes(const FFormatDesc& Desc, int32_t PlaneIndex, int32_t Height, int32_t BaseStride);

    /** 指定平面相对帧起始的字节偏移 */
    size_t GetPlaneOffsetBytes(const FFormatDesc& Desc, int32_t PlaneIndex, int32_t Height, int32_t BaseStride);

    /**
     * 单帧总字节数。返回 0 表示参数无效
     */
    size_t CalculateFrameSize(EImageFormat Format, int32_t Width, int32_t Height, int32_t InStride);

    /** 是否 YUV 家族 */
    bool IsYUV(EImageFormat Format);

    /** 是否 Bayer 家族 */
    bool IsBayer(EImageFormat Format);

    /**
     * 解析格式对应的有效位深状态。
     *
     * 固定格式忽略 Requested 并返回格式位深；可选格式保留用户值并给出允许范围。
     * 此函数不静默截断非法输入，加载器可据此明确拒绝损坏的配置。
     */
    FBitDepthPropertyState ResolveBitDepth(
        EImageFormat Format,
        int32_t Requested);

    /**
     * 解析格式对应的字节序状态。
     *
     * 固定或不适用时忽略 Requested 并返回格式默认值；可选时保留用户选择。
     */
    FByteOrderPropertyState ResolveByteOrder(
        EImageFormat Format,
        EByteOrder Requested);

    /**
     * 解析格式对应的有效位对齐状态。
     *
     * 当有效位已经占满容器时，高低位对齐没有区别，动态按固定低位处理。
     */
    FSampleAlignmentPropertyState ResolveSampleAlignment(
        EImageFormat Format,
        int32_t SourceBitDepth,
        ESampleAlignment Requested);

    /**
     * 根据格式、真实位深与对齐方式计算采样值在容器内的左移位数。
     */
    int32_t CalculateSampleShift(
        EImageFormat Format,
        int32_t SourceBitDepth,
        ESampleAlignment Requested);
}
