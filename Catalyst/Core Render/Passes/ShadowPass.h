#pragma once
#include <d3d12.h>
#include <directxmath.h>
#include <wrl.h>
#include <vector>
#include "RenderTypes.h"
#include "GameObject.h"

using Microsoft::WRL::ComPtr;

// Side length of the directional light's depth map. The renderer needs this to
// work out how large a texel is in world units, which is what the shadow bias
// and the PCF kernel are measured in.
static constexpr UINT kShadowMapResolution = 2048;

// Matches the slot count the renderer allocates in its shared constant buffer.
static constexpr UINT kMaxShadowCasters = 1000;

class ShadowPass {
public:
    void Initialize(ID3D12Device* device);
    
    void Render(ID3D12GraphicsCommandList* commandList, 
                const std::vector<GameObject>& gameObjects, 
                UINT8* pCbvDataBegin, 
                D3D12_GPU_VIRTUAL_ADDRESS cbAddress, 
                UINT objSize,
                DirectX::XMMATRIX lightViewProj);

    ID3D12Resource* GetShadowMap() { return m_shadowMap.Get(); }
    ID3D12DescriptorHeap* GetSRVHeap() { return m_shadowSrvHeap.Get(); }

private:
    ComPtr<ID3D12Resource> m_shadowMap;
    ComPtr<ID3D12DescriptorHeap> m_shadowDsvHeap;
    ComPtr<ID3D12DescriptorHeap> m_shadowSrvHeap;
    ComPtr<ID3D12RootSignature> m_shadowRootSignature;
    ComPtr<ID3D12PipelineState> m_shadowPipelineState;

    void CreateResources(ID3D12Device* device);
    void CreatePipeline(ID3D12Device* device);
};