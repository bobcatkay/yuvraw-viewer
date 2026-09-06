#include "FLogger.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    constexpr const char* kCurrentLogFileName = "YUVRaw.log";
    constexpr const char* kRotatedLogFilePrefix = "YUVRaw.";
    constexpr const char* kRotatedLogFileSuffix = ".log";
    constexpr size_t kTimestampBufferSize = 32u;
    constexpr size_t kMessageBufferSize = 4096u;

    struct FRedactionRule
    {
        std::string SensitivePrefix;
        std::string Placeholder;
    };

    struct FLoggerState
    {
        ~FLoggerState()
        {
            if (Stream.is_open())
            {
                Stream.flush();
                Stream.close();
            }
        }

        std::mutex Mutex;
        std::ofstream Stream;
        std::filesystem::path Directory;
        uintmax_t CurrentFileBytes = 0u;
        uintmax_t MaxFileBytes = FLogger::kDefaultMaxFileBytes;
        size_t MaxFileCount = FLogger::kDefaultMaxFileCount;
        bool bInitialized = false;
    };

    FLoggerState& GetState()
    {
        static FLoggerState State;

        return State;
    }

    void ReportInternalError(const char* Message)
    {
        const char* safeMessage = Message ? Message : "Unknown logger error";
        std::fprintf(stderr, "[LOGGER] %s\n", safeMessage);
        std::fflush(stderr);

        std::string debugMessage = std::string("[LOGGER] ") + safeMessage + "\n";
        OutputDebugStringA(debugMessage.c_str());
    }

    std::string GetEnvironmentValue(const char* Name)
    {
        char* value = nullptr;
        size_t length = 0u;

        if (_dupenv_s(&value, &length, Name) != 0 || !value)
        {
            return {};
        }

        const std::string result(value);
        std::free(value);

        return result;
    }

    char ToLowerAscii(char Value)
    {
        if (Value >= 'A' && Value <= 'Z')
        {
            return static_cast<char>(Value - 'A' + 'a');
        }

        return Value;
    }

    std::string ToLowerAscii(std::string Value)
    {
        std::transform(
            Value.begin(),
            Value.end(),
            Value.begin(),
            [](char value) { return ToLowerAscii(value); });

        return Value;
    }

    void ReplaceAllCaseInsensitive(
        std::string& Value,
        const std::string& Search,
        const std::string& Replacement)
    {
        if (Search.empty())
        {
            return;
        }

        std::string lowerValue = ToLowerAscii(Value);
        const std::string lowerSearch = ToLowerAscii(Search);
        const std::string lowerReplacement = ToLowerAscii(Replacement);
        size_t offset = 0u;

        while ((offset = lowerValue.find(lowerSearch, offset)) != std::string::npos)
        {
            Value.replace(offset, Search.size(), Replacement);
            lowerValue.replace(offset, Search.size(), lowerReplacement);
            offset += Replacement.size();
        }
    }

    const std::vector<FRedactionRule>& GetRedactionRules()
    {
        static const std::vector<FRedactionRule> Rules = []() {
            std::vector<FRedactionRule> rules;

            auto AddEnvironmentRule = [&rules](const char* Name, const char* Placeholder)
            {
                const std::string value = GetEnvironmentValue(Name);

                if (value.empty())
                {
                    return;
                }

                rules.push_back({ value, Placeholder });

                std::string alternate = value;
                std::replace(alternate.begin(), alternate.end(), '\\', '/');

                if (alternate != value)
                {
                    rules.push_back({ alternate, Placeholder });
                }
            };

            // 先匹配更具体的目录，避免 USERPROFILE 抢先截断 LOCALAPPDATA。
            AddEnvironmentRule("LOCALAPPDATA", "%LOCALAPPDATA%");
            AddEnvironmentRule("USERPROFILE", "%USERPROFILE%");

            return rules;
        }();

        return Rules;
    }

    std::string RedactSensitiveLocations(std::string Message)
    {
        // 路径对定位加载/导出问题很重要，但持久日志不能落下 Windows 用户名。
        // 只替换环境根目录，保留其后的相对路径和文件名，兼顾隐私与可诊断性。
        for (const FRedactionRule& rule : GetRedactionRules())
        {
            ReplaceAllCaseInsensitive(Message, rule.SensitivePrefix, rule.Placeholder);
        }

        return Message;
    }

    std::filesystem::path GetDefaultLogDirectory()
    {
        wchar_t* localAppData = nullptr;
        size_t environmentLength = 0u;
        const errno_t environmentError = _wdupenv_s(
            &localAppData,
            &environmentLength,
            L"LOCALAPPDATA");

        std::filesystem::path baseDirectory;

        if (environmentError == 0 && localAppData && *localAppData)
        {
            baseDirectory = localAppData;
        }

        std::free(localAppData);

        if (baseDirectory.empty())
        {
            std::error_code error;
            baseDirectory = std::filesystem::temp_directory_path(error);

            if (error)
            {
                error.clear();
                baseDirectory = std::filesystem::current_path(error);
            }

            if (error || baseDirectory.empty())
            {
                baseDirectory = L".";
            }
        }

        return baseDirectory / L"YUVRaw" / L"Logs";
    }

    std::filesystem::path GetLogPath(const FLoggerState& State, size_t Index)
    {
        if (Index == 0u)
        {
            return State.Directory / kCurrentLogFileName;
        }

        const std::string fileName = std::string(kRotatedLogFilePrefix)
            + std::to_string(Index)
            + kRotatedLogFileSuffix;

        return State.Directory / fileName;
    }

    bool OpenCurrentLog(FLoggerState& State, bool bTruncate)
    {
        State.Stream.clear();

        std::ios::openmode mode = std::ios::out | std::ios::binary;
        mode |= bTruncate ? std::ios::trunc : std::ios::app;
        State.Stream.open(GetLogPath(State, 0u), mode);

        if (!State.Stream.is_open())
        {
            State.CurrentFileBytes = 0u;
            ReportInternalError("Failed to open the log file");

            return false;
        }

        if (bTruncate)
        {
            State.CurrentFileBytes = 0u;
        }

        return true;
    }

    bool RotateLogs(FLoggerState& State)
    {
        if (State.Stream.is_open())
        {
            State.Stream.flush();
            State.Stream.close();
        }

        std::error_code error;

        if (State.MaxFileCount == 1u)
        {
            std::filesystem::remove(GetLogPath(State, 0u), error);
        }
        else
        {
            // 从最老的备份向后挪，最后把当前日志变成 .1。
            // 反向处理可避免源文件在成为下一轮目标前被覆盖。
            for (size_t destinationIndex = State.MaxFileCount - 1u;
                 destinationIndex > 0u;
                 --destinationIndex)
            {
                const size_t sourceIndex = destinationIndex - 1u;
                const std::filesystem::path source = GetLogPath(State, sourceIndex);
                const std::filesystem::path destination = GetLogPath(State, destinationIndex);

                error.clear();
                std::filesystem::remove(destination, error);

                error.clear();

                if (!std::filesystem::exists(source, error) || error)
                {
                    continue;
                }

                error.clear();
                std::filesystem::rename(source, destination, error);

                if (error)
                {
                    ReportInternalError("Failed to rotate a log file");
                }
            }
        }

        return OpenCurrentLog(State, true);
    }

    bool InitializeState(
        FLoggerState& State,
        const std::filesystem::path& Directory,
        uintmax_t MaxFileBytes,
        size_t MaxFileCount)
    {
        if (Directory.empty() || MaxFileBytes == 0u || MaxFileCount == 0u)
        {
            return false;
        }

        State.Directory = Directory;
        State.MaxFileBytes = MaxFileBytes;
        State.MaxFileCount = MaxFileCount;
        State.CurrentFileBytes = 0u;

        std::error_code error;
        std::filesystem::create_directories(State.Directory, error);

        if (error)
        {
            State.bInitialized = true;
            ReportInternalError("Failed to create the log directory");

            return false;
        }

        const std::filesystem::path currentPath = GetLogPath(State, 0u);
        const uintmax_t existingBytes = std::filesystem::file_size(currentPath, error);
        State.CurrentFileBytes = error ? 0u : existingBytes;

        bool bOpened = false;

        if (State.CurrentFileBytes >= State.MaxFileBytes)
        {
            bOpened = RotateLogs(State);
        }
        else
        {
            bOpened = OpenCurrentLog(State, false);
        }

        State.bInitialized = true;

        return bOpened;
    }

    void EnsureInitialized(FLoggerState& State)
    {
        if (!State.bInitialized)
        {
            InitializeState(
                State,
                GetDefaultLogDirectory(),
                FLogger::kDefaultMaxFileBytes,
                FLogger::kDefaultMaxFileCount);
        }
    }

    std::string FormatLogLine(
        const char* Level,
        const char* File,
        const char* Function,
        const char* Format,
        va_list Args)
    {
        std::array<char, kTimestampBufferSize> timestamp{};
        const std::time_t currentTime = std::time(nullptr);
        std::tm localTime{};

        if (localtime_s(&localTime, &currentTime) == 0)
        {
            std::strftime(
                timestamp.data(),
                timestamp.size(),
                "%Y-%m-%d %H:%M:%S",
                &localTime);
        }
        else
        {
            std::snprintf(timestamp.data(), timestamp.size(), "unknown-time");
        }

        std::array<char, kMessageBufferSize> message{};
        va_list copiedArgs;
        va_copy(copiedArgs, Args);
        std::vsnprintf(
            message.data(),
            message.size(),
            Format ? Format : "",
            copiedArgs);
        va_end(copiedArgs);
        message.back() = '\0';

        std::string line;
        line.reserve(kTimestampBufferSize + kMessageBufferSize);
        line += "[";
        line += timestamp.data();
        line += "] [";
        line += Level ? Level : "UNKNOWN";
        line += "] ";
        line += File ? File : "<unknown>";
        line += ":";
        line += Function ? Function : "<unknown>";
        line += ", ";
        line += RedactSensitiveLocations(message.data());
        line += "\r\n";

        return line;
    }

    void WriteFile(FLoggerState& State, const std::string& Line)
    {
        if (!State.Stream.is_open())
        {
            return;
        }

        std::string fileLine = Line;

        // printf 消息有固定上限，生产配置下不会进入这里；测试配置可能故意给出很小的文件。
        // 截断单条超限日志，保证“每个日志文件不超过上限”的约束始终成立。
        if (fileLine.size() > State.MaxFileBytes)
        {
            fileLine.resize(static_cast<size_t>(State.MaxFileBytes));
        }

        const uintmax_t remainingBytes = State.CurrentFileBytes < State.MaxFileBytes
            ? State.MaxFileBytes - State.CurrentFileBytes
            : 0u;

        if (fileLine.size() > remainingBytes)
        {
            if (!RotateLogs(State))
            {
                return;
            }
        }

        State.Stream.write(fileLine.data(), static_cast<std::streamsize>(fileLine.size()));
        State.Stream.flush();

        if (!State.Stream.good())
        {
            State.Stream.close();
            ReportInternalError("Failed to write the log file");

            return;
        }

        State.CurrentFileBytes += fileLine.size();
    }

    void WriteDebugOutputs(const std::string& Line)
    {
        std::fwrite(Line.data(), sizeof(char), Line.size(), stderr);
        std::fflush(stderr);
        OutputDebugStringA(Line.c_str());
    }
}

