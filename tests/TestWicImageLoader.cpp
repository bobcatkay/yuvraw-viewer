#include "PublicFixtures.h"
#include "Image/FWicImageLoader.h"
#include "Image/FImageLimits.h"
#include "Core/FLogger.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <limits>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace
{
    int gFailures = 0;
    void Check(const char* Label, bool Passed)
    {
        std::printf("%-65s %s\n", Label, Passed ? "OK" : "FAIL");
        if (!Passed) ++gFailures;
    }

    void TestCodecActivationClassification()
    {
        using namespace WicCodecAvailabilityDetail;
        using EAvailability = EWicCodecAvailability;
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Check("COM initialized for file-independent codec probes", SUCCEEDED(comResult));
        if (FAILED(comResult)) return;

        {
            Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
            const HRESULT missingClass = CoCreateInstance(CLSID_NULL, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&decoder));
            Check("unregistered COM class has exact absence error", missingClass == REGDB_E_CLASSNOTREG);
            Check("metadata without a decoder class is missing",
                MergeActivationResult(EAvailability::Missing, missingClass) == EAvailability::Missing);

            const HRESULT pngClass = CoCreateInstance(CLSID_WICPngDecoder, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&decoder));
            Check("PNG decoder activates without an input file", SUCCEEDED(pngClass) && decoder);
            Check("a later working decoder overrides missing classes",
                MergeActivationResult(EAvailability::Missing, pngClass) == EAvailability::Available);
            Check("a later working decoder overrides uncertain candidates",
                MergeActivationResult(EAvailability::Unknown, pngClass) == EAvailability::Available);
        }
        CoUninitialize();

        const HRESULT uncertainFailures[] = {
            WINCODEC_ERR_COMPONENTINITIALIZEFAILURE, WINCODEC_ERR_BADIMAGE,
            E_FAIL, E_ACCESSDENIED, E_OUTOFMEMORY
        };
        for (const HRESULT failure : uncertainFailures)
        {
            Check("non-absence activation failure remains unknown",
                MergeActivationResult(EAvailability::Missing, failure) == EAvailability::Unknown);
            Check("later missing class cannot erase an uncertain failure",
                MergeActivationResult(MergeActivationResult(EAvailability::Missing, failure),
                    REGDB_E_CLASSNOTREG) == EAvailability::Unknown);
        }
    }
    class FTempDirectory
    {
    public:
        FTempDirectory()
        {
            Directory = std::filesystem::temp_directory_path() / "YUVRawWicBoundaryTests";
            std::filesystem::create_directories(Directory);
        }
        ~FTempDirectory()
        {
            for (const auto& path : Files) { std::error_code ec; std::filesystem::remove(path, ec); }
            std::error_code ec;
            std::filesystem::remove(Directory, ec);
        }
        std::string Write(const char* Name, const std::vector<uint8_t>& Bytes)
        {
            const auto path = Directory / std::filesystem::u8path(Name);
            Check("fixture write", PublicFixtures::Write(path, Bytes));
            Files.push_back(path);
            return path.u8string();
        }
        std::filesystem::path Directory;
        std::vector<std::filesystem::path> Files;
    };
}

