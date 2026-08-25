#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <directxmath.h>
#include "../Scene/Asset.h"
#include "../Resources/Texture.h"
#include "../RenderTypes.h" 

using Microsoft::WRL::ComPtr;

class AssetPreviewPass {
public:
    void Initialize(ID3D12Device* device, ID3D12DescriptorHeap* uiHeap);
    
    void Render(ID3D12GraphicsCommandList* cmdList, 
                Asset* asset, 
                ID3D12RootSignature* rootSig, 
                ID3D12PipelineState* pso,
                ID3D12Resource* constantBuffer, 
                UINT8* cbDataStart,             
                D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
                Texture* defaultTexture,
                float yaw, float pitch, float zoom);

    D3D12_GPU_DESCRIPTOR_HANDLE GetOutputHandle() { return m_uiTextureHandleGPU; }

private:
    ComPtr<ID3D12Resource> m_previewTexture;
    ComPtr<ID3D12DescriptorHeap> m_previewRTVHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE m_previewRTVHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE m_uiTextureHandleGPU;
};