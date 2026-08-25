#include "EditorUIInternal.h"
#include "../Core Render/DXRenderer.h"
#include "../EngineApp.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <shlobj.h>
using namespace EditorUIInternal;

// ── Template / Category Data ─────────────────────────────────────────────────

struct LauncherTemplate {
    const char* name;
    const char* description;
    uint32_t    thumbBg;
    uint32_t    thumbIcon;
};
struct LauncherCategory {
    const char* name;
    uint32_t    bgColor;
};

static const LauncherCategory kCategories[] = {
    { "Games",        0xFF141E2C },
    { "Film & Video", 0xFF1A1428 },
    { "Architecture", 0xFF1C1414 },
    { "Simulation",   0xFF141C1C },
};
static const LauncherTemplate kTemplates[] = {
    { "Blank",
      "A completely empty project with no starter content. Build your game from scratch.",
      0xFF252535, 0xFF484868 },
    { "First Person",
      "A first-person character controller with basic movement and a starter scene.",
      0xFF1A2434, 0xFF3A6080 },
    { "Third Person",
      "A third-person camera setup with a character and camera follow system.",
      0xFF182A20, 0xFF386050 },
    { "Top Down",
      "An overhead view template with top-down movement controls and a starter map.",
      0xFF28220E, 0xFF706030 },
};
static const int kCategoryCount = (int)(sizeof(kCategories) / sizeof(kCategories[0]));
static const int kTemplateCount = (int)(sizeof(kTemplates)  / sizeof(kTemplates[0]));

// ── DrawLauncher ─────────────────────────────────────────────────────────────

