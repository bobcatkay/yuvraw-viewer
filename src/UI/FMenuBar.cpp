#include "Core/FLocalization.h"
#include "FMenuBar.h"
#include <imgui.h>

FMenuBar::FMenuBar()
{
}

FMenuBar::~FMenuBar()
{
}

void FMenuBar::Render()
{
    if (ImGui::BeginMenuBar())
    {
        RenderFileMenu();

        if (ImGui::MenuItem(FLocalization::Text(EUiText::Settings)))
        {
            if (OnSettings)
            {
                OnSettings();
            }
        }

        RenderHelpMenu();
        ImGui::EndMenuBar();
    }
}

void FMenuBar::RenderFileMenu()
{
    if (ImGui::BeginMenu(FLocalization::Text(EUiText::File)))
    {
        if (ImGui::MenuItem(FLocalization::Text(EUiText::OpenFileAction), "Ctrl+O"))
        {
            if (OnOpenFile)
            {
                OnOpenFile();
            }
        }
        if (ImGui::MenuItem(FLocalization::Text(EUiText::OpenDirectoryAction)))
        {
            if (OnOpenDirectory)
            {
                OnOpenDirectory();
            }
        }

        ImGui::Separator();

        if (ImGui::MenuItem(FLocalization::Text(EUiText::ExportAction), "Ctrl+E", false, bExportEnabled))
        {
            if (OnExport)
            {
                OnExport();
            }
        }

        ImGui::Separator();

        if (FileMenuExtras)
        {
            FileMenuExtras();
        }

        ImGui::Separator();
        if (ImGui::MenuItem(FLocalization::Text(EUiText::Exit), "Alt+F4"))
        {
            if (OnExit)
            {
                OnExit();
            }
        }
        ImGui::EndMenu();
    }
}

void FMenuBar::RenderHelpMenu()
{
    if (ImGui::BeginMenu(FLocalization::Text(EUiText::Help)))
    {
        if (ImGui::MenuItem(FLocalization::Text(EUiText::About)))
        {
            if (OnAbout)
            {
                OnAbout();
            }
        }
        ImGui::EndMenu();
    }
}

void FMenuBar::SetOnOpenFile(MenuCallback Callback)
{
    OnOpenFile = Callback;
}

void FMenuBar::SetOnOpenDirectory(MenuCallback Callback)
{
    OnOpenDirectory = Callback;
}

void FMenuBar::SetOnExport(MenuCallback Callback)
{
    OnExport = Callback;
}

void FMenuBar::SetExportEnabled(bool bEnabled)
{
    bExportEnabled = bEnabled;
}

void FMenuBar::SetOnExit(MenuCallback Callback)
{
    OnExit = Callback;
}

void FMenuBar::SetOnSettings(MenuCallback Callback)
{
    OnSettings = Callback;
}

void FMenuBar::SetOnAbout(MenuCallback Callback)
{
    OnAbout = Callback;
}

void FMenuBar::SetFileMenuExtras(MenuCallback Callback)
{
    FileMenuExtras = Callback;
}
