#include "EditorUIInternal.h"
#include "../Core Render/DXRenderer.h"
#include "../EngineApp.h"
#include "../Physics/PhysicsSystem.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
using namespace EditorUIInternal;

namespace {
PhysicsBodyType NextPhysicsBodyType(PhysicsBodyType type) {
    switch (type) {
    case PhysicsBodyType::Static:
        return PhysicsBodyType::Dynamic;
    case PhysicsBodyType::Dynamic:
        return PhysicsBodyType::Kinematic;
    case PhysicsBodyType::Kinematic:
    default:
        return PhysicsBodyType::Static;
    }
}

PhysicsColliderShape NextColliderShape(PhysicsColliderShape shape) {
    switch (shape) {
    case PhysicsColliderShape::Box:
        return PhysicsColliderShape::Sphere;
    case PhysicsColliderShape::Sphere:
    default:
        return PhysicsColliderShape::Box;
    }
}

std::vector<fs::path> CollectSortedChildFolders(const fs::path& parentPath) {
    std::vector<fs::path> childFolders;
    std::error_code existsError;
    if (!fs::exists(parentPath, existsError) || existsError) {
        return childFolders;
    }

    std::error_code iterateError;
    fs::directory_iterator iter(parentPath, fs::directory_options::skip_permission_denied, iterateError);
    const fs::directory_iterator endIter;
    while (!iterateError && iter != endIter) {
        std::error_code entryError;
        if (iter->is_directory(entryError) && !entryError) {
            childFolders.push_back(iter->path());
        }

        iter.increment(iterateError);
    }

    std::sort(childFolders.begin(), childFolders.end(), [](const fs::path& left, const fs::path& right) {
        return ToLowerCopy(left.filename().wstring()) < ToLowerCopy(right.filename().wstring());
    });
    return childFolders;
}

enum class TopEditorMenu {
    File = 0,
    Edit,
    Window,
    Tools,
    Build,
    Help,
    Count
};

struct TopMenuItem {
    std::string label;
    bool enabled = true;
};
}

