#include "EditorUIInternal.h"
#include "EditorUI.h"
#include "DXRenderer.h"
#include "../Launcher.h"

#include "Passes/RaytracePass.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>
#include <vector>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

constexpr float kRowHeight   = 26.0f;
constexpr float kRowSpacing  = 32.0f;
constexpr float kSidebarW    = 190.0f;

struct CategoryEntry {
    const char* label;
    bool isHeader;
};

// Sidebar contents. Headers are not selectable; the indented rows are.
constexpr std::array<CategoryEntry, 7> kCategories = {{
    {"EDITOR",     true},
    {"Viewport",   false},
    {"Layout",     false},
    {"PROJECT",    true},
    {"Rendering",  false},
    {"Raytracing", false},
    {"Physics",    false},
}};

// EditorUI.cpp keeps its own copy file-local, so measure here rather than
// widening that one's linkage for a single caller.
float MeasureOverlayTextWidth(FontManager& fontManager, const std::string& text) {
    float width = 0.0f;
    for (size_t i = 0; i < text.size();) {
        width += fontManager.GetGlyph(TextUtf8::NextCodepoint(text, i)).Advance;
    }
    return width;
}

}

void EditorUI::DrawSettingsWindow(DXRenderer* renderer, float w, float h) {
    if (!State.showSettingsWindow) {
        return;
    }

    auto& drawList = renderer->m_uiDrawList;
    auto& uiCtx    = renderer->m_uiContext;
    auto& fontMgr  = renderer->m_fontManager;

    EditorPreferences& prefs = renderer->GetEditorPreferences();
    ProjectSettings& project = renderer->GetProjectSettings();

    const float popupW = 760.0f;
    const float popupH = 560.0f;   // tall enough for the raytracing rows
    const float popupX = (w - popupW) * 0.5f;
    const float popupY = (h - popupH) * 0.5f;

    // Swallow clicks outside the window so the editor underneath stays inert.
    uiCtx.SetModalRegion(popupX, popupY, popupW, popupH);

    drawList.AddRectFilled(0.0f, 0.0f, w, h, 0xAA000000);
    drawList.AddRectFilled(popupX, popupY, popupW, popupH, 0xFF262626);
    drawList.AddRectFilled(popupX, popupY, popupW, 34.0f, 0xFF171717);
    drawList.AddRectFilled(popupX, popupY, popupW, 3.0f, 0xFFE07020);
    drawList.AddText(fontMgr, "Settings", popupX + 16.0f, popupY + 22.0f, 0xFFE8E8E8);

    // ---- sidebar ----------------------------------------------------------
    const float sidebarX = popupX;
    const float sidebarY = popupY + 34.0f;
    const float sidebarH = popupH - 34.0f - 56.0f;
    drawList.AddRectFilled(sidebarX, sidebarY, kSidebarW, sidebarH, 0xFF1E1E1E);
    drawList.AddRectFilled(sidebarX + kSidebarW, sidebarY, 1.0f, sidebarH, 0xFF3A3A3A);

    float categoryY = sidebarY + 12.0f;
    for (size_t i = 0; i < kCategories.size(); ++i) {
        const CategoryEntry& entry = kCategories[i];
        if (entry.isHeader) {
            drawList.AddText(fontMgr, entry.label, sidebarX + 14.0f, categoryY + 17.0f, 0xFF6E6E6E);
            categoryY += kRowSpacing;
            continue;
        }

        const bool selected = State.settingsCategory == static_cast<int>(i);
        if (selected) {
            drawList.AddRectFilled(sidebarX + 6.0f, categoryY, kSidebarW - 12.0f, kRowHeight, 0xFF313131);
            drawList.AddRectFilled(sidebarX + 6.0f, categoryY, 3.0f, kRowHeight, 0xFFE07020);
        }
        if (uiCtx.Button(std::string("  ") + entry.label, sidebarX + 6.0f, categoryY, kSidebarW - 12.0f, kRowHeight,
                         0x00000000, 0xFF2C2C2C, 0xFF383838)) {
            State.settingsCategory = static_cast<int>(i);
        }
        categoryY += kRowSpacing;
    }

    // ---- body -------------------------------------------------------------
    const float bodyX = popupX + kSidebarW + 24.0f;
    const float bodyW = popupW - kSidebarW - 48.0f;
    float rowY = sidebarY + 18.0f;

    // DragFloat renders the value itself at %.3f, so anything that would read
    // as 0.002 or 0.017 is presented in a friendlier unit and converted back.
    auto FloatRow = [&](const char* label, float& value, float dragSpeed) {
        uiCtx.DragFloat(label, value, dragSpeed, bodyX, rowY, bodyW, kRowHeight, 0.52f);
        rowY += kRowSpacing;
    };

    auto ScaledRow = [&](const char* label, float& value, float scale, float dragSpeed) {
        float shown = value * scale;
        if (uiCtx.DragFloat(label, shown, dragSpeed, bodyX, rowY, bodyW, kRowHeight, 0.52f)) {
            value = shown / scale;
        }
        rowY += kRowSpacing;
    };

    auto ReciprocalRow = [&](const char* label, float& value, float dragSpeed) {
        float hz = value > 0.0f ? (1.0f / value) : 60.0f;
        if (uiCtx.DragFloat(label, hz, dragSpeed, bodyX, rowY, bodyW, kRowHeight, 0.52f)) {
            if (hz > 0.0f) value = 1.0f / hz;
        }
        rowY += kRowSpacing;
    };

    auto SectionNote = [&](const std::string& text) {
        drawList.AddText(fontMgr, text, bodyX, rowY + 14.0f, 0xFF8A8A8A, bodyW);
        rowY += kRowSpacing;
    };

    const std::wstring activeProject = renderer->ResolveActiveProjectFilePath();
    const bool hasProject = !activeProject.empty();

    switch (State.settingsCategory) {
    case 1: // Editor > Viewport
        FloatRow("Camera move speed", prefs.cameraMoveSpeed, 0.05f);
        ScaledRow("Look sensitivity", prefs.cameraTurnSpeed, 1000.0f, 0.02f);
        FloatRow("Field of view",     prefs.viewportFov,     0.2f);
        rowY += 10.0f;
        uiCtx.Checkbox("FPS counter overlay", prefs.showFpsCounter, bodyX, rowY, 18.0f);
        rowY += kRowSpacing;
        uiCtx.Checkbox("Raytracing stats overlay", prefs.showRaytracingStats, bodyX, rowY, 18.0f);
        rowY += kRowSpacing;
        rowY += 8.0f;
        SectionNote("Applies as soon as you save. Scripts that set the camera FOV at runtime still override it during play.");
        break;

    case 2: // Editor > Layout
        FloatRow("Panel width",     prefs.rightPanelWidth,   0.5f);
        ScaledRow("Outliner share (%)", prefs.outlinerSplit, 100.0f, 0.2f);
        FloatRow("Panel height",    prefs.bottomPanelHeight, 0.5f);
        rowY += 8.0f;
        SectionNote("Saved preferences take priority over editor_layout.xml.");
        break;

    case 4: // Project > Rendering
        if (!hasProject) {
            SectionNote("No project is open, so these cannot be saved.");
        }
        FloatRow("Exposure",        project.exposure,       0.005f);
        FloatRow("Bloom threshold", project.bloomThreshold, 0.005f);
        FloatRow("Bloom intensity", project.bloomIntensity, 0.005f);
        FloatRow("Tint R",          project.colorTint.x,    0.005f);
        FloatRow("Tint G",          project.colorTint.y,    0.005f);
        FloatRow("Tint B",          project.colorTint.z,    0.005f);
        break;

    case 5: { // Project > Raytracing
        if (!hasProject) {
            SectionNote("No project is open, so these cannot be saved.");
        }

        const RaytracePass::Stats rtStats = renderer->GetRaytracePass().GetStats();
        if (!rtStats.available) {
            SectionNote("This GPU does not report DXR 1.1, so raytracing stays off.");
        }

        auto CheckRow = [&](const char* label, bool& value) {
            uiCtx.Checkbox(label, value, bodyX, rowY, 18.0f);
            rowY += kRowSpacing;
        };

        CheckRow("Raytracing enabled", project.raytraceEnabled);
        rowY += 6.0f;

        CheckRow("Shadows", project.raytraceShadowsEnabled);
        if (project.raytraceEnabled && project.raytraceShadowsEnabled) {
            FloatRow("   Strength", project.raytraceShadowStrength, 0.004f);
            FloatRow("   Softness", project.raytraceShadowSoftness, 0.0004f);
            FloatRow("   Samples",  project.raytraceShadowSamples,  0.05f);
        }

        CheckRow("Reflections", project.raytraceReflectionsEnabled);
        if (project.raytraceEnabled && project.raytraceReflectionsEnabled) {
            FloatRow("   Intensity", project.raytraceReflectionIntensity, 0.004f);
            FloatRow("   Max distance", project.raytraceReflectionDistance, 0.8f);
        }

        CheckRow("Ambient occlusion", project.raytraceAoEnabled);
        if (project.raytraceEnabled && project.raytraceAoEnabled) {
            FloatRow("   Strength", project.raytraceAoStrength, 0.004f);
            FloatRow("   Sample radius", project.raytraceAoRadius, 0.02f);
            FloatRow("   Samples",  project.raytraceAoSamples,  0.08f);
        }
        break;
    }

    case 6: // Project > Physics
        if (!hasProject) {
            SectionNote("No project is open, so these cannot be saved.");
        }
        FloatRow("Gravity X", project.gravity.x, 0.02f);
        FloatRow("Gravity Y", project.gravity.y, 0.02f);
        FloatRow("Gravity Z", project.gravity.z, 0.02f);
        ReciprocalRow("Simulation rate (Hz)", project.fixedTimeStep, 0.2f);
        rowY += 8.0f;
        SectionNote("Simulation rate is clamped to 15 - 480 Hz.");
        break;

    default:
        State.settingsCategory = 1;
        break;
    }

    // ---- footer -----------------------------------------------------------
    const bool projectCategory = State.settingsCategory >= 4;
    const float footerY = popupY + popupH - 44.0f;
    drawList.AddRectFilled(popupX, footerY - 8.0f, popupW, 1.0f, 0xFF3A3A3A);

    if (uiCtx.Button("Reset", popupX + 16.0f, footerY, 96.0f, 30.0f, 0xFF3D3D3D, 0xFF484848, 0xFF2C2C2C)) {
        if (projectCategory) {
            project = ProjectSettings{};
            renderer->ApplyProjectSettings();
        } else {
            prefs = EditorPreferences{};
            renderer->ApplyEditorPreferences();
        }
    }

    if (uiCtx.Button("Cancel", popupX + popupW - 232.0f, footerY, 96.0f, 30.0f, 0xFF3D3D3D, 0xFF484848, 0xFF2C2C2C)) {
        // Re-read from disk so edits made since opening are discarded.
        renderer->GetEditorPreferences() = m_settingsPrefsOnOpen;
        renderer->GetProjectSettings()   = m_settingsProjectOnOpen;
        renderer->ApplyEditorPreferences();
        renderer->ApplyProjectSettings();
        State.showSettingsWindow = false;
    }

    if (uiCtx.Button("Save", popupX + popupW - 124.0f, footerY, 108.0f, 30.0f, 0xFF2F6B33, 0xFF3C8542, 0xFF204A24)) {
        const bool savedPrefs = renderer->SaveEditorPreferences();
        bool savedProject = true;
        if (hasProject) {
            savedProject = renderer->SaveProjectSettings();
        }

        if (savedPrefs && savedProject) {
            State.settingsStatus = "Settings saved";
            State.settingsStatusColor = 0xFF89D185;
            m_settingsPrefsOnOpen   = renderer->GetEditorPreferences();
            m_settingsProjectOnOpen = renderer->GetProjectSettings();
            State.showSettingsWindow = false;
        } else {
            State.settingsStatus = savedPrefs ? "Could not write project settings" : "Could not write editor_settings.json";
            State.settingsStatusColor = 0xFFE07A7A;
        }
    }

    if (!State.settingsStatus.empty()) {
        drawList.AddText(fontMgr, State.settingsStatus, popupX + 130.0f, footerY + 20.0f, State.settingsStatusColor, 300.0f);
    }
}

