#pragma once

#include "FImageLoader.h"

/**
 * 无文件头格式的通用加载器
 *
 * 覆盖所有由格式描述表（FImageFormatDesc）描述的格式：
 * NV12/NV21/P010/YUV420SP16/I420/YV12/YUV422P/YUV444P/NV16/P210/YUY2/UYVY/
 * Android RAW_SENSOR/RAW10/RAW12/RAW14、通用 Bayer、灰度与裸 RGB。
 *
 * 这类格式没有文件头，宽高、行跨距、具体格式必须由 FImageLoadParams 提供，
 * 扩展名（.yuv/.raw）不足以判定格式。
 *
 * 新增一种此类格式**不需要动这个文件**，只需在格式描述表里加一行。
 */
class FRawImageLoader : public FImageLoader
{
public:
    std::unique_ptr<FImageData> LoadFromFile(const std::string& FilePath, const FImageLoadParams* Params = nullptr, EImageLoadError* OutError = nullptr) override;

    bool SupportsFormat(const std::string& FilePath) const override;

    bool SupportsFormat(EImageFormat Format) const override;

    std::vector<std::string> GetSupportedExtensions() const override;
};
