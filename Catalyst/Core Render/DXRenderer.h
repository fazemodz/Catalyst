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
#include "Passes/RaytracePass.h"
#include "UIContext.h"
#include "UIRenderer.h"
#include "EditorSettings.h"
#include "FontManager.h"
#include "InputManager.h"
#include "GameObject.h"
#include "Asset.h"
#include "../Physics/PhysicsSystem.h"
#include "EditorUI.h"
#include "../Blueprint/CookedUIBlueprint.h"
#include "../UI/XML/UIXMLParser.h"
#include "../UI/XML/UIXMLStyleSheet.h"
#include "../Scripting/CatalystAPI.h"
#include "../Scripting/ScriptModuleHost.h"
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
    void RenderBlueprintPreview(const std::wstring& assetPath);
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
    struct RuntimeWidgetInstruction {
        CookedUIBlueprint::Opcode opcode = CookedUIBlueprint::Opcode::SetTextColor;
        int targetNodeId = 0;
        uint32_t color = 0xFFFFFFFF;
        float durationSeconds = 0.0f;
        std::string text;
    };

    struct RuntimeWidgetEventHandler {
        CookedUIBlueprint::EventType type = CookedUIBlueprint::EventType::Construct;
        int sourceNodeId = 0;
        uint32_t firstInstructionIndex = 0;
        uint32_t instructionCount = 0;
    };

    struct RuntimeWidgetNode {
        int id = 0;
        CookedUIBlueprint::ElementType elementType = CookedUIBlueprint::ElementType::Canvas;
        std::string displayText;
        float canvasX = 0.0f;
        float canvasY = 0.0f;
        float canvasWidth = 0.0f;
        float canvasHeight = 0.0f;
        uint32_t tintColor = 0xFFFFFFFF;
        bool visibleInGame = true;
    };

    struct RuntimeWidgetInstance {
        std::wstring assetPath;
        std::vector<RuntimeWidgetNode> nodes;
        std::vector<RuntimeWidgetInstruction> instructions;
        std::vector<RuntimeWidgetEventHandler> events;
        int ownerObjectIndex = -1;
        int ownerWidgetIndex = -1;
        bool constructExecuted = false;
    };

    struct SavedNativeScriptState {
        std::string key;
        std::string value;
    };

    struct SavedNativeScriptInstance {
        uint64_t objectId = 0;
        uint64_t componentId = 0;
        std::string className;
        std::vector<SavedNativeScriptState> state;
    };

    struct RuntimeNativeScriptInstance {
        std::string className;
        uint64_t objectId = 0;
        uint64_t componentId = 0;
        bool started = false;
        Catalyst::NativeScript* script = nullptr;
        std::vector<uint64_t> activeCollisionObjects;
        std::vector<uint64_t> activeTriggerObjects;
    };

    struct ScriptTimerEntry {
        uint64_t objectId = 0;
        uint64_t componentId = 0;
        uint64_t timerId = 0;
        float remainingSeconds = 0.0f;
        float intervalSeconds = 0.0f;
        bool looping = false;
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
    bool SaveMaterialAsset(Material& material, const std::wstring& path);
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
    void PollScriptModuleHost();
    void StartNativeScriptRuntime();
    void StopNativeScriptRuntime();
    void ApplyNativeScripts(float deltaTime);
    bool HotReloadNativeScripts();
    void CollectSavedNativeScriptInstances(std::vector<SavedNativeScriptInstance>& outSavedInstances) const;
    void RecreateNativeScriptsFromSavedState(const std::vector<SavedNativeScriptInstance>& savedInstances, bool callOnStartForFreshInstances);
    GameObject* FindGameObjectById(uint64_t objectId);
    const GameObject* FindGameObjectById(uint64_t objectId) const;
    void FlushDestroyedGameObjects();
    void UpdateAttachedObjectTransforms();
    void UpdateWorldTransformForObject(GameObject& object, std::vector<uint64_t>& recursionStack);
    void SyncObjectLocalTransform(GameObject& object);
    void SyncAllRootObjectLocals();
    GameObject* DuplicateGameObject(uint64_t objectId);
    GameObject* InstantiateObjectFromAssetPath(const std::wstring& assetPath, const Catalyst::TransformData& transform);
    bool SetObjectParent(uint64_t childObjectId, uint64_t parentObjectId, bool keepWorldTransform);
    bool ClearObjectParent(uint64_t childObjectId, bool keepWorldTransform);
    void UpdateScriptTimers(float deltaTime);
    bool SaveScriptValue(const std::string& slotName, const std::string& key, const std::string& value);
    bool LoadScriptValue(const std::string& slotName, const std::string& key, std::string& outValue) const;
    NativeScriptComponentDesc* FindNativeScriptComponent(GameObject& object, uint64_t componentId);
    const NativeScriptComponentDesc* FindNativeScriptComponent(const GameObject& object, uint64_t componentId) const;
    static void ScriptHostLog(void* userData, const char* message);
    static void ScriptHostLogWarning(void* userData, const char* message);
    static void ScriptHostLogError(void* userData, const char* message);
    static bool ScriptHostGetKeyDown(void* userData, uint32_t keyCode);
    static bool ScriptHostGetKeyPressed(void* userData, uint32_t keyCode);
    static bool ScriptHostGetActionDown(void* userData, const char* actionName);
    static bool ScriptHostGetActionPressed(void* userData, const char* actionName);
    static float ScriptHostGetAxis(void* userData, const char* axisName);
    static float ScriptHostGetDeltaTime(void* userData);
    static float ScriptHostGetUnscaledDeltaTime(void* userData);
    static float ScriptHostGetTimeScale(void* userData);
    static bool ScriptHostSetTimeScale(void* userData, float timeScale);
    static bool ScriptHostGetObjectType(void* userData, Catalyst::ObjectId objectId, uint32_t* outObjectType);
    static bool ScriptHostGetObjectEnabled(void* userData, Catalyst::ObjectId objectId, bool* outEnabled);
    static bool ScriptHostSetObjectEnabled(void* userData, Catalyst::ObjectId objectId, bool enabled);
    static bool ScriptHostGetObjectTransform(void* userData, Catalyst::ObjectId objectId, Catalyst::TransformData* outTransform);
    static bool ScriptHostSetObjectTransform(void* userData, Catalyst::ObjectId objectId, const Catalyst::TransformData* transform);
    static bool ScriptHostTranslateObject(void* userData, Catalyst::ObjectId objectId, Catalyst::Vec3 delta);
    static bool ScriptHostCopyObjectName(void* userData, Catalyst::ObjectId objectId, char* buffer, uint32_t bufferSize);
    static bool ScriptHostCopyObjectTag(void* userData, Catalyst::ObjectId objectId, char* buffer, uint32_t bufferSize);
    static bool ScriptHostSetObjectTag(void* userData, Catalyst::ObjectId objectId, const char* tag);
    static bool ScriptHostGetObjectLayer(void* userData, Catalyst::ObjectId objectId, uint32_t* outLayer);
    static bool ScriptHostSetObjectLayer(void* userData, Catalyst::ObjectId objectId, uint32_t layer);
    static Catalyst::ObjectId ScriptHostFindObjectByName(void* userData, const char* name);
    static Catalyst::ObjectId ScriptHostFindFirstObjectWithTag(void* userData, const char* tag);
    static uint32_t ScriptHostGetObjectsWithTag(void* userData, const char* tag, Catalyst::ObjectId* outObjects, uint32_t capacity);
    static bool ScriptHostAttachObject(void* userData, Catalyst::ObjectId childObjectId, Catalyst::ObjectId parentObjectId, bool keepWorldTransform);
    static bool ScriptHostDetachObject(void* userData, Catalyst::ObjectId childObjectId, bool keepWorldTransform);
    static Catalyst::ObjectId ScriptHostDuplicateObject(void* userData, Catalyst::ObjectId objectId);
    static bool ScriptHostDestroyObject(void* userData, Catalyst::ObjectId objectId);
    static Catalyst::ObjectId ScriptHostInstantiateObjectFromAssetPath(void* userData, const char* assetPath, const Catalyst::TransformData* transform);
    static bool ScriptHostGetObjectCollider(void* userData, Catalyst::ObjectId objectId, Catalyst::ColliderData* outCollider);
    static bool ScriptHostSetObjectCollider(void* userData, Catalyst::ObjectId objectId, const Catalyst::ColliderData* collider);
    static bool ScriptHostGetObjectRigidBody(void* userData, Catalyst::ObjectId objectId, Catalyst::RigidbodyData* outRigidBody);
    static bool ScriptHostSetObjectRigidBody(void* userData, Catalyst::ObjectId objectId, const Catalyst::RigidbodyData* rigidBody);
    static bool ScriptHostAddForce(void* userData, Catalyst::ObjectId objectId, Catalyst::Vec3 force);
    static bool ScriptHostAddImpulse(void* userData, Catalyst::ObjectId objectId, Catalyst::Vec3 impulse);
    static bool ScriptHostRaycast(void* userData, const Catalyst::Vec3* origin, const Catalyst::Vec3* direction, float maxDistance, Catalyst::RaycastHit* outHit);
    static bool ScriptHostCopyObjectMaterialPath(void* userData, Catalyst::ObjectId objectId, char* buffer, uint32_t bufferSize);
    static bool ScriptHostSetObjectMaterialPath(void* userData, Catalyst::ObjectId objectId, const char* materialPath);
    static bool ScriptHostGetObjectColor(void* userData, Catalyst::ObjectId objectId, Catalyst::Vec4* outColor);
    static bool ScriptHostSetObjectColor(void* userData, Catalyst::ObjectId objectId, const Catalyst::Vec4* color);
    static bool ScriptHostGetLightIntensity(void* userData, Catalyst::ObjectId objectId, float* outIntensity);
    static bool ScriptHostSetLightIntensity(void* userData, Catalyst::ObjectId objectId, float intensity);
    static bool ScriptHostGetMainCameraFov(void* userData, float* outFovDegrees);
    static bool ScriptHostSetMainCameraFov(void* userData, float fovDegrees);
    static Catalyst::WidgetId ScriptHostFindWidgetByText(void* userData, Catalyst::ObjectId ownerObjectId, const char* text);
    static bool ScriptHostSetWidgetText(void* userData, Catalyst::ObjectId ownerObjectId, Catalyst::WidgetId widgetId, const char* text);
    static bool ScriptHostSetWidgetVisible(void* userData, Catalyst::ObjectId ownerObjectId, Catalyst::WidgetId widgetId, bool visible);
    static bool ScriptHostSetWidgetTint(void* userData, Catalyst::ObjectId ownerObjectId, Catalyst::WidgetId widgetId, const Catalyst::Vec4* color);
    static bool ScriptHostSetTimer(void* userData, Catalyst::ObjectId objectId, Catalyst::ComponentId componentId, uint64_t timerId, float delaySeconds, bool looping);
    static bool ScriptHostCancelTimer(void* userData, Catalyst::ObjectId objectId, Catalyst::ComponentId componentId, uint64_t timerId);
    static bool ScriptHostSaveString(void* userData, const char* slotName, const char* key, const char* value);
    static bool ScriptHostSaveFloat(void* userData, const char* slotName, const char* key, float value);
    static bool ScriptHostSaveBool(void* userData, const char* slotName, const char* key, bool value);
    static bool ScriptHostLoadString(void* userData, const char* slotName, const char* key, char* buffer, uint32_t bufferSize);
    static bool ScriptHostLoadFloat(void* userData, const char* slotName, const char* key, float* outValue);
    static bool ScriptHostLoadBool(void* userData, const char* slotName, const char* key, bool* outValue);
    static bool ScriptHostCopyAnimationState(void* userData, Catalyst::ObjectId objectId, char* buffer, uint32_t bufferSize);
    static bool ScriptHostSetAnimationState(void* userData, Catalyst::ObjectId objectId, const char* stateName);
    static bool ScriptHostTriggerAnimation(void* userData, Catalyst::ObjectId objectId, const char* triggerName);
    static Catalyst::AudioHandle ScriptHostPlayOneShot2D(void* userData, const char* assetPath, float volume);
    static Catalyst::AudioHandle ScriptHostPlayOneShot3D(void* userData, const char* assetPath, Catalyst::Vec3 position, float volume);
    static bool ScriptHostStopAudio(void* userData, Catalyst::AudioHandle handle);
    static bool ScriptHostSetAudioVolume(void* userData, Catalyst::AudioHandle handle, float volume);
    static bool ScriptHostSetAudioPitch(void* userData, Catalyst::AudioHandle handle, float pitch);
    void DispatchNativeScriptPhysicsEvents();
    void SetPlayerMouseLookLocked(bool locked, float viewportTop = 0.0f, float viewportWidth = 0.0f, float viewportHeight = 0.0f);
    void ApplyBlueprintGameplayNodes(float deltaTime, float viewportTop, float viewportWidth, float viewportHeight, bool mouseInViewport);
    void DrawRuntimeBlueprintWidgets(float viewportLeft, float viewportTop, float viewportWidth, float viewportHeight);
    bool LoadRuntimeWidgetInstance(const std::wstring& assetPath, RuntimeWidgetInstance& outInstance);
    RuntimeWidgetNode* FindRuntimeWidgetNode(RuntimeWidgetInstance& instance, int nodeId);
    const RuntimeWidgetNode* FindRuntimeWidgetNode(const RuntimeWidgetInstance& instance, int nodeId) const;
    void ExecuteRuntimeWidgetEvent(RuntimeWidgetInstance& instance, const RuntimeWidgetEventHandler& eventHandler);
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

    // Takes ownership of a freshly created mesh's staging buffers and drops
    // them once the GPU has consumed the copy. Meshes built mid-frame record
    // their copy into the frame's command list, so they cannot free the staging
    // memory themselves - and on a dense mesh that is hundreds of megabytes
    // held for nothing.
    void DeferUploadBufferRelease(Mesh* mesh);

    // The one directional light the scene is lit by. Kept here so the shadow
    // pass and the shading pass cannot disagree about where it points.
    DirectX::XMFLOAT3 m_sunDirection = {-0.72f, -0.62f, 0.31f};
    Texture* LoadTextureAsset(const std::wstring& path);
    void ReleaseCachedTexture(const std::shared_ptr<Texture>& texture);
    const RaytracePass& GetRaytracePass() const { return m_raytracePass; }

    // Settings menu access. Preferences are per-machine; project settings live
    // in the .CatalystProj and are re-read whenever a project is opened.
    EditorPreferences& GetEditorPreferences() { return m_editorPreferences; }
    ProjectSettings& GetProjectSettings() { return m_projectSettings; }
    void ApplyEditorPreferences();
    void ApplyProjectSettings();
    bool SaveEditorPreferences();
    bool SaveProjectSettings();
    void LoadProjectSettingsFor(const std::wstring& projectFilePath);

    // Smoothed so the viewport counter is readable rather than flickering.
    float GetSmoothedFps() const { return m_smoothedFps; }
    float GetSmoothedFrameMs() const { return m_smoothedFrameMs; }
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
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocators[FrameCount];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    
    Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer;
    UINT8* m_pCbvDataBegin = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue = 0;
    // Fence value each in-flight frame was submitted with, so a frame's
    // allocator and per-frame buffers are only reused once it has retired.
    UINT64 m_frameFenceValues[FrameCount] = {};
    void WaitForFrame(UINT frameIndex);

    struct PendingUploadRelease {
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> stagingBuffers;
        UINT64 fenceValue = 0;
    };
    std::vector<PendingUploadRelease> m_pendingUploadReleases;
    void RetireCompletedUploadBuffers();

    // Slot the shadow map's SRV occupies in the bindless heap. The shading pass
    // can only reach descriptors in the heap that is bound, and that is always
    // the bindless one.
    int m_shadowMapBindlessIndex = -1;

    // Light-space transform the shadow map was rendered with. The shading pass
    // has to use exactly this or the lookup lands in the wrong place.
    DirectX::XMMATRIX m_shadowLightMatrix = DirectX::XMMatrixIdentity();

    // Fits the light's orthographic frustum around everything that can cast,
    // and returns the matrix both passes use.
    DirectX::XMMATRIX BuildSunLightMatrix(float& outWorldTexelSize) const;
    void RenderShadowMap();
    HANDLE m_fenceEvent = nullptr;
    UINT m_frameIndex = 0;
    ULONGLONG m_lastFrameTick = 0;

    BindlessManager m_bindlessManager;
    UIRenderer m_uiRenderer;
    FontManager m_fontManager;
    UIContext m_uiContext;
    
    
    UIDrawList m_uiDrawList; 
    
    EditorUI m_editorUI;

    EditorPreferences m_editorPreferences;
    ProjectSettings m_projectSettings;
    // editor_layout.xml still supplies panel sizes until the user saves
    // preferences of their own; after that the settings window wins.
    bool m_hasSavedEditorPreferences = false;

    ShadowPass m_shadowPass;
    PostProcessPass m_postProcessPass;
    PostProcessSettings m_globalPP;
    QuantaMeshPass m_quantaMeshPass;
    SkyboxPass m_skyboxPass;
    RaytracePass m_raytracePass;

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
    float m_lastScaledScriptDeltaTime = 0.0f;
    float m_lastUnscaledScriptDeltaTime = 0.0f;
    float m_scriptTimeScale = 1.0f;
    float m_scriptCameraFov = 45.0f;
    float m_smoothedFps = 0.0f;
    float m_smoothedFrameMs = 0.0f;
    Catalyst::ScriptHostApi m_nativeScriptHostApi{};
    ScriptModuleHost m_scriptModuleHost;
    std::map<std::string, RuntimeWidgetInstance> m_runtimeWidgetInstances;
    std::vector<RuntimeNativeScriptInstance> m_runtimeNativeScripts;
    std::vector<ScriptTimerEntry> m_scriptTimers;
    std::vector<uint64_t> m_pendingDestroyedObjectIds;
    bool m_showClosePrompt = false;
    bool m_closePromptCloseAllWindows = false;
    std::wstring m_closePromptSummary;
    std::wstring m_closePromptError;
    WindowCommand m_pendingWindowCommand = WindowCommand::None;
    
    bool m_hasPendingLaunchPlay = false;
    std::wstring m_pendingLaunchProjectFile;
    std::wstring m_pendingLaunchMapPath;

    PhysicsSystem m_physicsSystem;

    // XML-driven UI
    UIXML::StyleSheet m_editorStyleSheet;
    UIXML::Document   m_editorLayoutDoc;
    ULONGLONG         m_lastXmlReloadTick = 0;

    void LoadEditorXmlFiles();
    void ReloadUIXML();
};
