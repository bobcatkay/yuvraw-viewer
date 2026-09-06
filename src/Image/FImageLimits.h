#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

/**
 * 图像几何与单帧内存的统一安全边界。
 *
 * 工程同时提供 Win32 构建，不能让来自文件名或命令行的尺寸在 size_t 上回绕。
 * 128 Mi 像素足以覆盖常见的 108 MP 相机输出；512 MiB 的单帧上限则避免单次
 * 分配耗尽 32 位地址空间，同时仍能容纳该像素数的 RGBA8 图像。
 */
namespace FImageLimits
{
    inline constexpr int32_t kMinimumDimension = 1;
    inline constexpr int32_t kMaximumDimension = 65535;
    inline constexpr int32_t kMinimumStrideBytes = 0;

    inline constexpr size_t kUnitsPerKibi = 1024u;
    inline constexpr size_t kKibisPerMebi = 1024u;
    inline constexpr size_t kUnitsPerMebi = kUnitsPerKibi * kKibisPerMebi;
    inline constexpr size_t kMaximumPixelCount = 128u * kUnitsPerMebi;
    inline constexpr size_t kMaximumFrameBytes = 512u * kUnitsPerMebi;
    inline constexpr int32_t kMaximumStrideBytes =
        static_cast<int32_t>(kMaximumFrameBytes);

    inline constexpr int32_t kMinimumRawBitsPerSample = 1;
    inline constexpr int32_t kMaximumRawBitsPerSample = 16;

    inline bool TryMultiplySize(size_t Left, size_t Right, size_t& OutValue) noexcept
    {
        if (Left != 0 && Right > std::numeric_limits<size_t>::max() / Left)
        {
            OutValue = 0;
            return false;
        }

        OutValue = Left * Right;
        return true;
    }

    inline bool TryAddSize(size_t Left, size_t Right, size_t& OutValue) noexcept
    {
        if (Right > std::numeric_limits<size_t>::max() - Left)
        {
            OutValue = 0;
            return false;
        }

        OutValue = Left + Right;
        return true;
    }

    inline bool TryGetPixelCount(
        int32_t Width,
        int32_t Height,
        size_t& OutPixelCount) noexcept
    {
        if (Width < kMinimumDimension ||
            Width > kMaximumDimension ||
            Height < kMinimumDimension ||
            Height > kMaximumDimension)
        {
            OutPixelCount = 0;
            return false;
        }

        if (!TryMultiplySize(
                static_cast<size_t>(Width),
                static_cast<size_t>(Height),
                OutPixelCount) ||
            OutPixelCount > kMaximumPixelCount)
        {
            OutPixelCount = 0;
            return false;
        }

        return true;
    }

    inline bool AreDimensionsSupported(int32_t Width, int32_t Height) noexcept
    {
        size_t pixelCount = 0;
        return TryGetPixelCount(Width, Height, pixelCount);
    }

    inline bool IsFrameByteCountSupported(size_t ByteCount) noexcept
    {
        return ByteCount > 0 && ByteCount <= kMaximumFrameBytes;
    }
}
