#pragma once

#include "FImageLoader.h"

enum class EWicCodecAvailability { Available, Missing, Unknown };

namespace WicCodecAvailabilityDetail
{
    // HRESULT is a signed long on Windows; keep Windows headers out of loader consumers.
    EWicCodecAvailability MergeActivationResult(EWicCodecAvailability Current, long Result);
}

/**
 * 基于 Windows Imaging Component (WIC) 的常规图像格式加载器
 *
 * 覆盖 PNG / JPEG / BMP / TIFF / GIF / HEIF 等自带文件头的格式。
 *
 * 选 WIC 而不是 stb_image / libjpeg 的原因：
 *   - WIC 是 Windows 系统组件，不需要引入、编译、更新任何第三方源码
 *   - 支持的格式比 stb_image 更多（TIFF / HEIF / 相机 RAW 视系统解码器而定）
 *   - 有硬件加速的 JPEG 解码路径
 *
 * 统一输出 RGBA8。位深更高的源（16bit PNG 等）会被 WIC 转换降到 8bit，
 * 如果后续需要保留高位深，改用 GUID_WICPixelFormat64bppRGBA 并输出 RGBA16。
 */
class FWicImageLoader : public FImageLoader
{
public:
    // 检查扩展名注册及无文件激活；只有确切缺失才返回 Missing，不以坏输入推断未安装。
    static EWicCodecAvailability GetCodecAvailability(const std::string& Extension);

    std::unique_ptr<FImageData> LoadFromFile(const std::string& FilePath, const FImageLoadParams* Params = nullptr, EImageLoadError* OutError = nullptr) override;

    bool SupportsFormat(const std::string& FilePath) const override;

    /**
     * 恒为 false：本加载器处理的格式由文件头自描述，
     * 用户在属性面板显式选择 NV21/RAW 等格式时不应命中这里
     */
    bool SupportsFormat(EImageFormat Format) const override;

    /// 恒为 true：这些格式的尺寸与像素格式都写在文件头里
    bool IsSelfDescribing() const override { return true; }

    std::vector<std::string> GetSupportedExtensions() const override;
};
