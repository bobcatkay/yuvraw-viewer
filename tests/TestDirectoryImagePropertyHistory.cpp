#include "Core/FDirectoryImagePropertyHistory.h"

#include "Image/FImageFormatDesc.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace
{
    int GFailures = 0;

    constexpr int32_t kFirstWidth = 8;
    constexpr int32_t kFirstHeight = 4;
    constexpr int32_t kSecondWidth = 4;
    constexpr int32_t kSecondHeight = 8;
    constexpr uint64_t kMismatchedExtraByteCount = 1;
    constexpr float kFirstExposureStops = 1.5f;
    constexpr float kSecondExposureStops = -0.5f;

    void Check(bool bCondition, const char* Message)
    {
        std::printf("%-72s %s\n", Message, bCondition ? "OK" : "**FAIL**");

        if (!bCondition)
        {
            ++GFailures;
        }
    }

    FDirectoryImageProperties MakeProperties(
        int32_t Width,
        int32_t Height,
        float ExposureStops)
    {
        FDirectoryImageProperties properties;
        properties.LoadParams.SetDetectedFormat(EImageFormat::NV21);
        properties.LoadParams.Width = Width;
        properties.LoadParams.Height = Height;
        properties.DisplaySettings.ColorSpace = EColorSpace::BT709;
        properties.DisplaySettings.Primaries = EColorPrimaries::BT2020;
        properties.DisplaySettings.Transfer = EColorTransfer::HLG;
        properties.DisplaySettings.ExposureStops = ExposureStops;
        return properties;
    }
}

