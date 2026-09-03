#define NOMINMAX
#include "Mesh.h"
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <functional>
#include <thread>
#include <DirectXMath.h>

using namespace DirectX;

namespace {

// Bounds and meshlet bounds are both pure per-element work over arrays that can
// run to tens of millions of entries, which is long enough to be worth
// spreading over the cores rather than walking on one.
void ParallelForRange(size_t total, const std::function<void(size_t, size_t)>& body) {
    if (total == 0) {
        return;
    }

    size_t workerCount = (std::max)(1u, std::thread::hardware_concurrency());
    if (total < 32768) {
        workerCount = 1;
    }
    workerCount = (std::min)(workerCount, total);

    if (workerCount == 1) {
        body(0, total);
        return;
    }

    std::vector<std::thread> workers;
    workers.reserve(workerCount - 1);
    auto run = [&](size_t worker) {
        const size_t begin = total * worker / workerCount;
        const size_t end = total * (worker + 1) / workerCount;
        if (begin < end) {
            body(begin, end);
        }
    };
    for (size_t worker = 1; worker < workerCount; ++worker) {
        workers.emplace_back(run, worker);
    }
    run(0);
    for (std::thread& worker : workers) {
        worker.join();
    }
}

// A vertex buffer view can only describe 4 GB and DrawIndexedInstanced takes a
// UINT, so this is the ceiling a single Mesh can represent. Failing here beats
// silently truncating the size and rendering garbage.
void ValidateBufferSize(UINT64 byteSize, const char* what) {
    if (byteSize > 0xFFFFFFFFull) {
        throw std::runtime_error(std::string("Mesh ") + what +
                                 " exceeds the 4 GB a D3D12 buffer view can address. Split the model into several meshes.");
    }
}

}

// =========================================================================
//  PROCEDURAL MESHES
// =========================================================================
Mesh::Mesh(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const Vertex* vertices, size_t vertexCount, const uint32_t* indices, size_t indexCount) {
    m_indexCount = static_cast<uint32_t>(indexCount);
    m_vertexCount = static_cast<uint32_t>(vertexCount);

    // The buffer holds the packed form; the caller's Vertex array stays as it is
    // so bounds and clusters can still be computed from plain floats below.
    std::vector<PackedVertex> packedVertices(vertexCount);
    ParallelForRange(vertexCount, [&](size_t begin, size_t end) {
        for (size_t index = begin; index < end; ++index) {
            packedVertices[index] = PackVertex(vertices[index]);
        }
    });

    const UINT64 vbSize = static_cast<UINT64>(vertexCount) * sizeof(PackedVertex);
    const UINT64 ibSize = static_cast<UINT64>(indexCount) * sizeof(uint32_t);
    ValidateBufferSize(vbSize, "vertex data");
    ValidateBufferSize(ibSize, "index data");

    // Create Default and Upload Heaps for Vertices
    D3D12_HEAP_PROPERTIES defaultHeap = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC vDesc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, vbSize, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    
    device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &vDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_vertexBuffer));
    device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &vDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertexUploadBuffer));
    
    // Copy Vertex Data
    void* pData;
    m_vertexUploadBuffer->Map(0, nullptr, &pData);
    memcpy(pData, packedVertices.data(), vbSize);
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
    m_vbView.StrideInBytes = sizeof(PackedVertex);
    m_vbView.SizeInBytes = static_cast<UINT>(vbSize);

    m_ibView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_ibView.SizeInBytes = static_cast<UINT>(ibSize);
    m_ibView.Format = DXGI_FORMAT_R32_UINT;
    
    ComputeBounds(vertices, vertexCount);

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
    m_vertexCount = static_cast<uint32_t>(vertices.size());
    ComPtr<ID3D12Resource> vUploadHeap, iUploadHeap;

    std::vector<PackedVertex> packedVertices(vertices.size());
    ParallelForRange(vertices.size(), [&](size_t begin, size_t end) {
        for (size_t index = begin; index < end; ++index) {
            packedVertices[index] = PackVertex(vertices[index]);
        }
    });

    const UINT64 vbSize = static_cast<UINT64>(vertices.size()) * sizeof(PackedVertex);
    const UINT64 ibSize = static_cast<UINT64>(indices.size()) * sizeof(uint32_t);
    ValidateBufferSize(vbSize, "vertex data");
    ValidateBufferSize(ibSize, "index data");

    m_vertexBuffer = CreateDefaultBuffer(device, cmdQueue, packedVertices.data(), vbSize, vUploadHeap);
    m_vbView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress(); m_vbView.StrideInBytes = sizeof(PackedVertex); m_vbView.SizeInBytes = static_cast<UINT>(vbSize);

    m_indexBuffer = CreateDefaultBuffer(device, cmdQueue, indices.data(), ibSize, iUploadHeap);
    m_ibView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress(); m_ibView.SizeInBytes = static_cast<UINT>(ibSize); m_ibView.Format = DXGI_FORMAT_R32_UINT;

    ComputeBounds(vertices.data(), vertices.size());

    GenerateMeshlets(vertices.data(), static_cast<uint32_t>(vertices.size()),
                     indices.data(), static_cast<uint32_t>(indices.size()));
}

