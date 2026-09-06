#pragma once

#include "Core/FLocalization.h"
#include <imgui_internal.h>

namespace FUiLayout
{
    /// Call after loading imgui.ini, before any application window is created.
    inline size_t MigrateLegacyWindowSettings()
    {
        constexpr EUiText kDockWindows[] = {
            EUiText::FileExplorer, EUiText::ImageViewer, EUiText::Properties,
            EUiText::Compare, EUiText::Histogram
        };
        size_t migrated = 0;
        for (EUiText key : kDockWindows)
        {
            const auto& resource = FLocalization::kTextResources[static_cast<size_t>(key)];
            const ImGuiID oldId = ImHashStr(resource.Chinese);
            const ImGuiID newId = ImHashStr(resource.ChineseWindowTitle);
            auto* legacy = ImGui::FindWindowSettingsByID(oldId);
            if (!legacy || legacy->WantDelete)
            {
                continue;
            }
            if (!ImGui::FindWindowSettingsByID(newId))
            {
                // CreateNewWindowSettings may reallocate the chunk stream: copy first, then reacquire pointers.
                const ImGuiWindowSettings snapshot = *legacy;
                auto* replacement = ImGui::CreateNewWindowSettings(resource.ChineseWindowTitle);
                *replacement = snapshot;
                replacement->ID = newId;
                const ImGuiID oldTabId = ImHashStr("#TAB", 0, oldId);
                const ImGuiID newTabId = ImHashStr("#TAB", 0, newId);
                for (auto& entry : ImGui::GetCurrentContext()->DockContext.Nodes.Data)
                {
                    auto* node = static_cast<ImGuiDockNode*>(entry.val_p);
                    if (node && node->SelectedTabId == oldTabId)
                    {
                        node->SelectedTabId = newTabId;
                    }
                }
                ++migrated;
            }
            // Prefer any already migrated state and omit the old duplicate when ImGui next saves.
            ImGui::FindWindowSettingsByID(oldId)->WantDelete = true;
        }
        return migrated;
    }
}
