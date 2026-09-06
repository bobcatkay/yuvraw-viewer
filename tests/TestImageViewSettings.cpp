#include "UI/FImageViewer.h"

#include <array>
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

    void CheckModeSynchronization(EDisplayMode Mode, int32_t Marker)
    {
        FImageViewSettings source;
        source.DisplayMode = Mode;
        source.ManualScale = 0.25f * static_cast<float>(Marker);
        source.PanOffsetX = 13.0f * static_cast<float>(Marker);
        source.PanOffsetY = -7.0f * static_cast<float>(Marker);
        source.RotationQuarters = 2;
        source.bFlipH = false;
        source.bFlipV = true;

        FImageViewSettings target;
        target.DisplayMode = EDisplayMode::AutoFit;
        target.ManualScale = 4.0f;
        target.PanOffsetX = -100.0f;
        target.PanOffsetY = 200.0f;
        target.RotationQuarters = 3;
        target.bFlipH = true;
        target.bFlipV = false;

        CopyPanZoomViewSettings(source, target);

        Check(
            target.DisplayMode == source.DisplayMode &&
            target.ManualScale == source.ManualScale &&
            target.PanOffsetX == source.PanOffsetX &&
            target.PanOffsetY == source.PanOffsetY,
            "display mode, zoom and pan are copied together");
        Check(
            target.RotationQuarters == 3 && target.bFlipH && !target.bFlipV,
            "target rotation and mirroring remain independent");
        Check(
            source.RotationQuarters == 2 && !source.bFlipH && source.bFlipV,
            "source view settings remain unchanged");
    }
}

int main()
{
    constexpr std::array<EDisplayMode, 4> modes = {
        EDisplayMode::AutoFit,
        EDisplayMode::Fill,
        EDisplayMode::OneToOne,
        EDisplayMode::Manual,
    };

    int32_t marker = 1;

    for (const EDisplayMode mode : modes)
    {
        CheckModeSynchronization(mode, marker++);
    }

    std::printf("\n%s\n", GFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return GFailures == 0 ? 0 : 1;
}
