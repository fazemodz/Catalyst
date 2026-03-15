#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <directxmath.h>
#include <vector>
#include <string>
#include <map> 
#include <wrl.h>
#include <stdexcept> 
#include <iostream>
#include <memory> 

#include "../Error handler/Common.h" 
#include "RenderTypes.h"      
#include "Passes/AssetPreviewPass.h" 
#include "Passes/ShadowPass.h" 
#include "Passes/PostProcessPass.h" 
#include "Passes/QuantaMeshPass.h" 
#include "BindlessManager.h"

#include "../Scene/Camera.h"
#include "../Scene/GameObject.h"
#include "../Scene/Asset.h" 
#include "../UI/UIManager.h"
#include "../Resources/Mesh.h"
#include "../Resources/Texture.h"
#include "../Resources/PrimitiveGenerator.h" 

using Microsoft::WRL::ComPtr;

class DXRenderer {
public:
    void Initialize(HWND hwnd, int width, int height);
    void Render();
    void OnResize(int width, int height);
    void Shutdown();
    
    ID3D12Device* GetDevice() { return m_device.Get(); }

private:
    static const uint8_t FrameCount = 2;
    static const int MAX_OBJECTS = 100;

    int m_width, m_height;

    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12Resource> m_depthBuffer;
    
    ShadowPass m_shadowPass;
    PostProcessPass m_postProcessPass;
    QuantaMeshPass m_quantaMeshPass;
    AssetPreviewPass m_previewPass; 
    BindlessManager m_bindlessManager;

    PostProcessSettings m_globalPP;

    ComPtr<ID3D12DescriptorHeap> m_frameSrvHeap;
    UINT m_srvDescriptorSize = 0;
    UINT m_frameHeapOffset = 0; 

    std::map<std::string, Mesh*> m_primitives;
    Texture* m_texWhite = nullptr; 
    Texture* m_texNormal = nullptr;
    Texture* m_texBlack = nullptr; 
    
    std::vector<std::shared_ptr<Asset>> m_assets; 
    ComPtr<ID3D12Resource> m_constantBuffer;
    UINT8* m_pCbvDataBegin = nullptr;
    std::vector<GameObject> m_gameObjects;
    int m_selectedObjectIndex = -1;

    Camera m_camera;
    UIManager m_ui; 

    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent;
    uint64_t m_fenceValue = 0;
    uint32_t m_frameIndex = 0;

    void PickObject(int mouseX, int mouseY);
    void CreateDepthBuffer();
    void CreateDefaultTextures(); 
    void CreateConstantBuffer();
    void CreateFrameHeap();       
    void FlushGPU();
};