#include "FImageData.h"
#include "FImageFormatDesc.h"

#include <cstring>

FImageData::FImageData()
    : Width(0)
    , Height(0)
    , Stride(0)
    , SourceBitDepth(0)
    , SampleShift(-1)
    , Format(EImageFormat::Unknown)
{
}

void FImageData::SetSampleLayout(int32_t InSourceBitDepth, int32_t InSampleShift)
{
    SourceBitDepth = InSourceBitDepth > 0 ? InSourceBitDepth : 0;
    SampleShift = InSampleShift >= 0 ? InSampleShift : -1;
}

int32_t FImageData::GetSourceBitDepth() const
{
    if (SourceBitDepth > 0)
    {
        return SourceBitDepth;
    }

    return FImageFormatDesc::Get(Format).BitDepth;
}

int32_t FImageData::GetSampleShift() const
{
    if (SampleShift >= 0)
    {
        return SampleShift;
    }

    return FImageFormatDesc::Get(Format).SampleShift;
}

float FImageData::GetSampleScale() const
{
    const FFormatDesc& desc = FImageFormatDesc::Get(Format);

    if (desc.PlaneCount <= 0)
    {
        return 1.0f;
    }

    // 8bit 容器不存在"位深小于容器"的情况，直接返回 1
    const int32_t bytesPerSample = desc.Planes[0].BytesPerSample;

    if (bytesPerSample <= 1)
    {
        return 1.0f;
    }

    const float containerMax = 65535.0f;
    const int32_t sourceMax = (1 << GetSourceBitDepth()) - 1;
    const float storedMax = static_cast<float>(sourceMax << GetSampleShift());

    if (storedMax <= 0.0f)
    {
        return 1.0f;
    }

    return containerMax / storedMax;
}

FImageData::~FImageData()
{
    Clear();
}

void FImageData::SetSize(int32_t InWidth, int32_t InHeight)
{
    Width = InWidth;
    Height = InHeight;
}

void FImageData::SetFormat(EImageFormat InFormat)
{
    Format = InFormat;
}

void FImageData::SetStride(int32_t InStride)
{
    Stride = InStride > 0 ? InStride : 0;
}

void FImageData::SetPixelData(const uint8_t* Data, size_t DataSize)
{
    PixelData.resize(DataSize);

    if (Data && DataSize > 0)
    {
        std::memcpy(PixelData.data(), Data, DataSize);
    }
}

void FImageData::AllocatePixelData(size_t DataSize)
{
    PixelData.resize(DataSize);
}

int32_t FImageData::GetStride() const
{
    return FImageFormatDesc::ResolveBaseStride(Format, Width, Stride);
}

const uint8_t* FImageData::GetPlaneData(int32_t PlaneIndex) const
{
    const FFormatDesc& desc = FImageFormatDesc::Get(Format);

    if (PlaneIndex < 0 || PlaneIndex >= desc.PlaneCount)
    {
        return nullptr;
    }

    const size_t offset = FImageFormatDesc::GetPlaneOffsetBytes(desc, PlaneIndex, Height, GetStride());

    if (offset >= PixelData.size())
    {
        return nullptr;
    }

    return PixelData.data() + offset;
}

int32_t FImageData::GetBitsPerPixel() const
{
    return GetSourceBitDepth();
}

int32_t FImageData::GetChannelCount() const
{
    const FFormatDesc& desc = FImageFormatDesc::Get(Format);

    switch (desc.ColorModel)
    {
    case EColorModel::Gray:
    case EColorModel::Bayer:
        return 1;

    case EColorModel::YUV:
        return 3;

    case EColorModel::RGB:
    default:
        // RGB 家族按第 0 平面的分量数（RGB=3, RGBA=4）
        return desc.PlaneCount > 0 ? desc.Planes[0].ChannelCount : 0;
    }
}

bool FImageData::IsValid() const
{
    return Width > 0 && Height > 0 && Format != EImageFormat::Unknown && !PixelData.empty();
}

void FImageData::Clear()
{
    Width = 0;
    Height = 0;
    Stride = 0;
    SourceBitDepth = 0;
    SampleShift = -1;
    Format = EImageFormat::Unknown;
    PixelData.clear();
}
