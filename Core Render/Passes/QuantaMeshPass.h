#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <map>
#include "RenderTypes.h"
#include "Camera.h"
#include "GameObject.h"
#include "Texture.h"
#include "Mesh.h" 

using Microsoft::WRL::ComPtr;

class QuantaMeshPass {
public:
    void Initialize(ID3D12Device* device);
    void Render(ID3D12GraphicsCommandList* commandList, ID3D12Device* device,
                const std::vector<GameObject>& gameObjects, const Camera& camera,
                int width, int height,
                DirectX::XMMATRIX lightSpaceMatrix, DirectX::XMFLOAT3 activeLightDir, float activeIntensity, Texture* activeSkybox,
                ID3D12DescriptorHeap* bindlessHeap, UINT srvDescriptorSize, UINT& frameHeapOffset,
                Texture* texWhite, Texture* texNormal, Texture* texBlack, ID3D12DescriptorHeap* shadowSrvHeap,
                Mesh* defaultSphereMesh); 

    ID3D12RootSignature* GetPreviewRootSignature() { return m_previewRootSignature.Get(); }
    ID3D12PipelineState* GetPreviewPipelineState() { return m_previewPipelineState.Get(); }

private:
    ComPtr<ID3D12RootSignature> m_computeRootSignature;
    ComPtr<ID3D12PipelineState> m_computePipelineState;
    ComPtr<ID3D12CommandSignature> m_commandSignature;
    ComPtr<ID3D12RootSignature> m_quantaRootSignature;
    ComPtr<ID3D12PipelineState> m_quantaPipelineState;

    ComPtr<ID3D12Resource> m_globalCB;
    GlobalBufferData* m_mappedGlobalCB = nullptr;
    ComPtr<ID3D12Resource> m_materialCB;
    MaterialConstants* m_mappedMaterialCB = nullptr;
    ComPtr<ID3D12Resource> m_objectDataBuffer;
    ObjectData* m_mappedObjectData = nullptr;
    ComPtr<ID3D12Resource> m_commandBuffer;
    ComPtr<ID3D12Resource> m_counterBuffer;
    ComPtr<ID3D12Resource> m_zeroBuffer;

    ComPtr<ID3D12RootSignature> m_previewRootSignature;
    ComPtr<ID3D12PipelineState> m_previewPipelineState; 
    ComPtr<ID3D12RootSignature> m_skyboxRootSignature;
    ComPtr<ID3D12PipelineState> m_skyboxPipelineState;

    void CreatePipelines(ID3D12Device* device);
    void CreateComputeBuffers(ID3D12Device* device);
};