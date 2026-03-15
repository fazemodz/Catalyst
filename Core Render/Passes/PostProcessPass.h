#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include "../RenderTypes.h"
#include "../../Scene/Camera.h"
#include "../../Scene/GameObject.h"

using Microsoft::WRL::ComPtr;

class PostProcessPass {
public:
    void Initialize(ID3D12Device* device, int width, int height);
    void OnResize(ID3D12Device* device, int width, int height);
    
    void TransitionToRTV(ID3D12GraphicsCommandList* commandList);
    void TransitionToSRV(ID3D12GraphicsCommandList* commandList);
    void Clear(ID3D12GraphicsCommandList* commandList);
    
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV() { return m_hdrRtvHeap->GetCPUDescriptorHandleForHeapStart(); }
    ID3D12Resource* GetRenderTarget() { return m_hdrRenderTarget.Get(); }

    void Render(ID3D12GraphicsCommandList* commandList, const Camera& camera, const std::vector<GameObject>& gameObjects, const PostProcessSettings& globalPP, int width, int height);

private:
    ComPtr<ID3D12Resource> m_hdrRenderTarget;
    ComPtr<ID3D12DescriptorHeap> m_hdrRtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_hdrSrvHeap;
    ComPtr<ID3D12RootSignature> m_postProcessRootSignature;
    ComPtr<ID3D12PipelineState> m_postProcessPipelineState;

    void CreateOffscreenRenderTarget(ID3D12Device* device, int width, int height);
    void CreatePipeline(ID3D12Device* device);
};