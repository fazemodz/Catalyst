#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <directxmath.h>
#include <vector>
#include <string>
#include <map> 
#include <wrl.h>
#include <stdexcept> 

// Error Helper
inline void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) throw std::runtime_error("DirectX Error");
}

#include "../Scene/Camera.h"
#include "../Scene/GameObject.h"
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

    // D3D12 Core Objects
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;

    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12Resource> m_depthBuffer;
    
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;

    // Resources
    std::map<std::string, Mesh*> m_primitives;
    Texture* m_defaultTexture = nullptr; 

    // Constant Buffer Structure
    struct ConstantBufferData {
        DirectX::XMMATRIX wvpMatrix;
        DirectX::XMMATRIX worldMatrix; 
        DirectX::XMFLOAT4 colorOverride;
        
        DirectX::XMFLOAT3 lightDir;      
        float lightIntensity;            
        
        DirectX::XMFLOAT3 cameraPos;     
        float padding;
    };

    ComPtr<ID3D12Resource> m_constantBuffer;
    UINT8* m_pCbvDataBegin = nullptr;

    // Scene Data
    std::vector<GameObject> m_gameObjects;
    int m_selectedObjectIndex = -1;

    Camera m_camera;
    UIManager m_ui; 

    // Synchronization
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent;
    uint64_t m_fenceValue = 0;
    uint32_t m_frameIndex = 0;

    // Internal Functions
    void PickObject(int mouseX, int mouseY);
    void CreateDepthBuffer();
    void CreateGraphicsPipeline();
    void CreateDefaultTexture(); 
    void CreateConstantBuffer();
    void FlushGPU();
};