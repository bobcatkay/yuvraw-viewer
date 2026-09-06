#include "Core/FUiResources.h"
#include "UI/FUiFont.h"
#include "UI/FUiLayout.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <regex>
#include <vector>

namespace
{
    constexpr float kTestFontPixelSize = 18.0f;
    constexpr ImWchar kFirstCommonHanCharacter = 0x4E00;
    constexpr ImWchar kLastCommonHanCharacter = 0x9FA5;
    constexpr int kExpectedArgumentCount = 3;

    void Require(bool bCondition, const char* Message)
    {
        if (!bCondition)
        {
            throw std::runtime_error(Message);
        }
    }

    std::vector<std::string> FormatTokens(const char* Text)
    {
        static const std::regex pattern(R"(%[-+ #0]*[0-9]*(\.[0-9]+)?(hh|ll|[hljztL])?[diuoxXfFeEgGaAcspn%])");
        const std::string text(Text);
        std::vector<std::string> tokens;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern);
             it != std::sregex_iterator(); ++it)
        {
            tokens.push_back(it->str());
        }
        return tokens;
    }

    void VerifyLocalization(const std::filesystem::path& FontPath)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();
        ImVector<ImWchar> ranges;
        FUiFont::BuildGlyphRanges(*io.Fonts, ranges);
        ImFont* font = io.Fonts->AddFontFromFileTTF(FontPath.u8string().c_str(),
            kTestFontPixelSize, nullptr, ranges.Data);
        Require(font && io.Fonts->Build(), "production bilingual font atlas must build");
        for (const auto& resource : FLocalization::kTextResources)
        {
            Require(FormatTokens(resource.Chinese) == FormatTokens(resource.English),
                "translations must preserve printf argument types and order");
            Require(ImHashStr(resource.ChineseWindowTitle) == ImHashStr(resource.EnglishWindowTitle),
                "both languages must share window and popup IDs");
            for (const char* text : { resource.Chinese, resource.English })
            {
                while (*text)
                {
                    unsigned int codepoint = 0;
                    const int bytes = ImTextCharFromUtf8(&codepoint, text, nullptr);
                    Require(bytes > 0, "UI resources must be valid UTF-8");
                    text += bytes;
                    if (codepoint == '\r' || codepoint == '\n' || codepoint == '\t')
                    {
                        continue;
                    }
                    if (!font->FindGlyphNoFallback(static_cast<ImWchar>(codepoint)))
                    {
                        std::fprintf(stderr, "Missing UI glyph U+%04X in %s\n", codepoint, FontPath.u8string().c_str());
                        Require(false, "production font atlas must cover every bilingual UI glyph");
                    }
                }
            }
        }

        FLocalization::SetLanguage(FLocalization::ELanguage::English);
        Require(std::string(FLocalization::Text(EUiText::Settings)) == "Settings",
            "language switch updates visible menu text");
        Require(std::string(FLocalization::Translate(u8"HDR 通路就绪")) == "HDR output ready",
            "a stored HDR status changes language when rendered");
        FLocalization::SetLanguage(FLocalization::ELanguage::SimplifiedChinese);
        Require(std::string(FLocalization::Text(EUiText::Settings)) == u8"设置",
            "switching back restores Chinese text");
        constexpr auto kInvalidLanguage = static_cast<FLocalization::ELanguage>(-1);
        FLocalization::SetLanguage(kInvalidLanguage);
        Require(FLocalization::GetLanguage() == FLocalization::ELanguage::English,
            "unsupported runtime language falls back to English");
    }

    void VerifyLayoutMigration()
    {
        constexpr short kWindowX = 23;
        constexpr short kWindowY = 41;
        constexpr short kWindowWidth = 350;
        constexpr short kWindowHeight = 600;
        constexpr ImGuiID kDockNodeId = 0x1234;
        const ImGuiID legacyId = ImHashStr(u8"属性面板");
        auto* legacy = ImGui::CreateNewWindowSettings(u8"属性面板");
        legacy->Pos = ImVec2ih(kWindowX, kWindowY);
        legacy->Size = ImVec2ih(kWindowWidth, kWindowHeight);
        legacy->DockId = kDockNodeId;
        legacy->DockOrder = 1;
        legacy->Collapsed = true;
        ImGui::DockBuilderAddNode(kDockNodeId);
        ImGui::DockBuilderGetNode(kDockNodeId)->SelectedTabId = ImHashStr("#TAB", 0, legacyId);
        Require(FUiLayout::MigrateLegacyWindowSettings() == 1, "legacy layout is migrated once");
        const ImGuiID newId = ImHashStr(FLocalization::WindowTitle(EUiText::Properties));
        const auto* migrated = ImGui::FindWindowSettingsByID(newId);
        Require(migrated && migrated->Pos.x == kWindowX && migrated->Pos.y == kWindowY &&
            migrated->Size.x == kWindowWidth && migrated->Size.y == kWindowHeight &&
            migrated->DockId == kDockNodeId && migrated->DockOrder == 1 && migrated->Collapsed,
            "migration preserves position, size, dock assignment, order and collapsed state");
        Require(ImGui::DockBuilderGetNode(kDockNodeId)->SelectedTabId == ImHashStr("#TAB", 0, newId),
            "migration preserves the selected dock tab");
        Require(FUiLayout::MigrateLegacyWindowSettings() == 0,
            "migration is idempotent and does not overwrite updated layouts");
    }

    struct FScopedLocalAppData
    {
        wchar_t* Previous = nullptr;

        explicit FScopedLocalAppData(const std::filesystem::path& Directory)
        {
            size_t length = 0;
            _wdupenv_s(&Previous, &length, L"LOCALAPPDATA");
            Require(_wputenv_s(L"LOCALAPPDATA", Directory.c_str()) == 0,
                "cannot isolate test user directory");
        }

        ~FScopedLocalAppData()
        {
            _wputenv_s(L"LOCALAPPDATA", Previous ? Previous : L"");
            free(Previous);
        }
    };
}

