#include "FShader.h"
#include "Util.h"

namespace
{
    constexpr GLsizei kShaderInfoLogCapacity = 512;
}

FShader::FShader()
    : ProgramID(0)
    , VertexShaderID(0)
    , FragmentShaderID(0)
{
}

FShader::~FShader()
{
    Destroy();
}

bool FShader::CreateFromSource(const std::string& VertexSource, const std::string& FragmentSource)
{
    // 同一包装对象可以重新编译；先释放旧程序并清空其 uniform 地址。
    Destroy();

    // 编译顶点着色器
    VertexShaderID = CompileShader(GL_VERTEX_SHADER, VertexSource);
    if (VertexShaderID == 0)
    {
        return false;
    }

    // 编译片段着色器
    FragmentShaderID = CompileShader(GL_FRAGMENT_SHADER, FragmentSource);
    if (FragmentShaderID == 0)
    {
        Destroy();
        return false;
    }

    // 创建着色器程序
    ProgramID = glCreateProgram();
    if (ProgramID == 0)
    {
        LOGE("CreateFromSource", "Failed to allocate shader program");
        Destroy();
        return false;
    }

    glAttachShader(ProgramID, VertexShaderID);
    glAttachShader(ProgramID, FragmentShaderID);
    glLinkProgram(ProgramID);

    // 检查链接状态
    GLint success = GL_FALSE;
    glGetProgramiv(ProgramID, GL_LINK_STATUS, &success);

    if (!success)
    {
        char infoLog[kShaderInfoLogCapacity] = {};
        glGetProgramInfoLog(ProgramID, kShaderInfoLogCapacity, nullptr, infoLog);

        LOGE("Create", "Shader program linking failed, InfoLog: %s", infoLog);

        // 统一清理并归零，避免析构再次删除已释放、甚至已被驱动复用的句柄。
        Destroy();
        return false;
    }

    // 删除着色器对象（已经链接到程序中）
    glDeleteShader(VertexShaderID);
    glDeleteShader(FragmentShaderID);
    VertexShaderID = 0;
    FragmentShaderID = 0;

    return true;
}

void FShader::Use() const
{

    if (ProgramID != 0)
    {
        glUseProgram(ProgramID);
    }
}

void FShader::SetInt(const std::string& Name, int Value) const
{
    GLint location = GetUniformLocation(Name);

    if (location != -1)
    {
        glUniform1i(location, Value);
    }
}

void FShader::SetFloat(const std::string& Name, float Value) const
{
    GLint location = GetUniformLocation(Name);

    if (location != -1)
    {
        glUniform1f(location, Value);
    }
}

void FShader::SetVec2(const std::string& Name, float X, float Y) const
{
    GLint location = GetUniformLocation(Name);

    if (location != -1)
    {
        glUniform2f(location, X, Y);
    }
}

void FShader::SetVec4(const std::string& Name, float X, float Y, float Z, float W) const
{
    GLint location = GetUniformLocation(Name);

    if (location != -1)
    {
        glUniform4f(location, X, Y, Z, W);
    }
}

void FShader::SetMat4(const std::string& Name, const float* Matrix) const
{
    GLint location = GetUniformLocation(Name);

    if (location != -1)
    {
        glUniformMatrix4fv(location, 1, GL_FALSE, Matrix);
    }
}

void FShader::SetVec3(const std::string& Name, float X, float Y, float Z) const
{
    GLint location = GetUniformLocation(Name);

    if (location != -1)
    {
        glUniform3f(location, X, Y, Z);
    }
}

void FShader::SetMat3(const std::string& Name, const float* Matrix) const
{
    GLint location = GetUniformLocation(Name);

    if (location != -1)
    {
        // Matrix 必须是列主序，因此 transpose 传 GL_FALSE
        glUniformMatrix3fv(location, 1, GL_FALSE, Matrix);
    }
}

void FShader::Destroy()
{
    UniformLocations.clear();

    if (ProgramID != 0)
    {
        glDeleteProgram(ProgramID);
        ProgramID = 0;
    }


    if (VertexShaderID != 0)
    {
        glDeleteShader(VertexShaderID);
        VertexShaderID = 0;
    }


    if (FragmentShaderID != 0)
    {
        glDeleteShader(FragmentShaderID);
        FragmentShaderID = 0;
    }
}

GLuint FShader::CompileShader(GLenum ShaderType, const std::string& Source)
{
    GLuint shader = glCreateShader(ShaderType);
    if (shader == 0)
    {
        LOGE("CompileShader", "Failed to allocate shader of type: %u", ShaderType);
        return 0;
    }

    const char* sourceCStr = Source.c_str();
    glShaderSource(shader, 1, &sourceCStr, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char infoLog[kShaderInfoLogCapacity] = {};
        glGetShaderInfoLog(shader, kShaderInfoLogCapacity, nullptr, infoLog);
        LOGE("CompileShader", "Shader compilation failed, InfoLog: %s", infoLog);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLint FShader::GetUniformLocation(const std::string& Name) const
{
    if (ProgramID == 0)
    {
        return -1;
    }

    const auto found = UniformLocations.find(Name);
    if (found != UniformLocations.end())
    {
        return found->second;
    }

    // 不存在或被 GLSL 优化掉的 uniform（-1）也缓存，避免每帧重复查询驱动。
    const GLint location = glGetUniformLocation(ProgramID, Name.c_str());
    UniformLocations.emplace(Name, location);
    return location;
}
