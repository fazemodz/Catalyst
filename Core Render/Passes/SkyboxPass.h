#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>
#include "Mesh.h"
#include "Camera.h"

class SkyboxPass {
public:
    void Initialize(ID3D12Device* device);
    void LoadHDR(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const wchar_t* filename);
    void Render(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, Mesh* cubeMesh, Camera& camera, ID3D12DescriptorHeap* bindlessHeap, uint32_t hdrIndex);
    
    ID3D12Resource* GetHDRResource() { return m_skyTexture.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
    
    Microsoft::WRL::ComPtr<ID3D12Resource> m_skyTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_skyUpload;
};