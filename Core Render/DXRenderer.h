#pragma once
#include "../Error Handler/Common.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <directxmath.h>

using Microsoft::WRL::ComPtr;

extern bool g_Keys[256];
extern bool g_RightMouseDown;
extern int g_MouseDeltaX;
extern int g_MouseDeltaY;

struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
};

class DXRenderer {
public:
    void Initialize(HWND hwnd);
    void Render();
    void FlushGPU();
    void Shutdown();

private:
    static const uint8_t FrameCount = 2;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    
    // Geometry Resources
    ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    ComPtr<ID3D12Resource> m_indexBuffer;      // <--- NEW
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;  // <--- NEW

    struct ConstantBufferData {
        DirectX::XMMATRIX wvpMatrix;
    };
    ComPtr<ID3D12Resource> m_constantBuffer;
    ConstantBufferData m_cbData;
    UINT8* m_pCbvDataBegin = nullptr;

    // Camera
    DirectX::XMFLOAT3 m_cameraPos = { 0.0f, 0.0f, -4.0f }; // Moved back a bit
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;

    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent;
    uint64_t m_fenceValue = 0;
    uint32_t m_frameIndex = 0;

    void CreateGraphicsPipeline();
    void CreateCubeMesh(); // <--- Renamed from CreateTriangleMesh
    void CreateConstantBuffer();
};