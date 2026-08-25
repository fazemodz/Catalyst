#define NOMINMAX
#include "Mesh.h"
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <DirectXMath.h>

using namespace DirectX;

// =========================================================================
//  PROCEDURAL MESHES
// =========================================================================
Mesh::Mesh(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const Vertex* vertices, size_t vertexCount, const uint32_t* indices, size_t indexCount) {
    m_indexCount = static_cast<uint32_t>(indexCount);

    UINT vbSize = static_cast<UINT>(vertexCount * sizeof(Vertex));
    UINT ibSize = static_cast<UINT>(indexCount * sizeof(uint32_t));

    // Create Default and Upload Heaps for Vertices
    D3D12_HEAP_PROPERTIES defaultHeap = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC vDesc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, vbSize, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    
    device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &vDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_vertexBuffer));
    device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &vDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertexUploadBuffer));
    
    // Copy Vertex Data
    void* pData;
    m_vertexUploadBuffer->Map(0, nullptr, &pData);
    memcpy(pData, vertices, vbSize);
    m_vertexUploadBuffer->Unmap(0, nullptr);

    D3D12_RESOURCE_BARRIER vertexCopyBarrier = {};
    vertexCopyBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    vertexCopyBarrier.Transition.pResource = m_vertexBuffer.Get();
    vertexCopyBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    vertexCopyBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    vertexCopyBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &vertexCopyBarrier);

    cmdList->CopyBufferRegion(m_vertexBuffer.Get(), 0, m_vertexUploadBuffer.Get(), 0, vbSize);

    // Create Default and Upload Heaps for Indices
    D3D12_RESOURCE_DESC iDesc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, ibSize, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &iDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_indexBuffer));
    device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &iDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_indexUploadBuffer));

    // Copy Index Data
    m_indexUploadBuffer->Map(0, nullptr, &pData);
    memcpy(pData, indices, ibSize);
    m_indexUploadBuffer->Unmap(0, nullptr);

    D3D12_RESOURCE_BARRIER indexCopyBarrier = {};
    indexCopyBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    indexCopyBarrier.Transition.pResource = m_indexBuffer.Get();
    indexCopyBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    indexCopyBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    indexCopyBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &indexCopyBarrier);

    cmdList->CopyBufferRegion(m_indexBuffer.Get(), 0, m_indexUploadBuffer.Get(), 0, ibSize);

    // Transition Buffers
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = m_vertexBuffer.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = m_indexBuffer.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
    
    cmdList->ResourceBarrier(2, barriers);

    // Setup Views
    m_vbView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vbView.StrideInBytes = sizeof(Vertex);
    m_vbView.SizeInBytes = vbSize;

    m_ibView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_ibView.SizeInBytes = ibSize;
    m_ibView.Format = DXGI_FORMAT_R32_UINT;
    
    // Bounds Calculation
    if (vertexCount > 0) {
        XMVECTOR minV = XMLoadFloat3(&vertices[0].position);
        XMVECTOR maxV = minV;
        for (size_t i = 1; i < vertexCount; i++) {
            XMVECTOR pos = XMLoadFloat3(&vertices[i].position);
            minV = XMVectorMin(minV, pos);
            maxV = XMVectorMax(maxV, pos);
        }
        XMVECTOR center = (minV + maxV) * 0.5f;
        XMVECTOR extents = (maxV - minV) * 0.5f;
        XMStoreFloat3(&m_bounds.Center, center);
        XMStoreFloat3(&m_bounds.Extents, extents);
    }

    GenerateMeshlets(vertices, static_cast<uint32_t>(vertexCount), indices, static_cast<uint32_t>(indexCount));
}
// =========================================================================

ComPtr<ID3D12Resource> Mesh::CreateDefaultBuffer(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, const void* initData, UINT64 byteSize, ComPtr<ID3D12Resource>& uploadBuffer) {
    ComPtr<ID3D12Resource> defaultBuffer;
    D3D12_HEAP_PROPERTIES defaultHeap = { D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };
    D3D12_RESOURCE_DESC bufferDesc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, byteSize, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&defaultBuffer)))) throw std::runtime_error("Failed to create Default Buffer");
    D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };
    if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)))) throw std::runtime_error("Failed to create Upload Buffer");
    D3D12_SUBRESOURCE_DATA subResourceData = {}; subResourceData.pData = initData; subResourceData.RowPitch = static_cast<LONG_PTR>(byteSize); subResourceData.SlicePitch = subResourceData.RowPitch;
    void* pData; uploadBuffer->Map(0, nullptr, &pData); memcpy(pData, initData, byteSize); uploadBuffer->Unmap(0, nullptr);
    ComPtr<ID3D12CommandAllocator> cmdAlloc; device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
    ComPtr<ID3D12GraphicsCommandList> cmdList; device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&cmdList));
    D3D12_RESOURCE_BARRIER copyBarrier = {};
    copyBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    copyBarrier.Transition.pResource = defaultBuffer.Get();
    copyBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    copyBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    copyBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &copyBarrier);
    cmdList->CopyBufferRegion(defaultBuffer.Get(), 0, uploadBuffer.Get(), 0, byteSize);
    D3D12_RESOURCE_BARRIER barrier = {}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barrier.Transition.pResource = defaultBuffer.Get(); barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER;
    cmdList->ResourceBarrier(1, &barrier); cmdList->Close();
    ID3D12CommandList* lists[] = { cmdList.Get() }; cmdQueue->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> fence; device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr); cmdQueue->Signal(fence.Get(), 1);
    if (fence->GetCompletedValue() < 1) { fence->SetEventOnCompletion(1, eventHandle); WaitForSingleObject(eventHandle, INFINITE); }
    CloseHandle(eventHandle);
    return defaultBuffer;
}

