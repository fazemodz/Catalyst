#include "EditorUIInternal.h"
#include "../Core Render/DXRenderer.h"
#include "../EngineApp.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
using namespace EditorUIInternal;

namespace {
uint32_t PackMaterialColor(const DirectX::XMFLOAT4& color) {
    const auto ToByte = [](float value) {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        return static_cast<uint32_t>(std::lround(clamped * 255.0f));
    };

    const uint32_t a = ToByte(color.w);
    const uint32_t r = ToByte(color.x);
    const uint32_t g = ToByte(color.y);
    const uint32_t b = ToByte(color.z);
    return (a << 24) | (r << 16) | (g << 8) | b;
}
}

void EditorUI::UpdatePreviewInteraction(float viewportTop, float viewportWidth, float viewportHeight,
                                        float& yaw, float& pitch, float& distance,
                                        bool& autoRotate, bool& isDragging,
                                        int& lastMouseX, int& lastMouseY,
                                        float& outViewportW, float& outViewportH) {
    outViewportW = viewportWidth;
    outViewportH = viewportHeight;

    if (autoRotate) {
        yaw += 0.0055f;
    }

    if (!g_InputManager) {
        return;
    }

    const bool mouseInViewport =
        State.mx >= 0 && State.mx <= viewportWidth &&
        State.my >= viewportTop && State.my <= viewportHeight;

    const int wheelDelta = g_InputManager->GetMouseWheelDelta();
    if (mouseInViewport && wheelDelta != 0) {
        const float zoomStep = (std::max)(0.2f, distance * 0.08f);
        distance -= (wheelDelta / 120.0f) * zoomStep;
        distance = std::clamp(distance, 0.6f, 500.0f);
    }

    if (mouseInViewport && g_InputManager->IsMouseButtonPressed(0)) {
        isDragging = true;
        lastMouseX = State.mx;
        lastMouseY = State.my;
    } else if (!g_InputManager->IsMouseButtonDown(0)) {
        isDragging = false;
    }

    if (isDragging) {
        const float deltaX = static_cast<float>(State.mx - lastMouseX);
        const float deltaY = static_cast<float>(State.my - lastMouseY);
        yaw += deltaX * 0.01f;
        pitch += deltaY * 0.01f;
        pitch = std::clamp(pitch, -1.35f, 1.10f);
        lastMouseX = State.mx;
        lastMouseY = State.my;
    }
}

void EditorUI::DrawMaterialBaseColor(DXRenderer* renderer, Material* material, const std::wstring& materialPath,
                                     float x, float width, float& y) {
    if (!material) {
        return;
    }

    auto& drawList = renderer->m_uiDrawList;
    auto& uiCtx = renderer->m_uiContext;
    auto& fontMgr = renderer->m_fontManager;

    drawList.AddText(fontMgr, "Base Color", x, y + 15.0f, 0xFFAAAAAA);
    drawList.AddRectFilled(x + width - 54.0f, y, 54.0f, 24.0f, 0xFF111111);
    drawList.AddRectFilled(x + width - 50.0f, y + 4.0f, 46.0f, 16.0f, PackMaterialColor(material->baseColor));
    y += 32.0f;

    bool changed = false;
    changed |= uiCtx.DragFloat("Base R", material->baseColor.x, 0.005f, x, y, width, 24.0f); y += 28.0f;
    changed |= uiCtx.DragFloat("Base G", material->baseColor.y, 0.005f, x, y, width, 24.0f); y += 28.0f;
    changed |= uiCtx.DragFloat("Base B", material->baseColor.z, 0.005f, x, y, width, 24.0f); y += 28.0f;
    changed |= uiCtx.DragFloat("Alpha", material->baseColor.w, 0.005f, x, y, width, 24.0f); y += 32.0f;

    material->baseColor.x = std::clamp(material->baseColor.x, 0.0f, 1.0f);
    material->baseColor.y = std::clamp(material->baseColor.y, 0.0f, 1.0f);
    material->baseColor.z = std::clamp(material->baseColor.z, 0.0f, 1.0f);
    material->baseColor.w = std::clamp(material->baseColor.w, 0.0f, 1.0f);

    if (uiCtx.Button("Reset Base Color", x, y, width, 24.0f, 0xFF303030, 0xFF4A4A4A, 0xFF242424)) {
        material->baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
        changed = true;
    }
    y += 34.0f;

    if (changed) {
        renderer->SaveMaterialAsset(*material, materialPath);
    }
}

