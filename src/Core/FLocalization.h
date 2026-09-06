#pragma once

#include <atomic>
#include <cstddef>
#include <string_view>
#include <unordered_map>

enum class EUiText
{
#define YUVRAW_UI_TEXT(Key, Chinese, English) Key,
#include "FUiText.inl"
#undef YUVRAW_UI_TEXT
    Count
};

namespace FLocalization
{
    enum class ELanguage
    {
        SimplifiedChinese,
        English
    };

    inline constexpr ELanguage kDefaultLanguage = ELanguage::English;
    inline constexpr const char* kChineseLanguageCode = "zh-CN";
    inline constexpr const char* kEnglishLanguageCode = "en-US";

    struct FTextResource
    {
        const char* Chinese;
        const char* English;
        const char* ChineseWindowTitle;
        const char* EnglishWindowTitle;
    };

    // ### gives both translations a shared ID. FUiLayout migrates pre-localization window settings.
    inline constexpr FTextResource kTextResources[] = {
#define YUVRAW_UI_TEXT(Key, Chinese, English) \
        { Chinese, English, Chinese "###" Chinese, English "###" Chinese },
#include "FUiText.inl"
#undef YUVRAW_UI_TEXT
    };
    static_assert(sizeof(kTextResources) / sizeof(kTextResources[0]) ==
        static_cast<size_t>(EUiText::Count));

    // Background export/comparison jobs may also resolve user-facing messages.
    inline std::atomic<ELanguage> CurrentLanguage{kDefaultLanguage};

    inline ELanguage GetLanguage()
    {
        return CurrentLanguage.load(std::memory_order_relaxed);
    }

    inline void SetLanguage(ELanguage Language)
    {
        const bool bKnownLanguage = Language == ELanguage::English || Language == ELanguage::SimplifiedChinese;
        CurrentLanguage.store(bKnownLanguage ? Language : kDefaultLanguage, std::memory_order_relaxed);
    }

    inline const char* LanguageCode(ELanguage Language)
    {
        return Language == ELanguage::English ? kEnglishLanguageCode : kChineseLanguageCode;
    }

    inline const char* Text(EUiText Key)
    {
        const FTextResource& resource = kTextResources[static_cast<size_t>(Key)];
        return GetLanguage() == ELanguage::English ? resource.English : resource.Chinese;
    }

    inline const char* WindowTitle(EUiText Key)
    {
        const FTextResource& resource = kTextResources[static_cast<size_t>(Key)];
        return GetLanguage() == ELanguage::English
            ? resource.EnglishWindowTitle : resource.ChineseWindowTitle;
    }

    // Resolve stored diagnostic text when drawn, so a status created before switching also updates.
    // Unknown text (including file names and system errors) is displayed unchanged.
    inline const char* Translate(const char* Source, ELanguage Language = GetLanguage())
    {
        if (!Source || Language != ELanguage::English)
        {
            return Source;
        }
        static const auto translations = [] {
            std::unordered_map<std::string_view, const char*> result;
            for (const FTextResource& resource : kTextResources)
            {
                result.emplace(resource.Chinese, resource.English);
            }
            return result;
        }();
        const auto found = translations.find(Source);
        return found != translations.end() ? found->second : Source;
    }
}
