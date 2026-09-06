#pragma once

#include <memory>
#include <string>
#include <vector>
#include "FImageData.h"
#include "FImageLoadParams.h"

// 错误随单次调用传递，避免共享 loader 的“最后错误”被并行导出/打开请求覆盖。
enum class EImageLoadError
{
    None,
    InvalidParameters,
    ResourceLimit,
    FileAccess,
    TruncatedData,
    DecodeFailure,
    OutOfMemory,
    CodecUnavailable,
};

inline void SetImageLoadError(EImageLoadError* OutError, EImageLoadError Error) noexcept
{
    if (OutError)
    {
        *OutError = Error;
    }
}

inline const char* GetImageLoadErrorText(EImageLoadError Error) noexcept
{
    switch (Error)
    {
    case EImageLoadError::InvalidParameters: return u8"格式、尺寸、行跨距或位深参数无效";
    case EImageLoadError::ResourceLimit: return u8"图像尺寸或解码缓冲超过安全上限";
    case EImageLoadError::FileAccess: return u8"无法读取文件，请检查文件是否存在及访问权限";
    case EImageLoadError::TruncatedData: return u8"文件数据不完整，无法读取完整图像";
    case EImageLoadError::DecodeFailure: return u8"无法解码该文件，文件可能损坏或格式不受支持";
    case EImageLoadError::OutOfMemory: return u8"内存不足，无法分配图像缓冲";
    case EImageLoadError::CodecUnavailable: return u8"系统缺少此格式的 WIC 解码器";
    default: return "";
    }
}

/**
 * 图像加载器接口
 * 负责从文件加载图像数据
 */
class FImageLoader
{
public:
    virtual ~FImageLoader() = default;

    /**
     * 从文件加载图像
     * @param FilePath 文件路径
     * @param Params 加载参数（无头格式必需）
     * @param OutError 可选失败类别出参，成功时写入 None
     * @return 图像数据，失败返回nullptr
     */
    virtual std::unique_ptr<FImageData> LoadFromFile(const std::string& FilePath, const FImageLoadParams* Params = nullptr, EImageLoadError* OutError = nullptr) = 0;

    /**
     * 检查是否支持该文件（按扩展名判断）
     * 仅用于"用户没有显式指定格式"时的兜底分发
     */
    virtual bool SupportsFormat(const std::string& FilePath) const = 0;

    /**
     * 检查是否支持该像素格式
     *
     * 用户在属性面板显式选了格式时，工厂用这个方法**显式查询**再调用，
     * 而不是挨个 loader 试着 Load、谁返回非空算谁的。
     */
    virtual bool SupportsFormat(EImageFormat Format) const = 0;

    /**
     * 本加载器处理的格式是否自带文件头
     *
     * 自带文件头 = 尺寸与像素格式由文件本身描述，用户无从指定也不该指定。
     * 这类文件**绝不能**套用属性面板上留下的无头格式参数：那会让工厂按格式
     * 命中 FRawImageLoader，把 PNG/BMP 的字节当 NV21 读出来。
     */
    virtual bool IsSelfDescribing() const { return false; }

    /**
     * 获取支持的扩展名列表（全小写，含点号）
     */
    virtual std::vector<std::string> GetSupportedExtensions() const = 0;
};

/**
 * 图像加载器工厂
 *
 * 分发规则：
 *   0. 文件自带文件头（IsSelfDescribingFile）-> 忽略 Params，直接按扩展名分发
 *   1. Params 指定了格式 -> 找第一个 SupportsFormat(EImageFormat) 为真的 loader
 *   2. 否则 -> 找第一个 SupportsFormat(FilePath) 为真的 loader（按扩展名）
 *
 * 规则 0 排在最前面是为了堵住一类实际发生过的错误：从 .yuv 切换到 .bmp 时，
 * 属性面板上还留着 NV21/1440x1920，套上去 .bmp 就会被当成 NV21 解码。
 */
class FImageLoaderFactory
{
public:
    /**
     * 注册加载器
     */
    static void RegisterLoader(std::unique_ptr<FImageLoader> Loader);

    /**
     * 从文件加载图像
     * @param FilePath 文件路径
     * @param Params 加载参数（可选，某些格式需要）
     * @param OutError 可选失败类别出参，不保存在共享工厂或 loader 中
     * @return 图像数据，失败返回nullptr
     */
    static std::unique_ptr<FImageData> LoadImage(const std::string& FilePath, const FImageLoadParams* Params = nullptr, EImageLoadError* OutError = nullptr);

    /**
     * 该文件是否由"自带文件头"的加载器认领（PNG/JPEG/BMP/WebP/TIFF…）
     *
     * 调用方据此决定两件事：加载时不要传无头格式参数，加载后把真实的格式与
     * 尺寸回填给 UI 并禁止编辑。判断只看扩展名，与文件内容无关。
     */
    static bool IsSelfDescribingFile(const std::string& FilePath);

    /**
     * 获取所有支持的扩展名
     */
    static std::vector<std::string> GetAllSupportedExtensions();

    /**
     * 初始化默认加载器
     */
    static void InitializeDefaultLoaders();

    /**
     * 释放所有加载器（进程退出前调用）
     */
    static void Shutdown();

private:
    static std::vector<std::unique_ptr<FImageLoader>>& GetLoaders();
};