int wmain(int Argc, wchar_t** Argv)
{
    try
    {
        Require(Argc == kExpectedArgumentCount, "expected repository and isolated output directories");
        const std::filesystem::path repository(Argv[1]);
        const std::filesystem::path output(Argv[2]);
        Require(output.is_absolute(), "test output must be absolute");

        // 真实复制到中文目录，覆盖 ImGui 的 UTF-8 fopen 包装与 OTF/CFF 解码。
        const std::filesystem::path installation = output / L"含中文安装目录";
        const std::filesystem::path fontPath = FUiResources::GetBundledFontPath(installation);
        std::filesystem::create_directories(fontPath.parent_path());
        std::filesystem::copy_file(repository / L"resources/fonts/NotoSansCJKsc-Regular.otf",
            fontPath, std::filesystem::copy_options::overwrite_existing);

        const std::filesystem::path otherDirectory = output / L"无关启动目录";
        std::filesystem::create_directories(otherDirectory);
        std::filesystem::current_path(otherDirectory);
        Require(FUiResources::GetBundledFontPath(installation) == fontPath,
            "font location must be independent of working directory");
        Require(FUiResources::GetBundledFontPath(L"Z:\\应用\\YUVRaw") ==
            std::filesystem::path(L"Z:\\应用\\YUVRaw\\resources\\fonts\\NotoSansCJKsc-Regular.otf"),
            "font location must not assume the C drive");
        Require(FUiResources::GetExecutableDirectory().is_absolute(), "executable lookup must be absolute");

        FScopedLocalAppData userDirectory(output / L"中文本机用户目录");
        const std::filesystem::path layout = FUiResources::GetLayoutSettingsPath();
        Require(layout == output / L"中文本机用户目录/YUVRaw/imgui.ini",
            "layout must use the isolated local user directory");
        Require(!std::filesystem::exists(otherDirectory / L"imgui.ini"),
            "working directory must not gain a layout file");
        {
            FScopedLocalAppData invalidUserDirectory(L"relative-user-directory");
            Require(FUiResources::GetLayoutSettingsPath() ==
                FUiResources::GetKnownDirectory(FOLDERID_LocalAppData) / L"YUVRaw/imgui.ini",
                "relative LOCALAPPDATA must use Windows known folders instead of working directory");
        }

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        // 检查字体完整中文 cmap，而不依赖本机安装的中文字体。
        ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.u8string().c_str(),
            kTestFontPixelSize, nullptr, io.Fonts->GetGlyphRangesChineseFull());
        Require(font != nullptr && io.Fonts->Build(), "bundled font atlas must build without system fonts");
        for (unsigned int character = kFirstCommonHanCharacter;
            character <= kLastCommonHanCharacter; ++character)
        {
            Require(font->FindGlyphNoFallback(static_cast<ImWchar>(character)) != nullptr,
                "bundled font must contain the full common Chinese ideograph block");
        }

        VerifyLocalization(fontPath);
        VerifyLayoutMigration();
        const auto systemFontPath = FUiResources::GetSystemChineseFontPath();
        if (std::filesystem::exists(systemFontPath))
        {
            VerifyLocalization(systemFontPath);
        }

        std::filesystem::create_directories(layout.parent_path());
        const std::string layoutUtf8 = layout.u8string();
        io.IniFilename = layoutUtf8.c_str();
        constexpr const char* kFixtureLayout = "[Window][Chinese path layout test]\nPos=10,20\nSize=400,300\nCollapsed=0\n";
        ImGui::LoadIniSettingsFromMemory(kFixtureLayout);
        ImGui::SaveIniSettingsToDisk(layout.u8string().c_str());
        std::ifstream storedLayout(layout, std::ios::binary);
        const std::string stored((std::istreambuf_iterator<char>(storedLayout)), {});
        storedLayout.close();
        Require(stored.find("Chinese path layout test") != std::string::npos,
            "ImGui layout must round-trip through a Chinese user path");
        Require(!std::filesystem::exists(otherDirectory / L"imgui.ini"),
            "layout persistence must not write into working directory");
        Require(std::filesystem::remove(layout), "layout deletion must succeed in the local directory");
        // 与“清除全部数据”的退出约束一致：清理后禁用本会话自动保存。
        io.IniFilename = nullptr;
        io.WantSaveIniSettings = false;
        ImGui::DestroyContext();
        Require(!std::filesystem::exists(layout), "context shutdown must not restore cleared layout");
        std::puts("UI resource tests passed: bilingual glyphs, translations, stable window IDs, Unicode paths and user layout");
        return 0;
    }
    catch (const std::exception& error)
    {
        if (ImGui::GetCurrentContext())
        {
            ImGui::GetIO().IniFilename = nullptr;
            ImGui::DestroyContext();
        }
        std::fprintf(stderr, "UI resource test failed: %s\n", error.what());
        return 1;
    }
}
