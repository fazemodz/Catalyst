#pragma once
#include <string>
#include <vector>
#include <windows.h>
#include <memory>
#include <future>
#include <atomic>
#include <DirectXMath.h>

#include "PrimitiveGenerator.h"
#include "../Launcher.h" 
#include "../Blueprint/BlueprintEditor.h"

struct BrowserItem {
    std::string name;
    bool isFolder;
};

struct LogEntry {
    std::string timestamp;
    std::string message;
    uint32_t color;
};

struct ActorViewerMeshLoadJob {
    std::atomic<bool> completed{false};
    MeshData meshData;
};

struct EditorUIState {
    int selectedObj = -1;
    bool showPlaceActorsMenu = false;
    int activeTopMenu = -1;
    bool showHelpPopup = false;
    bool deleteWasDown = false;
    bool f2WasDown = false;
    std::wstring currentProjectFile = L"";
    std::wstring currentProjectFolder = L"";
    std::wstring currentMapPath = L"";
    std::wstring currentBrowserPath = L"";
    std::vector<BrowserItem> discoveredAssets;
    DWORD lastScanTime = 0;
    int selectedContentAsset = -1;
    int draggedAssetIndex = -1;
    int pendingDragAssetIndex = -1;
    bool pendingDragWasSelected = false;
    int pendingDragStartMouseX = 0;
    int pendingDragStartMouseY = 0;
    float folderTreeScrollOffset = 0.0f;
    float assetBrowserScrollOffset = 0.0f;
    float outlinerScrollOffset = 0.0f;
    float detailsScrollOffset = 0.0f;
    bool showImportPopup = false;
    std::wstring pendingImportPath = L"";
    std::string pendingImportName = "";
    bool isImportNameActive = false;
    bool wasImportJustOpened = false;
    bool showContextMenu = false;
    float contextMenuX = 0.0f;
    float contextMenuY = 0.0f;
    std::wstring contextMenuTargetPath = L"";
    bool contextMenuTargetIsFolder = false;
    bool showRenamePopup = false;
    std::wstring renameTargetPath = L"";
    bool renameTargetIsFolder = false;
    std::string renameInputName = "";
    bool isRenameInputActive = false;
    bool showNewScriptNamePopup = false;
    std::string newScriptNameInput = "";
    bool newScriptNameInputActive = false;
    bool newScriptPopupJustOpened = false;
    std::wstring newScriptTargetFolder = L"";
    bool showDeleteItemConfirm = false;
    std::wstring deleteItemPath = L"";
    std::wstring deleteItemName = L"";
    bool deleteItemIsFolder = false;
    bool triggerEditorSwap = false;
    std::wstring editorSwapProjName = L"";
    bool showLauncherProjectMenu = false;
    float launcherProjectMenuX = 0.0f;
    float launcherProjectMenuY = 0.0f;
    std::wstring launcherProjectMenuPath = L"";
    std::wstring launcherProjectMenuName = L"";
    bool showDeleteProjectConfirm = false;
    std::wstring deleteProjectPath = L"";
    std::wstring deleteProjectName = L"";
    std::string launcherStatusMessage = "";
    uint32_t launcherStatusColor = 0xFF9A9A9A;
    DWORD launcherStatusUntil = 0;
    DWORD lastClickTime = 0;
    int lastClickedIndex = -1;
    int activeCategory = 0;
    std::string inputProjectName = "";
    bool isInputActive = false;
    std::vector<ProjectInfo> recentProjects; 
    bool recentsLoaded = false;
    bool isPlaying = false;
    bool standalonePlay = false;
    float playYOffset = 0.0f;
    std::string saveStatusMessage = "";
    uint32_t saveStatusColor = 0xFF9A9A9A;
    DWORD saveStatusUntil = 0;
    int mx = 0;
    int my = 0;
    bool showActorAssetViewer = false;
    bool showAddComponentMenu = false;
    int addComponentObjectIndex = -1;
    float addComponentMenuX = 0.0f;
    float addComponentMenuY = 0.0f;
    bool showScriptClassPicker = false;
    int scriptClassPickerObjectIndex = -1;
    uint64_t scriptClassPickerComponentId = 0;
    float scriptClassPickerX = 0.0f;
    float scriptClassPickerY = 0.0f;
    std::wstring actorViewerPath = L"";
    std::string actorViewerTitle = "";
    std::string actorViewerSearch = "";
    bool actorViewerSearchActive = false;
    float actorViewerYaw = 0.65f;
    float actorViewerPitch = -0.28f;
    float actorViewerDistance = 4.0f;
    float actorViewerFov = 35.0f;
    bool actorViewerIsDragging = false;
    int actorViewerLastMouseX = 0;
    int actorViewerLastMouseY = 0;
    bool actorViewerShowFloor = true;
    bool actorViewerShowSky = true;
    bool actorViewerShowStats = true;
    bool actorViewerAutoRotate = false;
    float actorViewerViewportW = 1.0f;
    float actorViewerViewportH = 1.0f;
    std::shared_ptr<ActorViewerMeshLoadJob> actorViewerMeshLoad;
    std::atomic<bool> isActorViewerLoading{false};
    bool showMaterialAssetViewer = false;
    std::wstring materialEditorPath = L"";
    std::string materialEditorTitle = "";
    std::string materialEditorSearch = "";
    bool materialEditorSearchActive = false;
    int materialEditorPreviewMesh = 0;
    float materialEditorYaw = 0.65f;
    float materialEditorPitch = -0.28f;
    float materialEditorDistance = 3.2f;
    float materialEditorFov = 35.0f;
    bool materialEditorIsDragging = false;
    int materialEditorLastMouseX = 0;
    int materialEditorLastMouseY = 0;
    bool materialEditorShowFloor = true;
    bool materialEditorShowSky = true;
    bool materialEditorShowStats = true;
    bool materialEditorAutoRotate = false;
    float materialEditorViewportW = 1.0f;
    float materialEditorViewportH = 1.0f;

