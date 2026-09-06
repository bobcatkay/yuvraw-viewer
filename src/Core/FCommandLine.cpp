#include "FCommandLine.h"

#include "Image/FImageFormatDesc.h"
#include "Image/FImageLimits.h"
#include "Util.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace
{
    std::string ToLower(const char* Value)
    {
        std::string s = Value ? Value : "";
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);

        return s;
    }

    /**
     * 在名字表里查一个枚举值
     * @return 匹配到返回下标，否则返回 -1 并打日志
     */
    int32_t FindName(const char* Option, const char* Value, const char* const* Names, int32_t Count)
    {
        const std::string wanted = ToLower(Value);

        for (int32_t i = 0; i < Count; ++i)
        {
            if (wanted == Names[i])
            {
                return i;
            }
        }

        LOGE("Parse", "Unknown value for %s: %s", Option, Value ? Value : "(null)");

        return -1;
    }

    /**
     * 严格解析有界十进制整数。
     *
     * atoi 会把 "12px" 当作 12，溢出行为也不可依赖。命令行尺寸会进入内存大小
     * 计算，因此必须要求整个 token 都是合法十进制文本，并在写入参数前校验范围。
     */
    bool TryParseBoundedInteger(
        const char* Option,
        const char* Value,
        int32_t Minimum,
        int32_t Maximum,
        int32_t& OutValue)
    {
        if (!Value)
        {
            LOGE("Parse", "Missing value for %s", Option);
            return false;
        }

        const std::string_view text(Value);
        int32_t parsed = 0;
        const std::from_chars_result result =
            std::from_chars(text.data(), text.data() + text.size(), parsed);

        if (text.empty() ||
            result.ec != std::errc() ||
            result.ptr != text.data() + text.size() ||
            parsed < Minimum ||
            parsed > Maximum)
        {
            LOGE(
                "Parse",
                "%s must be an integer in [%d, %d], got: %s",
                Option,
                Minimum,
                Maximum,
                Value);

            return false;
        }

        OutValue = parsed;
        return true;
    }

    /**
     * 按空白切分命令行，支持双引号包住带空格的路径
     */
    std::vector<std::string> Tokenize(const char* CommandLine)
    {
        std::vector<std::string> tokens;

        if (!CommandLine)
        {
            return tokens;
        }

        std::string current;
        bool bInQuotes = false;

        for (const char* p = CommandLine; *p; ++p)
        {
            const char c = *p;

            if (c == '"')
            {
                bInQuotes = !bInQuotes;

                continue;
            }

            if (!bInQuotes && (c == ' ' || c == '\t'))
            {
                if (!current.empty())
                {
                    tokens.push_back(current);
                    current.clear();
                }

                continue;
            }

            current.push_back(c);
        }

        if (!current.empty())
        {
            tokens.push_back(current);
        }

        return tokens;
    }
}

