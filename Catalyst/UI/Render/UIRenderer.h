#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>
#include "UIDrawList.h"

using Microsoft::WRL::ComPtr;

class UIRenderer {
public:
    // One set of dynamic buffers per frame-in-flight: the CPU fills the set for
    // the frame it is recording while the GPU may still be reading the other.
    static constexpr uint32_t kFrameCount = 2;

    void Initialize(ID3D12Device* device, DXGI_FORMAT rtvFormat);

    void Render(ID3D12GraphicsCommandList* cmdList, UIDrawList& drawList, float screenWidth, float screenHeight,
                ID3D12DescriptorHeap* bindlessHeap, uint32_t frameIndex);

    void Shutdown();

private:
    struct DynamicBuffers {
        ComPtr<ID3D12Resource> vertexBuffer;
        ComPtr<ID3D12Resource> indexBuffer;
        UIVertex* mappedVertices = nullptr;
        uint32_t* mappedIndices = nullptr;
        uint32_t vertexCapacity = 0;
        uint32_t indexCapacity = 0;
    };

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ID3D12Device* m_device = nullptr;

    DynamicBuffers m_frames[kFrameCount];

    static constexpr uint32_t kInitialVertices = 16384;
    static constexpr uint32_t kInitialIndices  = 24576;

    // Grows (never shrinks) the given frame's buffers to hold at least the
    // requested counts. Returns false if the buffers could not be created.
    bool EnsureCapacity(DynamicBuffers& frame, uint32_t vertexCount, uint32_t indexCount);

    void CreateDynamicBuffers(ID3D12Device* device);
    void CreatePipelineState(ID3D12Device* device, DXGI_FORMAT rtvFormat);
};
