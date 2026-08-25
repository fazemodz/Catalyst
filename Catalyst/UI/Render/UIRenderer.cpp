#include "UIRenderer.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
uint32_t RoundUpCapacity(uint32_t required, uint32_t current) {
    uint32_t capacity = (std::max)(current, 1u);
    while (capacity < required) {
        // Bail out to the exact figure rather than overflowing the doubling.
        if (capacity > (0xFFFFFFFFu / 2u)) {
            return required;
        }
        capacity *= 2u;
    }
    return capacity;
}
}

void UIRenderer::Initialize(ID3D12Device* device, DXGI_FORMAT rtvFormat) {
    m_device = device;
    CreatePipelineState(device, rtvFormat);
    CreateDynamicBuffers(device);
}

void UIRenderer::Render(ID3D12GraphicsCommandList* cmdList, UIDrawList& drawList,
                        float screenWidth, float screenHeight,
                        ID3D12DescriptorHeap* bindlessHeap, uint32_t frameIndex) {
    if (drawList.Vertices.empty()) return;

    DynamicBuffers& frame = m_frames[frameIndex % kFrameCount];

    const uint32_t vertexCount = static_cast<uint32_t>(drawList.Vertices.size());
    const uint32_t indexCount  = static_cast<uint32_t>(drawList.Indices.size());

    // The draw list is unbounded (every glyph is a quad), so grow to fit rather
    // than copying past the end of a fixed allocation.
    if (!EnsureCapacity(frame, vertexCount, indexCount)) {
        return;
    }

    memcpy(frame.mappedVertices, drawList.Vertices.data(), vertexCount * sizeof(UIVertex));
    memcpy(frame.mappedIndices,  drawList.Indices.data(),  indexCount  * sizeof(uint32_t));

    XMMATRIX orthoProj = XMMatrixTranspose(
        XMMatrixOrthographicOffCenterLH(0.0f, screenWidth, screenHeight, 0.0f, 0.0f, 1.0f));

    cmdList->SetPipelineState(m_pipelineState.Get());
    cmdList->SetGraphicsRootSignature(m_rootSignature.Get());
    cmdList->SetGraphicsRoot32BitConstants(0, 16, &orthoProj, 0);
    cmdList->SetGraphicsRootDescriptorTable(1, bindlessHeap->GetGPUDescriptorHandleForHeapStart());

    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = frame.vertexBuffer->GetGPUVirtualAddress();
    vbv.StrideInBytes  = sizeof(UIVertex);
    vbv.SizeInBytes    = vertexCount * sizeof(UIVertex);

    D3D12_INDEX_BUFFER_VIEW ibv = {};
    ibv.BufferLocation = frame.indexBuffer->GetGPUVirtualAddress();
    ibv.Format         = DXGI_FORMAT_R32_UINT;
    ibv.SizeInBytes    = indexCount * sizeof(uint32_t);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &vbv);
    cmdList->IASetIndexBuffer(&ibv);

    const D3D12_RECT fullScreen = {
        0, 0,
        (std::max)(1L, static_cast<LONG>(std::ceil(screenWidth))),
        (std::max)(1L, static_cast<LONG>(std::ceil(screenHeight)))
    };

    for (const auto& cmd : drawList.Commands) {
        if (cmd.ElementCount == 0) {
            continue;
        }

        D3D12_RECT scissor = fullScreen;
        if (cmd.ClipRect.Enabled) {
            const LONG left   = (std::clamp)(static_cast<LONG>(std::floor(cmd.ClipRect.X)),                      0L, fullScreen.right);
            const LONG top    = (std::clamp)(static_cast<LONG>(std::floor(cmd.ClipRect.Y)),                      0L, fullScreen.bottom);
            const LONG right  = (std::clamp)(static_cast<LONG>(std::ceil(cmd.ClipRect.X + cmd.ClipRect.Width)),  left, fullScreen.right);
            const LONG bottom = (std::clamp)(static_cast<LONG>(std::ceil(cmd.ClipRect.Y + cmd.ClipRect.Height)), top,  fullScreen.bottom);
            scissor = { left, top, right, bottom };
        }
        cmdList->RSSetScissorRects(1, &scissor);

        uint32_t texIdx = (uint32_t)cmd.TextureID;
        cmdList->SetGraphicsRoot32BitConstants(0, 1, &texIdx, 16);
        cmdList->DrawIndexedInstanced(cmd.ElementCount, 1, cmd.IndexOffset, 0, 0);
    }
}

bool UIRenderer::EnsureCapacity(DynamicBuffers& frame, uint32_t vertexCount, uint32_t indexCount) {
    if (m_device == nullptr) {
        return false;
    }
    if (frame.vertexBuffer && frame.indexBuffer &&
        vertexCount <= frame.vertexCapacity && indexCount <= frame.indexCapacity) {
        return true;
    }

    const uint32_t newVertexCapacity = RoundUpCapacity(vertexCount, (std::max)(frame.vertexCapacity, kInitialVertices));
    const uint32_t newIndexCapacity  = RoundUpCapacity(indexCount,  (std::max)(frame.indexCapacity,  kInitialIndices));

    D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };

    auto makeBuffer = [&](UINT64 byteSize, ComPtr<ID3D12Resource>& outResource, void** outMapped) -> bool {
        D3D12_RESOURCE_DESC desc = {
            D3D12_RESOURCE_DIMENSION_BUFFER, 0,
            byteSize,
            1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
            D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE
        };
        if (FAILED(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&outResource)))) {
            return false;
        }
        return SUCCEEDED(outResource->Map(0, nullptr, outMapped));
    };

    // Build both replacements before touching the live ones: a half-applied
    // grow would leave the frame holding an unmapped buffer and a stale pointer.
    ComPtr<ID3D12Resource> newVertexBuffer;
    ComPtr<ID3D12Resource> newIndexBuffer;
    void* mappedVertices = nullptr;
    void* mappedIndices  = nullptr;

    if (!makeBuffer(static_cast<UINT64>(newVertexCapacity) * sizeof(UIVertex), newVertexBuffer, &mappedVertices) ||
        !makeBuffer(static_cast<UINT64>(newIndexCapacity) * sizeof(uint32_t), newIndexBuffer, &mappedIndices)) {
        return false;
    }

    // Safe to retire the old buffers: the caller already waited on this frame's
    // fence, so the GPU is not reading them.
    if (frame.vertexBuffer) frame.vertexBuffer->Unmap(0, nullptr);
    if (frame.indexBuffer)  frame.indexBuffer->Unmap(0, nullptr);

    frame.vertexBuffer   = newVertexBuffer;
    frame.indexBuffer    = newIndexBuffer;
    frame.mappedVertices = static_cast<UIVertex*>(mappedVertices);
    frame.mappedIndices  = static_cast<uint32_t*>(mappedIndices);
    frame.vertexCapacity = newVertexCapacity;
    frame.indexCapacity  = newIndexCapacity;
    return true;
}

void UIRenderer::Shutdown() {
    for (DynamicBuffers& frame : m_frames) {
        if (frame.vertexBuffer) frame.vertexBuffer->Unmap(0, nullptr);
        if (frame.indexBuffer)  frame.indexBuffer->Unmap(0, nullptr);
        frame.mappedVertices = nullptr;
        frame.mappedIndices  = nullptr;
    }
}