    std::future<MeshData> asyncMeshFuture;
    std::atomic<bool> isAsyncLoading{false};
    std::string pendingAssetName = "";
    std::wstring pendingAssetSourcePath = L"";
    DirectX::XMFLOAT3 pendingSpawnPos = {0.0f, 0.0f, 0.0f};

    int bottomPanelTab = 0;
    std::vector<std::wstring> browserNavHistory;
    std::vector<LogEntry> editorLog;
    float logScrollOffset = 0.0f;
    bool logScrollToBottom = false;

    enum class BuildConfig {
        Debug,
        Release
    };
    BuildConfig buildConfig = BuildConfig::Release;
};

class DXRenderer;
class Material;
struct GameObject;

class EditorUI {
public:
    EditorUIState State;
    
    void DrawLauncher(DXRenderer* renderer, float w, float h);
    void DrawProjectLoading(DXRenderer* renderer, float w, float h);
    void DrawEditor(DXRenderer* renderer, float w, float h, float topH, float rightW, float bottomH);
    void ProcessDragAndDrop(DXRenderer* renderer, float w, float h, float topH, float viewW, float viewH);
    bool IsBlueprintEditorOpen() const { return m_blueprintEditor.IsOpen(); }
    bool HasUnsavedBlueprintChanges() const { return m_blueprintEditor.HasUnsavedChanges(); }
    bool SaveBlueprintChanges() { return m_blueprintEditor.SaveOpenAsset(); }
    std::wstring GetOpenBlueprintDisplayName() const { return m_blueprintEditor.GetDisplayName(); }
    void OpenBlueprintEditorForObject(const GameObject* targetObject) { m_blueprintEditor.Open(targetObject); }
    void OpenBlueprintAssetEditor(const std::wstring& assetPath) { m_blueprintEditor.OpenAsset(assetPath); }
    void CloseBlueprintAssetEditor() { m_blueprintEditor.Close(); }

private:
    BlueprintEditor m_blueprintEditor;
    void DrawActorAssetViewer(DXRenderer* renderer, float w, float h);
    void DrawMaterialAssetEditor(DXRenderer* renderer, float w, float h);
    void UpdatePreviewInteraction(float viewportTop, float viewportWidth, float viewportHeight,
                                  float& yaw, float& pitch, float& distance,
                                  bool& autoRotate, bool& isDragging,
                                  int& lastMouseX, int& lastMouseY,
                                  float& outViewportW, float& outViewportH);
    void DrawMaterialBaseColor(DXRenderer* renderer, Material* material, const std::wstring& materialPath,
                               float x, float width, float& y);
    void DrawMaterialTextureSlots(DXRenderer* renderer, HWND hwnd, Material* material, const std::wstring& materialPath,
                                  float x, float width, float& y);
};
