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
    // Resources the CPU writes each frame are kept per frame-in-flight, so the
    // renderer no longer has to stall the GPU between frames to reuse them.
    static constexpr UINT kFrameCount = 2;

    void Initialize(ID3D12Device* device);
    void Render(ID3D12GraphicsCommandList* commandList, ID3D12Device* device,
                const std::vector<GameObject>& gameObjects, const Camera& camera,
                int width, int height,
                DirectX::XMMATRIX lightSpaceMatrix, DirectX::XMFLOAT3 activeLightDir, float activeIntensity, Texture* activeSkybox,
                ID3D12DescriptorHeap* bindlessHeap, UINT srvDescriptorSize, UINT frameIndex, bool writeGBuffer,
                Texture* texWhite, Texture* texNormal, Texture* texBlack, ID3D12DescriptorHeap* shadowSrvHeap,
                Mesh* defaultSphereMesh); 

private:
    ComPtr<ID3D12RootSignature> m_computeRootSignature;
    ComPtr<ID3D12PipelineState> m_computePipelineState;
    ComPtr<ID3D12CommandSignature> m_commandSignature;
    ComPtr<ID3D12RootSignature> m_quantaRootSignature;
    ComPtr<ID3D12PipelineState> m_quantaPipelineState;
    ComPtr<ID3D12PipelineState> m_quantaPipelineStateGBuffer;

    ComPtr<ID3D12Resource> m_globalCB[kFrameCount];
    GlobalBufferData* m_mappedGlobalCB[kFrameCount] = {};
    ComPtr<ID3D12Resource> m_materialCB[kFrameCount];
    MaterialConstants* m_mappedMaterialCB[kFrameCount] = {};
    ComPtr<ID3D12Resource> m_objectDataBuffer[kFrameCount];
    ObjectData* m_mappedObjectData[kFrameCount] = {};
    ComPtr<ID3D12Resource> m_commandBuffer[kFrameCount];
    ComPtr<ID3D12Resource> m_counterBuffer[kFrameCount];
    ComPtr<ID3D12Resource> m_zeroBuffer; // immutable, safe to share across frames
    bool m_indirectBuffersInitialized[kFrameCount] = {};

    void CreatePipelines(ID3D12Device* device);
    void CreateComputeBuffers(ID3D12Device* device);
};
