#include "FImageExporter.h"

#include "FImageFormatDesc.h"
#include "FImageSampler.h"
#include "FWebpEncoder.h"
#include "Core/FLocalization.h"
#include "Util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

// windows.h 的 min/max 宏会把下面所有 std::min/std::max 的调用点打断
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{
    // 原名及 _1 到 _9999；耗尽候选时必须失败，不能返回一个已存在的文件。
    constexpr int32_t kMaxOutputPathCandidates = 10000;

    /**
     * 作用域内的 COM 初始化。GLFW 已经初始化过 COM，这里通常拿到 S_FALSE 或
     * RPC_E_CHANGED_MODE，都不算失败；只有自己成功初始化时才负责反初始化。
     */
    class FScopedCoInitialize
    {
    public:
        FScopedCoInitialize()
        {
            const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            bShouldUninitialize = SUCCEEDED(hr);
        }

        ~FScopedCoInitialize()
        {
            if (bShouldUninitialize)
            {
                CoUninitialize();
            }
        }

        FScopedCoInitialize(const FScopedCoInitialize&) = delete;
        FScopedCoInitialize& operator=(const FScopedCoInitialize&) = delete;

    private:
        bool bShouldUninitialize = false;
    };

    std::wstring ToWide(const std::string& Utf8)
    {
        if (Utf8.empty())
        {
            return std::wstring();
        }

        const int required = MultiByteToWideChar(CP_UTF8, 0, Utf8.c_str(), static_cast<int>(Utf8.size()), nullptr, 0);

        if (required <= 0)
        {
            return std::wstring();
        }

        std::wstring wide(static_cast<size_t>(required), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, Utf8.c_str(), static_cast<int>(Utf8.size()), wide.data(), required);

        return wide;
    }

    /**
     * 缩小：面积平均
     *
     * 直接抽点会让高频细节混叠成摩尔纹 —— 对着看 sensor 图案的场景是致命的，
     * 所以缩小一律把源区域内的像素平均掉。
     */
    void ResampleArea(
        const std::vector<uint8_t>& Src, int32_t SrcWidth, int32_t SrcHeight,
        std::vector<uint8_t>& Dst, int32_t DstWidth, int32_t DstHeight)
    {
        for (int32_t dy = 0; dy < DstHeight; ++dy)
        {
            int32_t y0 = static_cast<int32_t>(static_cast<int64_t>(dy) * SrcHeight / DstHeight);
            int32_t y1 = static_cast<int32_t>(static_cast<int64_t>(dy + 1) * SrcHeight / DstHeight);

            y0 = std::min(y0, SrcHeight - 1);
            y1 = std::min(std::max(y1, y0 + 1), SrcHeight);

            for (int32_t dx = 0; dx < DstWidth; ++dx)
            {
                int32_t x0 = static_cast<int32_t>(static_cast<int64_t>(dx) * SrcWidth / DstWidth);
                int32_t x1 = static_cast<int32_t>(static_cast<int64_t>(dx + 1) * SrcWidth / DstWidth);

                x0 = std::min(x0, SrcWidth - 1);
                x1 = std::min(std::max(x1, x0 + 1), SrcWidth);

                uint32_t sum[3] = { 0, 0, 0 };
                const uint32_t count = static_cast<uint32_t>(y1 - y0) * static_cast<uint32_t>(x1 - x0);

                for (int32_t sy = y0; sy < y1; ++sy)
                {
                    const uint8_t* row = Src.data() + (static_cast<size_t>(sy) * SrcWidth + x0) * 3;

                    for (int32_t sx = x0; sx < x1; ++sx, row += 3)
                    {
                        sum[0] += row[0];
                        sum[1] += row[1];
                        sum[2] += row[2];
                    }
                }

                uint8_t* out = Dst.data() + (static_cast<size_t>(dy) * DstWidth + dx) * 3;
                out[0] = static_cast<uint8_t>((sum[0] + count / 2) / count);
                out[1] = static_cast<uint8_t>((sum[1] + count / 2) / count);
                out[2] = static_cast<uint8_t>((sum[2] + count / 2) / count);
            }
        }
    }

    /**
     * 放大：双线性。面积平均在放大时会退化成最近邻，出现明显块状
     */
    void ResampleBilinear(
        const std::vector<uint8_t>& Src, int32_t SrcWidth, int32_t SrcHeight,
        std::vector<uint8_t>& Dst, int32_t DstWidth, int32_t DstHeight)
    {
        const float scaleX = static_cast<float>(SrcWidth) / static_cast<float>(DstWidth);
        const float scaleY = static_cast<float>(SrcHeight) / static_cast<float>(DstHeight);

        for (int32_t dy = 0; dy < DstHeight; ++dy)
        {
            // 按像素中心对齐，否则整幅图会偏移半个像素
            const float sy = std::max(0.0f, (dy + 0.5f) * scaleY - 0.5f);
            const int32_t y0 = std::min(static_cast<int32_t>(sy), SrcHeight - 1);
            const int32_t y1 = std::min(y0 + 1, SrcHeight - 1);
            const float fy = sy - static_cast<float>(y0);

            for (int32_t dx = 0; dx < DstWidth; ++dx)
            {
                const float sx = std::max(0.0f, (dx + 0.5f) * scaleX - 0.5f);
                const int32_t x0 = std::min(static_cast<int32_t>(sx), SrcWidth - 1);
                const int32_t x1 = std::min(x0 + 1, SrcWidth - 1);
                const float fx = sx - static_cast<float>(x0);

                const uint8_t* p00 = Src.data() + (static_cast<size_t>(y0) * SrcWidth + x0) * 3;
                const uint8_t* p01 = Src.data() + (static_cast<size_t>(y0) * SrcWidth + x1) * 3;
                const uint8_t* p10 = Src.data() + (static_cast<size_t>(y1) * SrcWidth + x0) * 3;
                const uint8_t* p11 = Src.data() + (static_cast<size_t>(y1) * SrcWidth + x1) * 3;

                uint8_t* out = Dst.data() + (static_cast<size_t>(dy) * DstWidth + dx) * 3;

                for (int32_t c = 0; c < 3; ++c)
                {
                    const float top = p00[c] + (p01[c] - p00[c]) * fx;
                    const float bottom = p10[c] + (p11[c] - p10[c]) * fx;
                    const float value = top + (bottom - top) * fy;

                    out[c] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, value + 0.5f)));
                }
            }
        }
    }

    const GUID& GetContainerFormat(EExportFormat Format)
    {
        switch (Format)
        {
        case EExportFormat::JPEG: return GUID_ContainerFormatJpeg;
        case EExportFormat::BMP:  return GUID_ContainerFormatBmp;
        case EExportFormat::PNG:
        default:                  return GUID_ContainerFormatPng;
        }
    }

    /**
     * 用 WIC 编码 PNG / JPEG / BMP
     *
     * @param Bgr 紧凑排列的 BGR8（WIC 的 24bpp 原生序）
     */
    bool EncodeWithWic(
        const std::vector<uint8_t>& Bgr,
        int32_t Width,
        int32_t Height,
        const FExportSettings& Settings,
        const std::string& OutputPath,
        std::string& OutError)
    {
        FScopedCoInitialize comInit;

        ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));

        if (FAILED(hr))
        {
            OutError = u8"无法创建 WIC 工厂";

            return false;
        }

        ComPtr<IWICStream> stream;
        hr = factory->CreateStream(&stream);

        if (SUCCEEDED(hr))
        {
            hr = stream->InitializeFromFilename(ToWide(OutputPath).c_str(), GENERIC_WRITE);
        }

        if (FAILED(hr))
        {
            OutError = u8"无法创建输出文件（目录不存在或没有写权限）";

            return false;
        }

        ComPtr<IWICBitmapEncoder> encoder;
        hr = factory->CreateEncoder(GetContainerFormat(Settings.Format), nullptr, &encoder);

        if (SUCCEEDED(hr))
        {
            hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        }

        if (FAILED(hr))
        {
            OutError = u8"系统缺少该格式的编码器";

            return false;
        }

        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> frameProperties;
        hr = encoder->CreateNewFrame(&frame, &frameProperties);

        if (FAILED(hr))
        {
            OutError = u8"无法创建输出帧";

            return false;
        }

        if (Settings.Format == EExportFormat::JPEG && frameProperties)
        {
            PROPBAG2 option = {};
            option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");

            // VT_R4 不持有任何资源，直接构造即可，不必牵进 oleaut32 的 VariantInit/Clear
            VARIANT value = {};
            value.vt = VT_R4;
            value.fltVal = std::min(1.0f, std::max(0.01f, Settings.JpegQuality / 100.0f));

            frameProperties->Write(1, &option, &value);
        }

        hr = frame->Initialize(frameProperties.Get());

        if (SUCCEEDED(hr))
        {
            hr = frame->SetSize(static_cast<UINT>(Width), static_cast<UINT>(Height));
        }

        if (FAILED(hr))
        {
            OutError = u8"初始化输出帧失败";

            return false;
        }

        // 编码器不一定接受 24bppBGR（比如某些配置下会改成 32bpp），
        // 所以要看它回填成了什么，不一致就让 WIC 自己转换一次
        WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat24bppBGR;
        frame->SetPixelFormat(&pixelFormat);

        const UINT stride = static_cast<UINT>(Width) * 3;

        ComPtr<IWICBitmap> bitmap;
        hr = factory->CreateBitmapFromMemory(
            static_cast<UINT>(Width),
            static_cast<UINT>(Height),
            GUID_WICPixelFormat24bppBGR,
            stride,
            static_cast<UINT>(Bgr.size()),
            const_cast<BYTE*>(Bgr.data()),
            &bitmap);

        if (FAILED(hr))
        {
            OutError = u8"无法建立源位图";

            return false;
        }

        if (IsEqualGUID(pixelFormat, GUID_WICPixelFormat24bppBGR))
        {
            hr = frame->WriteSource(bitmap.Get(), nullptr);
        }
        else
        {
            ComPtr<IWICFormatConverter> converter;
            hr = factory->CreateFormatConverter(&converter);

            if (SUCCEEDED(hr))
            {
                hr = converter->Initialize(
                    bitmap.Get(), pixelFormat, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut);
            }

            if (SUCCEEDED(hr))
            {
                hr = frame->WriteSource(converter.Get(), nullptr);
            }
        }

        if (SUCCEEDED(hr))
        {
            hr = frame->Commit();
        }

        if (SUCCEEDED(hr))
        {
            hr = encoder->Commit();
        }

        if (FAILED(hr))
        {
            OutError = u8"写入文件失败";

            return false;
        }

        return true;
    }

    bool WriteFileBytes(const std::string& Path, const std::vector<uint8_t>& Bytes, std::string& OutError)
    {
        FILE* file = nullptr;
        const errno_t err = _wfopen_s(&file, ToWide(Path).c_str(), L"wb");

        if (err != 0 || !file)
        {
            OutError = u8"无法创建输出文件（目录不存在或没有写权限）";

            return false;
        }

        const size_t written = std::fwrite(Bytes.data(), 1, Bytes.size(), file);
        std::fclose(file);

        if (written != Bytes.size())
        {
            OutError = u8"写入文件失败（磁盘空间不足？）";

            return false;
        }

        return true;
    }
}

