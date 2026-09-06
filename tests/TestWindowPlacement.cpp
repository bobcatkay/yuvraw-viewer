#include "Core/FWindowPlacement.h"

#include <cstdio>

namespace
{
    int GFailures = 0;

    void Check(bool bCondition, const char* Message)
    {
        std::printf("%-64s %s\n", Message, bCondition ? "OK" : "**FAIL**");

        if (!bCondition)
        {
            ++GFailures;
        }
    }

    bool IsWholeWindowInsideWorkArea(
        const FWindowGeometry::FPlacement& Placement,
        const FWindowGeometry::FFrameSize& Frame,
        const FWindowGeometry::FWorkArea& WorkArea)
    {
        const int64_t windowLeft =
            static_cast<int64_t>(Placement.X) - Frame.Left;
        const int64_t windowTop =
            static_cast<int64_t>(Placement.Y) - Frame.Top;
        const int64_t windowRight =
            static_cast<int64_t>(Placement.X) + Placement.Width + Frame.Right;
        const int64_t windowBottom =
            static_cast<int64_t>(Placement.Y) + Placement.Height + Frame.Bottom;
        const int64_t workRight =
            static_cast<int64_t>(WorkArea.X) + WorkArea.Width;
        const int64_t workBottom =
            static_cast<int64_t>(WorkArea.Y) + WorkArea.Height;
        return windowLeft >= WorkArea.X && windowTop >= WorkArea.Y &&
               windowRight <= workRight && windowBottom <= workBottom;
    }
}

int main()
{
    using namespace FWindowGeometry;

    const FWorkArea fullHdWorkArea{0, 0, 1920, 1032};
    const FFrameSize standardFrame{8, 31, 8, 8};
    const FPlacement fullHdContent{0, 0, 1920, 1080, false};
    const FPlacement fittedFullHd = ConstrainToWorkArea(
        fullHdContent,
        standardFrame,
        fullHdWorkArea);

    Check(
        fittedFullHd.X == 8 && fittedFullHd.Y == 31,
        "1080p fitting keeps the native title bar on screen");
    Check(
        fittedFullHd.Width == 1904 && fittedFullHd.Height == 993,
        "1080p fitting reserves all four native frame edges");
    Check(
        IsWholeWindowInsideWorkArea(
            fittedFullHd,
            standardFrame,
            fullHdWorkArea),
        "1080p fitting keeps the whole decorated window in the work area");

    const FWorkArea scaledWorkArea{0, 0, 1280, 680};
    const FFrameSize scaledFrame{12, 47, 12, 12};
    const FPlacement scaledContent{0, 0, 1280, 720, true};
    const FPlacement fittedScaled = ConstrainToWorkArea(
        scaledContent,
        scaledFrame,
        scaledWorkArea);

    Check(
        fittedScaled.X == 12 && fittedScaled.Y == 47,
        "high-DPI fitting uses the scaled frame extents");
    Check(
        fittedScaled.Width == 1256 && fittedScaled.Height == 621,
        "high-DPI fitting limits the content area after frame subtraction");
    Check(
        fittedScaled.bMaximized,
        "fitting preserves the saved maximized preference");
    Check(
        IsWholeWindowInsideWorkArea(
            fittedScaled,
            scaledFrame,
            scaledWorkArea),
        "high-DPI fitting keeps the decorated window visible");

    const FWorkArea secondaryWorkArea{-1920, 0, 1920, 1040};
    const FPlacement offscreenContent{-2100, -100, 1500, 900, false};
    const FPlacement fittedSecondary = ConstrainToWorkArea(
        offscreenContent,
        standardFrame,
        secondaryWorkArea);

    Check(
        fittedSecondary.X == -1912 && fittedSecondary.Y == 31,
        "negative-coordinate monitor clamps the content origin correctly");
    Check(
        IsWholeWindowInsideWorkArea(
            fittedSecondary,
            standardFrame,
            secondaryWorkArea),
        "secondary-monitor fitting keeps every frame edge visible");

    const FPlacement alreadyVisible{200, 160, 1200, 700, false};
    Check(
        ConstrainToWorkArea(
            alreadyVisible,
            standardFrame,
            fullHdWorkArea).X == alreadyVisible.X &&
        ConstrainToWorkArea(
            alreadyVisible,
            standardFrame,
            fullHdWorkArea).Y == alreadyVisible.Y,
        "an already visible window keeps its position");

    std::printf("\n%s\n", GFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return GFailures == 0 ? 0 : 1;
}