void EditorUI::DrawMaterialTextureSlots(DXRenderer* renderer, HWND hwnd, Material* material, const std::wstring& materialPath,
                                        float x, float width, float& y) {
    if (!material) {
        return;
    }

    auto& drawList = renderer->m_uiDrawList;
    auto& uiCtx = renderer->m_uiContext;
    auto& fontMgr = renderer->m_fontManager;

    std::wstring projectRoot = !State.currentProjectFolder.empty() ? State.currentProjectFolder : FindProjectRootFromAssetPath(materialPath);
    auto SaveMaterialState = [&]() {
        renderer->SaveMaterialAsset(*material, materialPath);
    };

    auto AssignTextureSlot = [&](const char* label, std::string& linkedPath) {
        drawList.AddText(fontMgr, std::string(label) + ": " + DescribeLinkedAsset(linkedPath), x, y + 15.0f, 0xFFAAAAAA);
        y += 24.0f;

        if (uiCtx.Button(std::string("Assign ") + label, x, y, width * 0.6f, 24.0f, 0xFF222222, 0xFF444444, 0xFF555555)) {
            std::wstring chosenTexture = BrowseForTextureFile(hwnd);
            if (!chosenTexture.empty() && !projectRoot.empty() && IsPathWithinRoot(chosenTexture, projectRoot)) {
                linkedPath = BuildMaterialLink(materialPath, chosenTexture);
                SaveMaterialState();
            }
        }

        if (uiCtx.Button(std::string("Clear ") + label, x + width * 0.65f, y, width * 0.35f, 24.0f, 0xFF303030, 0xFF555555, 0xFF666666)) {
            linkedPath.clear();
            SaveMaterialState();
        }

        y += 34.0f;
    };

    AssignTextureSlot("Base Color", material->albedoPath);
    AssignTextureSlot("Normal", material->normalPath);
    AssignTextureSlot("Roughness", material->roughnessPath);
}