FCommandLineOptions FCommandLineOptions::Parse(const char* CommandLine)
{
    FCommandLineOptions options;

    const std::vector<std::string> tokens = Tokenize(CommandLine);

    for (size_t i = 0; i < tokens.size(); ++i)
    {
        const std::string& token = tokens[i];

        // 需要取值的选项：取下一个 token
        auto NextValue = [&tokens, &i]() -> const char*
        {
            if (i + 1 < tokens.size())
            {
                ++i;

                return tokens[i].c_str();
            }

            return nullptr;
        };

        if (token == "--format" || token == "-f")
        {
            if (const char* value = NextValue())
            {
                const EImageFormat format = FImageFormatDesc::FindByName(value);

                if (format == EImageFormat::Unknown)
                {
                    LOGE("Parse", "Unknown format name: %s", value);
                }
                else
                {
                    options.Params.Format = format;
                    options.bHasFormat = true;
                }
            }
        }
        else if (token == "--width" || token == "-w")
        {
            const char* value = NextValue();
            int32_t width = 0;

            if (TryParseBoundedInteger(
                    token.c_str(),
                    value,
                    FImageLimits::kMinimumDimension,
                    FImageLimits::kMaximumDimension,
                    width))
            {
                options.Params.Width = width;
                options.bHasWidth = true;
            }
        }
        else if (token == "--height" || token == "-h")
        {
            const char* value = NextValue();
            int32_t height = 0;

            if (TryParseBoundedInteger(
                    token.c_str(),
                    value,
                    FImageLimits::kMinimumDimension,
                    FImageLimits::kMaximumDimension,
                    height))
            {
                options.Params.Height = height;
                options.bHasHeight = true;
            }
        }
        else if (token == "--stride" || token == "-s")
        {
            const char* value = NextValue();
            int32_t stride = 0;

            if (TryParseBoundedInteger(
                    token.c_str(),
                    value,
                    FImageLimits::kMinimumStrideBytes,
                    FImageLimits::kMaximumStrideBytes,
                    stride))
            {
                options.Params.Stride = stride;
                options.bHasStride = true;
            }
        }
        else if (token == "--bits" || token == "--bits-per-pixel" || token == "-b")
        {
            const char* value = NextValue();
            int32_t bitsPerPixel = 0;

            if (TryParseBoundedInteger(
                    token.c_str(),
                    value,
                    FImageLimits::kMinimumRawBitsPerSample,
                    FImageLimits::kMaximumRawBitsPerSample,
                    bitsPerPixel))
            {
                options.Params.BitsPerPixel = bitsPerPixel;
                options.bHasBitsPerPixel = true;
            }
        }
        else if (token == "--compare" || token == "-c")
        {
            if (const char* value = NextValue())
            {
                options.ComparePath = value;
            }
        }
        else if (token == "--matrix")
        {
            // YCbCr -> R'G'B' 的矩阵系数，对应 ffmpeg 的 colorspace
            static const char* kNames[] = { "bt601", "bt709", "bt2020" };

            if (const char* value = NextValue())
            {
                const int32_t index = FindName("--matrix", value, kNames, 3);

                if (index >= 0)
                {
                    options.Display.ColorSpace = static_cast<EColorSpace>(index);
                    options.bHasDisplay = true;
                }
            }
        }
        else if (token == "--range")
        {
            static const char* kNames[] = { "limited", "full" };

            if (const char* value = NextValue())
            {
                const int32_t index = FindName("--range", value, kNames, 2);

                if (index >= 0)
                {
                    options.Display.ColorRange = static_cast<EColorRange>(index);
                    options.bHasDisplay = true;
                }
            }
        }
        else if (token == "--primaries")
        {
            static const char* kNames[] = { "bt709", "bt2020", "p3", "bt601-525", "bt601-625" };

            if (const char* value = NextValue())
            {
                const int32_t index = FindName("--primaries", value, kNames, 5);

                if (index >= 0)
                {
                    options.Display.Primaries = static_cast<EColorPrimaries>(index);
                    options.bHasDisplay = true;
                }
            }
        }
        else if (token == "--transfer")
        {
            static const char* kNames[] = { "sdr", "bt1886", "pq", "hlg", "linear" };

            if (const char* value = NextValue())
            {
                const int32_t index = FindName("--transfer", value, kNames, 5);

                if (index >= 0)
                {
                    options.Display.Transfer = static_cast<EColorTransfer>(index);
                    options.bHasDisplay = true;
                }
            }
        }
        else if (token == "--tonemap")
        {
            static const char* kNames[] = { "clip", "reinhard", "aces" };

            if (const char* value = NextValue())
            {
                const int32_t index = FindName("--tonemap", value, kNames, 3);

                if (index >= 0)
                {
                    options.Display.ToneMap = static_cast<EToneMapOperator>(index);
                    options.bHasDisplay = true;
                }
            }
        }
        else if (token == "--refwhite")
        {
            if (const char* value = NextValue())
            {
                options.Display.ReferenceWhiteNits = static_cast<float>(std::atof(value));
                options.bHasDisplay = true;
            }
        }
        else if (token == "--exposure")
        {
            if (const char* value = NextValue())
            {
                options.Display.ExposureStops = static_cast<float>(std::atof(value));
                options.bHasDisplay = true;
            }
        }
        else if (token == "--out-of-range")
        {
            options.Display.bShowOutOfRange = true;
            options.bHasDisplay = true;
        }
        else if (token == "--no-hdr")
        {
            options.bDisableHdr = true;
        }
        else if (!token.empty() && token[0] != '-')
        {
            // 第一个非选项参数当作路径
            if (options.PathToOpen.empty())
            {
                options.PathToOpen = token;
            }
        }
        else
        {
            LOGE("Parse", "Unrecognized option: %s", token.c_str());
        }
    }

    return options;
}

void FCommandLineOptions::ApplyTo(FImageLoadParams& OutParams) const
{
    if (bHasFormat)
    {
        OutParams.SetDetectedFormat(Params.Format);
    }

    if (bHasWidth)
    {
        OutParams.Width = Params.Width;
    }

    if (bHasHeight)
    {
        OutParams.Height = Params.Height;
    }

    if (bHasStride)
    {
        OutParams.Stride = Params.Stride;
    }

    if (bHasBitsPerPixel)
    {
        OutParams.BitsPerPixel = Params.BitsPerPixel;
    }

}