void EditorUI::OpenSettingsWindow(DXRenderer* renderer, int category) {
    State.showSettingsWindow = true;
    State.settingsCategory = category;
    State.settingsStatus.clear();
    // Snapshot so Cancel can put everything back.
    m_settingsPrefsOnOpen   = renderer->GetEditorPreferences();
    m_settingsProjectOnOpen = renderer->GetProjectSettings();
}

// ---------------------------------------------------------------------------
// Viewport overlay: frame timing and raytracing state, both toggled from the
// Window menu and remembered in editor_settings.json.
// ---------------------------------------------------------------------------
void EditorUI::DrawViewportOverlay(DXRenderer* renderer, float viewportTop, float viewportWidth) {
    const EditorPreferences& prefs = renderer->GetEditorPreferences();
    if (!prefs.showFpsCounter && !prefs.showRaytracingStats) {
        return;
    }
    if (State.showActorAssetViewer || State.showMaterialAssetViewer || m_blueprintEditor.IsOpen()) {
        return;
    }

    auto& drawList = renderer->m_uiDrawList;
    auto& fontMgr  = renderer->m_fontManager;
    const RaytracePass::Stats rt = renderer->GetRaytracePass().GetStats();

    // Rows are label/value pairs so the values can share a column; a heading has
    // an empty value and a section rule drawn under it.
    struct Row {
        std::string label;
        std::string value;
        uint32_t valueColor;
        bool heading;
    };
    std::vector<Row> rows;
    char buffer[160];

    auto AddRow = [&](const std::string& label, const std::string& value, uint32_t color) {
        rows.push_back({label, value, color, false});
    };
    auto AddHeading = [&](const std::string& label) {
        rows.push_back({label, "", 0xFF7A7A7A, true});
    };

    const float fps = renderer->GetSmoothedFps();
    const uint32_t fpsColor = fps >= 60.0f ? 0xFF7FD98A : (fps >= 30.0f ? 0xFF5FC8E8 : 0xFF6B6BE8);

    if (prefs.showFpsCounter) {
        AddHeading("FRAME");
        std::snprintf(buffer, sizeof(buffer), "%.0f", fps);
        AddRow("Rate", std::string(buffer) + " fps", fpsColor);
        std::snprintf(buffer, sizeof(buffer), "%.2f ms", renderer->GetSmoothedFrameMs());
        AddRow("CPU frame", buffer, 0xFFD0D0D0);
    }

    if (prefs.showRaytracingStats) {
        AddHeading("RAYTRACING");
        if (!rt.available) {
            AddRow("Status", "unavailable", 0xFF6B6BE8);
        } else {
            AddRow("Backend", "DXR 1.1 inline", 0xFFD0D0D0);
            AddRow("Status", rt.tracedThisFrame ? "tracing" : "idle",
                   rt.tracedThisFrame ? 0xFF7FD98A : 0xFF9A9A9A);

            // Measured with GPU timestamps around the dispatch, so this is the
            // real cost of the trace rather than anything inferred on the CPU.
            std::snprintf(buffer, sizeof(buffer), "%.3f ms", rt.gpuMilliseconds);
            AddRow("GPU trace", buffer, rt.gpuMilliseconds > 0.0f ? 0xFF7FD98A : 0xFF9A9A9A);

            std::snprintf(buffer, sizeof(buffer), "%u x %u", rt.traceWidth, rt.traceHeight);
            AddRow("Resolution", buffer, 0xFFD0D0D0);

            AddHeading("ACCELERATION");
            std::snprintf(buffer, sizeof(buffer), "%u", rt.instanceCount);
            AddRow("TLAS instances", buffer, 0xFFD0D0D0);
            std::snprintf(buffer, sizeof(buffer), "%u", rt.blasCount);
            AddRow("BLAS cached", buffer, 0xFFD0D0D0);

            AddHeading("RAYS PER PIXEL");
            AddRow("Shadow", "1", 0xFFD0D0D0);
            AddRow("Reflection", "1 + 1 shadow", 0xFFD0D0D0);
            std::snprintf(buffer, sizeof(buffer), "%u", rt.aoSamples);
            AddRow("Ambient occlusion", buffer, 0xFFD0D0D0);

            const double pixels = static_cast<double>(rt.traceWidth) * static_cast<double>(rt.traceHeight);
            const double perPixel = 3.0 + static_cast<double>(rt.aoSamples);
            std::snprintf(buffer, sizeof(buffer), "%.1f M", (pixels * perPixel) / 1.0e6);
            AddRow("Peak per frame", buffer, 0xFFE8C070);
        }
    }

    if (rows.empty()) {
        return;
    }

    constexpr float kLineHeight = 21.0f;
    constexpr float kPadding    = 14.0f;

    float labelWidth = 0.0f;
    float valueWidth = 0.0f;
    for (const Row& row : rows) {
        labelWidth = (std::max)(labelWidth, MeasureOverlayTextWidth(fontMgr, row.label));
        valueWidth = (std::max)(valueWidth, MeasureOverlayTextWidth(fontMgr, row.value));
    }

    const float panelX = 16.0f;
    const float panelY = viewportTop + 14.0f;
    const float panelW = labelWidth + valueWidth + kPadding * 3.0f;
    const float panelH = rows.size() * kLineHeight + kPadding * 2.0f;
    if (panelX + panelW > viewportWidth) {
        return;
    }

    drawList.AddRectFilled(panelX, panelY, panelW, panelH, 0xC8121212);
    drawList.AddRectFilled(panelX, panelY, panelW, 1.0f, 0xFF3A3A3A);
    drawList.AddRectFilled(panelX, panelY + panelH - 1.0f, panelW, 1.0f, 0xFF3A3A3A);
    drawList.AddRectFilled(panelX, panelY, 3.0f, panelH, 0xFFE07020);

    const float valueX = panelX + kPadding + labelWidth + kPadding;
    float rowY = panelY + kPadding;
    for (const Row& row : rows) {
        const float baseline = rowY + 15.0f;
        if (row.heading) {
            drawList.AddText(fontMgr, row.label, panelX + kPadding, baseline, 0xFF7A7A7A);
            drawList.AddRectFilled(panelX + kPadding, rowY + kLineHeight - 4.0f,
                                   panelW - kPadding * 2.0f, 1.0f, 0xFF2E2E2E);
        } else {
            drawList.AddText(fontMgr, row.label, panelX + kPadding, baseline, 0xFF9A9A9A);
            drawList.AddText(fontMgr, row.value, valueX, baseline, row.valueColor);
        }
        rowY += kLineHeight;
    }
}