void EditorUI::DrawActorAssetViewer(DXRenderer* renderer, float w, float h) {
    auto& drawList = renderer->m_uiDrawList;
    auto& uiCtx = renderer->m_uiContext;
    auto& fontMgr = renderer->m_fontManager;
    HWND hwnd = renderer->m_hwnd;
    const bool closePromptOpen = renderer->IsClosePromptOpen();

    const float menuBarH = 28.0f;
    const float tabBarH = 36.0f;
    const float toolbarH = 34.0f;
    const float viewerTop = menuBarH + tabBarH + toolbarH;
    const float rightPanelW = 360.0f;
    const float socketPanelH = 220.0f;
    const float viewportW = (std::max)(1.0f, w - rightPanelW);

    if (!closePromptOpen) {
        UpdatePreviewInteraction(viewerTop, viewportW, h,
                                 State.actorViewerYaw, State.actorViewerPitch, State.actorViewerDistance,
                                 State.actorViewerAutoRotate, State.actorViewerIsDragging,
                                 State.actorViewerLastMouseX, State.actorViewerLastMouseY,
                                 State.actorViewerViewportW, State.actorViewerViewportH);
    }

    if (State.isActorViewerLoading) {
        drawList.AddRectFilled(0, 0, w, h, 0xFF141414);
        drawList.AddRectFilled(0, 0, w, menuBarH, 0xFF111111);
        drawList.AddRectFilled(0, menuBarH, w, tabBarH, 0xFF191919);
        drawList.AddRectFilled(0, menuBarH + tabBarH, w, toolbarH, 0xFF202020);
        drawList.AddRectFilled(viewportW, viewerTop, rightPanelW, h - viewerTop, 0xFF1A1A1A);
        drawList.AddText(fontMgr, "SM_" + State.actorViewerTitle, 54.0f, menuBarH + 24.0f, 0xFFFFFFFF);
        const char* closeLabel = renderer->IsStandaloneActorViewerWindow() ? "Close Viewer" : "Back To Editor";
        if (uiCtx.Button(closeLabel, w - 182.0f, menuBarH + 4.0f, 150.0f, 28.0f, 0xFF2C2C2C, 0xFF454545, 0xFF1E1E1E)) {
            if (renderer->IsStandaloneActorViewerWindow()) {
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            } else {
                renderer->CloseActorAssetViewer();
            }
            return;
        }
        drawList.AddText(fontMgr, "Loading preview mesh...", 34.0f, viewerTop + 54.0f, 0xFFFFFFFF);
        drawList.AddText(fontMgr, "The viewer now loads large .catalystactor files in the background.", 34.0f, viewerTop + 86.0f, 0xFFB8B8B8);
        return;
    }

    if (!renderer->m_actorViewerAsset || !renderer->m_actorViewerAsset->mesh) {
        drawList.AddRectFilled(0, 0, w, h, 0xFF141414);
        const char* closeLabel = renderer->IsStandaloneActorViewerWindow() ? "Close Viewer" : "Back To Editor";
        if (uiCtx.Button(closeLabel, w - 182.0f, 8.0f, 150.0f, 28.0f, 0xFF2C2C2C, 0xFF454545, 0xFF1E1E1E)) {
            if (renderer->IsStandaloneActorViewerWindow()) {
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            } else {
                renderer->CloseActorAssetViewer();
            }
            return;
        }
        drawList.AddText(fontMgr, "Preview asset unavailable.", 30.0f, 50.0f, 0xFFFFFFFF);
        return;
    }

    drawList.AddRectFilled(0, 0, w, menuBarH, 0xFF111111);
    drawList.AddRectFilled(0, menuBarH, w, tabBarH, 0xFF191919);
    drawList.AddRectFilled(0, menuBarH + tabBarH, w, toolbarH, 0xFF202020);
    drawList.AddRectFilled(viewportW, viewerTop, rightPanelW, h - viewerTop, 0xFF1A1A1A);
    drawList.AddRectFilled(viewportW, viewerTop, rightPanelW, 34.0f, 0xFF121212);

    float menuX = 14.0f;
    const char* viewerMenus[] = {"File", "Edit", "Asset", "Collision", "Window", "Tools", "Help"};
    for (const char* menu : viewerMenus) {
        uiCtx.Button(menu, menuX, 0.0f, 78.0f, menuBarH, 0x00000000, 0xFF232323, 0xFF303030);
        menuX += 78.0f;
    }

    drawList.AddRectFilled(18.0f, menuBarH + 6.0f, 26.0f, 24.0f, 0xFF0B8FB3);
    drawList.AddText(fontMgr, "SM_" + State.actorViewerTitle, 54.0f, menuBarH + 24.0f, 0xFFFFFFFF);
    const char* closeLabel = renderer->IsStandaloneActorViewerWindow() ? "Close Viewer" : "Back To Editor";
    if (uiCtx.Button(closeLabel, w - 182.0f, menuBarH + 4.0f, 150.0f, 28.0f, 0xFF2C2C2C, 0xFF454545, 0xFF1E1E1E)) {
        if (renderer->IsStandaloneActorViewerWindow()) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        } else {
            renderer->CloseActorAssetViewer();
        }
        return;
    }

    if (uiCtx.Button("Reimport Mesh", 16.0f, menuBarH + tabBarH + 4.0f, 118.0f, 26.0f, 0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
        renderer->OpenActorAssetViewer(State.actorViewerPath);
    }
    if (uiCtx.Button("Focus", 142.0f, menuBarH + tabBarH + 4.0f, 74.0f, 26.0f, 0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
        State.actorViewerYaw = 0.65f;
        State.actorViewerPitch = -0.28f;
        State.actorViewerDistance = (std::max)(2.2f, renderer->m_actorViewerBoundsRadius * 3.4f);
    }
    if (uiCtx.Button("-", 224.0f, menuBarH + tabBarH + 4.0f, 28.0f, 26.0f, 0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
        State.actorViewerDistance = (std::max)(0.6f, State.actorViewerDistance - (std::max)(0.25f, State.actorViewerDistance * 0.08f));
    }
    if (uiCtx.Button("+", 258.0f, menuBarH + tabBarH + 4.0f, 28.0f, 26.0f, 0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
        State.actorViewerDistance += (std::max)(0.25f, State.actorViewerDistance * 0.08f);
    }
    uiCtx.Button("Perspective", viewportW - 276.0f, menuBarH + tabBarH + 4.0f, 108.0f, 26.0f, 0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E);
    uiCtx.Button("Lit", viewportW - 160.0f, menuBarH + tabBarH + 4.0f, 50.0f, 26.0f, 0xFF544B2B, 0xFF6A6037, 0xFF3B341C);
    uiCtx.Button("Details", viewportW - 102.0f, menuBarH + tabBarH + 4.0f, 86.0f, 26.0f, 0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E);

    drawList.AddRectFilled(10.0f, viewerTop + 14.0f, 320.0f, 168.0f, 0xA5131313);
    if (State.actorViewerShowStats) {
        const uint32_t triangleCount = renderer->m_actorViewerAsset->mesh->GetIndexCount() / 3;
        const uint32_t vertexCount = renderer->m_actorViewerAsset->mesh->GetIndexCount();
        drawList.AddText(fontMgr, "Preview Stats", 20.0f, viewerTop + 34.0f, 0xFFFFFFFF);
        drawList.AddText(fontMgr, "Asset: " + FitName(State.actorViewerTitle, 28), 20.0f, viewerTop + 58.0f, 0xFFD8D8D8);
        drawList.AddText(fontMgr, "Triangles: " + std::to_string(triangleCount), 20.0f, viewerTop + 82.0f, 0xFFD8D8D8);
        drawList.AddText(fontMgr, "Vertices: " + std::to_string(vertexCount), 20.0f, viewerTop + 106.0f, 0xFFD8D8D8);
        drawList.AddText(fontMgr, "Bounds Radius: " + FormatFloat(renderer->m_actorViewerBoundsRadius), 20.0f, viewerTop + 130.0f, 0xFFD8D8D8);
        drawList.AddText(fontMgr, "LMB orbit  Mouse wheel zoom", 20.0f, viewerTop + 156.0f, 0xFFB0B0B0);
    } else {
        drawList.AddText(fontMgr, "Preview stats hidden", 20.0f, viewerTop + 42.0f, 0xFFB0B0B0);
    }

    const float panelX = viewportW;
    const float detailTop = viewerTop;
    const float socketTop = h - socketPanelH;
    float cursorY = detailTop + 46.0f;
    const float rowX = panelX + 14.0f;
    const float rowW = rightPanelW - 28.0f;

    drawList.AddText(fontMgr, "Details", panelX + 14.0f, detailTop + 22.0f, 0xFFFFFFFF);
    drawList.AddRectFilled(rowX, cursorY, rowW, 28.0f, 0xFF111111);
    drawList.AddText(fontMgr, State.actorViewerSearch.empty() ? "Search" : State.actorViewerSearch, rowX + 10.0f, cursorY + 19.0f, State.actorViewerSearch.empty() ? 0xFF777777 : 0xFFFFFFFF);
    cursorY += 42.0f;

    auto DrawSection = [&](const std::string& title) {
        drawList.AddRectFilled(panelX, cursorY, rightPanelW, 24.0f, 0xFF2A2A2A);
        drawList.AddText(fontMgr, title, panelX + 12.0f, cursorY + 16.0f, 0xFFFFFFFF);
        cursorY += 32.0f;
    };

    DrawSection("Material Slots");
    drawList.AddText(fontMgr, "Element 0", rowX, cursorY + 14.0f, 0xFFBEBEBE);
    const std::string viewerMaterialName = renderer->m_actorViewerMaterial ? renderer->m_actorViewerMaterial->name : "Default Preview";
    drawList.AddRectFilled(rowX, cursorY + 22.0f, rowW, 42.0f, 0xFF101010);
    drawList.AddText(fontMgr, FitName(viewerMaterialName, 28), rowX + 10.0f, cursorY + 48.0f, 0xFFFFFFFF);
    cursorY += 76.0f;
    if (uiCtx.Button("Assign .catalystmat", rowX, cursorY, rowW * 0.64f, 26.0f, 0xFF242424, 0xFF3B3B3B, 0xFF1A1A1A)) {
        std::wstring chosenMaterial = BrowseForMaterialFile(hwnd);
        if (!chosenMaterial.empty()) {
            renderer->m_actorViewerMaterial = renderer->LoadMaterialAsset(chosenMaterial);
        }
    }
    if (uiCtx.Button("Clear", rowX + rowW * 0.68f, cursorY, rowW * 0.32f, 26.0f, 0xFF303030, 0xFF454545, 0xFF242424)) {
        renderer->m_actorViewerMaterial = nullptr;
    }
    cursorY += 42.0f;

    DrawSection("Preview Scene");
    uiCtx.Checkbox("Show Floor", State.actorViewerShowFloor, rowX, cursorY); cursorY += 28.0f;
    uiCtx.Checkbox("Show Sky", State.actorViewerShowSky, rowX, cursorY); cursorY += 28.0f;
    uiCtx.Checkbox("Show Stats", State.actorViewerShowStats, rowX, cursorY); cursorY += 28.0f;
    uiCtx.Checkbox("Auto Rotate", State.actorViewerAutoRotate, rowX, cursorY); cursorY += 40.0f;

    DrawSection("Camera");
    uiCtx.DragFloat("FOV", State.actorViewerFov, 0.05f, rowX, cursorY, rowW); cursorY += 28.0f;
    uiCtx.DragFloat("Zoom", State.actorViewerDistance, 0.02f, rowX, cursorY, rowW); cursorY += 38.0f;
    State.actorViewerFov = std::clamp(State.actorViewerFov, 20.0f, 85.0f);
    State.actorViewerDistance = std::clamp(State.actorViewerDistance, 0.6f, 500.0f);

    DrawSection("Mesh Info");
    drawList.AddText(fontMgr, "Source File", rowX, cursorY + 14.0f, 0xFFBBBBBB);
    cursorY += 20.0f;
    drawList.AddRectFilled(rowX, cursorY, rowW, 42.0f, 0xFF101010);
    drawList.AddText(fontMgr, FitName(fs::path(State.actorViewerPath).filename().string(), 34), rowX + 10.0f, cursorY + 26.0f, 0xFFFFFFFF);
    cursorY += 54.0f;
    drawList.AddText(fontMgr, "Extents: " + FormatFloat(renderer->m_actorViewerBoundsExtents.x) + ", " + FormatFloat(renderer->m_actorViewerBoundsExtents.y) + ", " + FormatFloat(renderer->m_actorViewerBoundsExtents.z), rowX, cursorY + 14.0f, 0xFFD8D8D8, rowW);
    cursorY += 24.0f;
    drawList.AddText(fontMgr, "Center Offset: " + FormatFloat(renderer->m_actorViewerBoundsCenter.x) + ", " + FormatFloat(renderer->m_actorViewerBoundsCenter.y) + ", " + FormatFloat(renderer->m_actorViewerBoundsCenter.z), rowX, cursorY + 14.0f, 0xFFD8D8D8, rowW);

    drawList.AddRectFilled(panelX, socketTop, rightPanelW, socketPanelH, 0xFF171717);
    drawList.AddRectFilled(panelX, socketTop, rightPanelW, 30.0f, 0xFF111111);
    drawList.AddText(fontMgr, "Socket Manager", panelX + 12.0f, socketTop + 20.0f, 0xFFFFFFFF);
    drawList.AddRectFilled(panelX + rightPanelW - 34.0f, socketTop + 6.0f, 22.0f, 18.0f, 0xFF2C4B19);
    drawList.AddText(fontMgr, "+", panelX + rightPanelW - 27.0f, socketTop + 20.0f, 0xFFFFFFFF);
    drawList.AddRectFilled(rowX, socketTop + 40.0f, rowW, 28.0f, 0xFF111111);
    drawList.AddText(fontMgr, "Search", rowX + 10.0f, socketTop + 59.0f, 0xFF777777);
    drawList.AddText(fontMgr, "SOCKETS", rowX, socketTop + 92.0f, 0xFFB0B0B0);
    drawList.AddText(fontMgr, "0 sockets", rowX, socketTop + 126.0f, 0xFF8A8A8A);
    drawList.AddText(fontMgr, "Select a socket", panelX + 118.0f, socketTop + 198.0f, 0xFF707070);

    const float axisX = 28.0f;
    const float axisY = h - 34.0f;
    drawList.AddLine(axisX, axisY, axisX + 30.0f, axisY, 3.0f, 0xFFFF5555);
    drawList.AddLine(axisX, axisY, axisX, axisY - 30.0f, 3.0f, 0xFF55FF55);
    drawList.AddLine(axisX, axisY, axisX - 18.0f, axisY + 18.0f, 3.0f, 0xFF5577FF);
    drawList.AddText(fontMgr, "X", axisX + 34.0f, axisY + 8.0f, 0xFFFF5555);
    drawList.AddText(fontMgr, "Y", axisX - 6.0f, axisY - 34.0f, 0xFF55FF55);
    drawList.AddText(fontMgr, "Z", axisX - 30.0f, axisY + 28.0f, 0xFF5577FF);

    drawList.AddRectFilled(viewportW - 1.0f, 0.0f, 1.0f, h, 0xFF000000);
    drawList.AddRectFilled(0.0f, viewerTop - 1.0f, viewportW, 1.0f, 0x33000000);
}

void EditorUI::DrawMaterialAssetEditor(DXRenderer* renderer, float w, float h) {
    auto& drawList = renderer->m_uiDrawList;
    auto& uiCtx = renderer->m_uiContext;
    auto& fontMgr = renderer->m_fontManager;
    HWND hwnd = renderer->m_hwnd;
    const bool closePromptOpen = renderer->IsClosePromptOpen();

    const float menuBarH = 28.0f;
    const float tabBarH = 36.0f;
    const float toolbarH = 34.0f;
    const float editorTop = menuBarH + tabBarH + toolbarH;
    const float rightPanelW = 380.0f;
    const float viewportW = (std::max)(1.0f, w - rightPanelW);

    if (!closePromptOpen) {
        UpdatePreviewInteraction(editorTop, viewportW, h,
                                 State.materialEditorYaw, State.materialEditorPitch, State.materialEditorDistance,
                                 State.materialEditorAutoRotate, State.materialEditorIsDragging,
                                 State.materialEditorLastMouseX, State.materialEditorLastMouseY,
                                 State.materialEditorViewportW, State.materialEditorViewportH);
    }

    Material* material = renderer->m_materialEditorMaterial;
    if (!material) {
        drawList.AddRectFilled(0, 0, w, h, 0xFF141414);
        const char* closeLabel = renderer->IsStandaloneMaterialEditorWindow() ? "Close Editor" : "Back To Editor";
        if (uiCtx.Button(closeLabel, w - 184.0f, 8.0f, 152.0f, 28.0f, 0xFF2C2C2C, 0xFF454545, 0xFF1E1E1E)) {
            if (renderer->IsStandaloneMaterialEditorWindow()) {
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            } else {
                renderer->CloseMaterialAssetEditor();
            }
            return;
        }
        drawList.AddText(fontMgr, "Material file could not be loaded.", 28.0f, 48.0f, 0xFFFFFFFF);
        return;
    }

    auto SetPreviewMesh = [&](int previewMeshIndex) {
        State.materialEditorPreviewMesh = previewMeshIndex;
        if (renderer->m_materialEditorPreviewAsset) {
            renderer->m_materialEditorPreviewAsset->mesh = renderer->m_primitives[GetMaterialPreviewMeshKey(previewMeshIndex)];
        }
    };

    drawList.AddRectFilled(0, 0, w, menuBarH, 0xFF111111);
    drawList.AddRectFilled(0, menuBarH, w, tabBarH, 0xFF191919);
    drawList.AddRectFilled(0, menuBarH + tabBarH, w, toolbarH, 0xFF202020);
    drawList.AddRectFilled(viewportW, editorTop, rightPanelW, h - editorTop, 0xFF1A1A1A);
    drawList.AddRectFilled(viewportW, editorTop, rightPanelW, 34.0f, 0xFF121212);

    float menuX = 14.0f;
    const char* menus[] = {"File", "Edit", "Asset", "Window", "Tools", "Help"};
    for (const char* menu : menus) {
        uiCtx.Button(menu, menuX, 0.0f, 78.0f, menuBarH, 0x00000000, 0xFF232323, 0xFF303030);
        menuX += 78.0f;
    }

    drawList.AddRectFilled(18.0f, menuBarH + 6.0f, 26.0f, 24.0f, 0xFFB36A0B);
    drawList.AddText(fontMgr, "M_" + State.materialEditorTitle, 54.0f, menuBarH + 24.0f, 0xFFFFFFFF);
    const char* closeLabel = renderer->IsStandaloneMaterialEditorWindow() ? "Close Editor" : "Back To Editor";
    if (uiCtx.Button(closeLabel, w - 184.0f, menuBarH + 4.0f, 152.0f, 28.0f, 0xFF2C2C2C, 0xFF454545, 0xFF1E1E1E)) {
        if (renderer->IsStandaloneMaterialEditorWindow()) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        } else {
            renderer->CloseMaterialAssetEditor();
        }
        return;
    }

    if (uiCtx.Button("Save", 16.0f, menuBarH + tabBarH + 4.0f, 74.0f, 26.0f, 0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
        renderer->SaveMaterialAssetEditor();
    }
    if (uiCtx.Button("Reload", 98.0f, menuBarH + tabBarH + 4.0f, 86.0f, 26.0f, 0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E)) {
        material->LoadFromFile(State.materialEditorPath);
        renderer->SyncMaterialTextures(*material);
        renderer->RefreshMaterialEditorSavedDocument();
    }
    if (uiCtx.Button("Sphere", 204.0f, menuBarH + tabBarH + 4.0f, 72.0f, 26.0f,
                     State.materialEditorPreviewMesh == 0 ? 0xFF5B431A : 0xFF2A2A2A,
                     0xFF3B3B3B, 0xFF1E1E1E)) {
        SetPreviewMesh(0);
    }
    if (uiCtx.Button("Cube", 284.0f, menuBarH + tabBarH + 4.0f, 64.0f, 26.0f,
                     State.materialEditorPreviewMesh == 1 ? 0xFF5B431A : 0xFF2A2A2A,
                     0xFF3B3B3B, 0xFF1E1E1E)) {
        SetPreviewMesh(1);
    }
    if (uiCtx.Button("Cylinder", 356.0f, menuBarH + tabBarH + 4.0f, 88.0f, 26.0f,
                     State.materialEditorPreviewMesh == 2 ? 0xFF5B431A : 0xFF2A2A2A,
                     0xFF3B3B3B, 0xFF1E1E1E)) {
        SetPreviewMesh(2);
    }
    uiCtx.Button("Preview", viewportW - 182.0f, menuBarH + tabBarH + 4.0f, 82.0f, 26.0f, 0xFF2A2A2A, 0xFF3B3B3B, 0xFF1E1E1E);
    uiCtx.Button("Lit", viewportW - 92.0f, menuBarH + tabBarH + 4.0f, 50.0f, 26.0f, 0xFF544B2B, 0xFF6A6037, 0xFF3B341C);

    drawList.AddRectFilled(10.0f, editorTop + 14.0f, 330.0f, 150.0f, 0xA5131313);
    if (State.materialEditorShowStats) {
        const char* previewLabel = GetMaterialPreviewMeshLabel(State.materialEditorPreviewMesh);
        drawList.AddText(fontMgr, "Material Preview", 20.0f, editorTop + 34.0f, 0xFFFFFFFF);
        drawList.AddText(fontMgr, "Material: " + FitName(State.materialEditorTitle, 28), 20.0f, editorTop + 58.0f, 0xFFD8D8D8);
        drawList.AddText(fontMgr, std::string("Preview Mesh: ") + previewLabel, 20.0f, editorTop + 82.0f, 0xFFD8D8D8);
        drawList.AddText(fontMgr, "Base Texture: " + DescribeLinkedAsset(material->albedoPath), 20.0f, editorTop + 106.0f, 0xFFD8D8D8);
        drawList.AddText(fontMgr, "Normal: " + DescribeLinkedAsset(material->normalPath), 20.0f, editorTop + 130.0f, 0xFFD8D8D8);
        drawList.AddText(fontMgr, "Mouse orbit  Wheel zoom", 20.0f, editorTop + 154.0f, 0xFFB0B0B0);
    }

    const float panelX = viewportW;
    float cursorY = editorTop + 46.0f;
    const float rowX = panelX + 14.0f;
    const float rowW = rightPanelW - 28.0f;

    drawList.AddText(fontMgr, "Details", panelX + 14.0f, editorTop + 22.0f, 0xFFFFFFFF);
    drawList.AddRectFilled(rowX, cursorY, rowW, 28.0f, 0xFF111111);
    drawList.AddText(fontMgr, State.materialEditorSearch.empty() ? "Search" : State.materialEditorSearch, rowX + 10.0f, cursorY + 19.0f, State.materialEditorSearch.empty() ? 0xFF777777 : 0xFFFFFFFF);
    cursorY += 42.0f;

    auto DrawSection = [&](const std::string& title) {
        drawList.AddRectFilled(panelX, cursorY, rightPanelW, 24.0f, 0xFF2A2A2A);
        drawList.AddText(fontMgr, title, panelX + 12.0f, cursorY + 16.0f, 0xFFFFFFFF);
        cursorY += 32.0f;
    };

    DrawSection("Base Color");
    DrawMaterialBaseColor(renderer, material, State.materialEditorPath, rowX, rowW, cursorY);
    cursorY += 10.0f;

    DrawSection("Texture Slots");
    DrawMaterialTextureSlots(renderer, hwnd, material, State.materialEditorPath, rowX, rowW, cursorY);
    cursorY += 10.0f;

    DrawSection("Preview Scene");
    uiCtx.Checkbox("Show Floor", State.materialEditorShowFloor, rowX, cursorY); cursorY += 28.0f;
    uiCtx.Checkbox("Show Sky", State.materialEditorShowSky, rowX, cursorY); cursorY += 28.0f;
    uiCtx.Checkbox("Show Stats", State.materialEditorShowStats, rowX, cursorY); cursorY += 28.0f;
    uiCtx.Checkbox("Auto Rotate", State.materialEditorAutoRotate, rowX, cursorY); cursorY += 40.0f;

    DrawSection("Camera");
    uiCtx.DragFloat("FOV", State.materialEditorFov, 0.05f, rowX, cursorY, rowW); cursorY += 28.0f;
    uiCtx.DragFloat("Zoom", State.materialEditorDistance, 0.02f, rowX, cursorY, rowW); cursorY += 40.0f;
    State.materialEditorFov = std::clamp(State.materialEditorFov, 20.0f, 85.0f);
    State.materialEditorDistance = std::clamp(State.materialEditorDistance, 0.6f, 500.0f);

    DrawSection("Asset Info");
    drawList.AddText(fontMgr, "Source File", rowX, cursorY + 14.0f, 0xFFBBBBBB);
    cursorY += 20.0f;
    drawList.AddRectFilled(rowX, cursorY, rowW, 42.0f, 0xFF101010);
    drawList.AddText(fontMgr, FitName(fs::path(State.materialEditorPath).filename().string(), 34), rowX + 10.0f, cursorY + 26.0f, 0xFFFFFFFF);
    cursorY += 56.0f;
    drawList.AddText(fontMgr, "Preview Mesh: " + std::string(GetMaterialPreviewMeshLabel(State.materialEditorPreviewMesh)), rowX, cursorY + 14.0f, 0xFFD8D8D8, rowW);
    cursorY += 24.0f;
    drawList.AddText(fontMgr, "Roughness Map: " + DescribeLinkedAsset(material->roughnessPath), rowX, cursorY + 14.0f, 0xFFD8D8D8, rowW);

    drawList.AddRectFilled(viewportW - 1.0f, 0.0f, 1.0f, h, 0xFF000000);
    drawList.AddRectFilled(0.0f, editorTop - 1.0f, viewportW, 1.0f, 0x33000000);
}