namespace FImageExporter
{
    const char* GetExtension(EExportFormat Format)
    {
        switch (Format)
        {
        case EExportFormat::JPEG: return ".jpg";
        case EExportFormat::BMP:  return ".bmp";
        case EExportFormat::WEBP: return ".webp";
        case EExportFormat::PNG:
        default:                  return ".png";
        }
    }

    const char* GetDisplayName(EExportFormat Format)
    {
        switch (Format)
        {
        case EExportFormat::JPEG: return "JPEG (.jpg)";
        case EExportFormat::BMP:  return "BMP (.bmp)";
        case EExportFormat::WEBP: return "WebP (.webp)";
        case EExportFormat::PNG:
        default:                  return "PNG (.png)";
        }
    }

    bool NeedsColorMatrix(EImageFormat SourceFormat, EExportFormat /*TargetFormat*/)
    {
        return FImageFormatDesc::Get(SourceFormat).ColorModel == EColorModel::YUV;
    }

    void GetTargetSize(
        const FExportSettings& Settings,
        int32_t SourceWidth,
        int32_t SourceHeight,
        int32_t& OutWidth,
        int32_t& OutHeight)
    {
        OutWidth = SourceWidth;
        OutHeight = SourceHeight;

        if (SourceWidth <= 0 || SourceHeight <= 0)
        {
            return;
        }

        switch (Settings.ResizeMode)
        {
        case EExportResizeMode::Percent:
        {
            const float ratio = Settings.Percent / 100.0f;
            OutWidth = static_cast<int32_t>(std::lround(SourceWidth * ratio));
            OutHeight = static_cast<int32_t>(std::lround(SourceHeight * ratio));
            break;
        }

        case EExportResizeMode::Width:
        {
            if (Settings.TargetWidth > 0)
            {
                OutWidth = Settings.TargetWidth;
                // 高度由原始宽高比推算，保证等比例
                OutHeight = static_cast<int32_t>(
                    std::lround(static_cast<double>(SourceHeight) * Settings.TargetWidth / SourceWidth));
            }
            break;
        }

        case EExportResizeMode::Original:
        default:
            break;
        }

        OutWidth = std::max(1, OutWidth);
        OutHeight = std::max(1, OutHeight);
    }

