#include <Windows.h>
#include "Core/FAppVersion.h"
#include "Core/FApplication.h"
#include "Core/FLogger.h"
#include "Util.h"

#include <cwchar>

namespace
{
    /**
     * Windows 在宽字符入口中提供 UTF-16 命令行。项目内部路径统一保存为 UTF-8，
     * 这样 ImGui 显示、GLFW 拖放和后续文件加载使用同一套编码。
     */
    std::string WideToUtf8(const wchar_t* Wide)
    {
        if (!Wide || !*Wide)
        {
            return {};
        }

        const int wideLength = static_cast<int>(std::wcslen(Wide));
        const int required = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            Wide,
            wideLength,
            nullptr,
            0,
            nullptr,
            nullptr);

        if (required <= 0)
        {
            return {};
        }

        std::string utf8(static_cast<size_t>(required), '\0');
        const int written = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            Wide,
            wideLength,
            utf8.data(),
            required,
            nullptr,
            nullptr);

        return written == required ? utf8 : std::string();
    }
}

/**
 * Windows入口点
 * 使用新的架构
 */
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nShowCmd)
{
    // 日志先于窗口与图形栈初始化，确保启动失败也能留下诊断信息。
    FLogger::Initialize();
    LOGI("WinMain", "Starting YUVRaw %s", FAppVersion::String);

    const std::string commandLineUtf8 = WideToUtf8(lpCmdLine);

    if (lpCmdLine && *lpCmdLine && commandLineUtf8.empty())
    {
        LOGE("WinMain", "Failed to convert the UTF-16 command line to UTF-8");
        FLogger::Shutdown();

        return 1;
    }

    FApplication& App = FApplication::Get();

    // lpCmdLine 不含程序名；转换后交给只接收 UTF-8 的参数解析与路径链路。
    if (!App.Initialize(commandLineUtf8.c_str()))
    {
        LOGE("WinMain", "Application initialization failed with exit code %d", 1);
        App.Shutdown();
        FLogger::Shutdown();

        return 1;
    }

    const int exitCode = App.Run();

    // 不能把核心资源留给函数内静态对象的逆序析构：图像加载器和着色器单例
    // 都可能晚于 FApplication 创建、早于它析构。显式关闭才能保证文档归档时
    // 加载器仍然有效，并在 OpenGL 上下文销毁前释放全部 GL 资源。
    App.Shutdown();
    LOGI("WinMain", "YUVRaw %s exited with code %d", FAppVersion::String, exitCode);
    FLogger::Shutdown();

    return exitCode;
}
