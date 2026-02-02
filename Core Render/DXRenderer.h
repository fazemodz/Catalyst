#pragma once
#include "../Error Handler/Common.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <directxmath.h>
#include <vector>
#include <string>
#include <wrl.h>
#include <DirectXCollision.h> 

#include "../Scene/Camera.h"
#include "../Scene/GameObject.h"
#include "../UI/UIManager.h"
#include "../Resources/Mesh.h"
using Microsoft::WRL::ComPtr;

using Microsoft::WRL::ComPtr;

class DXRenderer {
public:
    void Initialize(HWND hwnd, int width, int height);
    void Render();
    void Shutdown();

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
    
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;

    // --- REPLACED: Raw buffers removed, replaced by Mesh object ---
    Mesh* m_cubeMesh = nullptr;

    struct ConstantBufferData {
        DirectX::XMMATRIX wvpMatrix;
        DirectX::XMFLOAT4 colorOverride;
    };
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
    void CreateGraphicsPipeline();
    void CreateCubeMesh(); // Now uses the Mesh class internally
    void CreateConstantBuffer();
    void FlushGPU();
};