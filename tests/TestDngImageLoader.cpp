#include "PublicFixtures.h"
#include "Image/FDngImageLoader.h"
#include "Image/FImageLimits.h"
#include "Core/FLogger.h"
#include <libraw/libraw.h>

// LibRaw 的 Winsock 头会间接引入 Win32 LoadImage 宏，此处调用的是应用的工厂 API。
#ifdef LoadImage
#undef LoadImage
#endif

#include <cstdio>
#include <limits>

namespace
{
    constexpr int32_t kRgbaChannels = 4;
    constexpr int32_t kAlphaChannel = 3;
    constexpr uint8_t kOpaqueAlpha = 255;
    int gFailures = 0;
    void Check(const char* Label, bool Passed)
    {
        std::printf("%-65s %s\n", Label, Passed ? "OK" : "FAIL");
        if (!Passed) ++gFailures;
    }
    void CheckImage(const FImageData& Image, bool IsSynthetic)
    {
        Check("decoded image valid", Image.IsValid());
        if (IsSynthetic)
        {
            Check("synthetic width exact", Image.GetWidth() == PublicFixtures::kWidth);
            Check("synthetic height exact", Image.GetHeight() == PublicFixtures::kHeight);
        }
        Check("decoded format RGBA8", Image.GetFormat() == EImageFormat::RGBA8);
        Check("tightly packed stride", Image.GetStride() == Image.GetWidth() * kRgbaChannels);
        const size_t expected = static_cast<size_t>(Image.GetWidth()) * Image.GetHeight() * kRgbaChannels;
        Check("byte count matches dimensions", Image.GetPixelDataSize() == expected);
        if (Image.GetPixelDataSize() != expected) return;
        uint8_t minimum = (std::numeric_limits<uint8_t>::max)();
        uint8_t maximum = 0;
        bool opaque = true;
        for (size_t offset = 0; offset < expected; offset += kRgbaChannels)
        {
            for (int channel = 0; channel < kAlphaChannel; ++channel)
            {
                minimum = (std::min)(minimum, Image.GetPixelData()[offset + channel]);
                maximum = (std::max)(maximum, Image.GetPixelData()[offset + channel]);
            }
            opaque = opaque && Image.GetPixelData()[offset + kAlphaChannel] == kOpaqueAlpha;
        }
        Check("alpha opaque", opaque);
        Check("non-constant RGB", maximum > minimum);
        if (IsSynthetic)
        {
            constexpr uint32_t kRampInset = 8;
            const auto* row = Image.GetPixelData() + static_cast<size_t>(Image.GetHeight() / 2) * Image.GetStride();
            int left = 0;
            int right = 0;
            for (int channel = 0; channel < kAlphaChannel; ++channel)
            {
                left += row[kRampInset * kRgbaChannels + channel];
                right += row[(Image.GetWidth() - 1 - kRampInset) * kRgbaChannels + channel];
            }
            Check("synthetic ramp preserves brightness direction", right > left);
        }
        std::printf("Decoded %dx%d RGB range %u..%u\n", Image.GetWidth(), Image.GetHeight(), minimum, maximum);
    }