    std::string MakeOutputPath(const FExportSettings& Settings, const std::string& SourcePath)
    {
        const std::filesystem::path source = std::filesystem::u8path(SourcePath);

        std::filesystem::path directory = Settings.OutputDirectory.empty()
            ? source.parent_path()
            : std::filesystem::u8path(Settings.OutputDirectory);

        const std::string stem = source.stem().u8string();
        const std::string extension = GetExtension(Settings.Format);

        std::filesystem::path candidate = directory / std::filesystem::u8path(stem + extension);

        if (Settings.bOverwrite)
        {
            return candidate.u8string();
        }

        // 不覆盖：依次试 _1、_2……源文件本身就是同名同格式时也走这条路径，
        // 否则"把 a.png 导出成 png"会把源文件读到一半又写回去
        for (int32_t suffix = 0; suffix < kMaxOutputPathCandidates; ++suffix)
        {
            if (suffix > 0)
            {
                candidate = directory / std::filesystem::u8path(
                    stem + "_" + std::to_string(suffix) + extension);
            }

            std::error_code ec;
            const bool bExists = std::filesystem::exists(candidate, ec);
            if (ec)
            {
                // 无法检查也不能当作不存在，否则“不覆盖”可能变成覆盖写入。
                LOGE("MakeOutputPath", "Failed to inspect output path: %s", ec.message().c_str());
                return {};
            }
            if (!bExists)
            {
                return candidate.u8string();
            }
        }

        LOGE("MakeOutputPath", "No available output name after %d candidates", kMaxOutputPathCandidates);
        return {};
    }

