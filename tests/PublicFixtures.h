#pragma once

// Entirely procedural fixtures: no camera data, original photographs, or external assets.
// TIFF/DNG tag definitions follow Adobe DNG 1.6 and TIFF 6.0; see docs/PUBLIC_FIXTURES.md.
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace PublicFixtures
{
    constexpr uint32_t kWidth = 64;
    constexpr uint32_t kHeight = 48;
    constexpr uint32_t kPaddingBytes = 16;
    constexpr uint32_t kNv21Stride = kWidth + kPaddingBytes;
    constexpr uint32_t kP010Stride = kWidth * sizeof(uint16_t) + kPaddingBytes;
    constexpr uint16_t kRawWhiteLevel = 4095;
    constexpr uint8_t kPaddingSentinel = 0xA5;
    constexpr uint16_t kP010Shift = 6;
    constexpr uint32_t kByteBits = 8;
    constexpr uint32_t kByteMask = 0xFF;

    inline void Append16(std::vector<uint8_t>& Bytes, uint16_t Value)
    {
        Bytes.push_back(static_cast<uint8_t>(Value));
        Bytes.push_back(static_cast<uint8_t>(Value >> kByteBits));
    }
    inline void Append32(std::vector<uint8_t>& Bytes, uint32_t Value)
    {
        constexpr uint32_t kWordBytes = sizeof(Value);
        for (uint32_t byte = 0; byte < kWordBytes; ++byte)
        {
            Bytes.push_back(static_cast<uint8_t>(Value >> (byte * kByteBits)));
        }
    }
    inline void Set32(std::vector<uint8_t>& Bytes, size_t Offset, uint32_t Value)
    {
        for (size_t byte = 0; byte < sizeof(Value); ++byte)
        {
            Bytes.at(Offset + byte) = static_cast<uint8_t>(Value >> (byte * kByteBits));
        }
    }
    inline void Set16(std::vector<uint8_t>& Bytes, size_t Offset, uint16_t Value)
    {
        Bytes.at(Offset) = static_cast<uint8_t>(Value);
        Bytes.at(Offset + 1) = static_cast<uint8_t>(Value >> kByteBits);
    }
    inline bool Write(const std::filesystem::path& Path, const std::vector<uint8_t>& Bytes)
    {
        std::ofstream file(Path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
        return file.good();
    }

    enum class ETiffTag : uint16_t
    {
        NewSubfileType = 254, Width = 256, Height = 257, BitsPerSample = 258,
        Compression = 259, Photometric = 262, Make = 271, Model = 272,
        StripOffsets = 273, Orientation = 274, SamplesPerPixel = 277,
        RowsPerStrip = 278, StripByteCounts = 279, PlanarConfiguration = 284,
        CfaRepeatPattern = 33421, CfaPattern = 33422, DngVersion = 50706,
        DngBackwardVersion = 50707, UniqueCameraModel = 50708,
        CfaPlaneColor = 50710, CfaLayout = 50711, BlackLevel = 50714,
        WhiteLevel = 50717, DefaultCropOrigin = 50719, DefaultCropSize = 50720,
        ColorMatrix1 = 50721, AsShotNeutral = 50728, CalibrationIlluminant1 = 50778,
    };
    enum class ETiffType : uint16_t { Byte = 1, Ascii = 2, Short = 3, Long = 4, Rational = 5, SRational = 10 };
    struct FTiffEntry { ETiffTag Tag; ETiffType Type; uint32_t Count; std::vector<uint8_t> Value; };

    inline FTiffEntry Scalars(ETiffTag Tag, ETiffType Type, std::initializer_list<uint32_t> Values)
    {
        FTiffEntry entry{Tag, Type, static_cast<uint32_t>(Values.size()), {}};
        for (uint32_t value : Values)
        {
            if (Type == ETiffType::Short) Append16(entry.Value, static_cast<uint16_t>(value));
            else if (Type == ETiffType::Byte) entry.Value.push_back(static_cast<uint8_t>(value));
            else Append32(entry.Value, value);
        }
        return entry;
    }
    inline FTiffEntry Ascii(ETiffTag Tag, const char* Value)
    {
        std::vector<uint8_t> bytes;
        do { bytes.push_back(static_cast<uint8_t>(*Value)); } while (*Value++);
        return {Tag, ETiffType::Ascii, static_cast<uint32_t>(bytes.size()), bytes};
    }
    inline FTiffEntry Rationals(ETiffTag Tag, ETiffType Type, std::initializer_list<uint32_t> Numerators)
    {
        FTiffEntry entry{Tag, Type, static_cast<uint32_t>(Numerators.size()), {}};
        for (uint32_t value : Numerators) { Append32(entry.Value, value); Append32(entry.Value, 1); }
        return entry;
    }

    // A single strip is deliberate: tests can truncate pixels without corrupting the IFD.
    inline std::vector<uint8_t> Tiff(uint32_t Width, uint32_t Height, bool IsDng, const std::vector<uint8_t>& Pixels)
    {
        constexpr uint16_t kTiffMagic = 42;
        constexpr uint32_t kIfdOffset = 8;
        constexpr uint32_t kEntryBytes = 12;
        constexpr uint32_t kInlineBytes = 4;
        constexpr uint32_t kGrayBlackIsZero = 1;
        constexpr uint32_t kCfaPhotometric = 32803;
        constexpr uint32_t kDngBits = 16;
        constexpr uint32_t kGrayBits = 8;
        constexpr uint32_t kD65Illuminant = 21;
        const uint32_t pixelBytes = Width * Height * (IsDng ? sizeof(uint16_t) : sizeof(uint8_t));
        std::vector<FTiffEntry> entries = {
            Scalars(ETiffTag::NewSubfileType, ETiffType::Long, {0}),
            Scalars(ETiffTag::Width, ETiffType::Long, {Width}),
            Scalars(ETiffTag::Height, ETiffType::Long, {Height}),
            Scalars(ETiffTag::BitsPerSample, ETiffType::Short, {IsDng ? kDngBits : kGrayBits}),
            Scalars(ETiffTag::Compression, ETiffType::Short, {1}),
            Scalars(ETiffTag::Photometric, ETiffType::Short, {IsDng ? kCfaPhotometric : kGrayBlackIsZero}),
            Scalars(ETiffTag::StripOffsets, ETiffType::Long, {0}),
            Scalars(ETiffTag::Orientation, ETiffType::Short, {1}),
            Scalars(ETiffTag::SamplesPerPixel, ETiffType::Short, {1}),
            Scalars(ETiffTag::RowsPerStrip, ETiffType::Long, {Height}),
            Scalars(ETiffTag::StripByteCounts, ETiffType::Long, {pixelBytes}),
            Scalars(ETiffTag::PlanarConfiguration, ETiffType::Short, {1}),
        };
        if (IsDng)
        {
            const std::vector<FTiffEntry> dng = {
                Ascii(ETiffTag::Make, "YUVRaw Synthetic"), Ascii(ETiffTag::Model, "Public Fixture"),
                Scalars(ETiffTag::CfaRepeatPattern, ETiffType::Short, {2, 2}),
                Scalars(ETiffTag::CfaPattern, ETiffType::Byte, {0, 1, 1, 2}),
                Scalars(ETiffTag::DngVersion, ETiffType::Byte, {1, 4, 0, 0}),
                Scalars(ETiffTag::DngBackwardVersion, ETiffType::Byte, {1, 1, 0, 0}),
                Ascii(ETiffTag::UniqueCameraModel, "YUVRaw Procedural RGGB"),
                Scalars(ETiffTag::CfaPlaneColor, ETiffType::Byte, {0, 1, 2}),
                Scalars(ETiffTag::CfaLayout, ETiffType::Short, {1}),
                Scalars(ETiffTag::BlackLevel, ETiffType::Long, {0}),
                Scalars(ETiffTag::WhiteLevel, ETiffType::Long, {kRawWhiteLevel}),
                Scalars(ETiffTag::DefaultCropOrigin, ETiffType::Long, {0, 0}),
                Scalars(ETiffTag::DefaultCropSize, ETiffType::Long, {Width, Height}),
                Rationals(ETiffTag::ColorMatrix1, ETiffType::SRational, {1, 0, 0, 0, 1, 0, 0, 0, 1}),
                Rationals(ETiffTag::AsShotNeutral, ETiffType::Rational, {1, 1, 1}),
                Scalars(ETiffTag::CalibrationIlluminant1, ETiffType::Short, {kD65Illuminant}),
            };
            entries.insert(entries.end(), dng.begin(), dng.end());
        }
        std::sort(entries.begin(), entries.end(), [](const auto& A, const auto& B) { return A.Tag < B.Tag; });
        std::vector<uint8_t> bytes{'I', 'I'};
        Append16(bytes, kTiffMagic);
        Append32(bytes, kIfdOffset);
        Append16(bytes, static_cast<uint16_t>(entries.size()));
        const size_t extrasOffset = kIfdOffset + sizeof(uint16_t) + entries.size() * kEntryBytes + sizeof(uint32_t);
        std::vector<uint8_t> extras;
        size_t stripOffsetValue = 0;
        for (const auto& entry : entries)
        {
            Append16(bytes, static_cast<uint16_t>(entry.Tag));
            Append16(bytes, static_cast<uint16_t>(entry.Type));
            Append32(bytes, entry.Count);
            if (entry.Tag == ETiffTag::StripOffsets) stripOffsetValue = bytes.size();
            if (entry.Value.size() <= kInlineBytes)
            {
                bytes.insert(bytes.end(), entry.Value.begin(), entry.Value.end());
                bytes.resize(bytes.size() + kInlineBytes - entry.Value.size(), 0);
            }
            else
            {
                Append32(bytes, static_cast<uint32_t>(extrasOffset + extras.size()));
                extras.insert(extras.end(), entry.Value.begin(), entry.Value.end());
                if (extras.size() % sizeof(uint16_t)) extras.push_back(0);
            }
        }
        Append32(bytes, 0);
        bytes.insert(bytes.end(), extras.begin(), extras.end());
        Set32(bytes, stripOffsetValue, static_cast<uint32_t>(bytes.size()));
        bytes.insert(bytes.end(), Pixels.begin(), Pixels.end());
        return bytes;
    }

    inline std::vector<uint8_t> Bayer()
    {
        std::vector<uint8_t> bytes;
        for (uint32_t y = 0; y < kHeight; ++y)
            for (uint32_t x = 0; x < kWidth; ++x)
            {
                // Equal channels in each RGGB cell give a neutral horizontal ramp after demosaicing.
                const uint16_t value = static_cast<uint16_t>((x / 2) * kRawWhiteLevel / (kWidth / 2 - 1));
                Append16(bytes, value);
            }
        return bytes;
    }
    inline std::vector<uint8_t> Nv21(bool Reverse = false)
    {
        constexpr uint8_t kLumaBlack = 16;
        constexpr uint8_t kLumaRange = 219;
        constexpr uint8_t kChromaNeutral = 128;
        std::vector<uint8_t> bytes(kNv21Stride * (kHeight + kHeight / 2), kPaddingSentinel);
        for (uint32_t y = 0; y < kHeight; ++y)
            for (uint32_t x = 0; x < kWidth; ++x)
                bytes[y * kNv21Stride + x] = static_cast<uint8_t>(kLumaBlack + (Reverse ? kWidth - 1 - x : x) * kLumaRange / (kWidth - 1));
        for (uint32_t y = 0; y < kHeight / 2; ++y)
            std::fill_n(bytes.begin() + (kHeight + y) * kNv21Stride, kWidth, kChromaNeutral);
        return bytes;
    }
    inline std::vector<uint8_t> P010()
    {
        constexpr uint16_t kLumaBlack = 64;
        constexpr uint16_t kLumaRange = 876;
        constexpr uint16_t kChromaNeutral = 512;
        std::vector<uint8_t> bytes(kP010Stride * (kHeight + kHeight / 2), kPaddingSentinel);
        for (uint32_t y = 0; y < kHeight; ++y)
            for (uint32_t x = 0; x < kWidth; ++x)
                Set16(bytes, y * kP010Stride + x * sizeof(uint16_t),
                    static_cast<uint16_t>((kLumaBlack + x * kLumaRange / (kWidth - 1)) << kP010Shift));
        for (uint32_t y = 0; y < kHeight / 2; ++y)
            for (uint32_t x = 0; x < kWidth; ++x)
                Set16(bytes, (kHeight + y) * kP010Stride + x * sizeof(uint16_t), kChromaNeutral << kP010Shift);
        return bytes;
    }
    inline std::vector<uint8_t> ColorBars()
    {
        constexpr uint8_t kMaximum = 255;
        constexpr std::array<std::array<uint8_t, 3>, 8> kBars = {{
            {kMaximum, kMaximum, kMaximum}, {kMaximum, kMaximum, 0},
            {0, kMaximum, kMaximum}, {0, kMaximum, 0}, {kMaximum, 0, kMaximum},
            {kMaximum, 0, 0}, {0, 0, kMaximum}, {0, 0, 0}}};
        std::vector<uint8_t> bytes;
        for (uint32_t y = 0; y < kHeight; ++y)
            for (uint32_t x = 0; x < kWidth; ++x)
            {
                const auto& color = kBars[x * kBars.size() / kWidth];
                bytes.insert(bytes.end(), color.begin(), color.end());
                bytes.push_back(kMaximum);
            }
        return bytes;
    }
}
