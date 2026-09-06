#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <filesystem>

/**
 * 线程安全的应用日志模块。
 *
 * 日志默认写入 %LOCALAPPDATA%\YUVRaw\Logs，同时保留 stderr 与
 * Visual Studio 输出窗口，方便开发和用户侧问题排查。
 */
namespace FLogger
{
    constexpr uintmax_t kBytesPerMiB = 1024u * 1024u;
    constexpr uintmax_t kDefaultMaxFileBytes = 10u * kBytesPerMiB;
    constexpr size_t kDefaultMaxFileCount = 5u;

    /**
     * 使用生产环境默认目录和滚动限制初始化。
     * 初始化失败不会影响应用启动，日志仍会输出到 stderr 和调试器。
     */
    bool Initialize();

    /**
     * 使用指定目录和限制初始化。主要供离线测试复用滚动逻辑。
     * 调用方需要先 Shutdown，不能在日志写入期间重新配置。
     */
    bool Initialize(const std::filesystem::path& Directory, uintmax_t MaxFileBytes, size_t MaxFileCount);

    /**
     * 刷新并关闭当前日志文件。正常应用退出由模块静态生命周期自动完成。
     */
    void Shutdown();

    /**
     * 写入一条 printf 风格日志。
     */
    void Write(const char* Level, const char* File, const char* Function, const char* Format, ...);
    void WriteV(const char* Level, const char* File, const char* Function, const char* Format, va_list Args);

    /**
     * 当前日志目录。初始化前返回默认目录。
     */
    std::filesystem::path GetLogDirectory();
}
