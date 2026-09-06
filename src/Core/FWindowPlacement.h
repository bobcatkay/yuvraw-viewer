#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace FWindowGeometry
{
    /**
     * GLFW 保存和恢复的窗口位置、尺寸都描述客户区，不包含原生标题栏和边框。
     */
    struct FPlacement
    {
        int32_t X = 0;
        int32_t Y = 0;
        int32_t Width = 0;
        int32_t Height = 0;
        bool bMaximized = false;
    };

    /** 原生窗口客户区四周的边框尺寸；Top 包含标题栏。 */
    struct FFrameSize
    {
        int32_t Left = 0;
        int32_t Top = 0;
        int32_t Right = 0;
        int32_t Bottom = 0;
    };

    /** 显示器扣除任务栏后的工作区。 */
    struct FWorkArea
    {
        int32_t X = 0;
        int32_t Y = 0;
        int32_t Width = 0;
        int32_t Height = 0;
    };

    constexpr int32_t kMinimumWindowContentWidth = 320;
    constexpr int32_t kMinimumWindowContentHeight = 240;

    inline int32_t ClampToInt32(int64_t Value)
    {
        return static_cast<int32_t>(std::clamp<int64_t>(
            Value,
            std::numeric_limits<int32_t>::min(),
            std::numeric_limits<int32_t>::max()));
    }

    inline FFrameSize SanitizeFrameSize(const FFrameSize& Frame)
    {
        return {
            std::max(Frame.Left, 0),
            std::max(Frame.Top, 0),
            std::max(Frame.Right, 0),
            std::max(Frame.Bottom, 0),
        };
    }

    /**
     * 计算带原生边框的窗口与工作区交集，用于在多显示器间选择恢复目标。
     */
    inline int64_t CalculateIntersectionArea(
        const FPlacement& Placement,
        const FFrameSize& Frame,
        const FWorkArea& WorkArea)
    {
        if (Placement.Width <= 0 || Placement.Height <= 0 ||
            WorkArea.Width <= 0 || WorkArea.Height <= 0)
        {
            return 0;
        }

        const FFrameSize frame = SanitizeFrameSize(Frame);
        const int64_t windowLeft =
            static_cast<int64_t>(Placement.X) - frame.Left;
        const int64_t windowTop =
            static_cast<int64_t>(Placement.Y) - frame.Top;
        const int64_t windowRight =
            static_cast<int64_t>(Placement.X) + Placement.Width + frame.Right;
        const int64_t windowBottom =
            static_cast<int64_t>(Placement.Y) + Placement.Height + frame.Bottom;
        const int64_t workRight =
            static_cast<int64_t>(WorkArea.X) + WorkArea.Width;
        const int64_t workBottom =
            static_cast<int64_t>(WorkArea.Y) + WorkArea.Height;

        const int64_t intersectionWidth = std::max<int64_t>(
            0,
            std::min(windowRight, workRight)
                - std::max<int64_t>(windowLeft, WorkArea.X));
        const int64_t intersectionHeight = std::max<int64_t>(
            0,
            std::min(windowBottom, workBottom)
                - std::max<int64_t>(windowTop, WorkArea.Y));
        return intersectionWidth * intersectionHeight;
    }

    /**
     * 将客户区约束到工作区内，同时为原生边框和标题栏预留空间。
     */
    inline FPlacement ConstrainToWorkArea(
        const FPlacement& Placement,
        const FFrameSize& Frame,
        const FWorkArea& WorkArea)
    {
        FPlacement result = Placement;

        if (WorkArea.Width <= 0 || WorkArea.Height <= 0)
        {
            result.Width = std::max(
                result.Width,
                kMinimumWindowContentWidth);
            result.Height = std::max(
                result.Height,
                kMinimumWindowContentHeight);
            return result;
        }

        const FFrameSize frame = SanitizeFrameSize(Frame);
        const int64_t maximumWidth = std::max<int64_t>(
            1,
            static_cast<int64_t>(WorkArea.Width)
                - frame.Left - frame.Right);
        const int64_t maximumHeight = std::max<int64_t>(
            1,
            static_cast<int64_t>(WorkArea.Height)
                - frame.Top - frame.Bottom);
        const int64_t minimumWidth = std::min<int64_t>(
            kMinimumWindowContentWidth,
            maximumWidth);
        const int64_t minimumHeight = std::min<int64_t>(
            kMinimumWindowContentHeight,
            maximumHeight);

        result.Width = ClampToInt32(std::clamp<int64_t>(
            Placement.Width,
            minimumWidth,
            maximumWidth));
        result.Height = ClampToInt32(std::clamp<int64_t>(
            Placement.Height,
            minimumHeight,
            maximumHeight));

        const int64_t minimumX =
            static_cast<int64_t>(WorkArea.X) + frame.Left;
        const int64_t minimumY =
            static_cast<int64_t>(WorkArea.Y) + frame.Top;
        const int64_t maximumX =
            static_cast<int64_t>(WorkArea.X) + WorkArea.Width
                - frame.Right - result.Width;
        const int64_t maximumY =
            static_cast<int64_t>(WorkArea.Y) + WorkArea.Height
                - frame.Bottom - result.Height;

        result.X = ClampToInt32(std::clamp<int64_t>(
            Placement.X,
            minimumX,
            std::max(minimumX, maximumX)));
        result.Y = ClampToInt32(std::clamp<int64_t>(
            Placement.Y,
            minimumY,
            std::max(minimumY, maximumY)));
        return result;
    }
}
