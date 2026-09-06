#pragma once

#include <memory>
#include <unordered_map>
#include "Image/FImageFormat.h"

// Forward declaration
class FShader;

/**
 * 着色器管理器
 * 统一管理所有格式的着色器，负责创建、缓存和提供着色器
 * 遵循单一职责原则，只负责着色器管理
 */
class FShaderManager
{
public:
    /**
     * 获取单例实例
     */
    static FShaderManager& Get();

    /**
     * 根据图像格式获取着色器
     * @param Format 图像格式
     * @return 着色器指针，失败返回nullptr
     */
    FShader* GetShaderForFormat(EImageFormat Format);

    /**
     * 显式编译全部着色器，仅供开发期 GPU 验证使用。
     * 正常启动走 GetShaderForFormat 延迟编译，避免首帧前阻塞。
     */
    void InitializeAllShaders();

    /**
     * 销毁所有着色器
     */
    void Shutdown();

private:
    FShaderManager() = default;
    ~FShaderManager();
    FShaderManager(const FShaderManager&) = delete;
    FShaderManager& operator=(const FShaderManager&) = delete;

    /**
     * 创建指定格式的着色器
     */
    std::unique_ptr<FShader> CreateShaderForFormat(EImageFormat Format);

    // 着色器缓存：格式 -> 着色器；nullptr 记住编译失败，Shutdown 后才允许重试。
    std::unordered_map<EImageFormat, std::unique_ptr<FShader>> ShaderCache;
};
