#include "Util.h"
#include "Core/FLogger.h"
#include "Image/FImageData.h"
#include "Image/FImageFormatDesc.h"
#include "Image/FImageLimits.h"
#include <regex>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace
{
    constexpr size_t kMaxJoinedFormatTokens = 3;
    constexpr int32_t kAndroidRowAlignmentBytes = 16;
    constexpr int32_t kMaxInspectedPaddingBytes = 256;

    struct FFilenameFormatAlias
    {
        const char* Token;
        EImageFormat Format;
    };

    std::string ToLowerAscii(const std::string& Value)
    {
        std::string result = Value;
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        return result;
    }

    bool TryParseFilenameDimension(const std::string& Text, int32_t& OutValue)
    {
        const std::string_view view(Text);
        int32_t parsed = 0;
        const std::from_chars_result result =
            std::from_chars(view.data(), view.data() + view.size(), parsed);

        if (view.empty() ||
            result.ec != std::errc() ||
            result.ptr != view.data() + view.size() ||
            parsed < FImageLimits::kMinimumDimension ||
            parsed > FImageLimits::kMaximumDimension)
        {
            return false;
        }

        OutValue = parsed;
        return true;
    }

    /**
     * 文件名里的格式标识通常由下划线、横线或点号分隔。
     * 按完整 token 匹配可避免把 "argb" 误认成 "rgb"。
     */
    std::vector<std::string> SplitFilenameTokens(const std::string& LowerFilename)
    {
        std::vector<std::string> tokens;
        std::string current;

        for (unsigned char c : LowerFilename)
        {
            if (std::isalnum(c))
            {
                current.push_back(static_cast<char>(c));
            }
            else if (!current.empty())
            {
                tokens.push_back(current);
                current.clear();
            }
        }

        if (!current.empty())
        {
            tokens.push_back(current);
        }

        return tokens;
    }

    bool HasFormatToken(const std::vector<std::string>& Tokens, const char* Wanted)
    {
        const std::string wanted = Wanted;

        for (size_t start = 0; start < Tokens.size(); ++start)
        {
            std::string joined;

            for (size_t i = start;
                 i < Tokens.size() && i - start < kMaxJoinedFormatTokens;
                 ++i)
            {
                joined += Tokens[i];

                if (joined == wanted)
                {
                    return true;
                }

                if (joined.size() >= wanted.size())
                {
                    break;
                }
            }
        }

        return false;
    }

    EImageFormat ParseFormatTokens(const std::vector<std::string>& Tokens)
    {
        // 长且具体的别名必须排在短名之前，尤其是 RGBA/RGB 与 Gray16/Gray。
        static const FFilenameFormatAlias kAliases[] =
        {
            { "bayerpacked12", EImageFormat::BayerPacked12 },
            { "bayerraw12",    EImageFormat::BayerPacked12 },
            { "androidraw12",  EImageFormat::BayerPacked12 },
            { "raw12",         EImageFormat::BayerPacked12 },
            { "bayerpacked10", EImageFormat::BayerPacked10 },
            { "bayerraw10",    EImageFormat::BayerPacked10 },
            { "androidraw10",  EImageFormat::BayerPacked10 },
            { "raw10",         EImageFormat::BayerPacked10 },
            { "bayerpacked14", EImageFormat::BayerPacked14 },
            { "bayerraw14",    EImageFormat::BayerPacked14 },
            { "androidraw14",  EImageFormat::BayerPacked14 },
            { "raw14",         EImageFormat::BayerPacked14 },
            { "bayer16",       EImageFormat::Bayer16 },
            { "bayer14",       EImageFormat::Bayer14 },
            { "bayer12",       EImageFormat::Bayer12 },
            { "bayer10",       EImageFormat::Bayer10 },
            { "bayer8",        EImageFormat::Bayer8 },

            { "rgba1010102",   EImageFormat::RGB10A2 },
            { "rgb10a2",       EImageFormat::RGB10A2 },
            { "rgba8888",      EImageFormat::RGBA8 },
            { "rgba8",         EImageFormat::RGBA8 },
            { "rgba",          EImageFormat::RGBA8 },
            { "rgb888",        EImageFormat::RGB8 },
            { "rgb24",         EImageFormat::RGB8 },
            { "rgb8",          EImageFormat::RGB8 },
            { "rgb",           EImageFormat::RGB8 },

            { "grayscale16",   EImageFormat::Grayscale16 },
            { "grey16",        EImageFormat::Grayscale16 },
            { "gray16le",      EImageFormat::Grayscale16 },
            { "gray16",        EImageFormat::Grayscale16 },
            { "y16",           EImageFormat::Grayscale16 },
            { "grayscale8",    EImageFormat::Grayscale8 },
            { "grayscale",     EImageFormat::Grayscale8 },
            { "grey8",         EImageFormat::Grayscale8 },
            { "grey",          EImageFormat::Grayscale8 },
            { "gray8",         EImageFormat::Grayscale8 },
            { "gray",          EImageFormat::Grayscale8 },
            { "y8",            EImageFormat::Grayscale8 },

            { "p010le",        EImageFormat::P010 },
            { "p010",          EImageFormat::P010 },
            { "p016le",        EImageFormat::YUV420SP16 },
            { "p016",          EImageFormat::YUV420SP16 },
            { "p210le",        EImageFormat::P210 },
            { "p210",          EImageFormat::P210 },

            { "yuv420p",       EImageFormat::YUV420P },
            { "yuv420",        EImageFormat::YUV420P },
            { "i420",          EImageFormat::YUV420P },
            { "yuv422p",       EImageFormat::YUV422P },
            { "yuv422",        EImageFormat::YUV422P },
            { "yuv444p",       EImageFormat::YUV444P },
            { "yuv444",        EImageFormat::YUV444P },
            { "bayer",         EImageFormat::Bayer16 },
        };

        for (const FFilenameFormatAlias& alias : kAliases)
        {
            if (HasFormatToken(Tokens, alias.Token))
            {
                return alias.Format;
            }
        }

        // 稳定格式名（NV21、YV12、YUY2、Bayer8 等）由描述表统一维护。
        // 从长组合开始，使 "bayer_14" 先于单独的 "bayer" 被识别。
        const size_t maxSpan = std::min(kMaxJoinedFormatTokens, Tokens.size());

        for (size_t span = maxSpan; span > 0; --span)
        {
            for (size_t start = 0; start + span <= Tokens.size(); ++start)
            {
                std::string joined;

                for (size_t i = start; i < start + span; ++i)
                {
                    joined += Tokens[i];
                }

                const EImageFormat format = FImageFormatDesc::FindByName(joined.c_str());

                if (format != EImageFormat::Unknown)
                {
                    return format;
                }
            }
        }

        return EImageFormat::Unknown;
    }
}