void Mesh::ComputeBounds(const Vertex* vertices, size_t vertexCount) {
    if (vertices == nullptr || vertexCount == 0) {
        m_bounds.Center = {0.0f, 0.0f, 0.0f};
        m_bounds.Extents = {0.0f, 0.0f, 0.0f};
        return;
    }

    // Each worker reduces its own slice into its own slot, then the slots are
    // combined. Nothing is shared for writing, so there is no synchronisation.
    size_t workerCount = (std::max)(1u, std::thread::hardware_concurrency());
    // Both operands are at least one, so the result never reaches zero.
    workerCount = (std::min)(workerCount, (vertexCount + 32767) / 32768);

    struct Range {
        XMFLOAT3 minimum;
        XMFLOAT3 maximum;
    };
    std::vector<Range> ranges(workerCount);

    auto reduce = [&](size_t worker) {
        const size_t begin = vertexCount * worker / workerCount;
        const size_t end = vertexCount * (worker + 1) / workerCount;

        XMVECTOR minV = XMLoadFloat3(&vertices[0].position);
        XMVECTOR maxV = minV;
        for (size_t index = begin; index < end; ++index) {
            const XMVECTOR position = XMLoadFloat3(&vertices[index].position);
            minV = XMVectorMin(minV, position);
            maxV = XMVectorMax(maxV, position);
        }
        XMStoreFloat3(&ranges[worker].minimum, minV);
        XMStoreFloat3(&ranges[worker].maximum, maxV);
    };

    if (workerCount == 1) {
        reduce(0);
    } else {
        std::vector<std::thread> workers;
        workers.reserve(workerCount - 1);
        for (size_t worker = 1; worker < workerCount; ++worker) {
            workers.emplace_back(reduce, worker);
        }
        reduce(0);
        for (std::thread& worker : workers) {
            worker.join();
        }
    }

    XMVECTOR minV = XMLoadFloat3(&ranges[0].minimum);
    XMVECTOR maxV = XMLoadFloat3(&ranges[0].maximum);
    for (size_t slot = 1; slot < ranges.size(); ++slot) {
        minV = XMVectorMin(minV, XMLoadFloat3(&ranges[slot].minimum));
        maxV = XMVectorMax(maxV, XMLoadFloat3(&ranges[slot].maximum));
    }

    XMStoreFloat3(&m_bounds.Center, (minV + maxV) * 0.5f);
    XMStoreFloat3(&m_bounds.Extents, (maxV - minV) * 0.5f);
}

