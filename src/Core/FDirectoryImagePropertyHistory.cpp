#include "FDirectoryImagePropertyHistory.h"

#include "Image/FResolutionGuess.h"

#include <algorithm>
#include <exception>
#include <filesystem>

void FDirectoryImagePropertyHistory::Clear()
{
    Entries.clear();
}

bool FDirectoryImagePropertyHistory::BuildHistoryKeys(
    const std::string& FilePath,
    std::string& OutDirectoryKey,
    std::string& OutExtensionKey)
{
    OutDirectoryKey.clear();
    OutExtensionKey.clear();

    if (FilePath.empty())
    {
        return false;
    }

    try
    {
        const std::filesystem::path inputPath =
            std::filesystem::u8path(FilePath);
        std::error_code ec;
        std::filesystem::path absolutePath =
            std::filesystem::absolute(inputPath, ec);

        if (ec)
        {
            ec.clear();
            absolutePath = inputPath;
        }

        std::filesystem::path normalized =
            std::filesystem::weakly_canonical(absolutePath, ec);

        if (ec)
        {
            normalized = absolutePath;
        }

        const std::filesystem::path normalizedPath = normalized.lexically_normal();
        const std::filesystem::path parent = normalizedPath.parent_path();

        if (parent.empty())
        {
            return false;
        }

        OutDirectoryKey = parent.generic_u8string();
        OutExtensionKey = normalizedPath.extension().generic_u8string();

        // Windows 扩展名不区分大小写；这里只转换 ASCII，避免受进程区域设置影响。
        std::transform(
            OutExtensionKey.begin(),
            OutExtensionKey.end(),
            OutExtensionKey.begin(),
            [](unsigned char value)
            {
                if (value >= static_cast<unsigned char>('A')
                    && value <= static_cast<unsigned char>('Z'))
                {
                    return static_cast<char>(
                        value - static_cast<unsigned char>('A') + 'a');
                }

                return static_cast<char>(value);
            });
        return !OutDirectoryKey.empty();
    }
    catch (const std::exception&)
    {
        return false;
    }
}

void FDirectoryImagePropertyHistory::Remember(
    const std::string& FilePath,
    const FImageLoadParams& LoadParams,
    const FDisplaySettings& DisplaySettings,
    bool bSelfDescribing)
{
    std::string directoryKey;
    std::string extensionKey;

    if (!BuildHistoryKeys(FilePath, directoryKey, extensionKey))
    {
        return;
    }

    // 失败请求会把尝试过的参数回填到文档；无效参数不能抹掉该目录同扩展名
    // 上一张成功 RAW/YUV 的历史，否则下一张同尺寸图片就失去了继承机会。
    if (!LoadParams.IsValid())
    {
        return;
    }

    if (bSelfDescribing)
    {
        const auto directory = Entries.find(directoryKey);

        if (directory != Entries.end())
        {
            directory->second.erase(extensionKey);

            if (directory->second.empty())
            {
                Entries.erase(directory);
            }
        }

        return;
    }

    FDirectoryImageProperties properties;
    properties.LoadParams = LoadParams;
    properties.LoadParams.ConstrainStorageLayout();
    properties.DisplaySettings = DisplaySettings;
    Entries[directoryKey][extensionKey] = properties;
}

EDirectoryImagePropertyLookup FDirectoryImagePropertyHistory::TryGetCompatible(
    const std::string& FilePath,
    uint64_t FileSize,
    bool bSelfDescribing,
    FDirectoryImageProperties& OutProperties) const
{
    if (bSelfDescribing)
    {
        return EDirectoryImagePropertyLookup::SelfDescribingTarget;
    }

    std::string directoryKey;
    std::string extensionKey;

    if (!BuildHistoryKeys(FilePath, directoryKey, extensionKey))
    {
        return EDirectoryImagePropertyLookup::NoHistoryForExtension;
    }

    const auto directory = Entries.find(directoryKey);

    if (directory == Entries.end())
    {
        return EDirectoryImagePropertyLookup::NoHistoryForExtension;
    }

    const auto found = directory->second.find(extensionKey);

    if (found == directory->second.end())
    {
        return EDirectoryImagePropertyLookup::NoHistoryForExtension;
    }

    const FImageLoadParams& params = found->second.LoadParams;
    const bool bMatches = FResolutionGuess::Matches(
        params.Format,
        params.Width,
        params.Height,
        params.Stride,
        FileSize);

    if (!bMatches)
    {
        return EDirectoryImagePropertyLookup::FileSizeMismatch;
    }

    OutProperties = found->second;
    return EDirectoryImagePropertyLookup::Compatible;
}