void logMessage(const char* level, const char* file, const char* func, const char* fmt, va_list args)
{
    FLogger::WriteV(level, file, func, fmt, args);
}

void logDebug(const char* file, const char* func, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    logMessage("DEBUG", file, func, fmt, args);
    va_end(args);
}

void logInfo(const char* file, const char* func, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    logMessage("INFO", file, func, fmt, args);
    va_end(args);
}

void logWarning(const char* file, const char* func, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    logMessage("WARN", file, func, fmt, args);
    va_end(args);
}

void logError(const char* file, const char* func, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    logMessage("ERROR", file, func, fmt, args);
    va_end(args);
}

EImageFormat ParseImageInfoFromFilename(const std::string& FilePath, int32_t& OutWidth, int32_t& OutHeight)
{
    OutWidth = 0;
    OutHeight = 0;
    const EImageFormat defaultFormat = EImageFormat::NV21;

    const std::filesystem::path path = std::filesystem::u8path(FilePath);
    const std::string fileName = path.filename().u8string();
    const std::string fileNameLower = ToLowerAscii(fileName);

    // Try to parse resolution: WIDTHxHEIGHT or WIDTHXHEIGHT
    std::regex resolutionPattern(R"((\d+)[xX](\d+))");
    std::smatch match;

    if (std::regex_search(fileName, match, resolutionPattern))
    {
        int32_t parsedWidth = 0;
        int32_t parsedHeight = 0;

        // 文件名来自外部，超长数字不能交给 stoi 抛异常并终止打开流程。
        if (TryParseFilenameDimension(match[1].str(), parsedWidth) &&
            TryParseFilenameDimension(match[2].str(), parsedHeight))
        {
            OutWidth = parsedWidth;
            OutHeight = parsedHeight;
        }
        else
        {
            LOGW(
                "ParseImageInfoFromFilename",
                "Ignoring out-of-range resolution token in file name: %s",
                fileName.c_str());
        }
    }

    const EImageFormat parsedFormat = ParseFormatTokens(SplitFilenameTokens(fileNameLower));

    if (parsedFormat != EImageFormat::Unknown)
    {
        return parsedFormat;
    }

    if (ToLowerAscii(path.extension().u8string()) == ".yuv" &&
        OutWidth > 0 &&
        OutHeight > 0)
    {
        // 历史兼容：只写分辨率的 .yuv 文件仍默认按 NV21 打开。
        return defaultFormat;
    }

    // If format not found but we have resolution, return default format
    if (OutWidth > 0 && OutHeight > 0)
    {
        return defaultFormat;
    }

    return EImageFormat::Unknown;
}

