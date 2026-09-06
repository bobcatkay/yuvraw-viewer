#pragma once

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>
#include <vector>

namespace WicDecodeTestHelper
{
    constexpr UINT kRgbChannelCount = 3;
    constexpr int kGuidTextLength = 39;

    inline bool CheckResult(const char* Stage, HRESULT Result)
    {
        if (SUCCEEDED(Result))
        {
            return true;
        }

        std::printf("WIC read-back FAIL: stage=%s HRESULT=0x%08lX\n",
                    Stage, static_cast<unsigned long>(Result));
        return false;
    }

    inline void PrintDecoderIdentity(IWICBitmapDecoder* Decoder)
    {
        Microsoft::WRL::ComPtr<IWICBitmapDecoderInfo> info;
        if (FAILED(Decoder->GetDecoderInfo(&info)))
        {
            std::printf("WIC decoder identity: unavailable\n");
            return;
        }

        CLSID clsid = {};
        wchar_t clsidText[kGuidTextLength] = {};
        if (SUCCEEDED(info->GetCLSID(&clsid)) &&
            StringFromGUID2(clsid, clsidText, kGuidTextLength) == 0)
        {
            std::printf("WIC decoder CLSID formatting unavailable\n");
        }

        UINT required = 0;
        std::vector<wchar_t> name;
        if (SUCCEEDED(info->GetFriendlyName(0, nullptr, &required)) && required > 0)
        {
            name.resize(required);
            if (FAILED(info->GetFriendlyName(required, name.data(), &required)))
            {
                name.clear();
            }
        }
        std::printf("WIC decoder: name=%ls CLSID=%ls\n",
                    name.empty() ? L"unavailable" : name.data(), clsidText);
    }

    inline void DiagnoseWebpActivation()
    {
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        const HRESULT result = CoCreateInstance(CLSID_WICWebpDecoder, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&decoder));
        // No stream is supplied: failure here is independent of the generated bitstream.
        std::printf("WIC WebP file-independent activation: HRESULT=0x%08lX\n",
                    static_cast<unsigned long>(result));
        if (SUCCEEDED(result))
        {
            PrintDecoderIdentity(decoder.Get());
        }
    }

    // Report the first failing operation without treating a registered but broken codec as absent.
    // The RGB24 conversion and byte-for-byte assertions remain the same on every runner.
    inline bool DecodeRgb8(const std::wstring& Path, int& OutWidth, int& OutHeight,
                          std::vector<unsigned char>& OutRgb)
    {
        using Microsoft::WRL::ComPtr;
        ComPtr<IWICImagingFactory> factory;
        if (!CheckResult("CoCreateInstance(WIC factory)", CoCreateInstance(
                CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        {
            return false;
        }

        ComPtr<IWICBitmapDecoder> decoder;
        if (!CheckResult("CreateDecoderFromFilename", factory->CreateDecoderFromFilename(
                Path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)))
        {
            const size_t extensionStart = Path.find_last_of(L'.');
            if (extensionStart != std::wstring::npos &&
                _wcsicmp(Path.c_str() + extensionStart, L".webp") == 0)
            {
                DiagnoseWebpActivation();
            }
            return false;
        }
        PrintDecoderIdentity(decoder.Get());

        ComPtr<IWICBitmapFrameDecode> frame;
        if (!CheckResult("GetFrame(0)", decoder->GetFrame(0, &frame)))
        {
            return false;
        }

        WICPixelFormatGUID sourceFormat = {};
        const HRESULT formatResult = frame->GetPixelFormat(&sourceFormat);
        wchar_t sourceFormatText[kGuidTextLength] = {};
        if (SUCCEEDED(formatResult) &&
            StringFromGUID2(sourceFormat, sourceFormatText, kGuidTextLength) == 0)
        {
            std::printf("WIC source pixel format GUID formatting unavailable\n");
        }
        // This additional diagnostic query does not replace or gate the original conversion.
        std::printf("WIC source pixel format: %ls HRESULT=0x%08lX; target=24bppRGB\n",
                    sourceFormatText, static_cast<unsigned long>(formatResult));

        ComPtr<IWICFormatConverter> converter;
        if (!CheckResult("CreateFormatConverter", factory->CreateFormatConverter(&converter)) ||
            !CheckResult("Initialize(24bppRGB)", converter->Initialize(frame.Get(),
                GUID_WICPixelFormat24bppRGB, WICBitmapDitherTypeNone, nullptr, 0.0,
                WICBitmapPaletteTypeMedianCut)))
        {
            return false;
        }

        UINT width = 0;
        UINT height = 0;
        if (!CheckResult("GetSize", converter->GetSize(&width, &height)))
        {
            return false;
        }
        if (width == 0 || height == 0)
        {
            std::printf("WIC read-back FAIL: empty dimensions %ux%u\n", width, height);
            return false;
        }

        const UINT stride = width * kRgbChannelCount;
        OutRgb.assign(static_cast<size_t>(stride) * height, 0);
        if (!CheckResult("CopyPixels(24bppRGB)", converter->CopyPixels(
                nullptr, stride, static_cast<UINT>(OutRgb.size()), OutRgb.data())))
        {
            return false;
        }
        OutWidth = static_cast<int>(width);
        OutHeight = static_cast<int>(height);
        return true;
    }
}
