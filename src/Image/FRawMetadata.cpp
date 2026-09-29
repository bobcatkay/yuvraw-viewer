#include "FRawMetadata.h"
#include "FImageLimits.h"
#include "Util.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>

namespace
{
    constexpr uintmax_t kMaximumMetadataBytes = 1024u * 1024u;
    constexpr const char* kFramePrefix = "[InputFrameIndex:";

    std::string Trim(const std::string& Text)
    {
        const auto first = Text.find_first_not_of(" \t\r\n");
        return first == std::string::npos ? std::string{} :
            Text.substr(first, Text.find_last_not_of(" \t\r\n") - first + 1);
    }

    bool ParseInt(const std::string& Text, int32_t& Value)
    {
        const auto result = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
        return result.ec == std::errc{} && result.ptr == Text.data() + Text.size();
    }

    template <size_t Count>
    bool ParseFloats(std::string Text, std::array<float, Count>& Values)
    {
        std::replace(Text.begin(), Text.end(), ',', ' ');
        std::istringstream stream(Text);
        stream.imbue(std::locale::classic());
        for (float& value : Values)
        {
            if (!(stream >> value) || !std::isfinite(value)) { return false; }
        }
        stream >> std::ws;
        return stream.eof();
    }
}

int32_t FRawMetadata::GetFrameIndex(const std::string& RawPath)
{
    const std::string stem = std::filesystem::u8path(RawPath).stem().u8string();
    const size_t separator = stem.find_last_of('_');
    int32_t frameIndex = 0;
    return separator != std::string::npos && ParseInt(stem.substr(separator + 1), frameIndex) &&
        frameIndex >= 0 && frameIndex <= kMaximumFrameIndex ? frameIndex : 0;
}

ERawMetadataResult FRawMetadata::LoadForRawFile(
    const std::string& RawPath, int32_t BitDepth, FRawDisplaySettings& OutSettings)
{
    const auto rawPath = std::filesystem::u8path(RawPath);
    auto metadataPath = rawPath;
    metadataPath.replace_extension(".txt");
    const std::string stem = rawPath.stem().u8string();
    const size_t separator = stem.find_last_of('_');
    const int32_t frameIndex = GetFrameIndex(RawPath);
    int32_t suffixIndex = 0;
    const bool bHasFrameSuffix = separator != std::string::npos &&
        ParseInt(stem.substr(separator + 1), suffixIndex) &&
        suffixIndex >= 0 && suffixIndex <= kMaximumFrameIndex;

    std::error_code ec;
    if (!std::filesystem::is_regular_file(metadataPath, ec) && bHasFrameSuffix)
    {
        metadataPath = rawPath.parent_path() / std::filesystem::u8path(stem.substr(0, separator) + ".txt");
    }
    if (!std::filesystem::is_regular_file(metadataPath, ec))
    {
        return ERawMetadataResult::NotFound;
    }
    const auto result = LoadFile(metadataPath.u8string(), frameIndex, BitDepth, OutSettings);
    if (result != ERawMetadataResult::Success)
    {
        LOGW("RawMetadata", "Companion TXT rejected, frame %d, reason %d",
            frameIndex, static_cast<int32_t>(result));
    }
    return result;
}

