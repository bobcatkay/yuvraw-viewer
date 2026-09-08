#include "FWicImageLoader.h"
#include "FImageData.h"
#include "FImageLimits.h"
#include "Util.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>
#include <new>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include <windows.h>
#include <wincodec.h>
#include <wincodecsdk.h>
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

    // 元数据来自文件/第三方 codec。给遍历与文本分配设置独立上限，异常不能拖垮像素加载。
    constexpr size_t kMaximumMetadataItems = 65536;
    constexpr size_t kMaximumMetadataDepth = 32;
    constexpr size_t kMaximumMetadataValueBytes = 16 * 1024 * 1024;
    constexpr size_t kMaximumMetadataTextBytes = 64 * 1024 * 1024;
    constexpr size_t kMaximumMetadataStringCharacters = kMaximumMetadataValueBytes / 4;
    constexpr size_t kMaximumMetadataValueElements = 4 * 1024 * 1024;
    constexpr size_t kHexByteTextWidth = 3;
    constexpr uint8_t kHexNibbleMask = 0x0F;
    constexpr int32_t kHexNibbleShift = 4;
    constexpr const char* kMetadataLogTag = "ExifMetadata";

    struct FScopedPropVariant
    {
        PROPVARIANT Value = {};
        ~FScopedPropVariant() { PropVariantClear(&Value); }
        FScopedPropVariant() = default;
        FScopedPropVariant(const FScopedPropVariant&) = delete;
        FScopedPropVariant& operator=(const FScopedPropVariant&) = delete;
    };

    struct FCoTaskStringDeleter
    {
        void operator()(wchar_t* Value) const { CoTaskMemFree(Value); }
    };

    class FMetadataCollector
    {
    public:
        FImageMetadata Metadata;

        void Read(IWICMetadataQueryReader* Reader, const std::string& Parent = {}, size_t Depth = 0)
        {
            if (!Reader)
            {
                return;
            }
            if (Depth >= kMaximumMetadataDepth || VisitedItems >= kMaximumMetadataItems ||
                TextBytes >= kMaximumMetadataTextBytes)
            {
                Metadata.bIncomplete = true;
                return;
            }

            ComPtr<IEnumString> names;
            HRESULT hr = Reader->GetEnumerator(&names);
            if (FAILED(hr) || !names)
            {
                RecordFailure(hr);
                return;
            }

            GUID format = {};
            Reader->GetContainerFormat(&format);
            const EImageMetadataBlock block = format == GUID_MetadataFormatExif
                ? EImageMetadataBlock::Exif
                : (format == GUID_MetadataFormatIfd || format == GUID_MetadataFormatSubIfd
                    ? EImageMetadataBlock::Ifd : EImageMetadataBlock::Other);
            // WIC 的 TIFF/EXIF/GPS 把 RATIONAL 编进 UI8/I8，不能显示成一个巨大的整数。
            const bool bRational = format == GUID_MetadataFormatIfd ||
                format == GUID_MetadataFormatSubIfd || format == GUID_MetadataFormatExif ||
                format == GUID_MetadataFormatGps || format == GUID_MetadataFormatInterop ||
                IsIfdPath(Parent);

            while (VisitedItems < kMaximumMetadataItems && TextBytes < kMaximumMetadataTextBytes)
            {
                LPOLESTR rawName = nullptr;
                ULONG fetched = 0;
                hr = names->Next(1, &rawName, &fetched);
                const std::unique_ptr<wchar_t, FCoTaskStringDeleter> name(rawName);
                if (hr == S_FALSE)
                {
                    return;
                }
                if (FAILED(hr) || fetched != 1 || !name)
                {
                    RecordFailure(FAILED(hr) ? hr : E_FAIL);
                    return;
                }
                ++VisitedItems;

                FScopedPropVariant value;
                hr = Reader->GetMetadataByName(name.get(), &value.Value);
                if (FAILED(hr))
                {
                    RecordFailure(hr);
                    continue;
                }
                const std::string relative = WideToUtf8(name.get());
                const std::string path = Parent +
                    (relative.empty() || relative.front() != '/' ? "/" : "") + relative;

                if (value.Value.vt == VT_UNKNOWN && value.Value.punkVal)
                {
                    ComPtr<IWICMetadataQueryReader> child;
                    hr = value.Value.punkVal->QueryInterface(IID_PPV_ARGS(&child));
                    if (SUCCEEDED(hr))
                    {
                        Read(child.Get(), path, Depth + 1);
                        continue;
                    }
                }

                std::string text;
                FormatValue(value.Value, bRational, text);
                if (path.size() + text.size() > kMaximumMetadataTextBytes - TextBytes)
                {
                    Metadata.bIncomplete = true;
                    return;
                }

                // 某些 codec 在 decoder 与 frame 重复暴露同一字段；只去除路径和值都相同的项。
                const auto matching = Seen.equal_range(path);
                const bool bDuplicate = std::any_of(matching.first, matching.second,
                    [&](const auto& existing) { return Metadata.Entries[existing.second].Value == text; });
                if (!bDuplicate)
                {
                    TextBytes += path.size() + text.size();
                    Metadata.Entries.push_back({ path, std::move(text), block });
                    Seen.emplace(path, Metadata.Entries.size() - 1);
                }
            }
            Metadata.bIncomplete = true;
        }

        void RecordFailure(HRESULT Result)
        {
            Metadata.bIncomplete = true;
            // 每幅图片只记录首次失败，不输出 EXIF 的定位、序列号等内容。
            if (!bLoggedFailure)
            {
                LOGW(kMetadataLogTag, "Metadata read incomplete, hr: 0x%08lX", static_cast<unsigned long>(Result));
                bLoggedFailure = true;
            }
        }

    private:
        static bool IsIfdPath(const std::string& Path)
        {
            std::string leaf = Path.substr(Path.find_last_of('/') + 1);
            const size_t indexEnd = leaf.find(']');
            if (indexEnd != std::string::npos) { leaf.erase(0, indexEnd + 1); }
            return leaf == "ifd" || leaf == "exif" || leaf == "gps" ||
                leaf == "interop" || leaf == "subifd";
        }

        std::string WideToUtf8(const wchar_t* Text)
        {
            if (!Text)
            {
                return {};
            }
            const size_t length = wcsnlen_s(Text, kMaximumMetadataStringCharacters);
            if (length == kMaximumMetadataStringCharacters)
            {
                Metadata.bIncomplete = true;
            }
            if (length == 0)
            {
                return {};
            }
            const int count = static_cast<int>(length);
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, Text, count, nullptr, 0, nullptr, nullptr);
            std::string result(static_cast<size_t>(bytes), '\0');
            if (bytes > 0)
            {
                WideCharToMultiByte(CP_UTF8, 0, Text, count, result.data(), bytes, nullptr, nullptr);
            }
            return result;
        }

        void Append(std::string& Out, const std::string& Text)
        {
            if (Text.size() > kMaximumMetadataValueBytes - Out.size())
            {
                Metadata.bIncomplete = true;
                return;
            }
            Out += Text;
        }

        void FormatBytes(const BYTE* Bytes, size_t Count, std::string& Out)
        {
            if (!Bytes)
            {
                Metadata.bIncomplete = Metadata.bIncomplete || Count != 0;
                return;
            }
            const size_t available = (kMaximumMetadataValueBytes - Out.size()) / kHexByteTextWidth;
            const size_t count = (std::min)(Count, available);
            Metadata.bIncomplete = Metadata.bIncomplete || count != Count;
            constexpr char kHexDigits[] = "0123456789ABCDEF";
            Out.reserve(Out.size() + count * kHexByteTextWidth);
            for (size_t index = 0; index < count; ++index)
            {
                if (index > 0) { Out += ' '; }
                Out += kHexDigits[Bytes[index] >> kHexNibbleShift];
                Out += kHexDigits[Bytes[index] & kHexNibbleMask];
            }
        }

        void FormatValue(const PROPVARIANT& Value, bool bRational, std::string& Out, size_t Depth = 0)
        {
            if (Depth >= kMaximumMetadataDepth || FormattedElements >= kMaximumMetadataValueElements)
            {
                Metadata.bIncomplete = true;
                return;
            }
            ++FormattedElements;
            if ((Value.vt & VT_VECTOR) != 0)
            {
                const VARTYPE type = Value.vt & VT_TYPEMASK;
                // PROPVARIANT 的所有 CA 向量均以 cElems 开头。使用对应字段读取实际元素。
                const ULONG count = Value.caub.cElems;
                for (ULONG index = 0; index < count; ++index)
                {
                    if (Out.size() >= kMaximumMetadataValueBytes || FormattedElements >= kMaximumMetadataValueElements)
                    {
                        Metadata.bIncomplete = true;
                        break;
                    }
                    PROPVARIANT element = {};
                    element.vt = type;
                    // 这些指针借自 Value，不能对 element 调用 PropVariantClear。
#define YUVRAW_METADATA_ELEMENT(Type, Vector, Scalar) \
                    case Type: \
                        if (!Value.Vector.pElems) { Metadata.bIncomplete = true; return; } \
                        element.Scalar = Value.Vector.pElems[index]; break;
                    switch (type)
                    {
                        YUVRAW_METADATA_ELEMENT(VT_I1, cac, cVal)
                        YUVRAW_METADATA_ELEMENT(VT_UI1, caub, bVal)
                        YUVRAW_METADATA_ELEMENT(VT_I2, cai, iVal)
                        YUVRAW_METADATA_ELEMENT(VT_UI2, caui, uiVal)
                        YUVRAW_METADATA_ELEMENT(VT_I4, cal, lVal)
                        YUVRAW_METADATA_ELEMENT(VT_UI4, caul, ulVal)
                        YUVRAW_METADATA_ELEMENT(VT_I8, cah, hVal)
                        YUVRAW_METADATA_ELEMENT(VT_UI8, cauh, uhVal)
                        YUVRAW_METADATA_ELEMENT(VT_R4, caflt, fltVal)
                        YUVRAW_METADATA_ELEMENT(VT_R8, cadbl, dblVal)
                        YUVRAW_METADATA_ELEMENT(VT_BOOL, cabool, boolVal)
                        YUVRAW_METADATA_ELEMENT(VT_LPSTR, calpstr, pszVal)
                        YUVRAW_METADATA_ELEMENT(VT_LPWSTR, calpwstr, pwszVal)
                        YUVRAW_METADATA_ELEMENT(VT_BSTR, cabstr, bstrVal)
                        YUVRAW_METADATA_ELEMENT(VT_FILETIME, cafiletime, filetime)
                    case VT_CLSID:
                        if (!Value.cauuid.pElems) { Metadata.bIncomplete = true; return; }
                        element.puuid = &Value.cauuid.pElems[index];
                        break;
                    case VT_VARIANT:
                        if (!Value.capropvar.pElems) { Metadata.bIncomplete = true; return; }
                        element = Value.capropvar.pElems[index];
                        break;
                    default:
                        Metadata.bIncomplete = true;
                        Append(Out, "VT=" + std::to_string(Value.vt));
                        return;
                    }
#undef YUVRAW_METADATA_ELEMENT
                    std::string elementText;
                    FormatValue(element, bRational, elementText, Depth + 1);
                    // 一些手机把定长 ASCII 的 NUL 填充暴露成空字符串向量，不能变成一串逗号。
                    if (!elementText.empty())
                    {
                        if (!Out.empty()) { Append(Out, ", "); }
                        Append(Out, elementText);
                    }
                }
                return;
            }

            std::ostringstream number;
            number.imbue(std::locale::classic());
            number << std::setprecision(std::numeric_limits<double>::max_digits10);
            switch (Value.vt)
            {
            case VT_EMPTY:
            case VT_NULL: return;
            case VT_I1: number << static_cast<int>(Value.cVal); break;
            case VT_UI1: number << static_cast<unsigned int>(Value.bVal); break;
            case VT_I2: number << Value.iVal; break;
            case VT_UI2: number << Value.uiVal; break;
            case VT_I4: number << Value.lVal; break;
            case VT_UI4: number << Value.ulVal; break;
            case VT_INT: number << Value.intVal; break;
            case VT_UINT: number << Value.uintVal; break;
            case VT_I8:
                if (bRational)
                {
                    number << static_cast<int32_t>(Value.hVal.LowPart) << '/' << Value.hVal.HighPart;
                }
                else { number << Value.hVal.QuadPart; }
                break;
            case VT_UI8:
                if (bRational)
                {
                    // WIC 的 rational pair 为低 32 位分子、高 32 位分母；与曝光属性代理一致。
                    // 保留零分母原值，交给展示层判断有效性，不猜测拍摄参数。
                    number << Value.uhVal.LowPart << '/' << Value.uhVal.HighPart;
                }
                else { number << Value.uhVal.QuadPart; }
                break;
            case VT_R4: number << Value.fltVal; break;
            case VT_R8: number << Value.dblVal; break;
            case VT_BOOL: number << (Value.boolVal != VARIANT_FALSE ? 1 : 0); break;
            case VT_LPSTR:
                if (Value.pszVal)
                {
                    const size_t length = strnlen_s(Value.pszVal, kMaximumMetadataStringCharacters);
                    Metadata.bIncomplete = Metadata.bIncomplete || length == kMaximumMetadataStringCharacters;
                    // EXIF ASCII 与 XMP UTF-8 均可直接显示；非法编码保留为十六进制，避免乱码。
                    if (length > 0 && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        Value.pszVal, static_cast<int>(length), nullptr, 0) == 0)
                    {
                        FormatBytes(reinterpret_cast<const BYTE*>(Value.pszVal), length, Out);
                    }
                    else { Append(Out, std::string(Value.pszVal, length)); }
                }
                return;
            case VT_LPWSTR: Append(Out, WideToUtf8(Value.pwszVal)); return;
            case VT_BSTR: Append(Out, WideToUtf8(Value.bstrVal)); return;
            case VT_BLOB: FormatBytes(Value.blob.pBlobData, Value.blob.cbSize, Out); return;
            case VT_CF:
                if (Value.pclipdata && Value.pclipdata->cbSize >= sizeof(Value.pclipdata->ulClipFmt))
                {
                    FormatBytes(Value.pclipdata->pClipData,
                        Value.pclipdata->cbSize - sizeof(Value.pclipdata->ulClipFmt), Out);
                    return;
                }
                Metadata.bIncomplete = true;
                return;
            case VT_FILETIME:
            {
                SYSTEMTIME time = {};
                if (FileTimeToSystemTime(&Value.filetime, &time))
                {
                    number << time.wYear << '-' << std::setfill('0') << std::setw(2) << time.wMonth
                        << '-' << std::setw(2) << time.wDay << ' ' << std::setw(2) << time.wHour
                        << ':' << std::setw(2) << time.wMinute << ':' << std::setw(2) << time.wSecond << " UTC";
                }
                else { Metadata.bIncomplete = true; }
                break;
            }
            case VT_CLSID:
            {
                constexpr size_t kGuidStringCapacity = 39;
                wchar_t guid[kGuidStringCapacity] = {};
                if (Value.puuid && StringFromGUID2(*Value.puuid, guid, static_cast<int>(kGuidStringCapacity)) > 0)
                {
                    Append(Out, WideToUtf8(guid));
                }
                return;
            }
            default:
                Metadata.bIncomplete = true;
                number << "VT=" << Value.vt;
                break;
            }
            Append(Out, number.str());
        }

        size_t VisitedItems = 0;
        size_t TextBytes = 0;
        size_t FormattedElements = 0;
        bool bLoggedFailure = false;
        std::unordered_multimap<std::string, size_t> Seen;
    };

    void ReadImageMetadata(IWICBitmapDecoder* Decoder, IWICBitmapFrameDecode* Frame, FImageData& Image)
    {
        FMetadataCollector collector;
        try
        {
            ComPtr<IWICMetadataQueryReader> reader;
            HRESULT hr = Frame->GetMetadataQueryReader(&reader);
            if (SUCCEEDED(hr)) { collector.Read(reader.Get()); }
            else if (hr != WINCODEC_ERR_UNSUPPORTEDOPERATION && hr != WINCODEC_ERR_PROPERTYNOTFOUND)
            {
                collector.RecordFailure(hr);
            }
            reader.Reset();
            hr = Decoder->GetMetadataQueryReader(&reader);
            if (SUCCEEDED(hr)) { collector.Read(reader.Get()); }
            else if (hr != WINCODEC_ERR_UNSUPPORTEDOPERATION && hr != WINCODEC_ERR_PROPERTYNOTFOUND)
            {
                collector.RecordFailure(hr);
            }
        }
        catch (const std::exception&)
        {
            collector.Metadata.bIncomplete = true;
            // 元数据是可选附加信息；像素已成功解码，不因元数据分配失败丢掉整张图。
            LOGW(kMetadataLogTag, "%s", "Metadata allocation failed; keeping decoded image");
        }
        try
        {
            LOGD(kMetadataLogTag, "Read %zu metadata fields, incomplete=%d",
                collector.Metadata.Entries.size(), collector.Metadata.bIncomplete ? 1 : 0);
            Image.SetMetadata(std::make_shared<FImageMetadata>(std::move(collector.Metadata)));
        }
        catch (const std::bad_alloc&)
        {
            LOGW(kMetadataLogTag, "%s", "Cannot allocate metadata result; keeping decoded image");
        }
    }

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

    ReadImageMetadata(decoder.Get(), frame.Get(), *imageData);
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