void EditorUI::DrawLauncher(DXRenderer* renderer, float w, float h) {
    auto& drawList = renderer->m_uiDrawList;
    auto& uiCtx    = renderer->m_uiContext;
    auto& fontMgr  = renderer->m_fontManager;
    HWND hwnd      = renderer->m_hwnd;

    // Theme colors — overridable via editor_theme.xmlstyle
    auto getColor = [renderer](const char* attr, uint32_t fallback) -> uint32_t {
        const auto& cls = renderer->m_editorStyleSheet.Find("theme");
        auto it = cls.find(attr);
        return (it != cls.end()) ? UIXML::ParseHexColor(it->second.c_str(), fallback) : fallback;
    };
    const uint32_t C_BG      = getColor("background", 0xFF0C0C0F);
    const uint32_t C_SIDEBAR = getColor("sidebar",    0xFF0F0F13);
    const uint32_t C_PANEL   = getColor("panel",      0xFF141418);
    const uint32_t C_CARD    = getColor("card",       0xFF1C1C24);
    const uint32_t C_ACCENT  = getColor("accent",     0xFFD77800);
    const uint32_t C_TXTHI   = getColor("textHigh",   0xFFECECEC);
    const uint32_t C_TXTMID  = getColor("textMid",    0xFF8888A0);
    const uint32_t C_TXTDIM  = getColor("textDim",    0xFF505068);
    const uint32_t C_BORDER  = getColor("border",     0xFF232330);

    const float sW = 220.0f;
    const float rW = 270.0f;
    const float bH = 100.0f;
    const float cW = w - sW - rW;

    if (!State.recentsLoaded) {
        State.recentProjects = renderer->GetRecentProjectsInfo();
        State.recentsLoaded  = true;
        if (State.projectLocationPath.empty()) {
            char docs[MAX_PATH] = {};
            SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docs);
            State.projectLocationPath = std::string(docs) + "\\Catalyst Projects";
        }
    }

    const float menuW = 194.0f;
    const float menuH = 110.0f;
    const float menuX = (std::min)(State.launcherProjectMenuX, w - menuW - 10.0f);
    const float menuY = (std::min)(State.launcherProjectMenuY, h - menuH - 10.0f);
    const bool overlayActive = State.showLauncherProjectMenu || State.showDeleteProjectConfirm;

    auto BeginProjectOpen = [&](const std::wstring& path, const std::wstring& name) {
        AddRecentProject(path);
        State.currentProjectFile   = path;
        State.currentProjectFolder = std::filesystem::path(path).parent_path().wstring();
        State.currentMapPath.clear();
        State.currentBrowserPath     = State.currentProjectFolder + L"\\Assets";
        State.triggerEditorSwap      = true;
        State.editorSwapProjName     = name;
        State.recentsLoaded          = false;
        State.showLauncherProjectMenu    = false;
        State.showDeleteProjectConfirm   = false;
    };

    // ── Backgrounds ───────────────────────────────────────────────────────────
    drawList.AddRectFilled(0, 0, w, h, C_BG);
    drawList.AddRectFilled(0, 0, sW, h, C_SIDEBAR);
    drawList.AddRectFilled(sW, 0, cW, h - bH, C_PANEL);
    drawList.AddRectFilled(w - rW, 0, rW, h - bH, C_SIDEBAR);
    drawList.AddRectFilled(0, h - bH, w, bH, C_BG);

    drawList.AddRectFilled(sW - 1, 0, 1, h, C_BORDER);
    drawList.AddRectFilled(w - rW, 0, 1, h - bH, C_BORDER);
    drawList.AddRectFilled(0, h - bH, w, 1, C_BORDER);

    // ── Sidebar ───────────────────────────────────────────────────────────────
    const float brandH = 72.0f;
    drawList.AddRectFilled(0, 0, sW, brandH, 0xFF0A0A0E);
    drawList.AddRectFilled(0, 0, sW, 3, C_ACCENT);
    drawList.AddRectFilled(0, brandH - 1, sW, 1, C_BORDER);
    drawList.AddText(fontMgr, "CATALYST", 20.0f, 26.0f, C_ACCENT);
    drawList.AddText(fontMgr, "Engine  v1.0", 20.0f, 48.0f, C_TXTDIM);

    drawList.AddText(fontMgr, "PROJECTS", 20.0f, brandH + 18.0f, C_TXTDIM);

    const float nY0 = brandH + 44.0f;
    const float nH  = 44.0f;
    for (int i = 0; i < 2; i++) {
        const float iy     = nY0 + i * nH;
        const bool  active = (State.activeCategory == i);
        if (active)
            drawList.AddRectFilled(0, iy, sW, nH, 0xFF1C1C28);

        bool clicked = false;
        if (!overlayActive)
            clicked = uiCtx.Button("", 0, iy, sW, nH, 0x00000000, 0x10FFFFFF, 0x18FFFFFF);

        if (active)
            drawList.AddRectFilled(0, iy, 3, nH, C_ACCENT);

        drawList.AddText(fontMgr, i == 0 ? "New Project" : "Open Project",
                         24.0f, iy + 15.0f, active ? C_TXTHI : C_TXTMID);

        if (clicked) {
            State.activeCategory    = i;
            State.isInputActive     = (i == 0);
            if (i == 0) State.showLauncherProjectMenu = false;
        }
    }

    // Category list (only in New Project mode)
    if (State.activeCategory == 0) {
        const float catStartY = nY0 + 2 * nH + 16.0f;
        drawList.AddText(fontMgr, "TEMPLATES", 20.0f, catStartY, C_TXTDIM);

        for (int i = 0; i < kCategoryCount; i++) {
            const float cy     = catStartY + 20.0f + i * 52.0f;
            const bool  active = (State.selectedLauncherCategory == i);

            drawList.AddRectFilled(0, cy, sW, 50.0f, kCategories[i].bgColor);
            if (active) {
                drawList.AddRectFilled(0, cy, sW, 50.0f, 0x18FFFFFF);
                drawList.AddRectFilled(0, cy, 3, 50.0f, C_ACCENT);
            }

            if (!overlayActive && uiCtx.Button("", 0, cy, sW, 50.0f, 0x00000000, 0x10FFFFFF, 0x18FFFFFF))
                State.selectedLauncherCategory = i;

            drawList.AddText(fontMgr, kCategories[i].name, 18.0f, cy + 17.0f,
                             active ? C_TXTHI : C_TXTMID);
        }
    }

    // ── Content + right panel header bars ─────────────────────────────────────
    const float hdrH = 48.0f;
    drawList.AddRectFilled(sW, 0, cW, hdrH, 0xFF101014);
    drawList.AddRectFilled(sW, hdrH - 1, cW, 1, C_BORDER);
    drawList.AddRectFilled(w - rW + 1, 0, rW - 1, hdrH, 0xFF0C0C10);
    drawList.AddRectFilled(w - rW + 1, hdrH - 1, rW - 1, 1, C_BORDER);

    // Status message
    if (!State.launcherStatusMessage.empty()) {
        if (GetTickCount() < State.launcherStatusUntil)
            drawList.AddText(fontMgr, State.launcherStatusMessage,
                             sW + 20.0f, h - bH - 22.0f,
                             State.launcherStatusColor, cW - 40.0f);
        else
            State.launcherStatusMessage.clear();
    }

    const float rX    = w - rW + 20.0f;
    const float rMaxW = rW - 40.0f;

    if (State.activeCategory == 0) {
        // ── New Project ───────────────────────────────────────────────────────

        // Header labels
        drawList.AddText(fontMgr, "SELECT TEMPLATE", sW + 20.0f, 18.0f, C_TXTMID);

        // ── Template grid (2 columns) ─────────────────────────────────────────
        const float gridX   = sW + 16.0f;
        const float spacing = 12.0f;
        const float cardW   = (cW - 3.0f * spacing) / 2.0f;
        const float cardH   = 148.0f;
        const float thumbH  = 96.0f;

        for (int t = 0; t < kTemplateCount; t++) {
            const int   col = t % 2;
            const int   row = t / 2;
            const float cx  = gridX + col * (cardW + spacing);
            const float cy  = hdrH + 16.0f + row * (cardH + spacing);

            // Border — orange if selected
            const uint32_t borderCol = (t == State.selectedTemplate) ? C_ACCENT : C_BORDER;
            drawList.AddRectFilled(cx - 2, cy - 2, cardW + 4, cardH + 4, borderCol);
            drawList.AddRectFilled(cx, cy, cardW, cardH, C_CARD);

            // Thumbnail
            drawList.AddRectFilled(cx, cy, cardW, thumbH, kTemplates[t].thumbBg);

            // Simple centered icon (diamond shape via lines)
            const float icx = cx + cardW * 0.5f;
            const float icy = cy + thumbH * 0.5f;
            const float ir  = 18.0f;
            const uint32_t ic = kTemplates[t].thumbIcon;
            drawList.AddLine(icx,      icy - ir, icx + ir, icy,      1.5f, ic);
            drawList.AddLine(icx + ir, icy,      icx,      icy + ir, 1.5f, ic);
            drawList.AddLine(icx,      icy + ir, icx - ir, icy,      1.5f, ic);
            drawList.AddLine(icx - ir, icy,      icx,      icy - ir, 1.5f, ic);

            // Footer
            drawList.AddRectFilled(cx, cy + thumbH, cardW, cardH - thumbH, 0xFF14141E);
            drawList.AddText(fontMgr, kTemplates[t].name, cx + 10.0f, cy + thumbH + 14.0f, C_TXTHI);

            // Click
            if (!overlayActive &&
                uiCtx.Button("", cx, cy, cardW, cardH, 0x00000000, 0x0CFFFFFF, 0x18FFFFFF))
                State.selectedTemplate = t;
        }

        // ── Right panel: template details ─────────────────────────────────────
        const auto& tmpl = kTemplates[State.selectedTemplate];

        // Large preview
        const float previewH = 160.0f;
        drawList.AddRectFilled(w - rW + 1, hdrH, rW - 1, previewH, tmpl.thumbBg);

        // Centered diamond icon in preview
        const float pcx = w - rW + 1 + (rW - 1) * 0.5f;
        const float pcy = hdrH + previewH * 0.5f;
        const float pr  = 28.0f;
        const uint32_t pc = tmpl.thumbIcon;
        drawList.AddLine(pcx,      pcy - pr, pcx + pr, pcy,      2.0f, pc);
        drawList.AddLine(pcx + pr, pcy,      pcx,      pcy + pr, 2.0f, pc);
        drawList.AddLine(pcx,      pcy + pr, pcx - pr, pcy,      2.0f, pc);
        drawList.AddLine(pcx - pr, pcy,      pcx,      pcy - pr, 2.0f, pc);

        const float dY = hdrH + previewH + 14.0f;
        drawList.AddText(fontMgr, tmpl.name, rX, dY, C_TXTHI);
        drawList.AddRectFilled(rX, dY + 22.0f, rMaxW, 1, C_BORDER);
        drawList.AddText(fontMgr, tmpl.description, rX, dY + 30.0f, C_TXTMID, rMaxW);

        const float defY = dY + 118.0f;
        drawList.AddRectFilled(rX, defY, rMaxW, 1, C_BORDER);
        drawList.AddText(fontMgr, "PROJECT DEFAULTS", rX, defY + 10.0f, C_TXTDIM);
        drawList.AddText(fontMgr, "Platform    Desktop", rX, defY + 30.0f, C_TXTMID);
        drawList.AddText(fontMgr, "Quality     Maximum", rX, defY + 48.0f, C_TXTMID);

        // ── Bottom bar: two-row layout ────────────────────────────────────────
        // Right-side buttons: Cancel (110px) + Create (110px) + gaps = ~240px
        const float btnW      = 110.0f;
        const float btnAreaW  = btnW * 2.0f + 30.0f;    // 250px total reserved
        const float btnCancelX = w - btnAreaW + 10.0f;
        const float btnCreateX = btnCancelX + btnW + 10.0f;
        const float btnMidY    = h - bH + (bH - 40.0f) * 0.5f;

        // Row 1 — Location
        const float row1Y    = h - bH + 10.0f;
        const float locLblX  = sW + 18.0f;
        const float locInpX  = locLblX + 76.0f;
        const float browseW  = 80.0f;
        const float browseX  = w - btnAreaW - browseW - 16.0f;
        const float locInpW  = browseX - locInpX - 8.0f;

        drawList.AddText(fontMgr, "Location", locLblX, row1Y + 11.0f, C_TXTDIM);

        if (!overlayActive) {
            uiCtx.TextInput("LocInput", State.projectLocationPath,
                            locInpX, row1Y, locInpW, 36.0f, State.isLocationInputActive);
        } else {
            drawList.AddRectFilled(locInpX - 2, row1Y - 2, locInpW + 4, 40.0f, C_BORDER);
            drawList.AddRectFilled(locInpX, row1Y, locInpW, 36.0f, C_CARD);
            drawList.AddText(fontMgr, State.projectLocationPath, locInpX + 8.0f, row1Y + 11.0f, C_TXTHI);
        }

        if (!overlayActive && uiCtx.Button("Browse", browseX, row1Y, browseW, 36.0f, 0xFF1E1E2C, 0xFF2C2C40, 0xFF141420)) {
            std::wstring folder = BrowseForProjectFolder(hwnd);
            if (!folder.empty())
                State.projectLocationPath = ToDisplayString(folder);
        }

        // Row 2 — Name
        const float row2Y    = h - bH + 54.0f;
        const float nameLblX = sW + 18.0f;
        const float nameInpX = nameLblX + 50.0f;
        const float nameInpW = 220.0f;

        drawList.AddText(fontMgr, "Name", nameLblX, row2Y + 11.0f, C_TXTDIM);

        if (!overlayActive) {
            uiCtx.TextInput("ProjNameInput", State.inputProjectName,
                            nameInpX, row2Y, nameInpW, 36.0f, State.isInputActive);
        } else {
            drawList.AddRectFilled(nameInpX - 2, row2Y - 2, nameInpW + 4, 40.0f, C_BORDER);
            drawList.AddRectFilled(nameInpX, row2Y, nameInpW, 36.0f, C_CARD);
            const std::string disp = State.inputProjectName.empty() ? "My Project" : State.inputProjectName;
            drawList.AddText(fontMgr, disp, nameInpX + 8.0f, row2Y + 11.0f,
                             State.inputProjectName.empty() ? C_TXTDIM : C_TXTHI);
        }

        // Buttons (vertically centered, right-aligned)
        if (!overlayActive && uiCtx.Button("Cancel", btnCancelX, btnMidY, btnW, 40.0f, 0xFF1A1A26, 0xFF252534, 0xFF111118))
            PostQuitMessage(0);

        if (!overlayActive && uiCtx.Button("Create", btnCreateX, btnMidY - 2.0f, btnW, 44.0f, C_ACCENT, 0xFFFF9020, 0xFFB05000)) {
            if (State.projectLocationPath.empty() || State.inputProjectName.empty()) {
                State.launcherStatusMessage = State.inputProjectName.empty()
                    ? "Enter a project name before creating."
                    : "Choose a project location before creating.";
                State.launcherStatusColor = 0xFFE07A7A;
                State.launcherStatusUntil = GetTickCount() + 3500;
            } else {
                std::wstring folder = StringToWide(State.projectLocationPath);
                CreateNewProject(folder, State.inputProjectName);
                std::wstring wname(State.inputProjectName.begin(), State.inputProjectName.end());
                State.currentProjectFolder = (std::filesystem::path(folder) / wname).wstring();
                State.currentProjectFile   = (std::filesystem::path(State.currentProjectFolder) / (wname + L".CatalystProj")).wstring();
                State.currentMapPath.clear();
                State.currentBrowserPath   = State.currentProjectFolder + L"\\Assets";
                State.triggerEditorSwap    = true;
                State.editorSwapProjName   = wname;
                State.recentsLoaded        = false;
                State.showLauncherProjectMenu  = false;
                State.showDeleteProjectConfirm = false;
            }
        }

    } else {
        // ── Open Project / Recent Projects ────────────────────────────────────
        drawList.AddText(fontMgr, "RECENT PROJECTS", sW + 20.0f, 18.0f, C_TXTMID);
        drawList.AddText(fontMgr, "TIPS",             rX,         18.0f, C_TXTDIM);

        drawList.AddText(fontMgr,
            "Right-click a project for more options.\n\nUse Browse to open a project from disk.",
            rX, hdrH + 20.0f, C_TXTMID, rMaxW);

        float py      = hdrH + 14.0f;
        bool clickedCard = false;

        for (const auto& pi : State.recentProjects) {
            std::string name, ver;
            for (wchar_t c : pi.ProjectName) name += (char)c;
            for (wchar_t c : pi.EngineVersion) ver  += (char)c;

            const float cardX = sW + 14.0f;
            const float cardH = 56.0f;
            const float cardW = cW - 28.0f;

            const bool hovered = !overlayActive &&
                State.mx >= cardX && State.mx <= cardX + cardW &&
                State.my >= py    && State.my <= py + cardH;

            drawList.AddRectFilled(cardX, py, cardW, cardH, C_CARD);

            bool cardClicked = false;
            if (!overlayActive)
                cardClicked = uiCtx.Button("", cardX, py, cardW, cardH, 0x00000000, 0x12FFFFFF, 0x20FFFFFF);

            drawList.AddRectFilled(cardX, py, 3, cardH, hovered ? C_ACCENT : 0xFF303040);
            drawList.AddText(fontMgr, name, cardX + 16.0f, py + 14.0f, C_TXTHI);
            drawList.AddText(fontMgr, "Catalyst  " + ver, cardX + 16.0f, py + 36.0f, C_TXTDIM);

            if (cardClicked) {
                BeginProjectOpen(pi.Path, pi.ProjectName);
                clickedCard = true;
            }
            if (g_InputManager && hovered && g_InputManager->IsMouseButtonPressed(1)) {
                State.showLauncherProjectMenu   = true;
                State.launcherProjectMenuX      = (float)State.mx;
                State.launcherProjectMenuY      = (float)State.my;
                State.launcherProjectMenuPath   = pi.Path;
                State.launcherProjectMenuName   = pi.ProjectName;
                State.showDeleteProjectConfirm  = false;
                clickedCard = true;
            }

            py += cardH + 8.0f;
        }

        if (State.recentProjects.empty())
            drawList.AddText(fontMgr,
                "No recent projects.\nCreate a new project or browse to open one.",
                sW + 20.0f, hdrH + 24.0f, C_TXTDIM, cW - 40.0f);

        if (!overlayActive && uiCtx.Button("Cancel",    w - 242.0f, h - bH + (bH - 40.0f) * 0.5f, 100.0f, 40.0f, 0xFF1A1A26, 0xFF252534, 0xFF111118))
            PostQuitMessage(0);
        if (!overlayActive && uiCtx.Button("Browse...", w - 132.0f, h - bH + (bH - 40.0f) * 0.5f, 122.0f, 40.0f, C_ACCENT, 0xFFFF9020, 0xFFB05000)) {
            std::wstring file = BrowseForProjectFile(hwnd);
            if (!file.empty()) {
                std::wstring pName = std::filesystem::path(file).stem().wstring();
                BeginProjectOpen(file, pName);
            }
        }

        // Context menu dismiss
        if (State.showLauncherProjectMenu && g_InputManager && g_InputManager->IsMouseButtonPressed(0) && !clickedCard) {
            const bool inside =
                State.mx >= menuX && State.mx <= menuX + menuW &&
                State.my >= menuY && State.my <= menuY + menuH;
            if (!inside) State.showLauncherProjectMenu = false;
        }

        // Context menu
        if (State.showLauncherProjectMenu) {
            drawList.AddRectFilled(menuX - 1, menuY - 1, menuW + 2, menuH + 2, C_BORDER);
            drawList.AddRectFilled(menuX, menuY, menuW, menuH, 0xFF161620);
            drawList.AddRectFilled(menuX, menuY, 3, menuH, C_ACCENT);
            drawList.AddText(fontMgr, "PROJECT", menuX + 14.0f, menuY + 14.0f, C_TXTDIM);

            if (uiCtx.Button("Open Code Solution", menuX + 8.0f, menuY + 32.0f, menuW - 16.0f, 30.0f, 0xFF1C2D48, 0xFF253D60, 0xFF162340)) {
                const bool ok = OpenProjectCodeSolution(State.launcherProjectMenuPath);
                State.launcherStatusMessage = ok ? "Opened code solution" : "Failed to open code solution";
                State.launcherStatusColor   = ok ? 0xFF89D185 : 0xFFE07A7A;
                State.launcherStatusUntil   = GetTickCount() + 3200;
                State.showLauncherProjectMenu = false;
            }
            if (uiCtx.Button("Delete Project...", menuX + 8.0f, menuY + 68.0f, menuW - 16.0f, 30.0f, 0xFF2E1818, 0xFF4C2020, 0xFF3C1010)) {
                State.showDeleteProjectConfirm = true;
                State.deleteProjectPath        = State.launcherProjectMenuPath;
                State.deleteProjectName        = State.launcherProjectMenuName;
                State.showLauncherProjectMenu  = false;
            }
        }
    }

    // ── Delete Confirm Dialog ─────────────────────────────────────────────────
    if (State.showDeleteProjectConfirm) {
        const float dW   = 440.0f, dH = 210.0f;
        const float dX   = (w - dW) * 0.5f, dY = (h - dH) * 0.5f;
        const float bX   = dX + 24.0f, bW = dW - 48.0f;
        const float btnY = dY + dH - 50.0f;
        const std::string projName = ToDisplayString(
            State.deleteProjectName.empty() ? L"Selected Project" : State.deleteProjectName);

        drawList.AddRectFilled(0, 0, w, h, 0xAA000000);
        drawList.AddRectFilled(dX - 1, dY - 1, dW + 2, dH + 2, C_BORDER);
        drawList.AddRectFilled(dX, dY, dW, dH, 0xFF13131A);
        drawList.AddRectFilled(dX, dY, dW, 38.0f, 0xFF0F0F14);
        drawList.AddRectFilled(dX, dY, dW, 2, 0xFFCC3333);
        drawList.AddText(fontMgr, "Delete Project", dX + 16.0f, dY + 24.0f, C_TXTHI);
        drawList.AddText(fontMgr, projName, bX, dY + 56.0f, C_TXTHI, bW);
        drawList.AddText(fontMgr,
            "This permanently deletes the project folder and removes it from Recent Projects. This cannot be undone.",
            bX, dY + 80.0f, C_TXTMID, bW);
        drawList.AddRectFilled(dX, btnY - 10, dW, 1, C_BORDER);

        if (uiCtx.Button("Cancel",        dX + 20.0f,        btnY, 110.0f, 34.0f, 0xFF1A1A26, 0xFF252534, 0xFF111118))
            State.showDeleteProjectConfirm = false;

        if (uiCtx.Button("Delete Forever", dX + dW - 152.0f, btnY, 132.0f, 34.0f, 0xFF601C1C, 0xFF822828, 0xFF9A3030)) {
            const bool ok = DeleteProject(State.deleteProjectPath);
            State.showDeleteProjectConfirm = false;
            State.launcherStatusMessage    = ok ? "Project deleted" : "Delete failed";
            State.launcherStatusColor      = ok ? 0xFF89D185 : 0xFFE07A7A;
            State.launcherStatusUntil      = GetTickCount() + 3200;
            State.recentsLoaded            = false;
            if (ok) {
                State.launcherProjectMenuPath.clear();
                State.launcherProjectMenuName.clear();
            }
        }
    }
}

void EditorUI::DrawProjectLoading(DXRenderer* renderer, float w, float h) {
    auto& drawList = renderer->m_uiDrawList;
    auto& fontMgr  = renderer->m_fontManager;

    const ULONGLONG tick     = GetTickCount64();
    const float pulse        = (sinf(static_cast<float>(tick) * 0.0065f) + 1.0f) * 0.5f;
    const float progress     = fmodf(static_cast<float>(tick) * 0.00028f, 1.0f);
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
    const float glowX     = barX + progress * (barW - glowWidth);
    drawList.AddRectFilled(glowX, barY, glowWidth, barH, 0xFFD77800);

    drawList.AddText(fontMgr, "Please wait. Large projects can take a moment to initialize.", cardX + 28.0f, cardY + 224.0f, 0xFF8E8E8E, cardW - 56.0f);

    const float accentY = h - 116.0f;
    drawList.AddRectFilled(0.0f, accentY, w, 116.0f, 0xFF181818);
    drawList.AddText(fontMgr, "Catalyst is preparing the editor workspace so the scene opens cleanly.", 34.0f, accentY + 46.0f, 0xFF8A8A8A, w - 68.0f);
}
