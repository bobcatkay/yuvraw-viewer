#include "FImageLoader.h"
#include "FDngImageLoader.h"
#include "FRawImageLoader.h"
#include "FWicImageLoader.h"
#include "Util.h"

#include <algorithm>
#include <filesystem>

std::vector<std::unique_ptr<FImageLoader>>& FImageLoaderFactory::GetLoaders()
{
    // 函数内静态：避免静态初始化顺序问题，进程退出时自动析构
    static std::vector<std::unique_ptr<FImageLoader>> Loaders;

    return Loaders;
}

void FImageLoaderFactory::RegisterLoader(std::unique_ptr<FImageLoader> Loader)
{
    if (Loader)
    {
        GetLoaders().push_back(std::move(Loader));
    }
}

std::unique_ptr<FImageData> FImageLoaderFactory::LoadImage(const std::string& FilePath, const FImageLoadParams* Params, EImageLoadError* OutError)
{
    SetImageLoadError(OutError, EImageLoadError::DecodeFailure);
    std::error_code ec;

    if (!std::filesystem::is_regular_file(std::filesystem::u8path(FilePath), ec))
    {
        SetImageLoadError(OutError, EImageLoadError::FileAccess);
        return nullptr;
    }

    const std::vector<std::unique_ptr<FImageLoader>>& loaders = GetLoaders();

    // 0) 自带文件头的格式：尺寸与像素格式由文件描述，Params 一律忽略。
    //    否则从 .yuv 切到 .bmp 时，面板上残留的 NV21 参数会把 .bmp 当成 NV21 解码。
    const bool bSelfDescribing = IsSelfDescribingFile(FilePath);

    // 1) 用户显式指定了格式：显式查询谁支持该格式，而不是挨个试
    if (!bSelfDescribing && Params && Params->Format != EImageFormat::Unknown)
    {
        for (const auto& loader : loaders)
        {
            if (loader && loader->SupportsFormat(Params->Format))
            {
                return loader->LoadFromFile(FilePath, Params, OutError);
            }
        }

        LOGE("LoadImage", "No loader supports format: %d", static_cast<int>(Params->Format));
        SetImageLoadError(OutError, EImageLoadError::InvalidParameters);

        return nullptr;
    }

    // 2) 否则按扩展名分发（PNG/JPG 这类自带文件头的格式走这条路）
    for (const auto& loader : loaders)
    {
        if (loader && loader->SupportsFormat(FilePath))
        {
            return loader->LoadFromFile(FilePath, bSelfDescribing ? nullptr : Params, OutError);
        }
    }

    return nullptr;
}

bool FImageLoaderFactory::IsSelfDescribingFile(const std::string& FilePath)
{
    for (const auto& loader : GetLoaders())
    {
        if (loader && loader->IsSelfDescribing() && loader->SupportsFormat(FilePath))
        {
            return true;
        }
    }

    return false;
}

std::vector<std::string> FImageLoaderFactory::GetAllSupportedExtensions()
{
    std::vector<std::string> extensions;

    for (const auto& loader : GetLoaders())
    {
        if (loader)
        {
            auto loaderExtensions = loader->GetSupportedExtensions();
            extensions.insert(extensions.end(), loaderExtensions.begin(), loaderExtensions.end());
        }
    }

    std::sort(extensions.begin(), extensions.end());
    extensions.erase(std::unique(extensions.begin(), extensions.end()), extensions.end());

    return extensions;
}

void FImageLoaderFactory::InitializeDefaultLoaders()
{
    if (!GetLoaders().empty())
    {
        return;
    }

    // DNG 需要相机 RAW 元数据（黑白电平、白平衡和颜色矩阵），不能只依赖系统上
    // 是否碰巧安装了 WIC RAW codec，因此由 LibRaw 专门处理。
    RegisterLoader(std::make_unique<FDngImageLoader>());

    // 常规有文件头格式优先：显式指定格式时它不会被选中（SupportsFormat(EImageFormat) 返回 false），
    // 按扩展名分发时它先于 FRawImageLoader 认领 .png/.jpg 等。
    RegisterLoader(std::make_unique<FWicImageLoader>());

    // 无头格式的通用加载器。新增此类格式只需在 FImageFormatDesc 的表里加一行，
    // 不需要新建 loader
    RegisterLoader(std::make_unique<FRawImageLoader>());
}

void FImageLoaderFactory::Shutdown()
{
    GetLoaders().clear();
}
