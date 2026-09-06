#pragma once

#include "Core/FLocalization.h"
#include <imgui.h>

namespace FUiFont
{
    inline void BuildGlyphRanges(ImFontAtlas& Atlas, ImVector<ImWchar>& OutRanges)
    {
        ImFontGlyphRangesBuilder builder;
        // Keep common Chinese file names readable in either language, and include every UI glyph.
        builder.AddRanges(Atlas.GetGlyphRangesChineseSimplifiedCommon());
        builder.AddRanges(Atlas.GetGlyphRangesDefault());
        for (const auto& resource : FLocalization::kTextResources)
        {
            builder.AddText(resource.Chinese);
            builder.AddText(resource.English);
        }
        builder.BuildRanges(&OutRanges);
    }
}
