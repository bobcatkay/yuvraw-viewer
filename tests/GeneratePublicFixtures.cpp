#include "PublicFixtures.h"
#include <cstdio>

int wmain(int Count, wchar_t** Arguments)
{
    if (Count != 2)
    {
        std::fprintf(stderr, "Usage: GeneratePublicFixtures.exe <output-directory>\n");
        return 2;
    }
    const std::filesystem::path directory(Arguments[1]);
    std::filesystem::create_directories(directory);
    using namespace PublicFixtures;
    auto sequence = Nv21();
    const auto second = Nv21(true);
    sequence.insert(sequence.end(), second.begin(), second.end());
    const bool success =
        Write(directory / "colorbars_64x48_RGBA8.raw", ColorBars()) &&
        Write(directory / "gradient_64x48_stride80_NV21.yuv", Nv21()) &&
        Write(directory / "gradient_64x48_stride144_P010.yuv", P010()) &&
        Write(directory / "gradient_64x48_Bayer12.raw", Bayer()) &&
        Write(directory / "two_frames_64x48_stride80_NV21.yuv", sequence) &&
        Write(directory / "synthetic_64x48.dng", Tiff(kWidth, kHeight, true, Bayer()));
    std::printf("Procedural public fixtures: %s\n", success ? "OK" : "FAIL");
    return success ? 0 : 1;
}
