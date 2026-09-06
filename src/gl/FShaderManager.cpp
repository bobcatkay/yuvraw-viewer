#include "FShaderManager.h"
#include "FShader.h"
#include "FShaders.h"
#include "Image/FImageFormatDesc.h"
#include "Util.h"

FShaderManager& FShaderManager::Get()
{
    static FShaderManager instance;
    return instance;
}

FShaderManager::~FShaderManager()
{
    Shutdown();
}

FShader* FShaderManager::GetShaderForFormat(EImageFormat Format)
{
    // 检查缓存中是否已有该格式的着色器
    auto it = ShaderCache.find(Format);

    if (it != ShaderCache.end())
    {
        return it->second && it->second->IsValid() ? it->second.get() : nullptr;
    }

    // 创建新着色器
    auto shader = CreateShaderForFormat(Format);

    if (!shader || !shader->IsValid())
    {
        // 查看器逐帧查询；同一份 GLSL 编译失败后重复编译只会卡顿并刷满日志。
        ShaderCache[Format] = nullptr;
        LOGE("GetShaderForFormat", "Failed to create shader for format: %d", static_cast<int>(Format));
        return nullptr;
    }

    // 缓存着色器
    FShader* shaderPtr = shader.get();
    ShaderCache[Format] = std::move(shader);

    LOGD("GetShaderForFormat", "Created and cached shader for format: %d", static_cast<int>(Format));

    return shaderPtr;
}

void FShaderManager::InitializeAllShaders()
{
    // 遍历格式描述表把所有着色器编一遍。
    // 着色器是运行时编译的，GLSL 里的笔误只有在用户恰好打开那种格式时才会暴露；
    // 开发期 GPU 验证会显式调用这里；正常启动仍按实际打开的格式延迟编译。
    int32_t succeeded = 0;
    int32_t failed = 0;

    for (const FFormatDesc& desc : FImageFormatDesc::GetAll())
    {
        if (desc.Format == EImageFormat::Unknown)
        {
            continue;
        }

        if (GetShaderForFormat(desc.Format))
        {
            ++succeeded;
        }
        else
        {
            ++failed;
            LOGE("InitializeAllShaders", "Shader compilation FAILED for format: %s", desc.Name);
        }
    }

    LOGD("InitializeAllShaders", "Shader precompile done: %d succeeded, %d failed", succeeded, failed);
}

void FShaderManager::Shutdown()
{
    if (ShaderCache.empty())
    {
        return;
    }

    ShaderCache.clear();
    LOGD("Shutdown", "Cleared all shader cache");
}

std::unique_ptr<FShader> FShaderManager::CreateShaderForFormat(EImageFormat Format)
{
    const char* vertexSource = FShaders::GetVertexShader();

    // 片段着色器是拼出来的（版本声明 + 色彩管线前导块 + 格式相关的取样代码），
    // 所以这里拿到的是 std::string 而不是字面量指针
    const std::string fragmentSource = FShaders::GetFragmentShaderForFormat(Format);

    if (!vertexSource || fragmentSource.empty())
    {
        LOGE("CreateShaderForFormat", "Failed to get shader source for format: %d", static_cast<int>(Format));
        return nullptr;
    }

    auto shader = std::make_unique<FShader>();

    if (!shader->CreateFromSource(vertexSource, fragmentSource))
    {
        LOGE("CreateShaderForFormat", "Failed to compile shader for format: %d", static_cast<int>(Format));
        return nullptr;
    }

    LOGD("CreateShaderForFormat", "Successfully created shader for format: %d", static_cast<int>(Format));

    return shader;
}
