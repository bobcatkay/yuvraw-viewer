#pragma once

#include <memory>
#include <string>

class FWindow;
class FRenderer;
class FMainDockSpace;

/**
 * 应用程序主类
 * 负责初始化、主循环和清理工作
 */
class FApplication
{
public:
    FApplication();
    ~FApplication();

    /**
     * 初始化应用程序
     * @param CommandLine UTF-8 编码的命令行（不含程序名），可为空
     * @return 是否初始化成功
     */
    bool Initialize(const char* CommandLine = nullptr);

    /**
     * 运行主循环
     * @return 退出代码
     */
    int Run();

    /**
     * 清理资源
     */
    void Shutdown();

    /**
     * 获取应用程序单例
     */
    static FApplication& Get();

    /**
     * 处理文件拖放
     */
    void HandleFileDrop(int Count, const char** Paths);

private:
    std::unique_ptr<FWindow> Window;
    std::unique_ptr<FRenderer> Renderer;
    std::unique_ptr<FMainDockSpace> MainDockSpace;

    bool bIsInitialized;
    bool bShouldClose;

    static FApplication* Instance;
};
