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
    void Initialize(HWND hwnd, int width, int height);
    void OnResize(int width, int height);
    void Render();
    void Shutdown();
    void SetEngineState(EngineState state) { m_engineState = state; }
    void SetStandaloneActorViewerWindow(bool isStandalone) { m_standaloneActorViewerWindow = isStandalone; }
    bool IsStandaloneActorViewerWindow() const { return m_standaloneActorViewerWindow; }
    void SetStandaloneMaterialEditorWindow(bool isStandalone) { m_standaloneMaterialEditorWindow = isStandalone; }
    bool IsStandaloneMaterialEditorWindow() const { return m_standaloneMaterialEditorWindow; }
    bool OpenActorAssetViewer(const std::wstring& path);
    bool OpenMaterialAssetEditor(const std::wstring& path);
    bool SaveCurrentScene();
    int GetNextAvailableAssetId() const;
    
    std::vector<ProjectInfo> GetRecentProjectsInfo();

private:
    bool FinalizeActorAssetViewerLoad(const MeshData& meshData, const std::wstring& path);
    void CloseActorAssetViewer();
    void CloseMaterialAssetEditor();
    void ResetSceneToDefaults();
    void QueueProjectStartupSceneLoad(const std::wstring& projectFilePath);
    void ProcessPendingProjectSceneLoad();
    void ClearProjectRuntimeAssets();
    bool LoadStartupSceneForProject(const std::wstring& projectFilePath);
    bool LoadSceneFromMap(const std::wstring& scenePath, const std::wstring& projectRoot);
    bool OpenSceneMap(const std::wstring& scenePath);
    std::wstring ResolveActiveProjectFilePath() const;
    std::wstring ResolveActiveProjectDisplayName() const;
    std::wstring ResolveActiveMapDisplayName() const;
    void RefreshWindowTitle();
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
    
    // THE FIX: Changed from ImDrawList to your custom UIDrawList wrapper
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
    bool m_lastPlayMode = false;
    
    uint32_t m_frameHeapOffset = 0;
    PhysicsSystem m_physicsSystem;
};