bool FLogger::Initialize()
{
    FLoggerState& state = GetState();
    std::lock_guard<std::mutex> lock(state.Mutex);

    if (state.bInitialized)
    {
        return state.Stream.is_open();
    }

    return InitializeState(
        state,
        GetDefaultLogDirectory(),
        kDefaultMaxFileBytes,
        kDefaultMaxFileCount);
}

bool FLogger::Initialize(
    const std::filesystem::path& Directory,
    uintmax_t MaxFileBytes,
    size_t MaxFileCount)
{
    FLoggerState& state = GetState();
    std::lock_guard<std::mutex> lock(state.Mutex);

    if (state.bInitialized)
    {
        return state.Stream.is_open();
    }

    return InitializeState(state, Directory, MaxFileBytes, MaxFileCount);
}

void FLogger::Shutdown()
{
    FLoggerState& state = GetState();
    std::lock_guard<std::mutex> lock(state.Mutex);

    if (state.Stream.is_open())
    {
        state.Stream.flush();
        state.Stream.close();
    }

    state.CurrentFileBytes = 0u;
    state.bInitialized = false;
}

void FLogger::Write(
    const char* Level,
    const char* File,
    const char* Function,
    const char* Format,
    ...)
{
    va_list args;
    va_start(args, Format);
    WriteV(Level, File, Function, Format, args);
    va_end(args);
}

void FLogger::WriteV(
    const char* Level,
    const char* File,
    const char* Function,
    const char* Format,
    va_list Args)
{
    const std::string line = FormatLogLine(Level, File, Function, Format, Args);
    FLoggerState& state = GetState();
    std::lock_guard<std::mutex> lock(state.Mutex);

    EnsureInitialized(state);
    WriteFile(state, line);
    WriteDebugOutputs(line);
}

std::filesystem::path FLogger::GetLogDirectory()
{
    FLoggerState& state = GetState();
    std::lock_guard<std::mutex> lock(state.Mutex);

    return state.Directory.empty() ? GetDefaultLogDirectory() : state.Directory;
}
