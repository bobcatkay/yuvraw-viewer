#pragma once

#include "FImageFormat.h"
#include "FImageFormatDesc.h"

#include <cstdint>

/**
 * 图像加载参数
 * 用于指定加载 YUV / RAW 等无文件头格式所需的参数
 */
struct FImageLoadParams
{
    // 图像格式（Unknown 表示交给按扩展名分发的加载器自行判断）
    EImageFormat Format = EImageFormat::Unknown;

    // 图像尺寸（无头格式必需）
    int32_t Width = 0;
    int32_t Height = 0;

    // 行跨距：第 0 平面每行的字节数，含行尾 padding。
    // 0 表示紧凑排列。
    // 相机 dump 的 YUV 常按 16/32/64 字节对齐，例如 1440 宽的 NV21 实际 stride 可能是 1472。
    int32_t Stride = 0;

    // 实际有效位数（用于 16bit 容器格式的显示，不参与平面尺寸计算）
    int32_t BitsPerPixel = 8;

    // Bayer 滤色阵列排布，仅对 Bayer 家族有意义
    EBayerPattern BayerPattern = EBayerPattern::RGGB;

    // 多字节采样在文件中的字节序。固定格式会由 ConstrainStorageLayout 强制回默认值。
    EByteOrder ByteOrder = EByteOrder::LittleEndian;

    // 有效位在采样容器中的对齐方式。packed 格式的跨字节布局不使用该参数。
    ESampleAlignment SampleAlignment = ESampleAlignment::LeastSignificantBits;

    /**
     * 把位深、字节序与有效位对齐恢复为当前格式默认值。
     *
     * 格式改变时必须重置可选属性，避免上一幅大端 RAW 的状态泄漏到下一种格式。
     */
    void ResetStorageLayout()
    {
        const FStorageLayoutDesc& layout =
            FImageFormatDesc::Get(Format).StorageLayout;
        BitsPerPixel = FImageFormatDesc::Get(Format).BitDepth;
        ByteOrder = layout.DefaultByteOrder;
        SampleAlignment = layout.DefaultSampleAlignment;
        ConstrainStorageLayout();
    }

    /**
     * 按当前格式约束有效位深与存储布局。
     *
     * 可选属性保留用户值；固定或不适用的属性强制使用格式默认值。
     */
    void ConstrainStorageLayout()
    {
        BitsPerPixel =
            FImageFormatDesc::ResolveBitDepth(
                Format,
                BitsPerPixel).Value;
        ByteOrder =
            FImageFormatDesc::ResolveByteOrder(Format, ByteOrder).Value;
        SampleAlignment =
            FImageFormatDesc::ResolveSampleAlignment(
                Format,
                BitsPerPixel,
                SampleAlignment).Value;
    }

    /**
     * 应用从文件名/文件头识别出的格式，并同步默认位深与格式相关布局。
     *
     * 存储布局由格式策略约束；切换格式时可选项也恢复该格式默认值，避免隐藏状态
     * 从上一种格式泄漏过来。
     */
    void SetDetectedFormat(EImageFormat DetectedFormat)
    {
        const bool bFormatChanged = Format != DetectedFormat;
        Format = DetectedFormat;

        if (DetectedFormat == EImageFormat::Unknown)
        {
            return;
        }

        const FFormatDesc& desc = FImageFormatDesc::Get(DetectedFormat);
        BitsPerPixel = desc.BitDepth;

        if (bFormatChanged)
        {
            // stride 的字节语义依赖像素格式；跨格式沿用会把紧凑 RGB 行当成 YUV padding。
            Stride = 0;
            ResetStorageLayout();
        }
        else
        {
            ConstrainStorageLayout();
        }
    }

    /**
     * 检查参数是否有效
     */
    bool IsValid() const
    {
        if (Format == EImageFormat::Unknown)
        {
            return false;
        }

        // 无头格式必须提供尺寸。是否需要由格式描述表回答，而不是在这里维护一张 if 列表。
        if (FImageFormatDesc::Get(Format).bNeedsExplicitSize && (Width <= 0 || Height <= 0))
        {
            return false;
        }

        return true;
    }

    /**
     * 创建默认参数
     */
    static FImageLoadParams Default()
    {
        return FImageLoadParams();
    }
};
