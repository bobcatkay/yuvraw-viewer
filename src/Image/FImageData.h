#pragma once

#include "FImageFormat.h"
#include "FImageMetadata.h"

#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <utility>

/**
 * 图像数据类型
 * 存储一帧图像的像素数据及其描述信息
 *
 * 像素格式的几何信息（平面数、降采样、每采样字节数）不在这里硬编码，
 * 统一查 FImageFormatDesc 的格式描述表。
 */
class FImageData
{
public:
    FImageData();
    ~FImageData();

    /**
     * 设置图像尺寸
     */
    void SetSize(int32_t InWidth, int32_t InHeight);

    /**
     * 设置图像格式
     */
    void SetFormat(EImageFormat InFormat);

    /**
     * 设置行跨距（第 0 平面每行的字节数，含行尾 padding）
     * 传 0 表示紧凑排列，此时 GetStride() 会按格式回退为紧凑值
     */
    void SetStride(int32_t InStride);

    /**
     * 覆盖采样值的真实位深与容器内的左移位数
     *
     * 相机 RAW 极常见的情况：10/12/14bit 数据装在 16bit 容器里。
     * 只知道容器是 16bit 不足以正确显示 —— 10bit 低位对齐的数据按 16bit 归一化会几乎全黑。
     *
     * @param InSourceBitDepth 采样值的真实位深（10/12/14/16），传 0 表示沿用格式默认
     * @param InSampleShift    采样值在容器内左移的位数。低位对齐传 0；
     *                         P010 或 YUV420SP16 高位对齐 10bit 这类数据传 6
     */
    void SetSampleLayout(int32_t InSourceBitDepth, int32_t InSampleShift);

    /**
     * 采样值的真实位深
     */
    int32_t GetSourceBitDepth() const;

    /**
     * 采样值在容器内的左移位数
     */
    int32_t GetSampleShift() const;

    /**
     * 着色器采样时需要乘上的缩放系数
     *
     * GL 归一化纹理返回 storedWord / containerMax，而我们想要 value / sourceMax。
     * 由于 storedWord = value << SampleShift，
     * 缩放系数 = containerMax / (sourceMax << SampleShift)。
     *
     * 例：10bit 高位对齐 -> 65535 / (1023 << 6) = 65535/65472
     *     16bit 容器里低位对齐的 10bit -> 65535 / 1023
     */
    float GetSampleScale() const;

    /**
     * 设置像素数据
     */
    void SetPixelData(const uint8_t* Data, size_t DataSize);

    /**
     * 分配像素数据缓冲区
     */
    void AllocatePixelData(size_t DataSize);

    /**
     * 获取宽度
     */
    int32_t GetWidth() const { return Width; }

    /**
     * 获取高度
     */
    int32_t GetHeight() const { return Height; }

    /**
     * 获取格式
     */
    EImageFormat GetFormat() const { return Format; }

    /**
     * 获取第 0 平面的行跨距（字节）。未显式设置时返回紧凑排列的值
     */
    int32_t GetStride() const;

    /**
     * 获取像素数据
     */
    const uint8_t* GetPixelData() const { return PixelData.data(); }
    uint8_t* GetPixelData() { return PixelData.data(); }

    /**
     * 获取像素数据大小
     */
    size_t GetPixelDataSize() const { return PixelData.size(); }

    const std::shared_ptr<const FImageMetadata>& GetMetadata() const { return Metadata; }
    void SetMetadata(std::shared_ptr<const FImageMetadata> InMetadata) { Metadata = std::move(InMetadata); }

    /**
     * 获取指定平面的数据起始地址，索引越界返回 nullptr
     */
    const uint8_t* GetPlaneData(int32_t PlaneIndex) const;

    /**
     * 获取每像素位数（该格式的有效位深）
     */
    int32_t GetBitsPerPixel() const;

    /**
     * 获取通道数
     */
    int32_t GetChannelCount() const;

    /**
     * 检查数据是否有效
     */
    bool IsValid() const;

    /**
     * 清空数据
     */
    void Clear();

private:
    int32_t Width;
    int32_t Height;
    int32_t Stride;           ///< 第 0 平面每行字节数，0 表示紧凑排列
    int32_t SourceBitDepth;   ///< 采样值真实位深，0 表示沿用格式默认
    int32_t SampleShift;      ///< 采样值在容器内的左移位数，-1 表示沿用格式默认
    EImageFormat Format;
    std::vector<uint8_t> PixelData;
    std::shared_ptr<const FImageMetadata> Metadata;
};