ERawMetadataResult FRawMetadata::LoadFile(
    const std::string& MetadataPath, int32_t FrameIndex, int32_t BitDepth,
    FRawDisplaySettings& OutSettings)
{
    const auto metadataPath = std::filesystem::u8path(MetadataPath);
    const int32_t frameIndex = FrameIndex;
    if (frameIndex < 0 || frameIndex > kMaximumFrameIndex) { return ERawMetadataResult::FrameNotFound; }
    if (BitDepth < FImageLimits::kMinimumRawBitsPerSample || BitDepth > FImageLimits::kMaximumRawBitsPerSample)
    {
        return ERawMetadataResult::BitDepthMismatch;
    }
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(metadataPath, ec);
    if (ec || bytes > kMaximumMetadataBytes) { return ERawMetadataResult::Invalid; }
    std::ifstream file(metadataPath);
    if (!file.is_open()) { return ERawMetadataResult::Invalid; }

    FRawDisplaySettings parsed = OutSettings;
    std::array<bool, FRawDisplaySettings::kCfaChannelCount> blackFound{};
    bool bRedFound = false, bBlueFound = false, bMatrixFound = false;
    bool bFrameFound = false;
    int32_t currentFrame = -1;
    int32_t metadataBitDepth = 0;
    std::string line;
    while (std::getline(file, line))
    {
        line = Trim(line);
        if (line.compare(0, std::char_traits<char>::length(kFramePrefix), kFramePrefix) == 0)
        {
            // 每份 TXT 可包含多帧，读取目标帧后停止，避免后续帧覆盖参数。
            if (bFrameFound) { break; }
            const size_t prefixLength = std::char_traits<char>::length(kFramePrefix);
            if (line.back() != ']' || !ParseInt(Trim(line.substr(prefixLength, line.size() - prefixLength - 1)), currentFrame))
            {
                return ERawMetadataResult::Invalid;
            }
            bFrameFound = currentFrame == frameIndex;
            continue;
        }
        if (currentFrame != frameIndex) { continue; }
        const size_t colon = line.find(':');
        if (colon == std::string::npos) { continue; }
        const std::string key = Trim(line.substr(0, colon));
        const std::string value = Trim(line.substr(colon + 1));
        std::array<float, 1> scalar{};
        if (key == "redGain" || key == "blueGain")
        {
            if (!ParseFloats(value, scalar)) { return ERawMetadataResult::Invalid; }
            const bool bRed = key == "redGain";
            parsed.WhiteBalance[bRed ? 0 : 2] = scalar[0];
            (bRed ? bRedFound : bBlueFound) = true;
        }
        else if (key == "CCM")
        {
            if (!ParseFloats(value, parsed.Ccm)) { return ERawMetadataResult::Invalid; }
            bMatrixFound = true;
        }
        else if (key == "imageBitDepth")
        {
            if (!ParseInt(value, metadataBitDepth)) { return ERawMetadataResult::Invalid; }
        }
        else
        {
            for (int32_t channel = 0; channel < FRawDisplaySettings::kCfaChannelCount; ++channel)
            {
                if (key == "blackLevel[" + std::to_string(channel) + "]")
                {
                    if (!ParseFloats(value, scalar)) { return ERawMetadataResult::Invalid; }
                    parsed.BlackLevel[channel] = scalar[0];
                    blackFound[channel] = true;
                }
            }
        }
    }
    if (file.bad()) { return ERawMetadataResult::Invalid; }
    if (!bFrameFound) { return ERawMetadataResult::FrameNotFound; }
    if (metadataBitDepth != BitDepth) { return ERawMetadataResult::BitDepthMismatch; }
    // 红蓝增益相对于绿色通道，绿以 1 为基准；白电平使用有效位深的满量程。
    parsed.WhiteBalance[1] = 1.0f;
    parsed.WhiteLevel = 0.0f;
    parsed.bEnabled = true;
    // 自动导入的参数允许下次打开时更新；手动导入/编辑由 UI 标记为用户配置。
    parsed.bConfigured = false;
    parsed.bApplyCcm = true;
    const float maximum = static_cast<float>((1 << BitDepth) - 1);
    if (!bRedFound || !bBlueFound || !bMatrixFound ||
        !std::all_of(blackFound.begin(), blackFound.end(), [](bool Found) { return Found; }) ||
        !std::all_of(parsed.BlackLevel.begin(), parsed.BlackLevel.end(),
            [maximum](float Black) { return Black < maximum; }) || !parsed.IsValid())
    {
        LOGW("RawMetadata", "Incomplete or invalid RAW colour metadata, frame %d", frameIndex);
        return ERawMetadataResult::Invalid;
    }
    OutSettings = parsed;
    LOGI("RawMetadata", "Imported RAW colour metadata, frame %d, bits %d", frameIndex, BitDepth);
    return ERawMetadataResult::Success;
}
