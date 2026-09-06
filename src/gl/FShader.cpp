#include "FShader.h"
#include "Util.h"
#include <iostream>
#include <vector>

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
        glDeleteShader(VertexShaderID);
        return false;
    }

    // 创建着色器程序
    ProgramID = glCreateProgram();
    glAttachShader(ProgramID, VertexShaderID);
    glAttachShader(ProgramID, FragmentShaderID);
    glLinkProgram(ProgramID);

    // 检查链接状态
    GLint success;
    glGetProgramiv(ProgramID, GL_LINK_STATUS, &success);

    if (!success)
    {
        char infoLog[512];
        glGetProgramInfoLog(ProgramID, 512, nullptr, infoLog);

        LOGE("Create", "Shader program linking failed, InfoLog: %s", infoLog);

        glDeleteProgram(ProgramID);
        glDeleteShader(VertexShaderID);
        glDeleteShader(FragmentShaderID);
        ProgramID = 0;
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
    const char* sourceCStr = Source.c_str();
    glShaderSource(shader, 1, &sourceCStr, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
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
    return glGetUniformLocation(ProgramID, Name.c_str());
}
