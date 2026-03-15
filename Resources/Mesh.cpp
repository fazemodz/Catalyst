#define NOMINMAX
#include "Mesh.h"
#include <stdexcept>
#include <algorithm>
#include <cmath>

using namespace DirectX;

ComPtr<ID3D12Resource> Mesh::CreateDefaultBuffer(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, const void* initData, UINT64 byteSize, ComPtr<ID3D12Resource>& uploadBuffer) {
    ComPtr<ID3D12Resource> defaultBuffer;
    D3D12_HEAP_PROPERTIES defaultHeap = { D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };
    D3D12_RESOURCE_DESC bufferDesc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, byteSize, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&defaultBuffer)))) throw std::runtime_error("Failed to create Default Buffer");
    D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };
    if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)))) throw std::runtime_error("Failed to create Upload Buffer");
    D3D12_SUBRESOURCE_DATA subResourceData = {}; subResourceData.pData = initData; subResourceData.RowPitch = static_cast<LONG_PTR>(byteSize); subResourceData.SlicePitch = subResourceData.RowPitch;
    void* pData; uploadBuffer->Map(0, nullptr, &pData); memcpy(pData, initData, byteSize); uploadBuffer->Unmap(0, nullptr);
    ComPtr<ID3D12CommandAllocator> cmdAlloc; device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
    ComPtr<ID3D12GraphicsCommandList> cmdList; device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&cmdList));
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

    GenerateMeshlets(vertices, indices);
}

void Mesh::GenerateMeshlets(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    m_meshlets.clear();
    const uint32_t MAX_PRIMS = 126; 
    for (size_t i = 0; i < indices.size(); i += (MAX_PRIMS * 3)) {
        Meshlet m = {};
        m.PrimOffset = static_cast<uint32_t>(i);
        uint32_t remainingIndices = static_cast<uint32_t>(indices.size() - i);
        uint32_t indicesInMeshlet = (std::min)(remainingIndices, MAX_PRIMS * 3);
        m.PrimCount = indicesInMeshlet / 3;
        m.VertexOffset = 0; 
        m.VertexCount = static_cast<uint32_t>(vertices.size()); 

        XMVECTOR minPos = XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, 0);
        XMVECTOR maxPos = XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, 0);
        for (uint32_t j = 0; j < indicesInMeshlet; j++) {
            uint32_t idx = indices[i + j];
            XMVECTOR pos = XMLoadFloat3(&vertices[idx].position);
            minPos = XMVectorMin(minPos, pos);
            maxPos = XMVectorMax(maxPos, pos);
        }
        XMVECTOR center = (minPos + maxPos) * 0.5f;
        XMStoreFloat3(&m.Center, center);
        
        float maxDistSq = 0.0f;
        for (uint32_t j = 0; j < indicesInMeshlet; j++) {
            uint32_t idx = indices[i + j];
            XMVECTOR pos = XMLoadFloat3(&vertices[idx].position);
            XMVECTOR distSqVec = XMVector3LengthSq(pos - center);
            float distSq; XMStoreFloat(&distSq, distSqVec);
            if(distSq > maxDistSq) maxDistSq = distSq;
        }
        m.Radius = sqrtf(maxDistSq);
        m_meshlets.push_back(m);
    }
}

D3D12_VERTEX_BUFFER_VIEW Mesh::GetVertexView() const { return m_vbView; }
D3D12_INDEX_BUFFER_VIEW Mesh::GetIndexView() const { return m_ibView; }