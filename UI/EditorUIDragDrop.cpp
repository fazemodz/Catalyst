#include "EditorUIInternal.h"
#include "../Core Render/DXRenderer.h"
#include "../EngineApp.h"
#include "../Physics/PhysicsSystem.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
using namespace EditorUIInternal;

void EditorUI::ProcessDragAndDrop(DXRenderer* renderer, float w, float h, float topH, float viewW, float viewH) {
    auto& drawList = renderer->m_uiDrawList;
    auto& fontMgr = renderer->m_fontManager;
    auto& gameObjects = renderer->m_gameObjects;
    auto& assets = renderer->m_assets;
    auto SetEditorStatus = [&](const std::string& message, uint32_t color, DWORD durationMs = 2600) {
        State.saveStatusMessage = message;
        State.saveStatusColor = color;
        State.saveStatusUntil = GetTickCount() + durationMs;
    };

    if (State.showActorAssetViewer || State.showMaterialAssetViewer) {
        State.draggedAssetIndex = -1;
        State.pendingDragAssetIndex = -1;
        State.pendingDragWasSelected = false;
        return;
    }

    const float bottomY = topH + viewH;
    const float bottomW = viewW;
    auto FindFolderDropTarget = [&](float* outTileX = nullptr, float* outTileY = nullptr) -> int {
        float assetX = 210.0f;
        float assetY = bottomY + 40.0f;
        for (int i = 0; i < static_cast<int>(State.discoveredAssets.size()); ++i) {
            const bool isHoveringTile =
                State.mx >= assetX && State.mx <= assetX + 90.0f &&
                State.my >= assetY && State.my <= assetY + 90.0f;
            if (isHoveringTile && State.discoveredAssets[i].isFolder) {
                if (outTileX) {
                    *outTileX = assetX;
                }
                if (outTileY) {
                    *outTileY = assetY;
                }
                return i;
            }

            assetX += 105.0f;
            if (assetX > bottomW - 100.0f) {
                assetX = 210.0f;
                assetY += 105.0f;
            }
        }

        return -1;
    };

    if (State.draggedAssetIndex == -1) {
        if (!g_InputManager->IsMouseButtonDown(0)) {
            State.pendingDragAssetIndex = -1;
            State.pendingDragWasSelected = false;
        } else if (!State.isAsyncLoading &&
                   State.pendingDragAssetIndex >= 0 &&
                   State.pendingDragAssetIndex < static_cast<int>(State.discoveredAssets.size()) &&
                   State.selectedContentAsset == State.pendingDragAssetIndex) {
            constexpr int dragThresholdPixels = 6;
            const int deltaX = State.mx - State.pendingDragStartMouseX;
            const int deltaY = State.my - State.pendingDragStartMouseY;
            if ((deltaX * deltaX) + (deltaY * deltaY) >= dragThresholdPixels * dragThresholdPixels) {
                State.draggedAssetIndex = State.pendingDragAssetIndex;
            }
        }
    }

   
    if (State.draggedAssetIndex != -1 && State.draggedAssetIndex < static_cast<int>(State.discoveredAssets.size()) && !State.isAsyncLoading) {
        if (g_InputManager->IsMouseButtonDown(0)) {
            float ghostX = static_cast<float>(State.mx) - 45.0f;
            float ghostY = static_cast<float>(State.my) - 45.0f;
            float textX = static_cast<float>(State.mx) - 35.0f;
            float textY = static_cast<float>(State.my) - 10.0f;
            float folderTileX = 0.0f;
            float folderTileY = 0.0f;
            const int folderDropIndex = FindFolderDropTarget(&folderTileX, &folderTileY);

            drawList.AddRectFilled(ghostX, ghostY, 90.0f, 90.0f, 0x88D77800); 
            if (folderDropIndex >= 0 && folderDropIndex != State.draggedAssetIndex) {
                drawList.AddRectFilled(folderTileX - 3.0f, folderTileY - 3.0f, 96.0f, 96.0f, 0x5596D296);
            }

            std::string sName = FitName(GetBrowserItemDisplayName(State.discoveredAssets[State.draggedAssetIndex].name,
                                                                  State.discoveredAssets[State.draggedAssetIndex].isFolder), 10);
            drawList.AddText(fontMgr, sName, textX, textY, 0xFFFFFFFF);
        } else {
            const int folderDropIndex = FindFolderDropTarget();
            if (folderDropIndex >= 0 &&
                folderDropIndex != State.draggedAssetIndex &&
                folderDropIndex < static_cast<int>(State.discoveredAssets.size())) {
                const BrowserItem& draggedItem = State.discoveredAssets[State.draggedAssetIndex];
                const BrowserItem& folderItem = State.discoveredAssets[folderDropIndex];
                const std::wstring sourcePath = (fs::path(State.currentBrowserPath) / StringToWide(draggedItem.name)).wstring();
                const std::wstring targetFolderPath = (fs::path(State.currentBrowserPath) / StringToWide(folderItem.name)).wstring();
                const std::wstring projectFilePath = renderer->ResolveActiveProjectFilePath();
                const std::wstring startupScenePath = projectFilePath.empty()
                    ? L""
                    : fs::path(ResolveProjectStartupScenePath(projectFilePath)).lexically_normal().wstring();

                std::wstring movedPath;
                const bool didMove = MoveBrowserEntry(sourcePath, targetFolderPath, &movedPath);
                if (didMove) {
                    State.currentMapPath = RemapRenamedPath(State.currentMapPath, sourcePath, movedPath);
                    State.currentBrowserPath = RemapRenamedPath(State.currentBrowserPath, sourcePath, movedPath);

                    if (!startupScenePath.empty()) {
                        const std::wstring remappedStartupScene = RemapRenamedPath(startupScenePath, sourcePath, movedPath);
                        if (remappedStartupScene != startupScenePath) {
                            UpdateProjectStartupScene(projectFilePath, remappedStartupScene);
                        }
                    }

                    State.lastScanTime = 0;
                    State.lastClickedIndex = -1;
                    State.selectedContentAsset = -1;
                    SetEditorStatus("Moved " + GetBrowserItemDisplayName(draggedItem.name, draggedItem.isFolder) +
                                    " to " + GetBrowserItemDisplayName(folderItem.name, true),
                                    0xFF89D185);
                } else {
                    SetEditorStatus("Move failed", 0xFFE07A7A);
                }

                State.draggedAssetIndex = -1;
                State.pendingDragAssetIndex = -1;
                State.pendingDragWasSelected = false;
                return;
            }

            if (State.mx >= 0 && State.mx <= viewW && State.my >= topH && State.my <= topH + viewH) {
                DirectX::XMFLOAT3 ro = {};
                DirectX::XMFLOAT3 rd = {};
                const bool hasRay = renderer->BuildViewportRay(State.mx, State.my, topH, viewW, viewH, ro, rd);

                float spawnX = ro.x, spawnY = 0.0f, spawnZ = ro.z;
                if (hasRay && abs(rd.y) > 0.001f) {
                    float t = -ro.y / rd.y;
                    if (t > 0) { spawnX = ro.x + rd.x * t; spawnZ = ro.z + rd.z * t; }
                }

                std::string aName = State.discoveredAssets[State.draggedAssetIndex].name;
                std::wstring fullPath = State.currentBrowserPath + L"\\" + StringToWide(aName);
                if (State.discoveredAssets[State.draggedAssetIndex].isFolder || IsMapAssetName(aName)) {
                    State.draggedAssetIndex = -1;
                    State.pendingDragAssetIndex = -1;
                    State.pendingDragWasSelected = false;
                    return;
                }
                if (IsMaterialAssetName(aName)) {
                    int hitObjectIndex = renderer->RaycastViewportObject(State.mx, State.my, topH, viewW, viewH);
                    if (hitObjectIndex >= 0 && hitObjectIndex < static_cast<int>(gameObjects.size())) {
                        Material* material = renderer->LoadMaterialAsset(fullPath);
                        if (material) {
                            gameObjects[hitObjectIndex].assignedMaterial = material;
                            State.selectedObj = hitObjectIndex;
                            State.selectedContentAsset = State.draggedAssetIndex;
                        }
                    }

                    State.draggedAssetIndex = -1;
                    State.pendingDragAssetIndex = -1;
                    State.pendingDragWasSelected = false;
                    return;
                }

                if (IsTextureAssetName(aName)) {
                    State.draggedAssetIndex = -1;
                    State.pendingDragAssetIndex = -1;
                    State.pendingDragWasSelected = false;
                    return;
                }

                if (IsBlueprintAssetName(aName)) {
                    GameObject newObj;
                    newObj.name = fs::path(aName).stem().string() + "_" + std::to_string(gameObjects.size());
                    newObj.position = {spawnX, spawnY, spawnZ};
                    newObj.scale = {1.0f, 1.0f, 1.0f};
                    newObj.color = {0.8f, 0.8f, 0.8f, 1.0f};
                    newObj.asset = nullptr;
                    newObj.blueprintAssetPath = fs::path(fullPath).lexically_normal().wstring();
                    PhysicsSystem::InitializeDefaultCollider(newObj, false, true);
                    renderer->RefreshObjectBlueprintRuntime(newObj);
                    gameObjects.push_back(newObj);

                    State.selectedObj = static_cast<int>(gameObjects.size()) - 1;
                    State.selectedContentAsset = -1;
                    SetEditorStatus("Placed Blueprint " + fs::path(aName).stem().string(), 0xFF89D185);
                    State.draggedAssetIndex = -1;
                    State.pendingDragAssetIndex = -1;
                    State.pendingDragWasSelected = false;
                    return;
                }

                if (IsUIBlueprintAssetName(aName)) {
                    SetEditorStatus("UI Blueprint placement is not supported yet", 0xFFE0C36F);
                    State.draggedAssetIndex = -1;
                    State.pendingDragAssetIndex = -1;
                    State.pendingDragWasSelected = false;
                    return;
                }

                int mappedId = -1; 
                for (size_t i = 0; i < assets.size(); i++) {
                    if (assets[i]->name == aName) { mappedId = static_cast<int>(i); break; }
                }

               
                if (mappedId == -1) {
                    State.isAsyncLoading = true;
                    State.pendingAssetName = aName;
                    State.pendingAssetSourcePath = fullPath;
                    State.pendingSpawnPos = {spawnX, spawnY, spawnZ};
                    
                    
                    State.asyncMeshFuture = std::async(std::launch::async, ParseCatalystActor, fullPath);
                } else {
                    GameObject newObj;
                    newObj.name = aName + "_" + std::to_string(gameObjects.size());
                    newObj.position = {spawnX, spawnY, spawnZ};
                    newObj.scale = {1, 1, 1};
                    newObj.color = {0.8f, 0.8f, 0.8f, 1.0f};
                    newObj.asset = assets[mappedId].get();
                    PhysicsSystem::InitializeDefaultCollider(newObj, false, true);
                    gameObjects.push_back(newObj);
                    
                    State.selectedObj = static_cast<int>(gameObjects.size()) - 1;
                    State.selectedContentAsset = -1;
                }
            }
            State.draggedAssetIndex = -1;
            State.pendingDragAssetIndex = -1;
            State.pendingDragWasSelected = false;
        }
    }
}