    void CheckLibRawMemoryPolicy(const std::filesystem::path& Directory)
    {
        constexpr uint32_t kMemoryProbeDimension = 1024;
        constexpr unsigned kProbeLimitMebiBytes = 1;
        const auto path = Directory / "synthetic_memory_limit.dng";
        const std::vector<uint8_t> pixels(
            static_cast<size_t>(kMemoryProbeDimension) * kMemoryProbeDimension * sizeof(uint16_t), 0);
        Check("memory policy fixture written", PublicFixtures::Write(path,
            PublicFixtures::Tiff(kMemoryProbeDimension, kMemoryProbeDimension, true, pixels)));
        const auto processor = std::make_unique<LibRaw>();
        processor->imgdata.rawparams.max_raw_memory_mb = kProbeLimitMebiBytes;
        Check("low-limit LibRaw can inspect metadata", processor->open_file(path.c_str()) == LIBRAW_SUCCESS);
        Check("open_file preserves explicitly set memory cap",
            processor->imgdata.rawparams.max_raw_memory_mb == kProbeLimitMebiBytes);
        // 故意用小上限验证实际拒绝路径，不通过分配数百 MiB 来制造内存压力。
        Check("LibRaw rejects RAW allocation above configured cap", processor->unpack() == LIBRAW_TOO_BIG);
        processor->recycle();
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    void CheckFactoryErrors(const std::filesystem::path& ValidPath)
    {
        FImageLoaderFactory::InitializeDefaultLoaders();
        EImageLoadError error = EImageLoadError::DecodeFailure;
        FImageLoadParams stale;
        stale.Format = EImageFormat::NV21;
        stale.Width = FImageLimits::kMaximumDimension + 1;
        stale.Height = 1;
        const auto image = FImageLoaderFactory::LoadImage(ValidPath.u8string(), &stale, &error);
        Check("factory ignores stale RAW params for DNG", image && error == EImageLoadError::None);
        const auto invalid = ValidPath.parent_path() / "factory_invalid.png";
        Check("factory invalid fixture written", PublicFixtures::Write(invalid, {'b', 'a', 'd'}));
        Check("factory forwards decode failure", !FImageLoaderFactory::LoadImage(invalid.u8string(), &stale, &error) && error != EImageLoadError::None);
        Check("factory default null error output remains usable", FImageLoaderFactory::LoadImage(ValidPath.u8string()) != nullptr);
        Check("factory rejects directory as input", !FImageLoaderFactory::LoadImage(ValidPath.parent_path().u8string(), nullptr, &error) && error == EImageLoadError::FileAccess);
        Check("factory success after failure clears error", FImageLoaderFactory::LoadImage(ValidPath.u8string(), nullptr, &error) && error == EImageLoadError::None);
        std::error_code ec;
        std::filesystem::remove(invalid, ec);
        FImageLoaderFactory::Shutdown();
    }
}

int wmain(int Count, wchar_t** Arguments)
{
    if (Count < 2 || Count > 3)
    {
        std::fprintf(stderr, "Usage: TestDngImageLoader.exe <fixture.dng> [--synthetic]\n");
        return 2;
    }
    const bool synthetic = Count == 3 && std::wstring(Arguments[2]) == L"--synthetic";
    const std::filesystem::path path(Arguments[1]);
    const auto logDirectory = std::filesystem::temp_directory_path() / "YUVRawDngTestLogs";
    std::filesystem::create_directories(logDirectory);
    FLogger::Initialize(logDirectory, FLogger::kDefaultMaxFileBytes, 1);
    FDngImageLoader loader;
    EImageLoadError error = EImageLoadError::None;
    Check("DNG extension recognized", loader.SupportsFormat(path.u8string()) && loader.SupportsFormat("fixture.DNG"));
    auto image = loader.LoadFromFile(path.u8string(), nullptr, &error);
    Check("LibRaw decoded fixture", image != nullptr && error == EImageLoadError::None);
    if (image) CheckImage(*image, synthetic);
    if (synthetic)
    {
        CheckLibRawMemoryPolicy(path.parent_path());
        CheckFactoryErrors(path);
        const auto missing = (path.parent_path() / "missing.dng").u8string();
        Check("missing DNG has access error", !loader.LoadFromFile(missing, nullptr, &error) && error == EImageLoadError::FileAccess);
        const auto damaged = path.parent_path() / "synthetic_corrupt.dng";
        Check("invalid DNG fixture written", PublicFixtures::Write(damaged, {'I', 'I', 0, 0}));
        Check("invalid DNG rejected", !loader.LoadFromFile(damaged.u8string(), nullptr, &error));
        Check("invalid DNG has readable error", *GetImageLoadErrorText(error) != '\0');
        const auto truncated = PublicFixtures::Tiff(PublicFixtures::kWidth, PublicFixtures::kHeight, true, {});
        Check("truncated DNG fixture written", PublicFixtures::Write(damaged, truncated));
        Check("truncated DNG rejected", !loader.LoadFromFile(damaged.u8string(), nullptr, &error));
        constexpr uint32_t kHugeDimension = 32768;
        const auto oversized = PublicFixtures::Tiff(kHugeDimension, kHugeDimension, true, {});
        Check("oversized DNG fixture written", PublicFixtures::Write(damaged, oversized));
        Check("oversized DNG rejected", !loader.LoadFromFile(damaged.u8string(), nullptr, &error));
        Check("oversized DNG rejected before unpack", error == EImageLoadError::ResourceLimit);
        image = loader.LoadFromFile(path.u8string(), nullptr, &error);
        Check("valid DNG loads after failures", image && error == EImageLoadError::None);
        std::error_code ec;
        std::filesystem::remove(damaged, ec);
    }
    FLogger::Shutdown();
    std::error_code ec;
    std::filesystem::remove(logDirectory / "YUVRaw.log", ec);
    std::filesystem::remove(logDirectory, ec);
    return gFailures ? 1 : 0;
}