void Mesh::UploadClusters(ID3D12Device* device,
                          ID3D12GraphicsCommandList* cmdList,
                          const std::vector<MeshCluster>& clusters,
                          uint32_t levelCount,
                          uint32_t baseTriangleCount) {
    m_clusters = clusters;
    m_clusterLevelCount = levelCount;
    m_baseTriangleCount = baseTriangleCount;
    if (clusters.empty() || device == nullptr || cmdList == nullptr) {
        return;
    }

    const UINT64 byteSize = static_cast<UINT64>(clusters.size()) * sizeof(MeshCluster);

    D3D12_HEAP_PROPERTIES defaultHeap = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC desc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, byteSize, 1, 1, 1,
                                 DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                 D3D12_RESOURCE_FLAG_NONE };

    if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_COMMON, nullptr,
                                               IID_PPV_ARGS(&m_clusterBuffer)))) {
        m_clusters.clear();
        return;
    }
    if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&m_clusterUploadBuffer)))) {
        m_clusterBuffer.Reset();
        m_clusters.clear();
        return;
    }

    void* mapped = nullptr;
    m_clusterUploadBuffer->Map(0, nullptr, &mapped);
    memcpy(mapped, clusters.data(), static_cast<size_t>(byteSize));
    m_clusterUploadBuffer->Unmap(0, nullptr);

    D3D12_RESOURCE_BARRIER toCopy = {};
    toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopy.Transition.pResource = m_clusterBuffer.Get();
    toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &toCopy);

    cmdList->CopyBufferRegion(m_clusterBuffer.Get(), 0, m_clusterUploadBuffer.Get(), 0, byteSize);

    D3D12_RESOURCE_BARRIER toRead = toCopy;
    toRead.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    toRead.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(1, &toRead);
}

void Mesh::ReleaseUploadBuffers() {
    m_vertexUploadBuffer.Reset();
    m_indexUploadBuffer.Reset();
    m_clusterUploadBuffer.Reset();
}

void Mesh::TakeUploadBuffers(std::vector<ComPtr<ID3D12Resource>>& outStagingBuffers) {
    // Every staging buffer here was filled into a command list the caller has
    // not submitted yet, so all of them have to stay alive until that work
    // retires - including the cluster table.
    if (m_vertexUploadBuffer) {
        outStagingBuffers.push_back(std::move(m_vertexUploadBuffer));
    }
    if (m_indexUploadBuffer) {
        outStagingBuffers.push_back(std::move(m_indexUploadBuffer));
    }
    if (m_clusterUploadBuffer) {
        outStagingBuffers.push_back(std::move(m_clusterUploadBuffer));
    }
    m_vertexUploadBuffer.Reset();
    m_indexUploadBuffer.Reset();
    m_clusterUploadBuffer.Reset();
}

void Mesh::GenerateMeshlets(const Vertex* vertices, uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount) {
    m_meshlets.clear();
    if (vertices == nullptr || indices == nullptr || vertexCount == 0 || indexCount < 3) {
        return;
    }

    const uint32_t indicesPerMeshlet = kMeshletMaxPrimitives * 3;
    const size_t meshletCount = (indexCount + indicesPerMeshlet - 1) / indicesPerMeshlet;
    m_meshlets.resize(meshletCount);

    // Meshlets are independent of one another, so a dense mesh can build all of
    // its bounding spheres at once.
    ParallelForRange(meshletCount, [&](size_t begin, size_t end) {
        for (size_t slot = begin; slot < end; ++slot) {
            const uint32_t first = static_cast<uint32_t>(slot) * indicesPerMeshlet;
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

            m_meshlets[slot] = meshlet;
        }
    });
}

D3D12_VERTEX_BUFFER_VIEW Mesh::GetVertexView() const { return m_vbView; }
D3D12_INDEX_BUFFER_VIEW Mesh::GetIndexView() const { return m_ibView; }
void Mesh::Draw(ID3D12GraphicsCommandList* cmdList) {
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &m_vbView); 
    cmdList->IASetIndexBuffer(&m_ibView);
    cmdList->DrawIndexedInstanced(m_indexCount, 1, 0, 0, 0);
}
