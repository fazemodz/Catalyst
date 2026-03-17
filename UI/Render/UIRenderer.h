#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>
#include "UIDrawList.h"

using Microsoft::WRL::ComPtr;

class UIRenderer {
public:
    void Initialize(ID3D12Device* device, DXGI_FORMAT rtvFormat);
    
    void Render(ID3D12GraphicsCommandList* cmdList, UIDrawList& drawList, float screenWidth, float screenHeight, ID3D12DescriptorHeap* bindlessHeap);
    
    void Shutdown();

private:
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    
    UIVertex* m_mappedVertices = nullptr;
    uint32_t* m_mappedIndices = nullptr;

    const uint32_t m_maxVertices = 10000;
    const uint32_t m_maxIndices = 30000;

    void CreateDynamicBuffers(ID3D12Device* device);
    void CreatePipelineState(ID3D12Device* device, DXGI_FORMAT rtvFormat);
};