void Mesh::Initialize(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    m_indexCount = static_cast<uint32_t>(indices.size());
    ComPtr<ID3D12Resource> vUploadHeap, iUploadHeap;

    UINT vbSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
    m_vertexBuffer = CreateDefaultBuffer(device, cmdQueue, vertices.data(), vbSize, vUploadHeap);
    m_vbView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress(); m_vbView.StrideInBytes = sizeof(Vertex); m_vbView.SizeInBytes = vbSize;

    UINT ibSize = static_cast<UINT>(indices.size() * sizeof(uint32_t));
    m_indexBuffer = CreateDefaultBuffer(device, cmdQueue, indices.data(), ibSize, iUploadHeap);
    m_ibView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress(); m_ibView.SizeInBytes = ibSize; m_ibView.Format = DXGI_FORMAT_R32_UINT;

    // --- CALCULATE BOUNDING BOX ---
    if (!vertices.empty()) {
        XMVECTOR minV = XMLoadFloat3(&vertices[0].position);
        XMVECTOR maxV = minV;
        for (const auto& v : vertices) {
            XMVECTOR pos = XMLoadFloat3(&v.position);
            minV = XMVectorMin(minV, pos);
            maxV = XMVectorMax(maxV, pos);
        }
        XMVECTOR center = (minV + maxV) * 0.5f;
        XMVECTOR extents = (maxV - minV) * 0.5f;
        XMStoreFloat3(&m_bounds.Center, center);
        XMStoreFloat3(&m_bounds.Extents, extents);
    }

    GenerateMeshlets(vertices.data(), static_cast<uint32_t>(vertices.size()),
                     indices.data(), static_cast<uint32_t>(indices.size()));
}

void Mesh::GenerateMeshlets(const Vertex* vertices, uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount) {
    m_meshlets.clear();
    if (vertices == nullptr || indices == nullptr || vertexCount == 0 || indexCount < 3) {
        return;
    }

    const uint32_t indicesPerMeshlet = kMeshletMaxPrimitives * 3;
    m_meshlets.reserve(indexCount / indicesPerMeshlet + 1);

    for (uint32_t first = 0; first < indexCount; first += indicesPerMeshlet) {
        Meshlet meshlet = {};
        meshlet.PrimOffset = first;

        const uint32_t remaining = indexCount - first;
        const uint32_t count = (std::min)(remaining, indicesPerMeshlet);
        meshlet.PrimCount = count / 3;
        meshlet.VertexOffset = 0;
        meshlet.VertexCount = vertexCount;

        XMVECTOR minPosition = XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, 0.0f);
        XMVECTOR maxPosition = XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, 0.0f);
        for (uint32_t offset = 0; offset < count; ++offset) {
            const uint32_t index = indices[first + offset];
            // A malformed model can index past the vertex array; skipping keeps
            // this from reading out of bounds.
            if (index >= vertexCount) {
                continue;
            }
            const XMVECTOR position = XMLoadFloat3(&vertices[index].position);
            minPosition = XMVectorMin(minPosition, position);
            maxPosition = XMVectorMax(maxPosition, position);
        }

        const XMVECTOR center = (minPosition + maxPosition) * 0.5f;
        XMStoreFloat3(&meshlet.Center, center);

        float maxDistanceSquared = 0.0f;
        for (uint32_t offset = 0; offset < count; ++offset) {
            const uint32_t index = indices[first + offset];
            if (index >= vertexCount) {
                continue;
            }
            const XMVECTOR position = XMLoadFloat3(&vertices[index].position);
            float distanceSquared = 0.0f;
            XMStoreFloat(&distanceSquared, XMVector3LengthSq(position - center));
            maxDistanceSquared = (std::max)(maxDistanceSquared, distanceSquared);
        }
        meshlet.Radius = sqrtf(maxDistanceSquared);

        m_meshlets.push_back(meshlet);
    }
}

D3D12_VERTEX_BUFFER_VIEW Mesh::GetVertexView() const { return m_vbView; }
D3D12_INDEX_BUFFER_VIEW Mesh::GetIndexView() const { return m_ibView; }
void Mesh::Draw(ID3D12GraphicsCommandList* cmdList) {
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &m_vbView); 
    cmdList->IASetIndexBuffer(&m_ibView);
    cmdList->DrawIndexedInstanced(m_indexCount, 1, 0, 0, 0);
}
