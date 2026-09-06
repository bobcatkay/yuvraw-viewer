#include "FWicImageLoader.h"
#include "FImageData.h"
#include "FImageLimits.h"
#include "Util.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <new>
#include <stdexcept>

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

EWicCodecAvailability WicCodecAvailabilityDetail::MergeActivationResult(
    EWicCodecAvailability Current, long Result)
{
    if (Current == EWicCodecAvailability::Available || SUCCEEDED(Result))
    {
        return EWicCodecAvailability::Available;
    }
    if (Current == EWicCodecAvailability::Unknown || Result != REGDB_E_CLASSNOTREG)
    {
        return EWicCodecAvailability::Unknown;
    }
    return EWicCodecAvailability::Missing;
}

namespace
{
    constexpr size_t kRgbaChannelCount = 4;

    EImageLoadError ClassifyWicFailure(HRESULT Result)
    {
        if (Result == E_OUTOFMEMORY)
        {
            return EImageLoadError::OutOfMemory;
        }
        if (Result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
            Result == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND) ||
            Result == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) ||
            Result == STG_E_ACCESSDENIED || Result == STG_E_FILENOTFOUND)
        {
            return EImageLoadError::FileAccess;
        }
        if (Result == WINCODEC_ERR_STREAMREAD)
        {
            return EImageLoadError::TruncatedData;
        }
        return EImageLoadError::DecodeFailure;
    }

    /**
     * 作用域内的 COM 初始化
     *
     * GLFW 在 Windows 上会自行调用 CoInitializeEx，所以这里可能拿到
     * S_FALSE（已初始化）或 RPC_E_CHANGED_MODE（线程套间模型不同）。
     * 两种情况都不算失败，但只有我们自己成功初始化时才负责反初始化。
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
        if (Utf8.empty() || Utf8.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        {
            return std::wstring();
        }

        const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Utf8.c_str(), static_cast<int>(Utf8.size()), nullptr, 0);

        if (required <= 0)
        {
            return std::wstring();
        }

        std::wstring wide(static_cast<size_t>(required), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, Utf8.c_str(), static_cast<int>(Utf8.size()), wide.data(), required);

        return wide;
    }
}

std::unique_ptr<FImageData> FWicImageLoader::LoadFromFile(const std::string& FilePath, const FImageLoadParams* /*Params*/, EImageLoadError* OutError)
try
{
    SetImageLoadError(OutError, EImageLoadError::DecodeFailure);
    FScopedCoInitialize comInit;

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));

    if (FAILED(hr))
    {
        SetImageLoadError(OutError, ClassifyWicFailure(hr));
        LOGE("WIC", "Failed to create WIC factory, hr: 0x%08lX", static_cast<unsigned long>(hr));

        return nullptr;
    }

    const std::wstring widePath = ToWide(FilePath);
    if (widePath.empty())
    {
        SetImageLoadError(OutError, EImageLoadError::FileAccess);
        LOGE("WIC", "%s", "Invalid UTF-8 file path");
        return nullptr;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(
        widePath.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &decoder);

    if (FAILED(hr))
    {
        SetImageLoadError(OutError, ClassifyWicFailure(hr));
        // 缺失文件、访问拒绝和内存不足有更明确的原因，不得被 codec 探测覆盖。
        const bool bMayBeUnavailable = hr == WINCODEC_ERR_COMPONENTNOTFOUND ||
            hr == WINCODEC_ERR_UNKNOWNIMAGEFORMAT ||
            hr == WINCODEC_ERR_COMPONENTINITIALIZEFAILURE || hr == REGDB_E_CLASSNOTREG;
        if (bMayBeUnavailable && GetCodecAvailability(
                std::filesystem::u8path(FilePath).extension().u8string()) == EWicCodecAvailability::Missing)
        {
            SetImageLoadError(OutError, EImageLoadError::CodecUnavailable);
        }
        LOGE("WIC", "Cannot open decoder, hr: 0x%08lX", static_cast<unsigned long>(hr));

        return nullptr;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);

    if (FAILED(hr))
    {
        SetImageLoadError(OutError, ClassifyWicFailure(hr));
        LOGE("WIC", "Failed to get frame 0, hr: 0x%08lX", static_cast<unsigned long>(hr));

        return nullptr;
    }

    UINT width = 0;
    UINT height = 0;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr))
    {
        SetImageLoadError(OutError, ClassifyWicFailure(hr));
        LOGE("WIC", "Cannot read frame dimensions, hr: 0x%08lX", static_cast<unsigned long>(hr));
        return nullptr;
    }

    size_t pixelCount = 0;
    size_t strideBytes = 0;
    size_t bufferSize = 0;
    // 先验证未缩窄的文件头，再创建转换器；部分 codec 在 Initialize 时即分配工作缓冲。
    if (width > static_cast<UINT>(FImageLimits::kMaximumDimension) ||
        height > static_cast<UINT>(FImageLimits::kMaximumDimension) ||
        !FImageLimits::TryGetPixelCount(static_cast<int32_t>(width), static_cast<int32_t>(height), pixelCount) ||
        !FImageLimits::TryMultiplySize(static_cast<size_t>(width), kRgbaChannelCount, strideBytes) ||
        strideBytes > static_cast<size_t>(FImageLimits::kMaximumStrideBytes) ||
        strideBytes > static_cast<size_t>((std::numeric_limits<UINT>::max)()) ||
        !FImageLimits::TryMultiplySize(strideBytes, static_cast<size_t>(height), bufferSize) ||
        !FImageLimits::IsFrameByteCountSupported(bufferSize) ||
        bufferSize > static_cast<size_t>((std::numeric_limits<UINT>::max)()))
    {
        SetImageLoadError(OutError, EImageLoadError::ResourceLimit);
        LOGE("WIC", "Frame geometry exceeds limits before conversion, %ux%u", width, height);
        return nullptr;
    }

    // 统一转成 RGBA8，避免在这里处理 WIC 的几十种像素格式
    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);

    if (FAILED(hr))
    {
        SetImageLoadError(OutError, ClassifyWicFailure(hr));
        LOGE("WIC", "Failed to create format converter, hr: 0x%08lX", static_cast<unsigned long>(hr));

        return nullptr;
    }

    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeMedianCut);

    if (FAILED(hr))
    {
        SetImageLoadError(OutError, ClassifyWicFailure(hr));
        LOGE("WIC", "Failed to initialize converter, hr: 0x%08lX", static_cast<unsigned long>(hr));

        return nullptr;
    }

    auto imageData = std::make_unique<FImageData>();
    imageData->SetSize(static_cast<int32_t>(width), static_cast<int32_t>(height));
    imageData->SetStride(static_cast<int32_t>(strideBytes));
    imageData->SetFormat(EImageFormat::RGBA8);
    imageData->AllocatePixelData(bufferSize);

    hr = converter->CopyPixels(
        nullptr,
        static_cast<UINT>(strideBytes),
        static_cast<UINT>(bufferSize),
        imageData->GetPixelData());

    if (FAILED(hr))
    {
        SetImageLoadError(OutError, ClassifyWicFailure(hr));
        LOGE("WIC", "CopyPixels failed, hr: 0x%08lX", static_cast<unsigned long>(hr));

        return nullptr;
    }

    LOGD("WIC", "Loaded %ux%u RGBA8", width, height);
    SetImageLoadError(OutError, EImageLoadError::None);

    return imageData;
}
catch (const std::bad_alloc&)
{
    SetImageLoadError(OutError, EImageLoadError::OutOfMemory);
    LOGE("WIC", "%s", "Out of memory while decoding RGBA frame");
    return nullptr;
}
catch (const std::length_error&)
{
    SetImageLoadError(OutError, EImageLoadError::ResourceLimit);
    LOGE("WIC", "%s", "Allocation size rejected while decoding RGBA frame");
    return nullptr;
}

