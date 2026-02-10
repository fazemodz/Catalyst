#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <string>
#include <filesystem>
#include <map> 
#include <commdlg.h>
#include <memory> // <--- NEW

#include "../Scene/GameObject.h"
#include "../Scene/Asset.h" 
#include "../Resources/Mesh.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"
#include "ImGuizmo.h"

using Microsoft::WRL::ComPtr;

class UIManager {
public:
    void Initialize(HWND hwnd, ID3D12Device* device, ID3D12CommandQueue* commandQueue, int frameCount);
    
    // Updated signature for Smart Pointers
    void Update(std::vector<GameObject>& gameObjects, std::vector<std::shared_ptr<Asset>>& assets, 
                int& selectedIndex, const DirectX::XMMATRIX& viewMatrix, const DirectX::XMMATRIX& projMatrix);
    
    void Draw(ID3D12GraphicsCommandList* cmdList);
    void Shutdown();

    void SetPrimitives(const std::map<std::string, Mesh*>& primitives) { m_primitives = primitives; }

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

    std::string OpenFileDialog();
    void DrawContentBrowser(std::vector<GameObject>& gameObjects, std::vector<std::shared_ptr<Asset>>& assets); 
    void DrawAssetEditorWindow(ID3D12Device* device, ID3D12CommandQueue* cmdQueue); 
};