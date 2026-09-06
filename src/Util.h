#ifndef YUVRAW_UTIL
#define YUVRAW_UTIL

#include <cstdarg>
#include <cstdint>
#include <string>

// Forward declaration for image format
enum class EImageFormat;

// Forward declarations
void logMessage(const char* level, const char* file, const char* func, const char* fmt, va_list args);

// Helper function to extract filename from full path
inline const char* getFileName(const char* filePath)
{
    const char* fileName = filePath;
    const char* p = filePath;
    while (*p)
    {
        if (*p == '/' || *p == '\\')
        {
            fileName = p + 1;
        }
        p++;
    }
    return fileName;
}

// New logging functions with file name and function name
void logDebug(const char* file, const char* func, const char* fmt, ...);
void logInfo(const char* file, const char* func, const char* fmt, ...);
void logWarning(const char* file, const char* func, const char* fmt, ...);
void logError(const char* file, const char* func, const char* fmt, ...);

// New logging macros with file name and function name
// Using variadic macros with __VA_ARGS__
#ifdef _MSC_VER
#define LOGD(func, fmt, ...) logDebug(getFileName(__FILE__), func, fmt, __VA_ARGS__)
#define LOGI(func, fmt, ...) logInfo(getFileName(__FILE__), func, fmt, __VA_ARGS__)
#define LOGW(func, fmt, ...) logWarning(getFileName(__FILE__), func, fmt, __VA_ARGS__)
#define LOGE(func, fmt, ...) logError(getFileName(__FILE__), func, fmt, __VA_ARGS__)
#else
#define LOGD(func, fmt, ...) logDebug(getFileName(__FILE__), func, fmt, ##__VA_ARGS__)
#define LOGI(func, fmt, ...) logInfo(getFileName(__FILE__), func, fmt, ##__VA_ARGS__)
#define LOGW(func, fmt, ...) logWarning(getFileName(__FILE__), func, fmt, ##__VA_ARGS__)
#define LOGE(func, fmt, ...) logError(getFileName(__FILE__), func, fmt, ##__VA_ARGS__)
#endif

/**
 * 从文件名解析格式与分辨率。
 *
 * 格式标识按由分隔符隔开的完整 token 匹配，支持格式表里的稳定名称以及
 * p010le、RGBA、RGB10_A2 / RGBA_1010102、Android RAW10/12/14 等常见别名。
 *
 * @param FilePath 完整文件路径
 * @param OutWidth 输出宽度，未找到时为 0
 * @param OutHeight 输出高度，未找到时为 0
 * @return 解析出的格式；只有带分辨率但没有格式标识时才回退为 NV21
 */
EImageFormat ParseImageInfoFromFilename(const std::string& FilePath, int32_t& OutWidth, int32_t& OutHeight);

/**
 * 识别 Android YUV_420_888 半平面 dump 中把 rowStride 写成宽度的常见布局。
 *
 * 仅处理文件名含 format35/yuv_420_888、且所有 Y/UV 行共享零填充后缀的 NV12/NV21，
 * 以避免把普通画面内容误当 padding。
 */
bool TryResolveAndroidSemiplanarStride(
    const std::string& FilePath,
    EImageFormat Format,
    int32_t EncodedWidth,
    int32_t Height,
    uint64_t FileSize,
    int32_t& OutVisibleWidth,
    int32_t& OutStride);

#endif
