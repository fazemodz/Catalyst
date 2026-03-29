#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl.h>
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <map>
#include <memory>

#include "RenderTypes.h" 
#include "Camera.h"
#include "Mesh.h"
#include "Material.h"
#include "Texture.h"
#include "BindlessManager.h"
#include "Passes/ShadowPass.h"
#include "Passes/PostProcessPass.h"
#include "Passes/QuantaMeshPass.h"
#include "Passes/SkyboxPass.h"
#include "UIContext.h"
#include "UIRenderer.h"
#include "FontManager.h"
#include "InputManager.h"
#include "GameObject.h"
#include "Asset.h"
#include "../Physics/PhysicsSystem.h"
#include "EditorUI.h"
#include "../Launcher.h" 

enum class EngineState {
    Launcher,
    ProjectLoading,
    Editor
};

class DXRenderer {
    friend class EditorUI;

public:
    enum class WindowCommand {
        None,
        CloseThisWindow,
        CloseAllDiscard,
        CloseAllSave
    };

    void Initialize(HWND hwnd, int width, int height);
    void OnResize(int width, int height);
    void Render();
    void Shutdown();
    void SetEngineState(EngineState state) { m_engineState = state; }
    void SetStandaloneActorViewerWindow(bool isStandalone) { m_standaloneActorViewerWindow = isStandalone; }
    bool IsStandaloneActorViewerWindow() const { return m_standaloneActorViewerWindow; }
    void SetStandaloneMaterialEditorWindow(bool isStandalone) { m_standaloneMaterialEditorWindow = isStandalone; }
    bool IsStandaloneMaterialEditorWindow() const { return m_standaloneMaterialEditorWindow; }
    void SetStandaloneBlueprintEditorWindow(bool isStandalone) { m_standaloneBlueprintEditorWindow = isStandalone; }
    bool IsStandaloneBlueprintEditorWindow() const { return m_standaloneBlueprintEditorWindow; }
    bool OpenActorAssetViewer(const std::wstring& path);
    bool OpenMaterialAssetEditor(const std::wstring& path);
    bool OpenBlueprintAssetEditor(const std::wstring& path);
    bool SaveCurrentScene();
    bool HasPendingUnsavedChanges() const;
    std::wstring GetUnsavedChangesDescription() const;
    void OpenClosePrompt(bool closeAllWindows, const std::wstring& summaryOverride = L"");
    bool IsClosePromptOpen() const;
    void SetClosePromptError(const std::wstring& errorMessage);
    bool SavePendingUnsavedChanges();
    WindowCommand ConsumePendingWindowCommand();
    int GetNextAvailableAssetId() const;
    
    std::vector<ProjectInfo> GetRecentProjectsInfo();

private:
    struct RuntimeWidgetLink {
        int fromNodeId = 0;
        int toNodeId = 0;
        std::string fromPinKind;
        std::string toPinKind;
    };

    struct RuntimeWidgetNode {
        int id = 0;
        std::string nodeTypeId;
        std::string displayText;
        float canvasX = 0.0f;
        float canvasY = 0.0f;
        float canvasWidth = 0.0f;
        float canvasHeight = 0.0f;
        DirectX::XMFLOAT4 tint = {1.0f, 1.0f, 1.0f, 1.0f};
    };

    struct RuntimeWidgetInstance {
        std::wstring assetPath;
        std::vector<RuntimeWidgetNode> nodes;
        std::vector<RuntimeWidgetLink> links;
    };