EWicCodecAvailability FWicImageLoader::GetCodecAvailability(const std::string& Extension)
{
    FScopedCoInitialize comInit;
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
    {
        return EWicCodecAvailability::Unknown;
    }
    ComPtr<IEnumUnknown> components;
    if (FAILED(factory->CreateComponentEnumerator(WICDecoder, WICComponentEnumerateDefault, &components)))
    {
        return EWicCodecAvailability::Unknown;
    }
    const std::wstring requested = ToWide(Extension);
    if (requested.empty())
    {
        return EWicCodecAvailability::Unknown;
    }
    ComPtr<IUnknown> component;
    EWicCodecAvailability availability = EWicCodecAvailability::Missing;
    HRESULT lastActivationFailure = S_OK;
    HRESULT next = S_OK;
    while ((next = components->Next(1, &component, nullptr)) == S_OK)
    {
        ComPtr<IWICBitmapDecoderInfo> info;
        UINT required = 0;
        if (FAILED(component.As(&info)) || FAILED(info->GetFileExtensions(0, nullptr, &required)))
        {
            return EWicCodecAvailability::Unknown;
        }
        std::vector<wchar_t> extensions(required);
        if (required == 0 || FAILED(info->GetFileExtensions(required, extensions.data(), &required)))
        {
            return EWicCodecAvailability::Unknown;
        }
        const std::wstring list(extensions.data());
        size_t start = 0;
        while (start < list.size())
        {
            const size_t end = list.find(L',', start);
            const std::wstring item = list.substr(start, end == std::wstring::npos ? end : end - start);
            if (_wcsicmp(item.c_str(), requested.c_str()) == 0)
            {
                CLSID decoderClass = {};
                if (FAILED(info->GetCLSID(&decoderClass)))
                {
                    availability = EWicCodecAvailability::Unknown;
                    break;
                }
                // Windows can expose codec metadata even when its backing class is absent.
                // Probe without an input file so malformed image data cannot cause a SKIP.
                ComPtr<IWICBitmapDecoder> decoder;
                const HRESULT activation = CoCreateInstance(decoderClass, nullptr,
                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&decoder));
                availability = WicCodecAvailabilityDetail::MergeActivationResult(availability, activation);
                if (availability == EWicCodecAvailability::Available)
                {
                    return availability;
                }
                lastActivationFailure = activation;
                // A different registered decoder may still support this extension.
                break;
            }
            if (end == std::wstring::npos)
            {
                break;
            }
            start = end + 1;
        }
        component.Reset();
    }
    if (FAILED(lastActivationFailure))
    {
        LOGD("WIC", "No decoder activated for extension %s, last activation hr: 0x%08lX",
             Extension.c_str(), static_cast<unsigned long>(lastActivationFailure));
    }
    return next == S_FALSE ? availability : EWicCodecAvailability::Unknown;
}

bool FWicImageLoader::SupportsFormat(const std::string& FilePath) const
{
    const std::filesystem::path path = std::filesystem::u8path(FilePath);
    std::string ext = path.extension().u8string();

    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const std::vector<std::string> supported = GetSupportedExtensions();

    return std::find(supported.begin(), supported.end(), ext) != supported.end();
}

bool FWicImageLoader::SupportsFormat(EImageFormat /*Format*/) const
{
    return false;
}

std::vector<std::string> FWicImageLoader::GetSupportedExtensions() const
{
    return { ".png", ".jpg", ".jpeg", ".jpe", ".bmp", ".tif", ".tiff", ".gif", ".ico", ".webp", ".heic", ".heif", ".jxr", ".dds" };
}
