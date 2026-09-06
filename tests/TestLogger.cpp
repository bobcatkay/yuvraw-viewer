#include "Core/FLogger.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    constexpr uintmax_t kRotationFileBytes = 512u;
    constexpr size_t kRotationFileCount = 5u;
    constexpr int32_t kRotationMessageCount = 30;
    constexpr uintmax_t kConcurrentFileBytes = FLogger::kBytesPerMiB;
    constexpr size_t kConcurrentFileCount = 2u;
    constexpr int32_t kConcurrentThreadCount = 2;
    constexpr int32_t kMessagesPerThread = 8;

    std::filesystem::path GetLogPath(const std::filesystem::path& Directory, size_t Index)
    {
        if (Index == 0u)
        {
            return Directory / "YUVRaw.log";
        }

        return Directory / ("YUVRaw." + std::to_string(Index) + ".log");
    }

    void Require(bool bCondition, const char* Message)
    {
        if (!bCondition)
        {
            throw std::runtime_error(Message);
        }
    }

    size_t CountLines(const std::filesystem::path& Path)
    {
        std::ifstream file(Path, std::ios::binary);
        const std::string contents(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

        return static_cast<size_t>(std::count(contents.begin(), contents.end(), '\n'));
    }

    std::string ReadContents(const std::filesystem::path& Path)
    {
        std::ifstream file(Path, std::ios::binary);

        return std::string(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
    }

    void TestRotation(const std::filesystem::path& Directory)
    {
        Require(
            FLogger::Initialize(Directory, kRotationFileBytes, kRotationFileCount),
            "logger initialization failed");

        for (int32_t index = 0; index < kRotationMessageCount; ++index)
        {
            FLogger::Write(
                "INFO",
                "TestLogger.cpp",
                "TestRotation",
                "entry=%d payload=abcdefghijklmnopqrstuvwxyz0123456789",
                index);
        }

        FLogger::Shutdown();

        for (size_t index = 0u; index < kRotationFileCount; ++index)
        {
            const std::filesystem::path path = GetLogPath(Directory, index);
            Require(std::filesystem::is_regular_file(path), "expected retained log file is missing");
            Require(std::filesystem::file_size(path) <= kRotationFileBytes, "log file exceeded size cap");
        }

        Require(
            !std::filesystem::exists(GetLogPath(Directory, kRotationFileCount)),
            "logger retained more files than configured");
    }

    void TestConcurrentWrites(const std::filesystem::path& Directory)
    {
        Require(
            FLogger::Initialize(Directory, kConcurrentFileBytes, kConcurrentFileCount),
            "concurrent logger initialization failed");

        std::vector<std::thread> workers;
        workers.reserve(kConcurrentThreadCount);

        for (int32_t threadIndex = 0; threadIndex < kConcurrentThreadCount; ++threadIndex)
        {
            workers.emplace_back([threadIndex]() {
                for (int32_t messageIndex = 0; messageIndex < kMessagesPerThread; ++messageIndex)
                {
                    FLogger::Write(
                        "DEBUG",
                        "TestLogger.cpp",
                        "TestConcurrentWrites",
                        "thread=%d message=%d",
                        threadIndex,
                        messageIndex);
                }
            });
        }

        for (std::thread& worker : workers)
        {
            worker.join();
        }

        FLogger::Shutdown();

        const size_t expectedLines = static_cast<size_t>(kConcurrentThreadCount * kMessagesPerThread);
        Require(
            CountLines(GetLogPath(Directory, 0u)) == expectedLines,
            "concurrent writes were lost or interleaved");
    }

    void TestUserProfileRedaction(const std::filesystem::path& Directory)
    {
        const char* userProfile = std::getenv("USERPROFILE");

        if (!userProfile || !*userProfile)
        {
            return;
        }

        Require(
            FLogger::Initialize(Directory, kConcurrentFileBytes, kConcurrentFileCount),
            "privacy logger initialization failed");

        const std::string privatePath =
            std::string(userProfile) + "\\Pictures\\private-image.raw";

        FLogger::Write(
            "INFO",
            "TestLogger.cpp",
            "TestUserProfileRedaction",
            "loading %s",
            privatePath.c_str());
        FLogger::Shutdown();

        const std::string contents = ReadContents(GetLogPath(Directory, 0u));

        Require(
            contents.find(userProfile) == std::string::npos,
            "persistent log leaked the Windows user profile path");
        Require(
            contents.find("%USERPROFILE%") != std::string::npos,
            "persistent log did not preserve the redacted path placeholder");
    }
}

int main()
{
    static_assert(FLogger::kDefaultMaxFileCount == 5u, "production retention must remain five files");
    static_assert(
        FLogger::kDefaultMaxFileBytes == 10u * FLogger::kBytesPerMiB,
        "production file cap must remain 10 MiB");

    const int64_t uniqueSuffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path testRoot = std::filesystem::temp_directory_path()
        / ("YUVRawLoggerTests_" + std::to_string(uniqueSuffix));

    try
    {
        TestRotation(testRoot / "rotation");
        TestConcurrentWrites(testRoot / "concurrent");
        TestUserProfileRedaction(testRoot / "privacy");

        std::error_code cleanupError;
        std::filesystem::remove_all(testRoot, cleanupError);

        std::cout << "Logger tests passed" << std::endl;

        return 0;
    }
    catch (const std::exception& error)
    {
        FLogger::Shutdown();

        std::error_code cleanupError;
        std::filesystem::remove_all(testRoot, cleanupError);
        std::cerr << "Logger tests failed: " << error.what() << std::endl;

        return 1;
    }
}