    bool FinalizeActorAssetViewerLoad(const MeshData& meshData, const std::wstring& path);
    void CloseActorAssetViewer();
    void CloseMaterialAssetEditor();
    void CloseBlueprintAssetEditor();
    void ResetSceneToDefaults();
    void QueueProjectStartupSceneLoad(const std::wstring& projectFilePath);
    void ProcessPendingProjectSceneLoad();
    void ClearProjectRuntimeAssets();
    bool LoadStartupSceneForProject(const std::wstring& projectFilePath);
    bool LoadSceneFromMap(const std::wstring& scenePath, const std::wstring& projectRoot);
    bool OpenSceneMap(const std::wstring& scenePath);
    bool HasUnsavedSceneChanges() const;
    bool HasUnsavedMaterialEditorChanges() const;
    bool SaveMaterialAssetEditor();
    bool SavePendingOpenDocuments();
    std::wstring ResolveActiveProjectFilePath() const;
    std::wstring ResolveActiveProjectDisplayName() const;
    std::wstring ResolveActiveMapDisplayName() const;
    std::wstring DescribeUnsavedChanges() const;
    void RefreshWindowTitle();
    void RefreshSceneSavedDocument();
    void RefreshMaterialEditorSavedDocument();
    void ClearClosePrompt();
    void DrawClosePrompt(float width, float height);
    void RefreshObjectBlueprintRuntime(GameObject& object);
    void RefreshSceneBlueprintRuntime();
    void SetPlayerMouseLookLocked(bool locked, float viewportTop = 0.0f, float viewportWidth = 0.0f, float viewportHeight = 0.0f);
    void ApplyBlueprintGameplayNodes(float deltaTime, float viewportTop, float viewportWidth, float viewportHeight, bool mouseInViewport);
    void DrawRuntimeBlueprintWidgets(float viewportLeft, float viewportTop, float viewportWidth, float viewportHeight);
    bool LoadRuntimeWidgetInstance(const std::wstring& assetPath, RuntimeWidgetInstance& outInstance);
    RuntimeWidgetNode* FindRuntimeWidgetNode(RuntimeWidgetInstance& instance, int nodeId);
    const RuntimeWidgetNode* FindRuntimeWidgetNode(const RuntimeWidgetInstance& instance, int nodeId) const;
    void ExecuteRuntimeWidgetNode(RuntimeWidgetInstance& instance, int nodeId);
    Asset* FindAssetById(int assetId) const;
    Asset* FindAssetBySourcePath(const std::wstring& sourcePath) const;
    Material* FindMaterialByPath(const std::wstring& materialPath) const;
    Texture* FindTextureByPath(const std::wstring& texturePath) const;
    Asset* ResolveSceneAsset(int assetId, const std::string& assetName, const std::wstring& assetSourcePath);
    std::wstring GetCachedMaterialPath(const Material* material) const;
    std::wstring GetCachedTexturePath(const Texture* texture) const;
    void CreateDefaultTextures();
    void CreateDepthBuffer();
    void CreateConstantBuffer();
    void FlushGPU();
    Texture* LoadTextureAsset(const std::wstring& path);
    Material* LoadMaterialAsset(const std::wstring& path);
    void SyncMaterialTextures(Material& material);
    std::string BuildCurrentSceneDocument() const;
    std::string BuildSceneDocument(const std::vector<GameObject>& objectsToSave, const std::wstring& projectRoot) const;
    std::string BuildMaterialDocument(const Material& material) const;
    bool BuildViewportRay(int mouseX, int mouseY, float topH, float viewW, float viewH,
                          DirectX::XMFLOAT3& rayOrigin, DirectX::XMFLOAT3& rayDirection) const;
    int RaycastViewportObject(int mouseX, int mouseY, float topH, float viewW, float viewH) const;

    static const int FrameCount = 2;

    HWND m_hwnd = nullptr;
    int m_width = 0;
    int m_height = 0;

    EngineState m_engineState = EngineState::Launcher;
    bool m_standaloneActorViewerWindow = false;
    bool m_standaloneMaterialEditorWindow = false;
    bool m_standaloneBlueprintEditorWindow = false;

    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    Microsoft::WRL::ComPtr<ID3D12Resource> m_depthBuffer;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    
    Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer;
    UINT8* m_pCbvDataBegin = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue = 0;
    HANDLE m_fenceEvent = nullptr;
    UINT m_frameIndex = 0;
    ULONGLONG m_lastFrameTick = 0;

    BindlessManager m_bindlessManager;
    UIRenderer m_uiRenderer;
    FontManager m_fontManager;
    UIContext m_uiContext;
    
    
    UIDrawList m_uiDrawList; 
    
    EditorUI m_editorUI;

    ShadowPass m_shadowPass;
    PostProcessPass m_postProcessPass;
    QuantaMeshPass m_quantaMeshPass;
    SkyboxPass m_skyboxPass;

    Texture* m_texWhite = nullptr;
    Texture* m_texBlack = nullptr;
    Texture* m_texNormal = nullptr;

    Camera m_camera;
    std::vector<GameObject> m_gameObjects;
    std::vector<GameObject> m_playModeSnapshot;
    std::map<std::string, Mesh*> m_primitives;
    std::vector<std::shared_ptr<Asset>> m_assets;
    std::map<std::wstring, std::shared_ptr<Texture>> m_textureCache;
    std::map<std::wstring, std::shared_ptr<Material>> m_materialCache;
    std::unique_ptr<Mesh> m_actorViewerMesh;
    std::shared_ptr<Asset> m_actorViewerAsset;
    Material* m_actorViewerMaterial = nullptr;
    DirectX::XMFLOAT3 m_actorViewerBoundsCenter = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 m_actorViewerBoundsExtents = {0.5f, 0.5f, 0.5f};
    float m_actorViewerBoundsRadius = 1.0f;
    std::shared_ptr<Asset> m_materialEditorPreviewAsset;
    Material* m_materialEditorMaterial = nullptr;
    bool m_hasPendingProjectSceneLoad = false;
    bool m_projectLoadingOverlayPresented = false;
    std::wstring m_pendingProjectFilePath;
    std::wstring m_lastWindowTitle;
    std::string m_savedSceneDocument;
    std::string m_savedMaterialDocument;
    bool m_lastPlayMode = false;
    bool m_jumpKeyWasDown = false;
    bool m_escapeKeyWasDown = false;
    bool m_playerMouseLookLocked = false;
    bool m_playerMouseLookSuppressed = false;
    float m_playerControllerYaw = 0.0f;
    float m_playerControllerPitch = 0.0f;
    std::map<std::string, RuntimeWidgetInstance> m_runtimeWidgetInstances;
    bool m_showClosePrompt = false;
    bool m_closePromptCloseAllWindows = false;
    std::wstring m_closePromptSummary;
    std::wstring m_closePromptError;
    WindowCommand m_pendingWindowCommand = WindowCommand::None;
    
    uint32_t m_frameHeapOffset = 0;
    PhysicsSystem m_physicsSystem;
};