int main()
{
    FTempDirectory files;
    FLogger::Initialize(files.Directory, FLogger::kDefaultMaxFileBytes, 1);
    TestCodecActivationClassification();
    FWicImageLoader loader;
    EImageLoadError error = EImageLoadError::None;
    constexpr uint32_t kValidWidth = 2;
    constexpr uint32_t kValidHeight = 2;
    const std::vector<uint8_t> pixels = {0, 64, 128, 255};
    const auto validBytes = PublicFixtures::Tiff(kValidWidth, kValidHeight, false, pixels);
    const auto valid = files.Write(u8"正常_2x2.tiff", validBytes);
    const auto image = loader.LoadFromFile(valid, nullptr, &error);
    Check("valid grayscale TIFF and Chinese path", image && image->IsValid());
    Check("success clears error", error == EImageLoadError::None);
    if (image)
    {
        constexpr size_t kRgbaChannels = 4;
        constexpr size_t kAlphaIndex = 3;
        bool exact = image->GetWidth() == kValidWidth && image->GetHeight() == kValidHeight;
        for (size_t pixel = 0; pixel < pixels.size(); ++pixel)
        {
            for (size_t channel = 0; channel < kAlphaIndex; ++channel)
                exact = exact && image->GetPixelData()[pixel * kRgbaChannels + channel] == pixels[pixel];
            exact = exact && image->GetPixelData()[pixel * kRgbaChannels + kAlphaIndex] == 255;
        }
        Check("RGBA dimensions, grayscale truth and alpha", exact);
    }
    struct FGeometry { const char* Name; uint32_t Width; uint32_t Height; };
    constexpr uint32_t kLargeWidth = 16384;
    constexpr uint32_t kLargeHeight = 8193;
    const FGeometry geometries[] = {
        {"dimension_limit.tiff", static_cast<uint32_t>(FImageLimits::kMaximumDimension) + 1, 1},
        {"pixel_limit.tiff", kLargeWidth, kLargeHeight},
        {"uint_width.tiff", (std::numeric_limits<uint32_t>::max)(), 2},
    };
    for (const auto& geometry : geometries)
    {
        const auto path = files.Write(geometry.Name, PublicFixtures::Tiff(geometry.Width, geometry.Height, false, {}));
        Check(geometry.Name, !loader.LoadFromFile(path, nullptr, &error));
        // A system codec may reject impossible TIFF geometry even earlier than our guard.
        Check("oversized header reports failure", error != EImageLoadError::None);
        if (geometry.Width != (std::numeric_limits<uint32_t>::max)())
            Check("WIC geometry rejected by application before allocation", error == EImageLoadError::ResourceLimit);
    }
    auto truncatedBytes = validBytes;
    truncatedBytes.resize(truncatedBytes.size() - pixels.size());
    const auto truncated = files.Write("truncated.tiff", truncatedBytes);
    Check("truncated pixel payload rejected", !loader.LoadFromFile(truncated, nullptr, &error));
    Check("truncation has readable error", *GetImageLoadErrorText(error) != '\0');
    const auto invalid = files.Write("invalid.png", {'n', 'o', 't', 'p', 'n', 'g'});
    Check("invalid PNG rejected", !loader.LoadFromFile(invalid, nullptr, &error));
    Check("invalid PNG is not missing built-in codec", error != EImageLoadError::CodecUnavailable);

    // Original 1x1 RGB fixture encoded independently with libwebp 1.4.0, not this encoder.
    // The same valid bytes exercise real optional-codec absence on Windows Server runners.
    const std::vector<uint8_t> knownWebpBytes = {
        0x52, 0x49, 0x46, 0x46, 0x1E, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50,
        0x56, 0x50, 0x38, 0x4C, 0x11, 0x00, 0x00, 0x00, 0x2F, 0x00, 0x00, 0x00,
        0x00, 0x07, 0x50, 0x91, 0x46, 0x74, 0xA6, 0xFF, 0x81, 0x88, 0xE8, 0x7F,
        0x00, 0x00,
    };
    const auto knownWebpPath = files.Write("known_1x1.webp", knownWebpBytes);
    const auto webpAvailability = FWicImageLoader::GetCodecAvailability(".webp");
    const auto knownWebp = loader.LoadFromFile(knownWebpPath, nullptr, &error);
    if (webpAvailability == EWicCodecAvailability::Missing)
    {
        Check("valid WebP reports missing codec when its class cannot activate",
            !knownWebp && error == EImageLoadError::CodecUnavailable);
    }
    else
    {
        constexpr uint8_t kExpectedWebpRgba[] = {17, 34, 51, 255};
        Check("available WebP codec decodes independent fixture exactly",
            knownWebp && knownWebp->GetWidth() == 1 && knownWebp->GetHeight() == 1 &&
            error == EImageLoadError::None &&
            std::equal(std::begin(kExpectedWebpRgba), std::end(kExpectedWebpRgba), knownWebp->GetPixelData()));
        const auto invalidWebp = files.Write("invalid.webp", {'b', 'a', 'd', 'w', 'e', 'b', 'p'});
        Check("bad WebP is not missing when its decoder can activate",
            !loader.LoadFromFile(invalidWebp, nullptr, &error) &&
            error != EImageLoadError::CodecUnavailable && error != EImageLoadError::None);
    }
    const auto again = loader.LoadFromFile(valid, nullptr, &error);
    Check("same loader opens valid file after failures", again && again->IsValid() && error == EImageLoadError::None);
    const auto missing = (files.Directory / "missing.tiff").u8string();
    Check("missing WIC file has access error", !loader.LoadFromFile(missing, nullptr, &error) && error == EImageLoadError::FileAccess);
    const auto missingUnknownCodec = (files.Directory / "missing.yuvraw_nonexistent_codec").u8string();
    Check("missing file error is not overwritten by missing codec", !loader.LoadFromFile(missingUnknownCodec, nullptr, &error) && error == EImageLoadError::FileAccess);
    Check("optional error pointer may be omitted", loader.LoadFromFile(valid) != nullptr);

    const char* extensions[] = {".png", ".webp", ".heic", ".heif"};
    for (const auto* extension : extensions)
    {
        const auto availability = FWicImageLoader::GetCodecAvailability(extension);
        Check("codec enumeration succeeds", availability != EWicCodecAvailability::Unknown);
        std::printf("WIC %s decoder: %s\n", extension,
            availability == EWicCodecAvailability::Available ? "AVAILABLE" :
            availability == EWicCodecAvailability::Missing ? "MISSING (optional tests may SKIP)" : "UNKNOWN");
    }
    Check("built-in PNG decoder registered", FWicImageLoader::GetCodecAvailability(".png") == EWicCodecAvailability::Available);
    Check("unknown extension is missing", FWicImageLoader::GetCodecAvailability(".yuvraw_nonexistent_codec") == EWicCodecAvailability::Missing);
    FLogger::Shutdown();
    std::error_code ec;
    std::filesystem::remove(files.Directory / "YUVRaw.log", ec);
    return gFailures ? 1 : 0;
}
