#include "EditorUIInternal.h"
#include "../Core Render/DXRenderer.h"
#include "../EngineApp.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
using namespace EditorUIInternal;

void EditorUI::DrawLauncher(DXRenderer* renderer, float w, float h) {
    auto& drawList = renderer->m_uiDrawList;
    auto& uiCtx = renderer->m_uiContext;
    auto& fontMgr = renderer->m_fontManager;
    HWND hwnd = renderer->m_hwnd;

    float l_leftW = 260.0f;
    float l_rightW = 320.0f;
    float l_botH = 80.0f;
    uint32_t unrealBlue = 0xFFD77800; 

    drawList.AddRectFilled(0, 0, l_leftW, h - l_botH, 0xFF111111);
    drawList.AddRectFilled(l_leftW, 0, w - l_leftW - l_rightW, h - l_botH, 0xFF1E1E1E);
    drawList.AddRectFilled(w - l_rightW, 0, l_rightW, h - l_botH, 0xFF151515);
    drawList.AddRectFilled(0, h - l_botH, w, l_botH, 0xFF222222);

    if (!State.recentsLoaded) {
        State.recentProjects = renderer->GetRecentProjectsInfo();
        State.recentsLoaded = true;
    }

    auto BeginProjectOpen = [&](const std::wstring& projectFilePath, const std::wstring& projectName) {
        AddRecentProject(projectFilePath);
        State.currentProjectFile = projectFilePath;
        State.currentProjectFolder = std::filesystem::path(projectFilePath).parent_path().wstring();
        State.currentMapPath.clear();
        State.currentBrowserPath = State.currentProjectFolder + L"\\Assets";
        State.triggerEditorSwap = true;
        State.editorSwapProjName = projectName;
        State.recentsLoaded = false;
        State.showLauncherProjectMenu = false;
        State.showDeleteProjectConfirm = false;
    };

    const float launcherMenuW = 180.0f;
    const float launcherMenuH = 70.0f;
    const float launcherMenuX = (std::min)(State.launcherProjectMenuX, w - launcherMenuW - 10.0f);
    const float launcherMenuY = (std::min)(State.launcherProjectMenuY, h - launcherMenuH - 10.0f);
    const bool launcherOverlayActive = State.showLauncherProjectMenu || State.showDeleteProjectConfirm;

    drawList.AddText(fontMgr, "PROJECT CATEGORIES", 20.0f, 20.0f, 0xFF888888);
    
    if (State.activeCategory == 0) {
        drawList.AddRectFilled(0, 60, l_leftW, 50, 0xFF2A2A2A);
        drawList.AddRectFilled(0, 60, 4, 50, unrealBlue);
    }
    if (!launcherOverlayActive && uiCtx.Button("New Project", 0, 60, l_leftW, 50, 0x00000000, 0x22FFFFFF, 0x44FFFFFF)) {
        State.activeCategory = 0;
        State.isInputActive = true;
        State.showLauncherProjectMenu = false;
    }

    if (State.activeCategory == 1) {
        drawList.AddRectFilled(0, 110, l_leftW, 50, 0xFF2A2A2A);
        drawList.AddRectFilled(0, 110, 4, 50, unrealBlue);
    }
    if (!launcherOverlayActive && uiCtx.Button("Load Project", 0, 110, l_leftW, 50, 0x00000000, 0x22FFFFFF, 0x44FFFFFF)) {
        State.activeCategory = 1;
        State.isInputActive = false;
    }

    if (!State.launcherStatusMessage.empty() && GetTickCount() < State.launcherStatusUntil) {
        drawList.AddText(fontMgr, State.launcherStatusMessage, l_leftW + 30.0f, h - l_botH - 18.0f, State.launcherStatusColor, w - l_leftW - l_rightW - 60.0f);
    } else if (!State.launcherStatusMessage.empty() && GetTickCount() >= State.launcherStatusUntil) {
        State.launcherStatusMessage.clear();
    }

    if (State.activeCategory == 0) {
        drawList.AddText(fontMgr, "New Project Templates", l_leftW + 30.0f, 20.0f, 0xFFFFFFFF);
        
        float tX = l_leftW + 30.0f; float tY = 80.0f;
        drawList.AddRectFilled(tX - 4, tY - 4, 128, 148, unrealBlue);
        drawList.AddRectFilled(tX, tY, 120, 140, 0xFF2A2A2A);
        drawList.AddText(fontMgr, "Blank", tX + 25, tY + 110, 0xFFFFFFFF);
        
        drawList.AddText(fontMgr, "Blank", w - l_rightW + 20.0f, 20.0f, 0xFFFFFFFF);
        drawList.AddText(fontMgr, "A clean empty project with no code. Start from scratch.", w - l_rightW + 20.0f, 60.0f, 0xFFAAAAAA, l_rightW - 40.0f);

        drawList.AddText(fontMgr, "Project Name:", 30.0f, h - l_botH + 25.0f, 0xFFFFFFFF);
        if (!launcherOverlayActive) {
            uiCtx.TextInput("ProjNameInput", State.inputProjectName, 210.0f, h - l_botH + 15.0f, 300.0f, 50.0f, State.isInputActive);
        } else {
            drawList.AddRectFilled(208.0f, h - l_botH + 13.0f, 304.0f, 54.0f, 0xFF444444);
            drawList.AddRectFilled(210.0f, h - l_botH + 15.0f, 300.0f, 50.0f, 0xFF2A2A2A);
            const std::string displayName = State.inputProjectName.empty() ? "Project Name..." : State.inputProjectName;
            const uint32_t textColor = State.inputProjectName.empty() ? 0xFF777777 : 0xFFFFFFFF;
            drawList.AddText(fontMgr, displayName, 220.0f, h - l_botH + 48.0f, textColor);
        }

        if (!launcherOverlayActive && uiCtx.Button("Cancel", w - 240, h - l_botH + 20, 100, 40, 0xFF333333, 0xFF444444, 0xFF222222)) PostQuitMessage(0);
        
        if (!launcherOverlayActive && uiCtx.Button("Create", w - 130, h - l_botH + 20, 100, 40, unrealBlue, 0xFFFF9020, 0xFFB05000)) {
            std::wstring folder = BrowseForProjectFolder(hwnd);
            if (!folder.empty() && !State.inputProjectName.empty()) { 
                CreateNewProject(folder, State.inputProjectName);
                std::wstring wProjName(State.inputProjectName.begin(), State.inputProjectName.end());
                State.currentProjectFolder = (std::filesystem::path(folder) / wProjName).wstring();
                State.currentProjectFile = (std::filesystem::path(State.currentProjectFolder) / (wProjName + L".CatalystProj")).wstring();
                State.currentMapPath.clear();
                State.currentBrowserPath = State.currentProjectFolder + L"\\Assets";
                State.triggerEditorSwap = true;
                State.editorSwapProjName = wProjName;
                State.recentsLoaded = false;
                State.showLauncherProjectMenu = false;
                State.showDeleteProjectConfirm = false;
            }
        }
    } else {
        drawList.AddText(fontMgr, "Recent Projects", l_leftW + 30.0f, 20.0f, 0xFFFFFFFF);
        float py = 80.0f;
        bool clickedOnProjectCard = false;
        
        for (const auto& projInfo : State.recentProjects) {
            std::string nameStr; for (wchar_t c : projInfo.ProjectName) nameStr += static_cast<char>(c);
            std::string verStr;  for (wchar_t c : projInfo.EngineVersion) verStr += static_cast<char>(c);
            const float cardX = l_leftW + 30.0f;
            const float cardY = py;
            const float cardW = w - l_leftW - l_rightW - 60.0f;
            const float cardH = 60.0f;
            
            drawList.AddRectFilled(cardX, cardY, cardW, cardH, 0xFF2A2A2A);
            drawList.AddText(fontMgr, nameStr, cardX + 20.0f, cardY + 20.0f, 0xFFFFFFFF);
            drawList.AddText(fontMgr, "Catalyst Engine " + verStr, cardX + 20.0f, cardY + 45.0f, 0xFF888888);
            
            if (!launcherOverlayActive && uiCtx.Button("", cardX, cardY, cardW, cardH, 0x00000000, 0x22FFFFFF, 0x44FFFFFF)) {
                BeginProjectOpen(projInfo.Path, projInfo.ProjectName);
                clickedOnProjectCard = true;
            }

            if (g_InputManager && !launcherOverlayActive) {
                const bool isHovered =
                    State.mx >= cardX && State.mx <= cardX + cardW &&
                    State.my >= cardY && State.my <= cardY + cardH;
                if (isHovered && g_InputManager->IsMouseButtonPressed(1)) {
                    State.showLauncherProjectMenu = true;
                    State.launcherProjectMenuX = static_cast<float>(State.mx);
                    State.launcherProjectMenuY = static_cast<float>(State.my);
                    State.launcherProjectMenuPath = projInfo.Path;
                    State.launcherProjectMenuName = projInfo.ProjectName;
                    State.showDeleteProjectConfirm = false;
                    clickedOnProjectCard = true;
                }
            }

            py += 70.0f;
        }

        if (!launcherOverlayActive && uiCtx.Button("Cancel", w - 240, h - l_botH + 20, 100, 40, 0xFF333333, 0xFF444444, 0xFF222222)) PostQuitMessage(0);
        
        if (!launcherOverlayActive && uiCtx.Button("Browse...", w - 130, h - l_botH + 20, 100, 40, unrealBlue, 0xFFFF9020, 0xFFB05000)) {
            std::wstring file = BrowseForProjectFile(hwnd);
            if (!file.empty()) {
                std::wstring pName = std::filesystem::path(file).stem().wstring();
                BeginProjectOpen(file, pName);
            }
        }

        if (State.showLauncherProjectMenu && g_InputManager && g_InputManager->IsMouseButtonPressed(0) && !clickedOnProjectCard) {
            const bool insideMenu =
                State.mx >= launcherMenuX && State.mx <= launcherMenuX + launcherMenuW &&
                State.my >= launcherMenuY && State.my <= launcherMenuY + launcherMenuH;
            if (!insideMenu) {
                State.showLauncherProjectMenu = false;
            }
        }

        if (State.showLauncherProjectMenu) {
            drawList.AddRectFilled(launcherMenuX, launcherMenuY, launcherMenuW, launcherMenuH, 0xFF252525);
            drawList.AddRectFilled(launcherMenuX, launcherMenuY, launcherMenuW, 2.0f, 0xFFD77800);
            drawList.AddText(fontMgr, "Project Actions", launcherMenuX + 10.0f, launcherMenuY + 18.0f, 0xFFAAAAAA);
            if (uiCtx.Button("Delete Project...", launcherMenuX + 8.0f, launcherMenuY + 30.0f, launcherMenuW - 16.0f, 26.0f, 0xFF3A2020, 0xFF6C2E2E, 0xFF7A3434)) {
                State.showDeleteProjectConfirm = true;
                State.deleteProjectPath = State.launcherProjectMenuPath;
                State.deleteProjectName = State.launcherProjectMenuName;
                State.showLauncherProjectMenu = false;
            }
        }
    }

    if (State.showDeleteProjectConfirm) {
        const float popupW = 430.0f;
        const float popupH = 260.0f;
        const float popupX = (w - popupW) * 0.5f;
        const float popupY = (h - popupH) * 0.5f;
        const float bodyX = popupX + 20.0f;
        const float bodyW = popupW - 40.0f;
        const float footerY = popupY + popupH - 68.0f;
        const float buttonY = popupY + popupH - 48.0f;
        const std::string projectName = ToDisplayString(State.deleteProjectName.empty() ? L"Selected Project" : State.deleteProjectName);

        drawList.AddRectFilled(0.0f, 0.0f, w, h, 0xAA000000);
        drawList.AddRectFilled(popupX, popupY, popupW, popupH, 0xFF1A1A1A);
        drawList.AddRectFilled(popupX, popupY, popupW, 34.0f, 0xFF121212);
        drawList.AddText(fontMgr, "Delete Project?", popupX + 16.0f, popupY + 22.0f, 0xFFFFFFFF);
        drawList.AddText(fontMgr, projectName, bodyX, popupY + 72.0f, 0xFFFFFFFF, bodyW);
        drawList.AddText(fontMgr, "This permanently deletes the entire project folder and removes it from Recent Projects. This cannot be undone.", bodyX, popupY + 108.0f, 0xFFB0B0B0, bodyW);
        drawList.AddRectFilled(popupX, footerY, popupW, 1.0f, 0xFF2B2B2B);

        if (uiCtx.Button("Cancel", popupX + 20.0f, buttonY, 120.0f, 32.0f, 0xFF333333, 0xFF444444, 0xFF222222)) {
            State.showDeleteProjectConfirm = false;
        }

        if (uiCtx.Button("Delete Project", popupX + popupW - 160.0f, buttonY, 140.0f, 32.0f, 0xFF7A2E2E, 0xFF9A3838, 0xFFB04040)) {
            const bool didDeleteProject = DeleteProject(State.deleteProjectPath);
            State.showDeleteProjectConfirm = false;
            State.launcherStatusMessage = didDeleteProject ? "Project deleted" : "Delete failed";
            State.launcherStatusColor = didDeleteProject ? 0xFF89D185 : 0xFFE07A7A;
            State.launcherStatusUntil = GetTickCount() + 3200;
            State.recentsLoaded = false;
            if (didDeleteProject) {
                State.launcherProjectMenuPath.clear();
                State.launcherProjectMenuName.clear();
            }
        }
    }
}

