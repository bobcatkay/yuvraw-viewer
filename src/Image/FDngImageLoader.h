#pragma once

#include "FImageLoader.h"

/**
 * DNG / camera RAW loader backed by LibRaw.
 *
 * DNG is self-describing: dimensions, CFA layout, black/white levels, white
 * balance and color matrices come from the file instead of the property panel.
 * LibRaw applies that metadata and this loader returns display-ready RGBA8.
 */
class FDngImageLoader : public FImageLoader
{
public:
    std::unique_ptr<FImageData> LoadFromFile(
        const std::string& FilePath,
        const FImageLoadParams* Params = nullptr,
        EImageLoadError* OutError = nullptr) override;

    bool SupportsFormat(const std::string& FilePath) const override;

    bool SupportsFormat(EImageFormat Format) const override;

    bool IsSelfDescribing() const override { return true; }

    std::vector<std::string> GetSupportedExtensions() const override;
};
