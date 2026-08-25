#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include "RenderTypes.h"
#include "Camera.h"
#include "GameObject.h"

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

    // width/height describe the HDR target; sceneWidth/sceneHeight describe the
    // top-left region of it the scene was actually rasterised into.
    void Render(ID3D12GraphicsCommandList* commandList, const Camera& camera, const std::vector<GameObject>& gameObjects, const PostProcessSettings& globalPP, int width, int height, int sceneWidth, int sceneHeight,
                bool raytracingActive = false, float shadowStrength = 0.0f, float aoStrength = 0.0f);

    // Points slots t1/t2 of the composite at the raytracing outputs. Safe to
    // call whenever those resources are recreated.
    void SetRaytracingInputs(ID3D12Device* device, ID3D12Resource* shadowMask, ID3D12Resource* reflections);

private:
    ComPtr<ID3D12Resource> m_hdrRenderTarget;
    ComPtr<ID3D12DescriptorHeap> m_hdrRtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_hdrSrvHeap;
    ComPtr<ID3D12RootSignature> m_postProcessRootSignature;
    ComPtr<ID3D12PipelineState> m_postProcessPipelineState;
    bool m_hasRaytracingInputs = false;

    void CreateOffscreenRenderTarget(ID3D12Device* device, int width, int height);
    void CreatePipeline(ID3D12Device* device);
};