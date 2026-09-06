#pragma once

#include <glad/glad.h>
#include <string>
#include <unordered_map>

/**
 * OpenGL着色器程序封装类
 */
class FShader
{
public:
    FShader();
    ~FShader();

    FShader(const FShader&) = delete;
    FShader& operator=(const FShader&) = delete;

    /**
     * 从源码创建着色器程序
     * @param VertexSource 顶点着色器源码
     * @param FragmentSource 片段着色器源码
     * @return 是否创建成功
     */
    bool CreateFromSource(const std::string& VertexSource, const std::string& FragmentSource);

    /**
     * 使用着色器程序
     */
    void Use() const;

    /**
     * 设置uniform变量
     */
    void SetInt(const std::string& Name, int Value) const;
    void SetFloat(const std::string& Name, float Value) const;
    void SetVec2(const std::string& Name, float X, float Y) const;
    void SetVec3(const std::string& Name, float X, float Y, float Z) const;
    void SetVec4(const std::string& Name, float X, float Y, float Z, float W) const;
    void SetMat4(const std::string& Name, const float* Matrix) const;
    /** Matrix 需为列主序的 9 个 float */
    void SetMat3(const std::string& Name, const float* Matrix) const;

    /**
     * 获取程序ID
     */
    GLuint GetProgramID() const { return ProgramID; }

    /**
     * 检查是否有效
     */
    bool IsValid() const { return ProgramID != 0; }

    /**
     * 销毁着色器程序
     */
    void Destroy();

private:
    GLuint CompileShader(GLenum ShaderType, const std::string& Source);
    GLint GetUniformLocation(const std::string& Name) const;

    GLuint ProgramID;
    GLuint VertexShaderID;
    GLuint FragmentShaderID;
    // uniform 地址仅在同一已链接程序内稳定，Destroy/重建时必须失效。
    mutable std::unordered_map<std::string, GLint> UniformLocations;
};