    bool Export(
        const FImageData& Source,
        EBayerPattern BayerPattern,
        const FExportSettings& Settings,
        const std::string& OutputPath,
        std::string& OutError)
    {
        if (OutputPath.empty())
        {
            OutError = FLocalization::Text(EUiText::ExportOutputPathUnavailable);
            LOGE("Export", "No output path is available");
            return false;
        }

        if (!Source.IsValid())
        {
            OutError = u8"源图像无效";

            return false;
        }

        // 与直方图/差值同一套解读，导出的就是查看器里看到的那幅图
        std::vector<uint8_t> rgb;

        if (!FImageSampler::ConvertToRgb8(Source, Settings.Display, BayerPattern, rgb))
        {
            OutError = u8"该格式暂不支持转换为 RGB";

            return false;
        }

        const int32_t sourceWidth = Source.GetWidth();
        const int32_t sourceHeight = Source.GetHeight();

        int32_t targetWidth = sourceWidth;
        int32_t targetHeight = sourceHeight;
        GetTargetSize(Settings, sourceWidth, sourceHeight, targetWidth, targetHeight);

        if (targetWidth != sourceWidth || targetHeight != sourceHeight)
        {
            std::vector<uint8_t> resized(static_cast<size_t>(targetWidth) * targetHeight * 3);

            if (targetWidth < sourceWidth || targetHeight < sourceHeight)
            {
                ResampleArea(rgb, sourceWidth, sourceHeight, resized, targetWidth, targetHeight);
            }
            else
            {
                ResampleBilinear(rgb, sourceWidth, sourceHeight, resized, targetWidth, targetHeight);
            }

            rgb.swap(resized);
        }

        if (Settings.Format == EExportFormat::WEBP)
        {
            // Windows 只带 WebP 解码器不带编码器，这条路径走项目自带的 VP8L 编码器
            std::vector<uint8_t> file;

            if (!FWebpEncoder::EncodeLosslessRgb8(rgb.data(), targetWidth, targetHeight, file, OutError))
            {
                return false;
            }

            return WriteFileBytes(OutputPath, file, OutError);
        }

        // WIC 的 24bpp 是 BGR 序
        std::vector<uint8_t> bgr(rgb.size());

        for (size_t i = 0; i + 2 < rgb.size(); i += 3)
        {
            bgr[i + 0] = rgb[i + 2];
            bgr[i + 1] = rgb[i + 1];
            bgr[i + 2] = rgb[i + 0];
        }

        return EncodeWithWic(bgr, targetWidth, targetHeight, Settings, OutputPath, OutError);
    }
}