int main()
{
    const std::string firstPath =
        u8"C:/YUVRawTests/目录一/first.yuv";
    const std::string secondPath =
        u8"C:/YUVRawTests/目录一/second.yuv";
    const std::string firstRawPath =
        u8"C:/YUVRawTests/目录一/first.raw";
    const std::string secondRawPath =
        u8"C:/YUVRawTests/目录一/second.RAW";
    const std::string unmatchedExtensionPath =
        u8"C:/YUVRawTests/目录一/frame.bin";
    const std::string otherDirectoryPath =
        u8"C:/YUVRawTests/目录二/first.yuv";
    const std::string selfDescribingPath =
        u8"C:/YUVRawTests/目录一/photo.png";

    const FDirectoryImageProperties firstProperties =
        MakeProperties(kFirstWidth, kFirstHeight, kFirstExposureStops);
    const uint64_t matchingFileSize = static_cast<uint64_t>(
        FImageFormatDesc::CalculateFrameSize(
            firstProperties.LoadParams.Format,
            firstProperties.LoadParams.Width,
            firstProperties.LoadParams.Height,
            firstProperties.LoadParams.Stride));

    FDirectoryImagePropertyHistory history;
    FDirectoryImageProperties resolved;

    Check(
        history.TryGetCompatible(
            firstPath,
            matchingFileSize,
            false,
            resolved) == EDirectoryImagePropertyLookup::NoHistoryForExtension,
        "first image for an extension uses automatic detection");

    history.Remember(
        firstPath,
        firstProperties.LoadParams,
        firstProperties.DisplaySettings,
        false);

    Check(
        history.TryGetCompatible(
            secondPath,
            matchingFileSize,
            false,
            resolved) == EDirectoryImagePropertyLookup::Compatible,
        "same-directory image with matching extension and size reuses properties");
    Check(
        resolved.LoadParams.Format == firstProperties.LoadParams.Format
            && resolved.LoadParams.Width == kFirstWidth
            && resolved.LoadParams.Height == kFirstHeight
            && resolved.DisplaySettings.Transfer == EColorTransfer::HLG
            && resolved.DisplaySettings.ExposureStops == kFirstExposureStops,
        "load and display properties are copied together");

    FImageLoadParams invalidParams;
    history.Remember(
        secondPath,
        invalidParams,
        FDisplaySettings{},
        false);
    Check(
        history.TryGetCompatible(
            secondPath,
            matchingFileSize,
            false,
            resolved) == EDirectoryImagePropertyLookup::Compatible
            && resolved.LoadParams.Width == kFirstWidth
            && resolved.LoadParams.Height == kFirstHeight,
        "invalid failed parameters do not erase successful directory history");

    Check(
        history.TryGetCompatible(
            secondPath,
            matchingFileSize + kMismatchedExtraByteCount,
            false,
            resolved) == EDirectoryImagePropertyLookup::FileSizeMismatch,
        "any inherited frame-size mismatch falls back to automatic detection");

    Check(
        history.TryGetCompatible(
            otherDirectoryPath,
            matchingFileSize,
            false,
            resolved) == EDirectoryImagePropertyLookup::NoHistoryForExtension,
        "history does not leak properties across directories");

    FDirectoryImagePropertyHistory relativePathHistory;
    relativePathHistory.Remember(
        "relative-first.yuv",
        firstProperties.LoadParams,
        firstProperties.DisplaySettings,
        false);
    Check(
        relativePathHistory.TryGetCompatible(
            "relative-second.yuv",
            matchingFileSize,
            false,
            resolved) == EDirectoryImagePropertyLookup::Compatible,
        "relative file names share the normalized working directory");

    Check(
        history.TryGetCompatible(
            selfDescribingPath,
            matchingFileSize,
            true,
            resolved) == EDirectoryImagePropertyLookup::SelfDescribingTarget,
        "self-describing target always uses its own metadata");

    const FDirectoryImageProperties secondProperties =
        MakeProperties(kSecondWidth, kSecondHeight, kSecondExposureStops);

    FDirectoryImagePropertyHistory extensionHistory;
    extensionHistory.Remember(
        firstRawPath,
        firstProperties.LoadParams,
        firstProperties.DisplaySettings,
        false);
    extensionHistory.Remember(
        firstPath,
        secondProperties.LoadParams,
        secondProperties.DisplaySettings,
        false);

    Check(
        extensionHistory.TryGetCompatible(
            secondRawPath,
            matchingFileSize,
            false,
            resolved) == EDirectoryImagePropertyLookup::Compatible
            && resolved.LoadParams.Width == kFirstWidth
            && resolved.LoadParams.Height == kFirstHeight
            && resolved.DisplaySettings.ExposureStops == kFirstExposureStops,
        "RAW history survives viewing a YUV file in the same directory");
    Check(
        extensionHistory.TryGetCompatible(
            secondPath,
            matchingFileSize,
            false,
            resolved) == EDirectoryImagePropertyLookup::Compatible
            && resolved.LoadParams.Width == kSecondWidth
            && resolved.LoadParams.Height == kSecondHeight
            && resolved.DisplaySettings.ExposureStops == kSecondExposureStops,
        "each extension keeps an independent property history");
    Check(
        extensionHistory.TryGetCompatible(
            unmatchedExtensionPath,
            matchingFileSize,
            false,
            resolved) == EDirectoryImagePropertyLookup::NoHistoryForExtension,
        "a different extension never inherits directory properties blindly");

    history.Remember(
        secondPath,
        secondProperties.LoadParams,
        secondProperties.DisplaySettings,
        false);

    Check(
        history.TryGetCompatible(
            firstPath,
            matchingFileSize,
            false,
            resolved) == EDirectoryImagePropertyLookup::Compatible
            && resolved.LoadParams.Width == kSecondWidth
            && resolved.LoadParams.Height == kSecondHeight
            && resolved.DisplaySettings.ExposureStops == kSecondExposureStops,
        "newer image replaces the extension's previous properties");

    history.Remember(
        selfDescribingPath,
        secondProperties.LoadParams,
        secondProperties.DisplaySettings,
        true);
    Check(
        history.TryGetCompatible(
            firstPath,
            matchingFileSize,
            false,
            resolved) == EDirectoryImagePropertyLookup::Compatible
            && resolved.LoadParams.Width == kSecondWidth
            && resolved.LoadParams.Height == kSecondHeight,
        "self-describing extension does not erase YUV directory history");

    relativePathHistory.Clear();
    Check(
        relativePathHistory.TryGetCompatible(
            "relative-second.yuv",
            matchingFileSize,
            false,
            resolved) == EDirectoryImagePropertyLookup::NoHistoryForExtension,
        "clearing application data removes in-session directory history");

    std::printf("\n%s\n", GFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return GFailures == 0 ? 0 : 1;
}