bool TryResolveAndroidSemiplanarStride(
    const std::string& FilePath,
    EImageFormat Format,
    int32_t EncodedWidth,
    int32_t Height,
    uint64_t FileSize,
    int32_t& OutVisibleWidth,
    int32_t& OutStride)
{
    OutVisibleWidth = 0;
    OutStride = 0;

    const std::string lowerName =
        ToLowerAscii(std::filesystem::u8path(FilePath).filename().u8string());
    const bool bAndroid420Dump =
        lowerName.find("format35") != std::string::npos ||
        lowerName.find("yuv_420_888") != std::string::npos;

    if (!bAndroid420Dump ||
        (Format != EImageFormat::NV12 && Format != EImageFormat::NV21) ||
        EncodedWidth <= kAndroidRowAlignmentBytes ||
        Height <= 0 ||
        (Height & 1) != 0 ||
        FImageFormatDesc::CalculateFrameSize(Format, EncodedWidth, Height, 0) != FileSize)
    {
        return false;
    }

    const int32_t inspectedBytes =
        std::min(kMaxInspectedPaddingBytes, EncodedWidth - kAndroidRowAlignmentBytes);
    const int32_t rowCount = Height + Height / 2;
    int32_t commonZeroSuffix = inspectedBytes;

    std::ifstream file(std::filesystem::u8path(FilePath), std::ios::binary);

    if (!file.is_open())
    {
        return false;
    }

    std::vector<uint8_t> suffix(static_cast<size_t>(inspectedBytes));

    for (int32_t row = 0; row < rowCount && commonZeroSuffix > 0; ++row)
    {
        const std::streamoff offset =
            static_cast<std::streamoff>(row) * EncodedWidth +
            (EncodedWidth - inspectedBytes);
        file.seekg(offset, std::ios::beg);
        file.read(
            reinterpret_cast<char*>(suffix.data()),
            static_cast<std::streamsize>(suffix.size()));

        if (file.gcount() != static_cast<std::streamsize>(suffix.size()))
        {
            return false;
        }

        int32_t rowZeroSuffix = 0;

        while (rowZeroSuffix < inspectedBytes &&
               suffix[static_cast<size_t>(inspectedBytes - rowZeroSuffix - 1)] == 0)
        {
            ++rowZeroSuffix;
        }

        commonZeroSuffix = std::min(commonZeroSuffix, rowZeroSuffix);
    }

    if (commonZeroSuffix < kAndroidRowAlignmentBytes ||
        commonZeroSuffix % kAndroidRowAlignmentBytes != 0)
    {
        return false;
    }

    const int32_t visibleWidth = EncodedWidth - commonZeroSuffix;

    if (visibleWidth <= 0 ||
        (visibleWidth & 1) != 0 ||
        FImageFormatDesc::CalculateFrameSize(
            Format, visibleWidth, Height, EncodedWidth) != FileSize)
    {
        return false;
    }

    OutVisibleWidth = visibleWidth;
    OutStride = EncodedWidth;

    LOGI(
        "TryResolveAndroidSemiplanarStride",
        "Detected Android YUV row padding, EncodedWidth: %d, VisibleWidth: %d, Stride: %d",
        EncodedWidth,
        visibleWidth,
        EncodedWidth);

    return true;
}
