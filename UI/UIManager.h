#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <string>
#include <filesystem>
#include <map> 
#include <commdlg.h>
#include <memory> 
#include <future> 

#include "GameObject.h"
#include "Asset.h" 
#include "Mesh.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include "ImGuizmo.h"

using Microsoft::WRL::ComPtr;

class UIManager {
public:
    void Initialize(HWND hwnd, ID3D12Device* device, ID3D12CommandQueue* commandQueue, int frameCount);
    void Update(std::vector<GameObject>& gameObjects, std::vector<std::shared_ptr<Asset>>& assets,
                int& selectedIndex, const DirectX::XMMATRIX& viewMatrix, const DirectX::XMMATRIX& projMatrix,
                D3D12_GPU_DESCRIPTOR_HANDLE previewTex, PostProcessSettings& globalPP);
    
    void Draw(ID3D12GraphicsCommandList* cmdList);
    void Shutdown();

    Asset* GetEditingAsset() { return m_showAssetEditor ? m_editingAsset : nullptr; }
    ID3D12DescriptorHeap* GetSRVHeap() { return m_srvHeap.Get(); }
    void SetPrimitives(const std::map<std::string, Mesh*>& primitives) { m_primitives = primitives; }

    float GetPreviewYaw() const { return m_previewYaw; }
    float GetPreviewPitch() const { return m_previewPitch; }
    float GetPreviewDistance() const { return m_previewDistance; }

private:
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    ID3D12Device* m_device = nullptr;
    ID3D12CommandQueue* m_cmdQueue = nullptr;
    HWND m_hwnd = nullptr;

    float m_thumbnailSize = 64.0f;
    float m_padding = 16.0f;

    std::map<std::string, Mesh*> m_primitives;

    Asset* m_editingAsset = nullptr;
    bool m_showAssetEditor = false;
    
    float m_previewYaw = 0.0f;
    float m_previewPitch = 0.4f; 
    float m_previewDistance = 1.2f;

    ImGuizmo::OPERATION m_currentGizmoOp = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE m_currentGizmoMode = ImGuizmo::WORLD;

    bool m_isImporting = false;
    bool m_isImportingHDR = false;
    std::string m_importStatus;
    std::string m_pendingImportPath;
    std::string m_pendingImportName;
    std::future<Mesh*> m_importFuture;
    std::future<Texture*> m_importHDRFuture; 

    void StartImport(const std::string& path);
    void StartImportHDR(const std::string& path); 
    
    std::string OpenFileDialog(const char* filter);
    void DrawContentBrowser(std::vector<GameObject>& gameObjects, std::vector<std::shared_ptr<Asset>>& assets); 
    void DrawAssetEditorWindow(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, D3D12_GPU_DESCRIPTOR_HANDLE previewTex); 
};