void EditorUI::DrawProjectLoading(DXRenderer* renderer, float w, float h) {
    auto& drawList = renderer->m_uiDrawList;
    auto& fontMgr = renderer->m_fontManager;

    const ULONGLONG tick = GetTickCount64();
    const float pulse = (sinf(static_cast<float>(tick) * 0.0065f) + 1.0f) * 0.5f;
    const float progress = fmodf(static_cast<float>(tick) * 0.00028f, 1.0f);
    const std::string projectName = ToDisplayString(State.editorSwapProjName.empty() ? L"Untitled Project" : State.editorSwapProjName);

    drawList.AddRectFilled(0.0f, 0.0f, w, h, 0xFF131313);
    drawList.AddRectFilled(0.0f, 0.0f, w, 72.0f, 0xFF101010);
    drawList.AddRectFilled(0.0f, 72.0f, w, 2.0f, 0x33000000);

    const float cardW = 540.0f;
    const float cardH = 240.0f;
    const float cardX = (w - cardW) * 0.5f;
    const float cardY = (h - cardH) * 0.5f - 28.0f;

    drawList.AddRectFilled(cardX - 3.0f, cardY - 3.0f, cardW + 6.0f, cardH + 6.0f, 0x88201008);
    drawList.AddRectFilled(cardX, cardY, cardW, cardH, 0xFF1E1E1E);
    drawList.AddRectFilled(cardX, cardY, cardW, 4.0f, 0xFFD77800);

    drawList.AddText(fontMgr, "LOADING PROJECT", cardX + 28.0f, cardY + 42.0f, 0xFF9A9A9A);
    drawList.AddText(fontMgr, projectName, cardX + 28.0f, cardY + 92.0f, 0xFFFFFFFF, cardW - 56.0f);
    drawList.AddText(fontMgr, "Rebuilding scene objects, materials, and assets for this project.", cardX + 28.0f, cardY + 136.0f, 0xFFBEBEBE, cardW - 56.0f);

    const float barX = cardX + 28.0f;
    const float barY = cardY + 178.0f;
    const float barW = cardW - 56.0f;
    const float barH = 14.0f;
    drawList.AddRectFilled(barX, barY, barW, barH, 0xFF111111);

    const float glowWidth = 110.0f + pulse * 90.0f;
    const float glowX = barX + progress * (barW - glowWidth);
    drawList.AddRectFilled(glowX, barY, glowWidth, barH, 0xFFD77800);

    drawList.AddText(fontMgr, "Please wait. Large projects can take a moment to initialize.", cardX + 28.0f, cardY + 224.0f, 0xFF8E8E8E, cardW - 56.0f);

    const float accentY = h - 116.0f;
    drawList.AddRectFilled(0.0f, accentY, w, 116.0f, 0xFF181818);
    drawList.AddText(fontMgr, "Catalyst is preparing the editor workspace so the scene opens cleanly.", 34.0f, accentY + 46.0f, 0xFF8A8A8A, w - 68.0f);
}