void EditorUI::DrawEditor(DXRenderer* renderer, float w, float h, float topH, float rightW, float bottomH) {
    auto& drawList = renderer->m_uiDrawList;
    auto& uiCtx = renderer->m_uiContext;
    auto& fontMgr = renderer->m_fontManager;
    auto& gameObjects = renderer->m_gameObjects;
    auto& assets = renderer->m_assets;
    HWND hwnd = renderer->m_hwnd;
    const bool closePromptOpen = renderer->IsClosePromptOpen();
    GameObject* selectedBlueprintObject =
        (State.selectedObj >= 0 && State.selectedObj < static_cast<int>(gameObjects.size()))
            ? &gameObjects[State.selectedObj]
            : nullptr;

    if (State.isActorViewerLoading && State.actorViewerMeshLoad &&
        State.actorViewerMeshLoad->completed.load(std::memory_order_acquire)) {
        MeshData loadedPreviewMesh = std::move(State.actorViewerMeshLoad->meshData);
        State.actorViewerMeshLoad.reset();
        State.isActorViewerLoading = false;
        if (State.showActorAssetViewer) {
            renderer->FinalizeActorAssetViewerLoad(loadedPreviewMesh, State.actorViewerPath);
        }
    }

    if (State.showMaterialAssetViewer) {
        DrawMaterialAssetEditor(renderer, w, h);
        return;
    }

    if (State.showActorAssetViewer) {
        DrawActorAssetViewer(renderer, w, h);
        return;
    }

    if (m_blueprintEditor.IsOpen()) {
        m_blueprintEditor.Draw(drawList, uiCtx, fontMgr, *g_InputManager, w, h, selectedBlueprintObject,
                              renderer->IsStandaloneBlueprintEditorWindow(), hwnd, closePromptOpen);
        return;
    }

    float outlinerH = (h - topH) * 0.45f;
    float bottomY = h - bottomH;
    float bottomW = w - rightW;
    auto OpenImportPopup = [&]() {
        std::wstring sourceFile = BrowseForAssetFile(hwnd);
        if (!sourceFile.empty() && !State.currentBrowserPath.empty()) {
            State.pendingImportPath = sourceFile;
            State.pendingImportName = std::filesystem::path(sourceFile).stem().string();
            State.showImportPopup = true;
            State.wasImportJustOpened = true;
        }
    };
    auto SetEditorStatus = [&](const std::string& message, uint32_t color, DWORD durationMs = 2600) {
        State.saveStatusMessage = message;
        State.saveStatusColor = color;
        State.saveStatusUntil = GetTickCount() + durationMs;
    };
    const bool coreModalOpen = State.showImportPopup || State.showRenamePopup || State.showDeleteItemConfirm || closePromptOpen;

    bool deleteIsDown = (GetAsyncKeyState(VK_DELETE) & 0x8000) != 0;
    if (!coreModalOpen &&
        deleteIsDown &&
        !State.deleteWasDown &&
        State.selectedObj >= 0 &&
        State.selectedObj < static_cast<int>(gameObjects.size())) {
        gameObjects.erase(gameObjects.begin() + State.selectedObj);
        State.selectedObj = -1;
    }
    State.deleteWasDown = deleteIsDown;

    if (coreModalOpen || State.showHelpPopup) {
        State.activeTopMenu = -1;
        State.showContextMenu = false;
        State.contextMenuTargetPath.clear();
        State.contextMenuTargetIsFolder = false;
    }

    if (g_InputManager->IsMouseButtonPressed(0)) {
        if (State.showContextMenu) {
            const float contextMenuW = 150.0f;
            const float contextMenuH = State.contextMenuTargetPath.empty() ? 194.0f : 75.0f;
            const float contextMenuX = (std::min)(State.contextMenuX, bottomW - contextMenuW - 10.0f);
            const float contextMenuY = (std::min)(State.contextMenuY, h - contextMenuH - 10.0f);
            if (!(State.mx >= contextMenuX && State.mx <= contextMenuX + contextMenuW && State.my >= contextMenuY && State.my <= contextMenuY + contextMenuH)) {
                State.showContextMenu = false;
                State.contextMenuTargetPath.clear();
                State.contextMenuTargetIsFolder = false;
            }
        }
    }

    drawList.AddRectFilled(0, 0, w, 25.0f, 0xFF000000); 
    const std::array<std::string, static_cast<size_t>(TopEditorMenu::Count)> topMenus = {"File", "Edit", "Window", "Tools", "Build", "Help"};
    std::array<float, static_cast<size_t>(TopEditorMenu::Count)> topMenuXs = {};
    std::array<float, static_cast<size_t>(TopEditorMenu::Count)> topMenuWidths = {};
    auto MeasureLabelWidth = [&](const std::string& text) {
        float width = 0.0f;
        for (char ch : text) {
            width += fontMgr.GetGlyph(ch).Advance;
        }
        return width;
    };

    float menuX = 10.0f;
    for (size_t menuIndex = 0; menuIndex < topMenus.size(); ++menuIndex) {
        const float menuWidth = (std::max)(56.0f, MeasureLabelWidth(topMenus[menuIndex]) + 28.0f);
        const bool isMenuActive = State.activeTopMenu == static_cast<int>(menuIndex);
        topMenuXs[menuIndex] = menuX;
        topMenuWidths[menuIndex] = menuWidth;

        if (uiCtx.Button(topMenus[menuIndex], menuX, 0.0f, menuWidth, 25.0f,
                         isMenuActive ? 0xFF2B2B2B : 0x00000000,
                         isMenuActive ? 0xFF3A3A3A : 0xFF333333,
                         0xFF555555)) {
            State.activeTopMenu = isMenuActive ? -1 : static_cast<int>(menuIndex);
            State.showContextMenu = false;
            State.contextMenuTargetPath.clear();
            State.contextMenuTargetIsFolder = false;
        }
        menuX += menuWidth + 4.0f;
    }
    const bool topMenuOverlayOpen = State.activeTopMenu >= 0 || State.showHelpPopup || closePromptOpen;
    const bool editorModalOpen = coreModalOpen || topMenuOverlayOpen;
    
    drawList.AddRectFilled(0, 25.0f, w, 40.0f, 0xFF151515); 
    if (!topMenuOverlayOpen && uiCtx.Button("Save All", 15.0f, 30.0f, 80.0f, 30.0f, 0x00000000, 0xFF333333, 0xFF444444)) {
        const bool didSave = renderer->SaveCurrentScene();
        const std::string activeMapName = State.currentMapPath.empty()
            ? "scene"
            : fs::path(State.currentMapPath).filename().string();
        SetEditorStatus(didSave ? ("Saved " + activeMapName) : "Save failed",
                        didSave ? 0xFF89D185 : 0xFFE07A7A);
    }
    
    uint32_t playBtnColor = State.isPlaying ? 0xFF222288 : 0xFF225522; 
    std::string playText = State.isPlaying ? "Stop" : "Play";
    if (!topMenuOverlayOpen && uiCtx.Button(playText, 105.0f, 30.0f, 60.0f, 30.0f, playBtnColor, 0xFF337733, 0xFF114411)) State.isPlaying = !State.isPlaying;
    if (!topMenuOverlayOpen && uiCtx.Button("Place Actors +", 175.0f, 30.0f, 130.0f, 30.0f, 0x00000000, 0xFF333333, 0xFF555555)) State.showPlaceActorsMenu = !State.showPlaceActorsMenu;
    if (!topMenuOverlayOpen && uiCtx.Button("Import", 315.0f, 30.0f, 80.0f, 30.0f, 0x00000000, 0xFF333333, 0xFF555555)) OpenImportPopup();
    if (!topMenuOverlayOpen && uiCtx.Button("Blueprint", 405.0f, 30.0f, 100.0f, 30.0f, 0x00000000, 0xFF333333, 0xFF555555)) {
        m_blueprintEditor.Open(selectedBlueprintObject);
    }

    if (!State.saveStatusMessage.empty() && GetTickCount() < State.saveStatusUntil) {
        drawList.AddText(fontMgr, State.saveStatusMessage, 520.0f, 50.0f, State.saveStatusColor, w - 540.0f);
    } else if (!State.saveStatusMessage.empty() && GetTickCount() >= State.saveStatusUntil) {
        State.saveStatusMessage.clear();
    }

    float rightX = w - rightW;
    drawList.AddRectFilled(rightX, topH, rightW, outlinerH, 0xFF1A1A1A); 
    drawList.AddRectFilled(rightX, topH, rightW, 30.0f, 0xFF0E0E0E);     
    drawList.AddText(fontMgr, "Outliner", rightX + 15.0f, topH + 20.0f, 0xFFFFFFFF);

    if (State.selectedObj >= static_cast<int>(gameObjects.size())) State.selectedObj = -1;
    
    float listY = topH + 35.0f;
    for (int i = 0; i < static_cast<int>(gameObjects.size()); ++i) {
        uint32_t btnColor = (State.selectedObj == i) ? 0xFFD77800 : 0x00000000;
        if (uiCtx.Button(gameObjects[i].name, rightX, listY, rightW, 25.0f, btnColor, 0xFF333333, 0xFF555555)) {
            State.selectedObj = i;
            State.selectedContentAsset = -1;
        }
        listY += 25.0f;
    }

    float detailsY = topH + outlinerH;
    float detailsH = h - detailsY;
    drawList.AddRectFilled(rightX, detailsY, rightW, detailsH, 0xFF1A1A1A); 
    drawList.AddRectFilled(rightX, detailsY, rightW, 30.0f, 0xFF0E0E0E);    
    drawList.AddText(fontMgr, "Details", rightX + 15.0f, detailsY + 20.0f, 0xFFFFFFFF);

    const bool hasSelectedMaterialAsset =
        State.selectedContentAsset >= 0 &&
        State.selectedContentAsset < static_cast<int>(State.discoveredAssets.size()) &&
        !State.discoveredAssets[State.selectedContentAsset].isFolder &&
        IsMaterialAssetName(State.discoveredAssets[State.selectedContentAsset].name);
    const bool hasSelectedActorAsset =
        State.selectedContentAsset >= 0 &&
        State.selectedContentAsset < static_cast<int>(State.discoveredAssets.size()) &&
        !State.discoveredAssets[State.selectedContentAsset].isFolder &&
        IsActorAssetName(State.discoveredAssets[State.selectedContentAsset].name);
    const bool hasSelectedMapAsset =
        State.selectedContentAsset >= 0 &&
        State.selectedContentAsset < static_cast<int>(State.discoveredAssets.size()) &&
        !State.discoveredAssets[State.selectedContentAsset].isFolder &&
        IsMapAssetName(State.discoveredAssets[State.selectedContentAsset].name);
    const bool hasSelectedUIBlueprintAsset =
        State.selectedContentAsset >= 0 &&
        State.selectedContentAsset < static_cast<int>(State.discoveredAssets.size()) &&
        !State.discoveredAssets[State.selectedContentAsset].isFolder &&
        IsUIBlueprintAssetName(State.discoveredAssets[State.selectedContentAsset].name);
    const bool hasSelectedBlueprintAsset =
        State.selectedContentAsset >= 0 &&
        State.selectedContentAsset < static_cast<int>(State.discoveredAssets.size()) &&
        !State.discoveredAssets[State.selectedContentAsset].isFolder &&
        IsBlueprintAssetName(State.discoveredAssets[State.selectedContentAsset].name);

    if (hasSelectedMapAsset) {
        const std::wstring mapPath = (fs::path(State.currentBrowserPath) / StringToWide(State.discoveredAssets[State.selectedContentAsset].name)).wstring();
        const std::wstring normalizedMapPath = fs::path(mapPath).lexically_normal().wstring();
        const std::wstring startupMapPath = ResolveProjectStartupScenePath(renderer->ResolveActiveProjectFilePath());
        const bool isCurrentMap = !State.currentMapPath.empty() &&
            NormalizeForCompare(fs::path(State.currentMapPath)) == NormalizeForCompare(fs::path(normalizedMapPath));
        const bool isStartupMap = !startupMapPath.empty() &&
            NormalizeForCompare(fs::path(startupMapPath)) == NormalizeForCompare(fs::path(normalizedMapPath));

        float insX = rightX + 15.0f;
        float insY = detailsY + 45.0f;
        float sW = rightW - 30.0f;

        drawList.AddRectFilled(insX, insY, sW, 30.0f, 0xFF222222);
        drawList.AddText(fontMgr, fs::path(mapPath).stem().string(), insX + 5.0f, insY + 22.0f, 0xFFFFFFFF);
        insY += 42.0f;

        drawList.AddText(fontMgr, isCurrentMap ? "Open In Editor" : "Not Open", insX, insY + 15.0f, isCurrentMap ? 0xFF89D185 : 0xFFAAAAAA);
        insY += 26.0f;
        drawList.AddText(fontMgr, isStartupMap ? "Startup Map" : "Not Startup", insX, insY + 15.0f, isStartupMap ? 0xFFD7B570 : 0xFFAAAAAA);
        insY += 34.0f;

        if (uiCtx.Button("Open Map", insX, insY, sW, 28.0f, 0xFF243224, 0xFF355035, 0xFF1B271B)) {
            const bool didOpenMap = renderer->OpenSceneMap(mapPath);
            SetEditorStatus(didOpenMap ? ("Opened " + fs::path(mapPath).filename().string()) : "Map load failed",
                            didOpenMap ? 0xFF89D185 : 0xFFE07A7A);
            if (didOpenMap) {
                State.selectedObj = -1;
            }
        }
        insY += 38.0f;

        if (uiCtx.Button("Set As Startup Map", insX, insY, sW, 28.0f, 0xFF3A3220, 0xFF5A4C2D, 0xFF2A2418)) {
            const std::wstring projectFilePath = renderer->ResolveActiveProjectFilePath();
            const bool didSetStartup = !projectFilePath.empty() && UpdateProjectStartupScene(projectFilePath, mapPath);
            SetEditorStatus(didSetStartup ? ("Startup map set to " + fs::path(mapPath).filename().string()) : "Startup map update failed",
                            didSetStartup ? 0xFF89D185 : 0xFFE07A7A,
                            3000);
        }
        insY += 40.0f;

        drawList.AddText(fontMgr, "Double-click a map in the content browser to switch levels.", insX, insY + 15.0f, 0xFF888888, sW);
    } else if (hasSelectedUIBlueprintAsset) {
        const std::wstring uiBlueprintPath = (fs::path(State.currentBrowserPath) / StringToWide(State.discoveredAssets[State.selectedContentAsset].name)).wstring();
        float insX = rightX + 15.0f;
        float insY = detailsY + 45.0f;
        float sW = rightW - 30.0f;

        drawList.AddRectFilled(insX, insY, sW, 30.0f, 0xFF222222);
        drawList.AddText(fontMgr, fs::path(uiBlueprintPath).stem().string(), insX + 5.0f, insY + 22.0f, 0xFFFFFFFF);
        insY += 44.0f;

        drawList.AddText(fontMgr, "UI Blueprint Asset", insX, insY + 15.0f, 0xFF86D7C7);
        insY += 28.0f;
        drawList.AddText(fontMgr, "Extension: .catalystuiblueprint", insX, insY + 15.0f, 0xFFAAAAAA, sW);
        insY += 26.0f;
        drawList.AddText(fontMgr, "Open this asset in the Blueprint editor to author UI graph logic and preview its designer scaffold.",
                         insX, insY + 15.0f, 0xFF888888, sW);
        insY += 34.0f;

        if (uiCtx.Button("Open UI Blueprint", insX, insY, sW, 28.0f, 0xFF204042, 0xFF2A575A, 0xFF183235)) {
            const bool didOpenBlueprint = OpenBlueprintEditorWindow(uiBlueprintPath);
            SetEditorStatus(didOpenBlueprint ? ("Opened " + fs::path(uiBlueprintPath).filename().string()) : "UI Blueprint open failed",
                            didOpenBlueprint ? 0xFF89D185 : 0xFFE07A7A);
        }
        insY += 40.0f;

        drawList.AddText(fontMgr, "Double-click a UI Blueprint tile to jump straight into the widget editor.",
                         insX, insY + 15.0f, 0xFF888888, sW);
    } else if (hasSelectedBlueprintAsset) {
        const std::wstring blueprintPath = (fs::path(State.currentBrowserPath) / StringToWide(State.discoveredAssets[State.selectedContentAsset].name)).wstring();
        float insX = rightX + 15.0f;
        float insY = detailsY + 45.0f;
        float sW = rightW - 30.0f;

        drawList.AddRectFilled(insX, insY, sW, 30.0f, 0xFF222222);
        drawList.AddText(fontMgr, fs::path(blueprintPath).stem().string(), insX + 5.0f, insY + 22.0f, 0xFFFFFFFF);
        insY += 44.0f;

        drawList.AddText(fontMgr, "Actor Blueprint Asset", insX, insY + 15.0f, 0xFF7FA8E5);
        insY += 28.0f;
        drawList.AddText(fontMgr, "Extension: .catalystblueprint", insX, insY + 15.0f, 0xFFAAAAAA, sW);
        insY += 26.0f;
        drawList.AddText(fontMgr, "Open this asset in a dedicated Blueprint editor window.", insX, insY + 15.0f, 0xFF888888, sW);
        insY += 34.0f;

        if (uiCtx.Button("Open Blueprint", insX, insY, sW, 28.0f, 0xFF243B5E, 0xFF35537B, 0xFF1C2D47)) {
            const bool didOpenBlueprint = OpenBlueprintEditorWindow(blueprintPath);
            SetEditorStatus(didOpenBlueprint ? ("Opened " + fs::path(blueprintPath).filename().string()) : "Blueprint open failed",
                            didOpenBlueprint ? 0xFF89D185 : 0xFFE07A7A);
        }
        insY += 40.0f;

        drawList.AddText(fontMgr, "Double-click a Blueprint tile, or click it again after selecting, to jump into the editor faster.",
                         insX, insY + 15.0f, 0xFF888888, sW);
    } else if (hasSelectedMaterialAsset) {
        const std::wstring materialPath = (fs::path(State.currentBrowserPath) / StringToWide(State.discoveredAssets[State.selectedContentAsset].name)).wstring();
        Material* material = renderer->LoadMaterialAsset(materialPath);
        float insX = rightX + 15.0f;
        float insY = detailsY + 45.0f;
        float sW = rightW - 30.0f;

        drawList.AddRectFilled(insX, insY, sW, 30.0f, 0xFF222222);
        drawList.AddText(fontMgr, fs::path(materialPath).stem().string(), insX + 5.0f, insY + 22.0f, 0xFFFFFFFF);
        insY += 45.0f;

        if (material) {
            drawList.AddText(fontMgr, "Material Textures", insX, insY + 15.0f, 0xFFCCCCCC);
            insY += 30.0f;
            DrawMaterialTextureSlots(renderer, hwnd, material, materialPath, insX, sW, insY);

            drawList.AddText(fontMgr, "Drag this material onto a mesh in the viewport to hot-swap it.", insX, insY + 15.0f, 0xFF888888, sW);
        } else {
            drawList.AddText(fontMgr, "Material file could not be loaded.", insX, insY + 15.0f, 0xFFCC6666);
        }
    } else if (State.selectedObj >= 0 && State.selectedObj < static_cast<int>(gameObjects.size())) {
        auto& obj = gameObjects[State.selectedObj];
        float insX = rightX + 15.0f;
        float insY = detailsY + 45.0f;
        float sW = rightW - 30.0f;
        
        drawList.AddRectFilled(insX, insY, sW, 30.0f, 0xFF222222);
        drawList.AddText(fontMgr, obj.name, insX + 5.0f, insY + 22.0f, 0xFFFFFFFF);
        insY += 45.0f;
        
        if (obj.name.find("Sky") != std::string::npos) {
            drawList.AddText(fontMgr, "Zenith Color", insX, insY + 15.0f, 0xFFCCCCCC);
            insY += 25.0f;
            uiCtx.DragFloat("Top R", obj.color.x, 0.01f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.DragFloat("Top G", obj.color.y, 0.01f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.DragFloat("Top B", obj.color.z, 0.01f, insX, insY, sW, 24.0f); insY += 35.0f;

            drawList.AddText(fontMgr, "Horizon Color", insX, insY + 15.0f, 0xFFCCCCCC);
            insY += 25.0f;
            uiCtx.DragFloat("Bot R", obj.skyHorizonColor.x, 0.01f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.DragFloat("Bot G", obj.skyHorizonColor.y, 0.01f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.DragFloat("Bot B", obj.skyHorizonColor.z, 0.01f, insX, insY, sW, 24.0f); insY += 28.0f;
        } else {
            drawList.AddText(fontMgr, "Transform", insX, insY + 15.0f, 0xFFCCCCCC);
            insY += 25.0f;
            uiCtx.DragFloat("Loc X", obj.position.x, 0.05f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.DragFloat("Loc Y", obj.position.y, 0.05f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.DragFloat("Loc Z", obj.position.z, 0.05f, insX, insY, sW, 24.0f); insY += 35.0f;

            drawList.AddText(fontMgr, "Material", insX, insY + 15.0f, 0xFFCCCCCC);
            insY += 25.0f;
            uiCtx.DragFloat("Col R", obj.color.x, 0.01f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.DragFloat("Col G", obj.color.y, 0.01f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.DragFloat("Col B", obj.color.z, 0.01f, insX, insY, sW, 24.0f); insY += 28.0f;

            std::string materialName = obj.assignedMaterial ? obj.assignedMaterial->name : "None";
            drawList.AddText(fontMgr, "Assigned Material: " + materialName, insX, insY + 15.0f, 0xFFAAAAAA, sW);
            insY += 26.0f;
            if (obj.assignedMaterial && uiCtx.Button("Clear Material Override", insX, insY, sW, 24.0f, 0xFF303030, 0xFF555555, 0xFF666666)) {
                obj.assignedMaterial = nullptr;
            }
            insY += 36.0f;

            drawList.AddText(fontMgr, "Physics", insX, insY + 15.0f, 0xFFCCCCCC);
            insY += 26.0f;

            uiCtx.Checkbox("Enable Collider", obj.physics.collider.enabled, insX, insY, 18.0f);
            insY += 28.0f;

            if (uiCtx.Button(
                    "Collider Shape: " + std::string(PhysicsColliderShapeToString(obj.physics.collider.shape)),
                    insX, insY, sW, 24.0f, 0xFF303030, 0xFF4A4A4A, 0xFF202020)) {
                obj.physics.collider.shape = NextColliderShape(obj.physics.collider.shape);
            }
            insY += 34.0f;

            const bool rigidBodyToggled = uiCtx.Checkbox("Simulate Physics", obj.physics.rigidBody.enabled, insX, insY, 18.0f);
            if (rigidBodyToggled && obj.physics.rigidBody.enabled && !obj.physics.collider.enabled) {
                obj.physics.collider.enabled = true;
            }
            if (rigidBodyToggled && obj.physics.rigidBody.enabled && obj.physics.rigidBody.bodyType == PhysicsBodyType::Static) {
                obj.physics.rigidBody.bodyType = PhysicsBodyType::Dynamic;
            }
            insY += 28.0f;

            if (uiCtx.Button(
                    "Body Type: " + std::string(PhysicsBodyTypeToString(obj.physics.rigidBody.bodyType)),
                    insX, insY, sW, 24.0f, 0xFF303030, 0xFF4A4A4A, 0xFF202020)) {
                obj.physics.rigidBody.bodyType = NextPhysicsBodyType(obj.physics.rigidBody.bodyType);
            }
            insY += 34.0f;

            uiCtx.Checkbox("Use Gravity", obj.physics.rigidBody.useGravity, insX, insY, 18.0f);
            insY += 30.0f;

            uiCtx.DragFloat("Mass", obj.physics.rigidBody.mass, 0.02f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.DragFloat("Damping", obj.physics.rigidBody.linearDamping, 0.01f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.DragFloat("Bounce", obj.physics.rigidBody.restitution, 0.01f, insX, insY, sW, 24.0f); insY += 35.0f;

            drawList.AddText(fontMgr, "Velocity", insX, insY + 15.0f, 0xFFCCCCCC);
            insY += 25.0f;
            uiCtx.DragFloat("Vel X", obj.physics.rigidBody.velocity.x, 0.02f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.DragFloat("Vel Y", obj.physics.rigidBody.velocity.y, 0.02f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.DragFloat("Vel Z", obj.physics.rigidBody.velocity.z, 0.02f, insX, insY, sW, 24.0f); insY += 35.0f;

            drawList.AddText(fontMgr, "Collider Bounds", insX, insY + 15.0f, 0xFFCCCCCC);
            insY += 25.0f;
            uiCtx.DragFloat("Off X", obj.physics.collider.centerOffset.x, 0.02f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.DragFloat("Off Y", obj.physics.collider.centerOffset.y, 0.02f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.DragFloat("Off Z", obj.physics.collider.centerOffset.z, 0.02f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.DragFloat("Ext X", obj.physics.collider.boxExtents.x, 0.02f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.DragFloat("Ext Y", obj.physics.collider.boxExtents.y, 0.02f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.DragFloat("Ext Z", obj.physics.collider.boxExtents.z, 0.02f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.DragFloat("Radius", obj.physics.collider.sphereRadius, 0.02f, insX, insY, sW, 24.0f); insY += 28.0f;
            uiCtx.Checkbox("Trigger Volume", obj.physics.collider.isTrigger, insX, insY, 18.0f);
            insY += 32.0f;

            const size_t blueprintFieldCount =
                GetRigidBodyBlueprintFields().size() + GetColliderBlueprintFields().size();
            const fs::path assignedBlueprintPath = obj.blueprintAssetPath.empty()
                ? fs::path()
                : fs::path(obj.blueprintAssetPath).lexically_normal();
            std::error_code blueprintExistsError;
            const bool hasAssignedBlueprint = !obj.blueprintAssetPath.empty();
            const bool assignedBlueprintExists = hasAssignedBlueprint && fs::exists(assignedBlueprintPath, blueprintExistsError);
            const std::string assignedBlueprintName = hasAssignedBlueprint
                ? ToDisplayString(assignedBlueprintPath.filename().wstring())
                : "None";
            drawList.AddText(fontMgr,
                             std::to_string(blueprintFieldCount) + " optional physics field nodes can be added from the Blueprint action menu.",
                             insX, insY + 15.0f, 0xFF888888, sW);
            insY += 28.0f;

            drawList.AddText(fontMgr, "Assigned Blueprint: " + assignedBlueprintName,
                             insX, insY + 15.0f, hasAssignedBlueprint ? 0xFFAAAAAA : 0xFF888888, sW);
            insY += 26.0f;

            if (hasAssignedBlueprint) {
                drawList.AddText(fontMgr,
                                 assignedBlueprintExists
                                     ? (obj.blueprintPlayerControlled
                                            ? "Player Character is active. This object uses WASD in Play mode."
                                            : "Add the Player Character node to make this object controllable in Play mode.")
                                     : "The assigned Blueprint asset could not be found.",
                                 insX, insY + 15.0f,
                                 assignedBlueprintExists
                                     ? (obj.blueprintPlayerControlled ? 0xFF89D185 : 0xFF7FA8E5)
                                     : 0xFFE07A7A,
                                 sW);
            } else {
                drawList.AddText(fontMgr,
                                 "Assign a .catalystblueprint asset to drive this object's gameplay behavior.",
                                 insX, insY + 15.0f, 0xFF7FA8E5, sW);
            }
            insY += 34.0f;

            if (uiCtx.Button("Assign Blueprint Asset", insX, insY, sW, 28.0f, 0xFF243B5E, 0xFF35537B, 0xFF1C2D47)) {
                const std::wstring selectedBlueprintPath = BrowseForBlueprintFile(hwnd);
                if (!selectedBlueprintPath.empty()) {
                    const fs::path normalizedBlueprintPath = fs::path(selectedBlueprintPath).lexically_normal();
                    if (ToLowerCopy(normalizedBlueprintPath.extension().wstring()) == L".catalystblueprint") {
                        obj.blueprintAssetPath = normalizedBlueprintPath.wstring();
                        renderer->RefreshObjectBlueprintRuntime(obj);
                        SetEditorStatus("Assigned " + ToDisplayString(normalizedBlueprintPath.filename().wstring()),
                                        0xFF89D185);
                    } else {
                        SetEditorStatus("Pick a .catalystblueprint asset", 0xFFE07A7A);
                    }
                }
            }
            insY += 38.0f;

            const std::string openBlueprintLabel = hasAssignedBlueprint ? "Open Assigned Blueprint" : "Open Blueprint Editor";
            if (uiCtx.Button(openBlueprintLabel, insX, insY, sW, 28.0f, 0xFF243B5E, 0xFF35537B, 0xFF1C2D47)) {
                if (hasAssignedBlueprint) {
                    const bool didOpenBlueprint = assignedBlueprintExists && OpenBlueprintEditorWindow(assignedBlueprintPath.wstring());
                    SetEditorStatus(didOpenBlueprint ? ("Opened " + assignedBlueprintName)
                                                     : (assignedBlueprintExists ? "Blueprint open failed" : "Assigned Blueprint is missing"),
                                    didOpenBlueprint ? 0xFF89D185 : 0xFFE07A7A);
                } else {
                    m_blueprintEditor.Open(&obj);
                }
            }
            insY += 38.0f;

            if (hasAssignedBlueprint && uiCtx.Button("Clear Blueprint Asset", insX, insY, sW, 24.0f, 0xFF303030, 0xFF555555, 0xFF666666)) {
                obj.blueprintAssetPath.clear();
                renderer->RefreshObjectBlueprintRuntime(obj);
                SetEditorStatus("Cleared Blueprint assignment", 0xFFB7BEC8);
            }
            insY += hasAssignedBlueprint ? 34.0f : 0.0f;

            if (State.isPlaying) {
                drawList.AddText(fontMgr,
                                 "Play mode restores the authored scene when you stop.",
                                 insX, insY + 15.0f, 0xFF6FA4FF, sW);
            }

            obj.physics.rigidBody.mass = (std::max)(0.001f, obj.physics.rigidBody.mass);
            obj.physics.rigidBody.linearDamping = (std::max)(0.0f, obj.physics.rigidBody.linearDamping);
            obj.physics.rigidBody.restitution = (std::clamp)(obj.physics.rigidBody.restitution, 0.0f, 1.0f);
            obj.physics.collider.boxExtents.x = (std::max)(0.05f, obj.physics.collider.boxExtents.x);
            obj.physics.collider.boxExtents.y = (std::max)(0.05f, obj.physics.collider.boxExtents.y);
            obj.physics.collider.boxExtents.z = (std::max)(0.05f, obj.physics.collider.boxExtents.z);
            obj.physics.collider.sphereRadius = (std::max)(0.05f, obj.physics.collider.sphereRadius);
        }
    }

    drawList.AddRectFilled(0, bottomY, bottomW, bottomH, 0xFF151515);
    drawList.AddRectFilled(0, bottomY, bottomW, 30.0f, 0xFF0E0E0E); 
    
    const fs::path assetsRootPath = fs::path(State.currentProjectFolder) / L"Assets";
    const std::wstring rootAssetsPath = assetsRootPath.wstring();
    auto OpenBrowserFolder = [&](const fs::path& targetPath) {
        State.currentBrowserPath = targetPath.lexically_normal().wstring();
        State.lastScanTime = 0;
        State.selectedContentAsset = -1;
        State.lastClickedIndex = -1;
        State.draggedAssetIndex = -1;
        State.pendingDragAssetIndex = -1;
        State.pendingDragWasSelected = false;
        State.showContextMenu = false;
        State.contextMenuTargetPath.clear();
        State.contextMenuTargetIsFolder = false;
    };
    const float browserHeaderX = 15.0f;
    const float backButtonW = 80.0f;
    const float backButtonX = bottomW - backButtonW - 12.0f;
    const bool showBackButton = State.currentBrowserPath != rootAssetsPath && !State.currentBrowserPath.empty();
    const float browserHeaderMaxW = showBackButton ? (backButtonX - browserHeaderX - 12.0f) : (bottomW - browserHeaderX - 15.0f);
    const std::string currentPathStr = BuildBrowserPathLabel(State.currentBrowserPath, State.currentProjectFolder);
    const std::string browserHeaderLabel = FitTextToWidth(fontMgr, "Content Browser - " + currentPathStr, browserHeaderMaxW);
    drawList.AddText(fontMgr, browserHeaderLabel, browserHeaderX, bottomY + 20.0f, 0xFFFFFFFF);

    if (showBackButton) {
        if (uiCtx.Button("<- Back", backButtonX, bottomY + 3.0f, backButtonW, 24.0f, 0xFF444444, 0xFF666666, 0xFF888888)) {
            OpenBrowserFolder(fs::path(State.currentBrowserPath).parent_path());
        }
    }

    const float treePanelX = 10.0f;
    const float treePanelY = bottomY + 40.0f;
    const float treePanelW = 180.0f;
    const float treePanelH = bottomH - 50.0f;
    drawList.AddRectFilled(treePanelX, treePanelY, treePanelW, treePanelH, 0xFF1A1A1A);
    drawList.AddRectFilled(treePanelX, treePanelY, treePanelW, 28.0f, 0xFF131313);
    drawList.AddText(fontMgr, "Folders", treePanelX + 12.0f, treePanelY + 18.0f, 0xFFFFFFFF);

    float treeCursorY = treePanelY + 34.0f;
    const float treeRowH = 24.0f;
    const float treeMaxY = treePanelY + treePanelH - treeRowH - 4.0f;
    const std::wstring normalizedCurrentBrowserPath = fs::path(State.currentBrowserPath).lexically_normal().wstring();
    auto DrawTreeRow = [&](int depth, const std::string& label, const fs::path& targetPath, bool expanded, bool hasChildren) -> bool {
        if (treeCursorY > treeMaxY) {
            return false;
        }

        const std::wstring normalizedTargetPath = targetPath.lexically_normal().wstring();
        const bool isSelected = NormalizeForCompare(fs::path(normalizedCurrentBrowserPath)) == NormalizeForCompare(fs::path(normalizedTargetPath));
        const bool isOnCurrentBranch = IsPathWithinRoot(fs::path(normalizedCurrentBrowserPath), targetPath);
        const uint32_t rowColor = isSelected ? 0xFF29456B : (isOnCurrentBranch ? 0xFF222A32 : 0x00000000);
        const uint32_t hoverColor = isSelected ? 0xFF35537B : 0xFF242424;
        const uint32_t clickColor = isSelected ? 0xFF1D324E : 0xFF1B1B1B;
        const float rowX = treePanelX + 4.0f;
        const float rowW = treePanelW - 8.0f;
        const float indentX = rowX + 8.0f + static_cast<float>(depth) * 14.0f;
        const std::string arrow = hasChildren ? (expanded ? "v" : ">") : "-";

        if (uiCtx.Button("", rowX, treeCursorY, rowW, treeRowH, rowColor, hoverColor, clickColor)) {
            OpenBrowserFolder(targetPath);
        }

        drawList.AddText(fontMgr, arrow, indentX, treeCursorY + 16.0f, isSelected ? 0xFFFFFFFF : 0xFF8E8E8E);
        drawList.AddText(fontMgr, FitTextToWidth(fontMgr, label, rowW - (indentX - rowX) - 16.0f),
                         indentX + 12.0f, treeCursorY + 16.0f,
                         isSelected ? 0xFFFFFFFF : (isOnCurrentBranch ? 0xFFD0D7E3 : 0xFFAAAAAA));

        treeCursorY += treeRowH;
        return true;
    };

    if (!State.currentProjectFolder.empty() && fs::exists(assetsRootPath)) {
        const std::string projectLabel = ToDisplayString(fs::path(State.currentProjectFolder).filename().wstring());
        const std::vector<fs::path> assetChildFolders = CollectSortedChildFolders(assetsRootPath);
        drawList.AddText(fontMgr, "v", treePanelX + 12.0f, treeCursorY + 16.0f, 0xFF8E8E8E);
        drawList.AddText(fontMgr, FitTextToWidth(fontMgr, projectLabel.empty() ? "Project" : projectLabel, treePanelW - 36.0f),
                         treePanelX + 24.0f, treeCursorY + 16.0f, 0xFFD0D7E3);
        treeCursorY += treeRowH;
        DrawTreeRow(1, "Assets", assetsRootPath, true, !assetChildFolders.empty());

        std::function<void(const fs::path&, int)> DrawFolderBranch = [&](const fs::path& folderPath, int depth) {
            if (treeCursorY > treeMaxY) {
                return;
            }

            const bool isOnCurrentBranch = IsPathWithinRoot(fs::path(normalizedCurrentBrowserPath), folderPath);
            const bool shouldExpand = isOnCurrentBranch;
            const std::vector<fs::path> childFolders = CollectSortedChildFolders(folderPath);
            const bool didDraw = DrawTreeRow(depth, ToDisplayString(folderPath.filename().wstring()), folderPath,
                                             shouldExpand, !childFolders.empty());
            if (!didDraw || !shouldExpand) {
                return;
            }

            for (const fs::path& childFolder : childFolders) {
                DrawFolderBranch(childFolder, depth + 1);
                if (treeCursorY > treeMaxY) {
                    break;
                }
            }
        };

        for (const fs::path& childFolder : assetChildFolders) {
            DrawFolderBranch(childFolder, 2);
            if (treeCursorY > treeMaxY) {
                break;
            }
        }
    } else {
        drawList.AddText(fontMgr, "No Assets folder found.", treePanelX + 12.0f, treePanelY + 56.0f, 0xFFAAAAAA);
    }
    
    if (GetTickCount() - State.lastScanTime > 2000) {
        State.discoveredAssets.clear();
        std::error_code browseError;
        if (!State.currentBrowserPath.empty() && std::filesystem::exists(State.currentBrowserPath, browseError) && !browseError) {
            std::filesystem::directory_iterator iter(State.currentBrowserPath, std::filesystem::directory_options::skip_permission_denied, browseError);
            const std::filesystem::directory_iterator endIter;
            while (!browseError && iter != endIter) {
                const auto& entry = *iter;
                std::error_code entryError;
                const std::wstring extension = ToLowerCopy(entry.path().extension().wstring());
                if (entry.is_directory(entryError) && !entryError) {
                    State.discoveredAssets.push_back({entry.path().filename().string(), true});
                } else if (extension == L".catalystactor" || extension == L".catalystmat" || extension == L".catalystmap" ||
                           extension == L".catalystblueprint" || extension == L".catalystuiblueprint" ||
                           extension == L".cs" || extension == L".png" || extension == L".jpg" || extension == L".jpeg") {
                    State.discoveredAssets.push_back({entry.path().filename().string(), false});
                }

                iter.increment(browseError);
            }
        }
        std::sort(State.discoveredAssets.begin(), State.discoveredAssets.end(), [](const BrowserItem& left, const BrowserItem& right) {
            if (left.isFolder != right.isFolder) {
                return left.isFolder > right.isFolder;
            }

            return ToLowerCopy(StringToWide(left.name)) < ToLowerCopy(StringToWide(right.name));
        });
        State.lastScanTime = GetTickCount();
    }

    auto BeginRenameItem = [&](int itemIndex) {
        if (itemIndex < 0 || itemIndex >= static_cast<int>(State.discoveredAssets.size())) {
            return;
        }

        const BrowserItem& item = State.discoveredAssets[itemIndex];
        State.selectedContentAsset = itemIndex;
        State.renameTargetPath = (fs::path(State.currentBrowserPath) / StringToWide(item.name)).wstring();
        State.renameTargetIsFolder = item.isFolder;
        State.renameInputName = GetBrowserItemDisplayName(item.name, item.isFolder);
        State.isRenameInputActive = true;
        State.showRenamePopup = true;
        State.showContextMenu = false;
        State.contextMenuTargetPath.clear();
        State.contextMenuTargetIsFolder = false;
    };

    auto BeginRenameCreatedPath = [&](const std::wstring& createdPath, bool isFolder) {
        if (createdPath.empty()) {
            return;
        }

        State.selectedContentAsset = -1;
        State.lastClickedIndex = -1;
        State.renameTargetPath = createdPath;
        State.renameTargetIsFolder = isFolder;
        State.renameInputName = GetBrowserItemDisplayName(fs::path(createdPath).filename().string(), isFolder);
        State.isRenameInputActive = true;
        State.showRenamePopup = true;
        State.showContextMenu = false;
        State.contextMenuTargetPath.clear();
        State.contextMenuTargetIsFolder = false;
    };

    auto ClearDeleteItemState = [&]() {
        State.showDeleteItemConfirm = false;
        State.deleteItemPath.clear();
        State.deleteItemName.clear();
        State.deleteItemIsFolder = false;
    };

    auto BeginDeleteItemPath = [&](const std::wstring& targetPath, bool isFolder) {
        if (targetPath.empty()) {
            return;
        }

        State.deleteItemPath = fs::path(targetPath).lexically_normal().wstring();
        State.deleteItemName = fs::path(targetPath).filename().wstring();
        State.deleteItemIsFolder = isFolder;
        State.showDeleteItemConfirm = true;
        State.showContextMenu = false;
        State.contextMenuTargetPath.clear();
        State.contextMenuTargetIsFolder = false;
    };

    auto CommitRenameItem = [&]() {
        if (State.renameTargetPath.empty()) {
            SetEditorStatus("Rename failed", 0xFFE07A7A);
            return;
        }

        const std::wstring originalPath = fs::path(State.renameTargetPath).lexically_normal().wstring();
        const std::wstring projectFilePath = renderer->ResolveActiveProjectFilePath();
        const std::wstring startupScenePath = projectFilePath.empty() ? L"" : fs::path(ResolveProjectStartupScenePath(projectFilePath)).lexically_normal().wstring();

        std::wstring renamedPath;
        const bool didRename = RenameBrowserEntry(originalPath, State.renameInputName, State.renameTargetIsFolder, &renamedPath);
        if (didRename) {
            State.currentMapPath = RemapRenamedPath(State.currentMapPath, originalPath, renamedPath);
            State.currentBrowserPath = RemapRenamedPath(State.currentBrowserPath, originalPath, renamedPath);

            if (!startupScenePath.empty()) {
                const std::wstring remappedStartupScene = RemapRenamedPath(startupScenePath, originalPath, renamedPath);
                if (remappedStartupScene != startupScenePath) {
                    UpdateProjectStartupScene(projectFilePath, remappedStartupScene);
                }
            }

            State.lastScanTime = 0;
            State.lastClickedIndex = -1;
            State.selectedContentAsset = -1;
            SetEditorStatus("Renamed to " + GetBrowserItemDisplayName(fs::path(renamedPath).filename().string(), State.renameTargetIsFolder), 0xFF89D185);
        } else {
            SetEditorStatus("Rename failed", 0xFFE07A7A);
        }

        State.showRenamePopup = false;
        State.renameTargetPath.clear();
        State.renameTargetIsFolder = false;
        State.renameInputName.clear();
        State.isRenameInputActive = false;
    };

    auto CleanupDeletedWorldReferences = [&](const std::wstring& deletedRootPath) {
        if (deletedRootPath.empty()) {
            return 0;
        }

        const fs::path deletedRoot = fs::path(deletedRootPath).lexically_normal();
        auto MatchesDeletedRoot = [&](const std::wstring& candidatePath) {
            return !candidatePath.empty() && IsPathWithinRoot(fs::path(candidatePath), deletedRoot);
        };
        auto AssetMatchesDeletedRoot = [&](const Asset* asset) {
            if (!asset || asset->sourcePath.empty()) {
                return false;
            }

            return MatchesDeletedRoot(StringToWide(asset->sourcePath));
        };
        auto MaterialMatchesDeletedRoot = [&](const Material* material) {
            return material != nullptr && MatchesDeletedRoot(renderer->GetCachedMaterialPath(material));
        };
        auto TextureMatchesDeletedRoot = [&](const Texture* texture) {
            return texture != nullptr && MatchesDeletedRoot(renderer->GetCachedTexturePath(texture));
        };
        auto ClearMaterialDeletedTextures = [&](Material* material) {
            if (!material) {
                return;
            }

            if (MatchesDeletedRoot(material->ResolveLinkedTexturePath(material->albedoPath))) {
                material->albedoTexture = nullptr;
            }
            if (MatchesDeletedRoot(material->ResolveLinkedTexturePath(material->normalPath))) {
                material->normalTexture = nullptr;
            }
            if (MatchesDeletedRoot(material->ResolveLinkedTexturePath(material->roughnessPath))) {
                material->roughnessTexture = nullptr;
            }
        };
        auto PruneObjectCollection = [&](std::vector<GameObject>& objects) {
            const size_t beforeCount = objects.size();
            objects.erase(std::remove_if(objects.begin(), objects.end(), [&](GameObject& object) {
                const bool removeObject =
                    AssetMatchesDeletedRoot(object.asset) ||
                    MatchesDeletedRoot(object.blueprintAssetPath);
                if (removeObject) {
                    return true;
                }

                if (MaterialMatchesDeletedRoot(object.assignedMaterial)) {
                    object.assignedMaterial = nullptr;
                }
                if (TextureMatchesDeletedRoot(object.overrideAlbedo)) {
                    object.overrideAlbedo = nullptr;
                }
                if (TextureMatchesDeletedRoot(object.overrideNormal)) {
                    object.overrideNormal = nullptr;
                }
                if (TextureMatchesDeletedRoot(object.overrideMetallic)) {
                    object.overrideMetallic = nullptr;
                }
                if (TextureMatchesDeletedRoot(object.overrideRoughness)) {
                    object.overrideRoughness = nullptr;
                }
                if (TextureMatchesDeletedRoot(object.overrideAO)) {
                    object.overrideAO = nullptr;
                }

                return false;
            }), objects.end());
            return static_cast<int>(beforeCount - objects.size());
        };

        if (State.showActorAssetViewer && MatchesDeletedRoot(State.actorViewerPath)) {
            renderer->CloseActorAssetViewer();
        }
        if (State.showMaterialAssetViewer && MatchesDeletedRoot(State.materialEditorPath)) {
            renderer->CloseMaterialAssetEditor();
        }
        if (m_blueprintEditor.HasOpenAsset() && MatchesDeletedRoot(m_blueprintEditor.GetAssetPath())) {
            renderer->CloseBlueprintAssetEditor();
        }

        ClearMaterialDeletedTextures(renderer->m_materialEditorMaterial);
        ClearMaterialDeletedTextures(renderer->m_actorViewerMaterial);

        int removedWorldObjects = PruneObjectCollection(gameObjects);
        PruneObjectCollection(renderer->m_playModeSnapshot);

        for (const auto& asset : renderer->m_assets) {
            if (!asset) {
                continue;
            }

            if (TextureMatchesDeletedRoot(asset->albedoMap)) {
                asset->albedoMap = nullptr;
            }
            if (TextureMatchesDeletedRoot(asset->normalMap)) {
                asset->normalMap = nullptr;
            }
            if (TextureMatchesDeletedRoot(asset->metallicMap)) {
                asset->metallicMap = nullptr;
            }
            if (TextureMatchesDeletedRoot(asset->roughnessMap)) {
                asset->roughnessMap = nullptr;
            }
            if (TextureMatchesDeletedRoot(asset->aoMap)) {
                asset->aoMap = nullptr;
            }
            if (TextureMatchesDeletedRoot(asset->hdrTexture)) {
                asset->hdrTexture = nullptr;
            }
        }

        for (auto materialIt = renderer->m_materialCache.begin(); materialIt != renderer->m_materialCache.end();) {
            if (MatchesDeletedRoot(materialIt->first)) {
                materialIt = renderer->m_materialCache.erase(materialIt);
                continue;
            }

            ClearMaterialDeletedTextures(materialIt->second.get());
            ++materialIt;
        }

        for (auto textureIt = renderer->m_textureCache.begin(); textureIt != renderer->m_textureCache.end();) {
            if (MatchesDeletedRoot(textureIt->first)) {
                textureIt = renderer->m_textureCache.erase(textureIt);
            } else {
                ++textureIt;
            }
        }

        for (auto assetIt = renderer->m_assets.begin(); assetIt != renderer->m_assets.end();) {
            const std::shared_ptr<Asset>& asset = *assetIt;
            if (!asset || asset->sourcePath.empty() || !MatchesDeletedRoot(StringToWide(asset->sourcePath))) {
                ++assetIt;
                continue;
            }

            Mesh* meshToDelete = asset->mesh;
            for (auto primitiveIt = renderer->m_primitives.begin(); primitiveIt != renderer->m_primitives.end();) {
                if (primitiveIt->second == meshToDelete) {
                    delete primitiveIt->second;
                    primitiveIt = renderer->m_primitives.erase(primitiveIt);
                } else {
                    ++primitiveIt;
                }
            }

            assetIt = renderer->m_assets.erase(assetIt);
        }

        if (removedWorldObjects > 0 || State.selectedObj >= static_cast<int>(gameObjects.size())) {
            State.selectedObj = -1;
        }

        renderer->m_physicsSystem.Reset();
        return removedWorldObjects;
    };

    auto CommitDeleteItem = [&]() {
        if (State.deleteItemPath.empty()) {
            SetEditorStatus("Delete failed", 0xFFE07A7A);
            ClearDeleteItemState();
            return;
        }

        const std::wstring deletePath = fs::path(State.deleteItemPath).lexically_normal().wstring();
        const std::wstring projectFilePath = renderer->ResolveActiveProjectFilePath();
        const std::wstring startupScenePath = projectFilePath.empty()
            ? L""
            : fs::path(ResolveProjectStartupScenePath(projectFilePath)).lexically_normal().wstring();
        const bool deletedCurrentMap = !State.currentMapPath.empty() && IsPathWithinRoot(fs::path(State.currentMapPath), fs::path(deletePath));
        const bool deletedStartupMap = !startupScenePath.empty() && IsPathWithinRoot(fs::path(startupScenePath), fs::path(deletePath));

        std::wstring fallbackScenePath;
        if (deletedCurrentMap) {
            fallbackScenePath = (!startupScenePath.empty() && !deletedStartupMap)
                ? startupScenePath
                : ChooseFallbackScenePath(projectFilePath, deletePath);
        } else if (deletedStartupMap) {
            fallbackScenePath = ChooseFallbackScenePath(projectFilePath, deletePath);
        }

        const std::string deletedDisplayName = GetBrowserItemDisplayName(fs::path(deletePath).filename().string(), State.deleteItemIsFolder);
        const bool didDelete = DeleteBrowserEntry(deletePath, State.deleteItemIsFolder);
        if (didDelete) {
            bool didRepairSceneRefs = true;
            int removedWorldObjects = 0;

            if (deletedStartupMap && !fallbackScenePath.empty()) {
                didRepairSceneRefs = EnsureEmptySceneMapFile(fallbackScenePath) &&
                                     UpdateProjectStartupScene(projectFilePath, fallbackScenePath);
            }

            if (deletedCurrentMap) {
                if (!fallbackScenePath.empty()) {
                    didRepairSceneRefs = didRepairSceneRefs &&
                                         EnsureEmptySceneMapFile(fallbackScenePath) &&
                                         renderer->OpenSceneMap(fallbackScenePath);
                } else {
                    State.currentMapPath.clear();
                    State.selectedObj = -1;
                }
            }

            removedWorldObjects = CleanupDeletedWorldReferences(deletePath);

            State.lastScanTime = 0;
            State.lastClickedIndex = -1;
            State.selectedContentAsset = -1;

            SetEditorStatus(didRepairSceneRefs
                                ? (removedWorldObjects > 0
                                       ? ("Deleted " + deletedDisplayName + " and removed " + std::to_string(removedWorldObjects) + " world object(s)")
                                       : ("Deleted " + deletedDisplayName))
                                : (removedWorldObjects > 0
                                       ? ("Deleted " + deletedDisplayName + ", removed " + std::to_string(removedWorldObjects) + " world object(s), with fallback warnings")
                                       : ("Deleted " + deletedDisplayName + " with fallback warnings")),
                            didRepairSceneRefs ? 0xFF89D185 : 0xFFD7B570,
                            didRepairSceneRefs ? 2600 : 3400);
        } else {
            SetEditorStatus("Delete failed", 0xFFE07A7A);
        }

        ClearDeleteItemState();
    };

    const std::wstring activeProjectFilePath = renderer->ResolveActiveProjectFilePath();
    const std::wstring startupScenePath = activeProjectFilePath.empty()
        ? L""
        : fs::path(ResolveProjectStartupScenePath(activeProjectFilePath)).lexically_normal().wstring();
    const bool hasSelectedBrowserItem =
        State.selectedContentAsset >= 0 &&
        State.selectedContentAsset < static_cast<int>(State.discoveredAssets.size());
    const bool selectedBrowserItemIsFolder = hasSelectedBrowserItem && State.discoveredAssets[State.selectedContentAsset].isFolder;
    const std::wstring selectedBrowserItemPath = hasSelectedBrowserItem
        ? (fs::path(State.currentBrowserPath) / StringToWide(State.discoveredAssets[State.selectedContentAsset].name)).wstring()
        : L"";
    const bool hasSelectedObjectBlueprint =
        selectedBlueprintObject != nullptr && !selectedBlueprintObject->blueprintAssetPath.empty();

    auto BeginProjectOpen = [&](const std::wstring& projectFilePath, const std::wstring& projectName) {
        if (projectFilePath.empty()) {
            return;
        }

        AddRecentProject(projectFilePath);
        State.currentProjectFile = projectFilePath;
        State.currentProjectFolder = fs::path(projectFilePath).parent_path().wstring();
        State.currentMapPath.clear();
        State.currentBrowserPath = (fs::path(State.currentProjectFolder) / L"Assets").wstring();
        State.triggerEditorSwap = true;
        State.editorSwapProjName = projectName;
        State.recentsLoaded = false;
        State.activeTopMenu = -1;
        State.showHelpPopup = false;
        State.showContextMenu = false;
        State.contextMenuTargetPath.clear();
        State.contextMenuTargetIsFolder = false;
        SetEditorStatus("Loading " + ToDisplayString(projectName), 0xFF89D185);
    };

    auto ReturnToLauncher = [&]() {
        renderer->CloseBlueprintAssetEditor();
        renderer->CloseActorAssetViewer();
        renderer->CloseMaterialAssetEditor();
        renderer->ClearProjectRuntimeAssets();
        renderer->ResetSceneToDefaults();
        renderer->SetEngineState(EngineState::Launcher);

        State.currentProjectFile.clear();
        State.currentProjectFolder.clear();
        State.currentMapPath.clear();
        State.currentBrowserPath.clear();
        State.discoveredAssets.clear();
        State.lastScanTime = 0;
        State.selectedObj = -1;
        State.selectedContentAsset = -1;
        State.lastClickedIndex = -1;
        State.showPlaceActorsMenu = false;
        State.showImportPopup = false;
        State.showRenamePopup = false;
        State.showDeleteItemConfirm = false;
        State.showContextMenu = false;
        State.activeTopMenu = -1;
        State.showHelpPopup = false;
        State.activeCategory = 1;
        State.recentsLoaded = false;
        State.editorSwapProjName.clear();

        const DWORD launcherStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT launcherRect = {0, 0, 1000, 650};
        AdjustWindowRect(&launcherRect, launcherStyle, FALSE);
        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);
        const int launcherW = launcherRect.right - launcherRect.left;
        const int launcherH = launcherRect.bottom - launcherRect.top;

        SetWindowLongPtr(hwnd, GWL_STYLE, launcherStyle);
        SetWindowPos(hwnd, HWND_TOP,
                     (screenW - launcherW) / 2,
                     (screenH - launcherH) / 2,
                     launcherW,
                     launcherH,
                     SWP_FRAMECHANGED);
        ShowWindow(hwnd, SW_SHOWNORMAL);
        renderer->RefreshWindowTitle();
    };

    auto CreateFolderInBrowser = [&]() {
        if (State.currentBrowserPath.empty()) {
            SetEditorStatus("Choose a folder first", 0xFFE07A7A);
            return;
        }

        std::wstring finalName = L"NewFolder";
        int counter = 1;
        const fs::path currentFolder(State.currentBrowserPath);
        while (fs::exists(currentFolder / finalName)) {
            finalName = L"NewFolder_" + std::to_wstring(counter++);
        }

        const fs::path createdPath = currentFolder / finalName;
        if (fs::create_directory(createdPath)) {
            State.lastScanTime = 0;
            BeginRenameCreatedPath(createdPath.wstring(), true);
        } else {
            SetEditorStatus("Create failed", 0xFFE07A7A);
        }
    };

    auto CreateMaterialInBrowser = [&]() {
        if (State.currentBrowserPath.empty()) {
            SetEditorStatus("Choose a folder first", 0xFFE07A7A);
            return;
        }

        std::wstring finalName = L"NewMaterial.catalystmat";
        int counter = 1;
        const fs::path currentFolder(State.currentBrowserPath);
        while (fs::exists(currentFolder / finalName)) {
            finalName = L"NewMaterial_" + std::to_wstring(counter++) + L".catalystmat";
        }

        const fs::path createdPath = currentFolder / finalName;
        Material newMaterial;
        newMaterial.name = createdPath.stem().string();
        if (newMaterial.SaveToFile(createdPath.wstring()) && fs::exists(createdPath)) {
            State.lastScanTime = 0;
            BeginRenameCreatedPath(createdPath.wstring(), false);
        } else {
            SetEditorStatus("Create failed", 0xFFE07A7A);
        }
    };

    auto CreateActorBlueprintInBrowser = [&]() {
        if (State.currentBrowserPath.empty()) {
            SetEditorStatus("Choose a folder first", 0xFFE07A7A);
            return;
        }

        std::wstring createdPath;
        if (CreateEmptyBlueprintAsset(State.currentBrowserPath, &createdPath)) {
            State.lastScanTime = 0;
            BeginRenameCreatedPath(createdPath, false);
        } else {
            SetEditorStatus("Create failed", 0xFFE07A7A);
        }
    };

    auto CreateUIBlueprintInBrowser = [&]() {
        if (State.currentBrowserPath.empty()) {
            SetEditorStatus("Choose a folder first", 0xFFE07A7A);
            return;
        }

        std::wstring createdPath;
        if (CreateEmptyUIBlueprintAsset(State.currentBrowserPath, &createdPath)) {
            State.lastScanTime = 0;
            BeginRenameCreatedPath(createdPath, false);
        } else {
            SetEditorStatus("Create failed", 0xFFE07A7A);
        }
    };

    auto OpenSelectedAssetFromMenu = [&]() {
        if (!hasSelectedBrowserItem || selectedBrowserItemIsFolder) {
            return;
        }

        bool didOpen = false;
        if (hasSelectedBlueprintAsset || hasSelectedUIBlueprintAsset) {
            didOpen = OpenBlueprintEditorWindow(selectedBrowserItemPath);
        } else if (hasSelectedActorAsset) {
            didOpen = OpenActorViewerWindow(selectedBrowserItemPath);
        } else if (hasSelectedMaterialAsset) {
            didOpen = OpenMaterialEditorWindow(selectedBrowserItemPath);
        } else if (hasSelectedMapAsset) {
            didOpen = renderer->OpenSceneMap(selectedBrowserItemPath);
        }

        if (didOpen) {
            SetEditorStatus("Opened " + GetBrowserItemDisplayName(fs::path(selectedBrowserItemPath).filename().string(), false), 0xFF89D185);
        } else {
            SetEditorStatus("Open failed", 0xFFE07A7A);
        }
    };

    auto OpenObjectBlueprintFromMenu = [&]() {
        if (!hasSelectedObjectBlueprint) {
            return;
        }

        const bool didOpenBlueprint = OpenBlueprintEditorWindow(fs::path(selectedBlueprintObject->blueprintAssetPath).lexically_normal().wstring());
        SetEditorStatus(didOpenBlueprint ? "Opened assigned Blueprint" : "Blueprint open failed",
                        didOpenBlueprint ? 0xFF89D185 : 0xFFE07A7A);
    };

    if (State.activeTopMenu >= 0) {
        const int activeMenuIndex = State.activeTopMenu;
        std::vector<TopMenuItem> menuItems;
        switch (static_cast<TopEditorMenu>(activeMenuIndex)) {
        case TopEditorMenu::File:
            menuItems = {
                {"Open Project...", true},
                {"Save Scene", !activeProjectFilePath.empty()},
                {"Open Startup Map", !startupScenePath.empty()},
                {"Return To Launcher", true},
                {"Exit Catalyst", true},
            };
            break;
        case TopEditorMenu::Edit:
            menuItems = {
                {"Rename Selected Item", hasSelectedBrowserItem},
                {"Delete Selected Item...", hasSelectedBrowserItem},
                {"Delete Selected Object", State.selectedObj >= 0},
                {"Clear Selection", State.selectedObj >= 0 || hasSelectedBrowserItem},
            };
            break;
        case TopEditorMenu::Window:
            menuItems = {
                {"Project Assets", !rootAssetsPath.empty()},
                {"Open Selected Asset", hasSelectedActorAsset || hasSelectedMaterialAsset || hasSelectedBlueprintAsset || hasSelectedUIBlueprintAsset || hasSelectedMapAsset},
                {"Open Object Blueprint", hasSelectedObjectBlueprint},
                {"Blueprint Graph", true},
            };
            break;
        case TopEditorMenu::Tools:
            menuItems = {
                {"Import Asset...", !State.currentBrowserPath.empty()},
                {"New Folder", !State.currentBrowserPath.empty()},
                {"New Map", !State.currentBrowserPath.empty()},
                {"New Material", !State.currentBrowserPath.empty()},
                {"New Blueprint", !State.currentBrowserPath.empty()},
            };
            break;
        case TopEditorMenu::Build:
            menuItems = {
                {State.isPlaying ? "Stop Play Mode" : "Play Current Map", true},
                {"Save And Play", !State.isPlaying},
                {"Set Current Map As Startup", !activeProjectFilePath.empty() && !State.currentMapPath.empty()},
                {"Open Startup Map", !startupScenePath.empty()},
            };
            break;
        case TopEditorMenu::Help:
            menuItems = {
                {"About / Shortcuts", true},
            };
            break;
        default:
            break;
        }

        float popupW = 150.0f;
        for (const TopMenuItem& item : menuItems) {
            popupW = (std::max)(popupW, MeasureLabelWidth(item.label) + 34.0f);
        }
        const float itemH = 24.0f;
        const float popupH = 8.0f + static_cast<float>(menuItems.size()) * itemH + 8.0f;
        const float popupX = (std::min)(topMenuXs[static_cast<size_t>(activeMenuIndex)], w - popupW - 10.0f);
        const float popupY = 25.0f;
        drawList.AddRectFilled(popupX, popupY, popupW, popupH, 0xFF202020);
        drawList.AddRectFilled(popupX, popupY, popupW, 2.0f, 0xFFD77800);

        int clickedMenuItem = -1;
        for (int itemIndex = 0; itemIndex < static_cast<int>(menuItems.size()); ++itemIndex) {
            const float itemY = popupY + 6.0f + itemIndex * itemH;
            if (menuItems[itemIndex].enabled) {
                if (uiCtx.Button(menuItems[itemIndex].label, popupX + 4.0f, itemY, popupW - 8.0f, itemH - 2.0f,
                                 0x00000000, 0xFF303030, 0xFF454545)) {
                    clickedMenuItem = itemIndex;
                }
            } else {
                drawList.AddRectFilled(popupX + 4.0f, itemY, popupW - 8.0f, itemH - 2.0f, 0x00000000);
                drawList.AddText(fontMgr, menuItems[itemIndex].label, popupX + 18.0f, itemY + 16.0f, 0xFF666666);
            }
        }

        if (clickedMenuItem >= 0) {
            switch (static_cast<TopEditorMenu>(activeMenuIndex)) {
            case TopEditorMenu::File:
                if (clickedMenuItem == 0) {
                    const std::wstring projectFile = BrowseForProjectFile(hwnd);
                    if (!projectFile.empty()) {
                        BeginProjectOpen(projectFile, fs::path(projectFile).stem().wstring());
                    }
                } else if (clickedMenuItem == 1) {
                    const bool didSave = renderer->SaveCurrentScene();
                    SetEditorStatus(didSave ? "Scene saved" : "Save failed", didSave ? 0xFF89D185 : 0xFFE07A7A);
                } else if (clickedMenuItem == 2) {
                    const bool didOpenMap = !startupScenePath.empty() && renderer->OpenSceneMap(startupScenePath);
                    SetEditorStatus(didOpenMap ? "Opened startup map" : "Startup map open failed",
                                    didOpenMap ? 0xFF89D185 : 0xFFE07A7A);
                } else if (clickedMenuItem == 3) {
                    ReturnToLauncher();
                } else if (clickedMenuItem == 4) {
                    PostQuitMessage(0);
                }
                break;
            case TopEditorMenu::Edit:
                if (clickedMenuItem == 0 && hasSelectedBrowserItem) {
                    BeginRenameItem(State.selectedContentAsset);
                } else if (clickedMenuItem == 1 && hasSelectedBrowserItem) {
                    BeginDeleteItemPath(selectedBrowserItemPath, selectedBrowserItemIsFolder);
                } else if (clickedMenuItem == 2 && State.selectedObj >= 0 && State.selectedObj < static_cast<int>(gameObjects.size())) {
                    const std::string deletedName = gameObjects[State.selectedObj].name;
                    gameObjects.erase(gameObjects.begin() + State.selectedObj);
                    State.selectedObj = -1;
                    SetEditorStatus("Deleted " + deletedName, 0xFF89D185);
                } else if (clickedMenuItem == 3) {
                    State.selectedObj = -1;
                    State.selectedContentAsset = -1;
                    State.lastClickedIndex = -1;
                }
                break;
            case TopEditorMenu::Window:
                if (clickedMenuItem == 0) {
                    OpenBrowserFolder(fs::path(rootAssetsPath));
                } else if (clickedMenuItem == 1) {
                    OpenSelectedAssetFromMenu();
                } else if (clickedMenuItem == 2) {
                    OpenObjectBlueprintFromMenu();
                } else if (clickedMenuItem == 3) {
                    m_blueprintEditor.Open(selectedBlueprintObject);
                }
                break;
            case TopEditorMenu::Tools:
                if (clickedMenuItem == 0) {
                    OpenImportPopup();
                } else if (clickedMenuItem == 1) {
                    CreateFolderInBrowser();
                } else if (clickedMenuItem == 2) {
                    std::wstring createdPath;
                    if (CreateEmptySceneMap(State.currentBrowserPath, &createdPath)) {
                        State.lastScanTime = 0;
                        BeginRenameCreatedPath(createdPath, false);
                    } else {
                        SetEditorStatus("Create failed", 0xFFE07A7A);
                    }
                } else if (clickedMenuItem == 3) {
                    CreateMaterialInBrowser();
                } else if (clickedMenuItem == 4) {
                    std::wstring createdPath;
                    if (CreateEmptyBlueprintAsset(State.currentBrowserPath, &createdPath)) {
                        State.lastScanTime = 0;
                        BeginRenameCreatedPath(createdPath, false);
                    } else {
                        SetEditorStatus("Create failed", 0xFFE07A7A);
                    }
                }
                break;
            case TopEditorMenu::Build:
                if (clickedMenuItem == 0) {
                    State.isPlaying = !State.isPlaying;
                    SetEditorStatus(State.isPlaying ? "Play mode enabled" : "Play mode stopped",
                                    State.isPlaying ? 0xFF89D185 : 0xFFB7BEC8);
                } else if (clickedMenuItem == 1) {
                    const bool didSave = renderer->SaveCurrentScene();
                    State.isPlaying = didSave;
                    SetEditorStatus(didSave ? "Saved and entered Play mode" : "Save failed",
                                    didSave ? 0xFF89D185 : 0xFFE07A7A);
                } else if (clickedMenuItem == 2) {
                    const bool didUpdateStartupScene = !activeProjectFilePath.empty() &&
                        !State.currentMapPath.empty() &&
                        UpdateProjectStartupScene(activeProjectFilePath, State.currentMapPath);
                    SetEditorStatus(didUpdateStartupScene ? "Startup map updated" : "Startup map update failed",
                                    didUpdateStartupScene ? 0xFF89D185 : 0xFFE07A7A);
                } else if (clickedMenuItem == 3) {
                    const bool didOpenMap = !startupScenePath.empty() && renderer->OpenSceneMap(startupScenePath);
                    SetEditorStatus(didOpenMap ? "Opened startup map" : "Startup map open failed",
                                    didOpenMap ? 0xFF89D185 : 0xFFE07A7A);
                }
                break;
            case TopEditorMenu::Help:
                if (clickedMenuItem == 0) {
                    State.showHelpPopup = true;
                }
                break;
            default:
                break;
            }

            State.activeTopMenu = -1;
        }

        if (g_InputManager->IsMouseButtonPressed(0)) {
            bool clickedMenuLabel = false;
            for (size_t menuIndex = 0; menuIndex < topMenus.size(); ++menuIndex) {
                if (State.mx >= topMenuXs[menuIndex] && State.mx <= topMenuXs[menuIndex] + topMenuWidths[menuIndex] &&
                    State.my >= 0.0f && State.my <= 25.0f) {
                    clickedMenuLabel = true;
                    break;
                }
            }

            const bool clickedMenuPopup =
                State.mx >= popupX && State.mx <= popupX + popupW &&
                State.my >= popupY && State.my <= popupY + popupH;
            if (!clickedMenuLabel && !clickedMenuPopup) {
                State.activeTopMenu = -1;
            }
        }
    }

    float assetX = 210.0f;
    float assetY = bottomY + 40.0f;
    int hoveredContentAsset = -1;
    
    for(int i = 0; i < static_cast<int>(State.discoveredAssets.size()); i++) {
        uint32_t baseColor = 0xFF222222;
        if (State.discoveredAssets[i].isFolder) {
            baseColor = 0xFF554422;
        } else if (IsActorAssetName(State.discoveredAssets[i].name)) {
            baseColor = 0xFF20344A;
        } else if (IsMapAssetName(State.discoveredAssets[i].name)) {
            baseColor = 0xFF2F3F22;
        } else if (IsUIBlueprintAssetName(State.discoveredAssets[i].name)) {
            baseColor = 0xFF21484C;
        } else if (IsBlueprintAssetName(State.discoveredAssets[i].name)) {
            baseColor = 0xFF2A2A58;
        } else if (IsMaterialAssetName(State.discoveredAssets[i].name)) {
            baseColor = 0xFF4A3320;
        } else if (IsTextureAssetName(State.discoveredAssets[i].name)) {
            baseColor = 0xFF204235;
        }
        uint32_t boxColor = (State.selectedContentAsset == i) ? 0xFFD77800 : baseColor;
        
        bool isHoveringAsset = (State.mx >= assetX && State.mx <= assetX + 90.0f && State.my >= assetY && State.my <= assetY + 90.0f);
        if (isHoveringAsset) {
            hoveredContentAsset = i;
        }
        if (isHoveringAsset && g_InputManager->IsMouseButtonPressed(0) && !State.showContextMenu && !State.isAsyncLoading && !editorModalOpen) {
            const bool wasAlreadySelected = (State.selectedContentAsset == i);
            State.selectedContentAsset = i;
            State.draggedAssetIndex = -1;
            State.pendingDragAssetIndex = -1;
            State.pendingDragWasSelected = false;
            const bool isDoubleClick = (GetTickCount() - State.lastClickTime < 400 && State.lastClickedIndex == i);
            if (State.discoveredAssets[i].isFolder) {
                if (isDoubleClick) {
                    std::wstring wFolderName(State.discoveredAssets[i].name.begin(), State.discoveredAssets[i].name.end());
                    State.currentBrowserPath += L"\\" + wFolderName;
                    State.lastScanTime = 0; State.lastClickedIndex = -1; State.selectedContentAsset = -1;
                    State.pendingDragAssetIndex = -1;
                    State.pendingDragWasSelected = false;
                } else {
                    State.lastClickTime = GetTickCount();
                    State.lastClickedIndex = i;
                    State.pendingDragAssetIndex = i;
                    State.pendingDragWasSelected = wasAlreadySelected;
                    State.pendingDragStartMouseX = State.mx;
                    State.pendingDragStartMouseY = State.my;
                }
            } else {
                if (isDoubleClick && IsBlueprintAssetName(State.discoveredAssets[i].name)) {
                    const std::wstring assetPath = (fs::path(State.currentBrowserPath) / StringToWide(State.discoveredAssets[i].name)).wstring();
                    if (OpenBlueprintEditorWindow(assetPath)) {
                        State.draggedAssetIndex = -1;
                        State.lastClickedIndex = -1;
                        State.selectedObj = -1;
                        return;
                    }
                }
                if (isDoubleClick && IsUIBlueprintAssetName(State.discoveredAssets[i].name)) {
                    const std::wstring assetPath = (fs::path(State.currentBrowserPath) / StringToWide(State.discoveredAssets[i].name)).wstring();
                    if (OpenBlueprintEditorWindow(assetPath)) {
                        State.draggedAssetIndex = -1;
                        State.pendingDragAssetIndex = -1;
                        State.pendingDragWasSelected = false;
                        State.lastClickedIndex = -1;
                        State.selectedObj = -1;
                        return;
                    }
                }
                if (isDoubleClick && IsActorAssetName(State.discoveredAssets[i].name)) {
                    const std::wstring assetPath = (fs::path(State.currentBrowserPath) / StringToWide(State.discoveredAssets[i].name)).wstring();
                    if (OpenActorViewerWindow(assetPath)) {
                        State.draggedAssetIndex = -1;
                        State.lastClickedIndex = -1;
                        State.selectedObj = -1;
                        return;
                    }
                }
                if (isDoubleClick && IsMaterialAssetName(State.discoveredAssets[i].name)) {
                    const std::wstring assetPath = (fs::path(State.currentBrowserPath) / StringToWide(State.discoveredAssets[i].name)).wstring();
                    if (OpenMaterialEditorWindow(assetPath)) {
                        State.draggedAssetIndex = -1;
                        State.lastClickedIndex = -1;
                        State.selectedObj = -1;
                        return;
                    }
                }
                if (isDoubleClick && IsMapAssetName(State.discoveredAssets[i].name)) {
                    const std::wstring assetPath = (fs::path(State.currentBrowserPath) / StringToWide(State.discoveredAssets[i].name)).wstring();
                    const bool didOpenMap = renderer->OpenSceneMap(assetPath);
                    SetEditorStatus(didOpenMap ? ("Opened " + fs::path(assetPath).filename().string()) : "Map load failed",
                                    didOpenMap ? 0xFF89D185 : 0xFFE07A7A);
                    if (didOpenMap) {
                        State.draggedAssetIndex = -1;
                        State.lastClickedIndex = -1;
                        State.selectedObj = -1;
                        return;
                    }
                }

                State.lastClickTime = GetTickCount();
                State.lastClickedIndex = i;
                State.pendingDragAssetIndex = i;
                State.pendingDragWasSelected = wasAlreadySelected;
                State.pendingDragStartMouseX = State.mx;
                State.pendingDragStartMouseY = State.my;
            }
        }
        
        drawList.AddRectFilled(assetX - 2, assetY - 2, 94.0f, 94.0f, boxColor); 
        drawList.AddRectFilled(assetX, assetY, 90.0f, 90.0f, 0xFF333333);       
        drawList.AddRectFilled(assetX, assetY + 65.0f, 90.0f, 25.0f, 0xFF222222);
        
        const std::string displayName = GetBrowserItemDisplayName(State.discoveredAssets[i].name, State.discoveredAssets[i].isFolder);
        std::string shortName = FitName(displayName, 10);
        drawList.AddText(fontMgr, shortName, assetX + 5.0f, assetY + 82.0f, 0xFFFFFFFF);
        uiCtx.Button("", assetX, assetY, 90.0f, 90.0f, 0x00000000, 0x22FFFFFF, 0x44FFFFFF);
        
        assetX += 105.0f;
        if (assetX > bottomW - 100.0f) { assetX = 210.0f; assetY += 105.0f; }
    }

    const bool f2IsDown = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
    if (f2IsDown && !State.f2WasDown && !editorModalOpen && !State.showContextMenu) {
        const int renameAssetIndex = hoveredContentAsset >= 0 ? hoveredContentAsset : State.selectedContentAsset;
        if (renameAssetIndex >= 0 && renameAssetIndex < static_cast<int>(State.discoveredAssets.size())) {
            BeginRenameItem(renameAssetIndex);
        }
    }
    State.f2WasDown = f2IsDown;

    if (!editorModalOpen && g_InputManager->IsMouseButtonPressed(1)) {
        if (State.mx >= 0 && State.mx <= bottomW && State.my >= bottomY && State.my <= h) {
            State.showContextMenu = true;
            State.contextMenuX = static_cast<float>(State.mx);
            State.contextMenuY = static_cast<float>(State.my);
            if (hoveredContentAsset >= 0 && hoveredContentAsset < static_cast<int>(State.discoveredAssets.size())) {
                const BrowserItem& hoveredItem = State.discoveredAssets[hoveredContentAsset];
                State.selectedContentAsset = hoveredContentAsset;
                State.contextMenuTargetPath = (fs::path(State.currentBrowserPath) / StringToWide(hoveredItem.name)).wstring();
                State.contextMenuTargetIsFolder = hoveredItem.isFolder;
            } else {
                State.contextMenuTargetPath.clear();
                State.contextMenuTargetIsFolder = false;
            }
        } else {
            State.showContextMenu = false;
            State.contextMenuTargetPath.clear();
            State.contextMenuTargetIsFolder = false;
        }
    }

    drawList.AddRectFilled(0, topH - 2.0f, w, 2.0f, 0xFF000000);
    drawList.AddRectFilled(bottomW - 2.0f, topH, 2.0f, h, 0xFF000000);
    drawList.AddRectFilled(0, bottomY - 2.0f, bottomW, 2.0f, 0xFF000000);

    if (State.showPlaceActorsMenu) {
        float menuX = 175.0f;
        drawList.AddRectFilled(menuX, topH, 150.0f, 175.0f, 0xFF2A2A2A);
        drawList.AddRectFilled(menuX, topH, 150.0f, 2.0f, 0xFFD77800); 

        std::string primitives[5] = {"Cube", "Sphere", "Plane", "Cylinder", "Sky Atmosphere"};
        for (int i = 0; i < 5; i++) {
            if (uiCtx.Button(primitives[i], menuX + 5.0f, topH + 10.0f + (i * 32.0f), 140.0f, 28.0f, 0xFF222222, 0xFF444444, 0xFF555555)) {
                GameObject newObj;
                newObj.name = primitives[i] + "_" + std::to_string(gameObjects.size());
                if (i == 4) {
                    newObj.position = {0, -9999.0f, 0}; newObj.scale = {1,1,1}; newObj.color = {1,1,1,1}; newObj.skyHorizonColor = {1,1,1,1}; newObj.asset = assets[0].get(); 
                } else {
                    newObj.position = {0, 0, 0}; newObj.scale = {1,1,1}; newObj.color = {0.8f, 0.8f, 0.8f, 1.0f}; newObj.asset = assets[i].get(); 
                    PhysicsSystem::InitializeDefaultCollider(newObj, false, true);
                }
                gameObjects.push_back(newObj);
                State.selectedObj = static_cast<int>(gameObjects.size()) - 1;
                State.selectedContentAsset = -1;
                State.showPlaceActorsMenu = false; 
            }
        }
        
        if (g_InputManager->IsMouseButtonPressed(0)) {
            bool inMenu = (State.mx >= menuX && State.mx <= menuX + 150.0f && State.my >= topH && State.my <= topH + 175.0f);
            bool inButton = (State.mx >= 175.0f && State.mx <= 175.0f + 130.0f && State.my >= 30.0f && State.my <= 60.0f);
            if (!inMenu && !inButton) State.showPlaceActorsMenu = false;
        }
    }

    if (State.showImportPopup) {
        float popupW = 350.0f; float popupH = 180.0f;
        float popupX = (w - popupW) / 2.0f; float popupY = (h - popupH) / 2.0f;

        drawList.AddRectFilled(0, 0, w, h, 0xAA000000); 
        drawList.AddRectFilled(popupX, popupY, popupW, popupH, 0xFF1A1A1A);
        drawList.AddRectFilled(popupX, popupY, popupW, 30.0f, 0xFF0E0E0E);
        drawList.AddText(fontMgr, "Name Imported Asset", popupX + 15.0f, popupY + 20.0f, 0xFFFFFFFF);
        drawList.AddText(fontMgr, "Asset Name:", popupX + 20.0f, popupY + 65.0f, 0xFFCCCCCC);
        
        uiCtx.TextInput("ImportNameInput", State.pendingImportName, popupX + 20.0f, popupY + 80.0f, popupW - 40.0f, 40.0f, State.isImportNameActive);

        if (State.wasImportJustOpened) { State.isImportNameActive = true; State.wasImportJustOpened = false; }
        if (uiCtx.Button("Cancel", popupX + 20.0f, popupY + 130.0f, 100.0f, 30.0f, 0xFF333333, 0xFF444444, 0xFF222222)) State.showImportPopup = false;
        
        if (uiCtx.Button("Import ", popupX + popupW - 120.0f, popupY + 130.0f, 100.0f, 30.0f, 0xFFD77800, 0xFFFF9020, 0xFFB05000)) {
            if (!State.pendingImportName.empty()) {
                std::wstring wNewName(State.pendingImportName.begin(), State.pendingImportName.end());
                if (ImportAssetToProject(State.pendingImportPath, State.currentBrowserPath, wNewName)) {
                    State.lastScanTime = 0;
                    State.showImportPopup = false;
                }
            }
        }
    }

    if (State.showRenamePopup) {
        const float popupW = 360.0f;
        const float popupH = 180.0f;
        const float popupX = (w - popupW) * 0.5f;
        const float popupY = (h - popupH) * 0.5f;
        const std::string targetName = GetBrowserItemDisplayName(fs::path(State.renameTargetPath).filename().string(), State.renameTargetIsFolder);

        drawList.AddRectFilled(0, 0, w, h, 0xAA000000);
        drawList.AddRectFilled(popupX, popupY, popupW, popupH, 0xFF1A1A1A);
        drawList.AddRectFilled(popupX, popupY, popupW, 30.0f, 0xFF0E0E0E);
        drawList.AddText(fontMgr, "Rename Item", popupX + 15.0f, popupY + 20.0f, 0xFFFFFFFF);
        drawList.AddText(fontMgr, targetName, popupX + 20.0f, popupY + 58.0f, 0xFFAAAAAA, popupW - 40.0f);

        uiCtx.TextInput("RenameItemInput", State.renameInputName, popupX + 20.0f, popupY + 76.0f, popupW - 40.0f, 40.0f, State.isRenameInputActive);

        if (uiCtx.Button("Cancel", popupX + 20.0f, popupY + 130.0f, 100.0f, 30.0f, 0xFF333333, 0xFF444444, 0xFF222222)) {
            State.showRenamePopup = false;
            State.renameTargetPath.clear();
            State.renameTargetIsFolder = false;
            State.renameInputName.clear();
            State.isRenameInputActive = false;
        }

        if (uiCtx.Button("Rename", popupX + popupW - 120.0f, popupY + 130.0f, 100.0f, 30.0f, 0xFFD77800, 0xFFFF9020, 0xFFB05000)) {
            CommitRenameItem();
        }
    }

    if (State.showDeleteItemConfirm) {
        const float popupW = 430.0f;
        const float popupH = 250.0f;
        const float popupX = (w - popupW) * 0.5f;
        const float popupY = (h - popupH) * 0.5f;
        const float bodyX = popupX + 20.0f;
        const float bodyW = popupW - 40.0f;
        const float footerY = popupY + popupH - 68.0f;
        const float buttonY = popupY + popupH - 48.0f;
        const std::string targetName = ToDisplayString(State.deleteItemName.empty() ? L"Selected Item" : State.deleteItemName);
        const char* warningText = State.deleteItemIsFolder
            ? "This permanently deletes this folder and everything inside it from the project. This cannot be undone."
            : "This permanently deletes this asset from the project. This cannot be undone.";

        drawList.AddRectFilled(0.0f, 0.0f, w, h, 0xAA000000);
        drawList.AddRectFilled(popupX, popupY, popupW, popupH, 0xFF1A1A1A);
        drawList.AddRectFilled(popupX, popupY, popupW, 34.0f, 0xFF121212);
        drawList.AddText(fontMgr, State.deleteItemIsFolder ? "Delete Folder?" : "Delete Asset?", popupX + 16.0f, popupY + 22.0f, 0xFFFFFFFF);
        drawList.AddText(fontMgr, targetName, bodyX, popupY + 72.0f, 0xFFFFFFFF, bodyW);
        drawList.AddText(fontMgr, warningText, bodyX, popupY + 108.0f, 0xFFB0B0B0, bodyW);
        drawList.AddRectFilled(popupX, footerY, popupW, 1.0f, 0xFF2B2B2B);

        if (uiCtx.Button("Cancel", popupX + 20.0f, buttonY, 120.0f, 32.0f, 0xFF333333, 0xFF444444, 0xFF222222)) {
            ClearDeleteItemState();
        }

        if (uiCtx.Button(State.deleteItemIsFolder ? "Delete Folder" : "Delete Asset",
                         popupX + popupW - 160.0f, buttonY, 140.0f, 32.0f,
                         0xFF7A2E2E, 0xFF9A3838, 0xFFB04040)) {
            CommitDeleteItem();
        }
    }

    if (State.showHelpPopup) {
        const float popupW = 520.0f;
        const float popupH = 340.0f;
        const float popupX = (w - popupW) * 0.5f;
        const float popupY = (h - popupH) * 0.5f;
        const float bodyX = popupX + 22.0f;
        const float bodyW = popupW - 44.0f;
        const std::string projectName = activeProjectFilePath.empty()
            ? "No project loaded"
            : ToDisplayString(fs::path(activeProjectFilePath).stem().wstring());
        const std::string mapName = State.currentMapPath.empty()
            ? "No map loaded"
            : ToDisplayString(fs::path(State.currentMapPath).stem().wstring());
        const std::string shortcutText =
            "F2  Rename selected browser item\n"
            "Delete  Remove selected object or browser item\n"
            "Double-click asset tiles to open them\n"
            "Drag actor and Blueprint assets into the viewport to place them\n"
            "Right-click the content browser for create and delete actions";

        drawList.AddRectFilled(0.0f, 0.0f, w, h, 0xAA000000);
        drawList.AddRectFilled(popupX, popupY, popupW, popupH, 0xFF1A1A1A);
        drawList.AddRectFilled(popupX, popupY, popupW, 34.0f, 0xFF121212);
        drawList.AddRectFilled(popupX, popupY, popupW, 3.0f, 0xFFD77800);
        drawList.AddText(fontMgr, "Catalyst Editor Help", popupX + 16.0f, popupY + 22.0f, 0xFFFFFFFF);
        drawList.AddText(fontMgr, "Project: " + projectName, bodyX, popupY + 70.0f, 0xFFFFFFFF, bodyW);
        drawList.AddText(fontMgr, "Map: " + mapName, bodyX, popupY + 98.0f, 0xFFB7BEC8, bodyW);
        drawList.AddText(fontMgr, "Top menus now drive real editor actions for files, browser tools, play mode, and Blueprint workflows.",
                         bodyX, popupY + 138.0f, 0xFFB0B0B0, bodyW);
        drawList.AddText(fontMgr, shortcutText, bodyX, popupY + 210.0f, 0xFFB0B0B0, bodyW);

        if (uiCtx.Button("Close", popupX + popupW - 132.0f, popupY + popupH - 48.0f, 110.0f, 32.0f,
                         0xFF333333, 0xFF444444, 0xFF222222)) {
            State.showHelpPopup = false;
        }
    }

    if (State.showContextMenu) {
        float mW = 150.0f; float mH = State.contextMenuTargetPath.empty() ? 194.0f : 75.0f;
        float mX = (std::min)(State.contextMenuX, bottomW - mW - 10.0f);
        float mY = (std::min)(State.contextMenuY, h - mH - 10.0f);
        drawList.AddRectFilled(mX, mY, mW, mH, 0xFF2A2A2A);
        drawList.AddRectFilled(mX, mY, mW, 2.0f, 0xFFD77800); 

        if (!State.contextMenuTargetPath.empty()) {
            if (uiCtx.Button("Rename", mX + 5.0f, mY + 10.0f, mW - 10.0f, 25.0f, 0xFF222222, 0xFF444444, 0xFF555555)) {
                State.renameTargetPath = State.contextMenuTargetPath;
                State.renameTargetIsFolder = State.contextMenuTargetIsFolder;
                State.renameInputName = GetBrowserItemDisplayName(fs::path(State.contextMenuTargetPath).filename().string(), State.contextMenuTargetIsFolder);
                State.isRenameInputActive = true;
                State.showRenamePopup = true;
                State.showContextMenu = false;
                State.contextMenuTargetPath.clear();
                State.contextMenuTargetIsFolder = false;
            }
            if (uiCtx.Button("Delete...", mX + 5.0f, mY + 40.0f, mW - 10.0f, 25.0f, 0xFF3A2020, 0xFF6C2E2E, 0xFF7A3434)) {
                BeginDeleteItemPath(State.contextMenuTargetPath, State.contextMenuTargetIsFolder);
            }
        } else {
            if (uiCtx.Button("New Folder", mX + 5.0f, mY + 10.0f, mW - 10.0f, 25.0f, 0xFF222222, 0xFF444444, 0xFF555555)) {
                if (!State.currentBrowserPath.empty()) {
                    std::wstring finalName = L"NewFolder"; int counter = 1;
                    const std::filesystem::path currentFolder(State.currentBrowserPath);
                    while(std::filesystem::exists(currentFolder / finalName)) finalName = L"NewFolder_" + std::to_wstring(counter++);
                    const std::filesystem::path createdPath = currentFolder / finalName;
                    if (std::filesystem::create_directory(createdPath)) {
                        State.lastScanTime = 0;
                        BeginRenameCreatedPath(createdPath.wstring(), true);
                    } else {
                        SetEditorStatus("Create failed", 0xFFE07A7A);
                    }
                }
                State.showContextMenu = false;
            }
            if (uiCtx.Button("New Map", mX + 5.0f, mY + 40.0f, mW - 10.0f, 25.0f, 0xFF222222, 0xFF444444, 0xFF555555)) {
                std::wstring createdPath;
                if (CreateEmptySceneMap(State.currentBrowserPath, &createdPath)) {
                    State.lastScanTime = 0;
                    BeginRenameCreatedPath(createdPath, false);
                } else {
                    SetEditorStatus("Create failed", 0xFFE07A7A);
                }
                State.showContextMenu = false;
            }
            if (uiCtx.Button("New Material", mX + 5.0f, mY + 70.0f, mW - 10.0f, 25.0f, 0xFF222222, 0xFF444444, 0xFF555555)) {
                CreateMaterialInBrowser();
                State.showContextMenu = false;
            }
            drawList.AddRectFilled(mX + 6.0f, mY + 104.0f, mW - 12.0f, 1.0f, 0xFF3A3A3A);
            drawList.AddText(fontMgr, "Blueprints", mX + 10.0f, mY + 121.0f, 0xFF7FA8E5);
            if (uiCtx.Button("Actor Blueprint", mX + 5.0f, mY + 130.0f, mW - 10.0f, 25.0f, 0xFF222222, 0xFF444444, 0xFF555555)) {
                CreateActorBlueprintInBrowser();
                State.showContextMenu = false;
            }
            if (uiCtx.Button("UI Blueprint", mX + 5.0f, mY + 160.0f, mW - 10.0f, 25.0f, 0xFF222222, 0xFF444444, 0xFF555555)) {
                CreateUIBlueprintInBrowser();
                State.showContextMenu = false;
            }
        }
    }

    if (State.isAsyncLoading) {
        float centerX = w / 2.0f;
        float centerY = h / 2.0f;
        
        drawList.AddRectFilled(0, 0, w, h, 0x88000000);
        drawList.AddRectFilled(centerX - 150.0f, centerY - 40.0f, 300.0f, 80.0f, 0xFF1A1A1A);
        drawList.AddRectFilled(centerX - 150.0f, centerY - 40.0f, 300.0f, 4.0f, 0xFFD77800);
        drawList.AddText(fontMgr, "PARSING ASSET...", centerX - 70.0f, centerY - 10.0f, 0xFFFFFFFF);

        if (State.asyncMeshFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            State.isAsyncLoading = false;
            MeshData customData;
            try {
                customData = State.asyncMeshFuture.get();
            } catch (...) {
                SetEditorStatus("Asset parse failed", 0xFFE07A7A);
                State.pendingAssetName.clear();
                State.pendingAssetSourcePath.clear();
                return;
            }

            if (!customData.Vertices.empty()) {
                try {
                    ID3D12Device* device = renderer->m_device.Get();
                    ID3D12GraphicsCommandList* cmdList = renderer->m_commandList.Get();

                    Mesh* newMesh = new Mesh(device, cmdList, customData.Vertices.data(), customData.Vertices.size(), customData.Indices.data(), customData.Indices.size());
                    renderer->m_primitives[State.pendingAssetName] = newMesh;
                    
                    auto customAsset = std::make_shared<Asset>();
                    customAsset->id = renderer->GetNextAvailableAssetId();
                    customAsset->name = State.pendingAssetName;
                    customAsset->sourcePath = ToDisplayString(State.pendingAssetSourcePath);
                    customAsset->type = AssetType::Mesh;
                    customAsset->mesh = newMesh;
                    renderer->m_assets.push_back(customAsset);

                    GameObject newObj;
                    newObj.name = State.pendingAssetName + "_" + std::to_string(renderer->m_gameObjects.size());
                    newObj.position = State.pendingSpawnPos;
                    newObj.scale = {1, 1, 1};
                    newObj.color = {0.8f, 0.8f, 0.8f, 1.0f};
                    newObj.asset = customAsset.get();
                    PhysicsSystem::InitializeDefaultCollider(newObj, false, true);
                    renderer->m_gameObjects.push_back(newObj);
                    
                    State.selectedObj = static_cast<int>(renderer->m_gameObjects.size()) - 1;
                    State.selectedContentAsset = -1;
                } catch (...) {
                    SetEditorStatus("Mesh upload failed", 0xFFE07A7A);
                }
                State.pendingAssetSourcePath.clear();
            } else {
                SetEditorStatus("Asset parse failed", 0xFFE07A7A);
                State.pendingAssetName.clear();
                State.pendingAssetSourcePath.clear();
            }
        }
    }
}


