#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <directxmath.h>
#include <DirectXCollision.h>
#include <cmath>
#include <cstdint>

struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
    DirectX::XMFLOAT2 uv;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT3 tangent;
};

// What actually reaches the GPU. The authoring Vertex above stays 60 bytes so
// the import and simplification code can work in plain floats; this is the
// 32-byte form the vertex buffer holds, which is close to half the bandwidth
// and half the video memory for the same geometry.
//
// Positions keep full precision - a large scene cannot afford quantised
// positions - and so do UVs, where 16-bit floats would land several texels off
// on a 4K texture. Normals and tangents are octahedral pairs, which carries a
// unit vector far more accurately than three 16-bit components would.
struct PackedVertex {
    DirectX::XMFLOAT3 position;  // offset  0, R32G32B32_FLOAT
    uint32_t color;              // offset 12, R8G8B8A8_UNORM
    DirectX::XMFLOAT2 uv;        // offset 16, R32G32_FLOAT
    int16_t normal[2];           // offset 24, R16G16_SNORM, octahedral
    int16_t tangent[2];          // offset 28, R16G16_SNORM, octahedral
};
static_assert(sizeof(PackedVertex) == 32, "PackedVertex must match the input layout");

inline int16_t QuantiseSnorm16(float value) {
    const float clamped = value < -1.0f ? -1.0f : (value > 1.0f ? 1.0f : value);
    return static_cast<int16_t>(clamped * 32767.0f + (clamped >= 0.0f ? 0.5f : -0.5f));
}

// Folds the unit sphere onto an octahedron and unwraps it into the square. The
// error is far more even than a naive xy projection, and the z sign comes back
// for free.
inline void OctahedralEncode(const DirectX::XMFLOAT3& normal, int16_t out[2]) {
    const float scale = fabsf(normal.x) + fabsf(normal.y) + fabsf(normal.z);
    if (scale < 1e-20f) {
        out[0] = 0;
        out[1] = 0;   // decodes to +Z, a sane stand-in for a missing normal
        return;
    }

    float x = normal.x / scale;
    float y = normal.y / scale;
    const float z = normal.z / scale;
    if (z < 0.0f) {
        const float foldedX = (1.0f - fabsf(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        const float foldedY = (1.0f - fabsf(x)) * (y >= 0.0f ? 1.0f : -1.0f);
        x = foldedX;
        y = foldedY;
    }
    out[0] = QuantiseSnorm16(x);
    out[1] = QuantiseSnorm16(y);
}

inline uint32_t PackColorRgba8(const DirectX::XMFLOAT4& color) {
    auto channel = [](float value) -> uint32_t {
        const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        return static_cast<uint32_t>(clamped * 255.0f + 0.5f);
    };
    return channel(color.x) | (channel(color.y) << 8) | (channel(color.z) << 16) | (channel(color.w) << 24);
}

inline PackedVertex PackVertex(const Vertex& vertex) {
    PackedVertex packed = {};
    packed.position = vertex.position;
    packed.color = PackColorRgba8(vertex.color);
    packed.uv = vertex.uv;
    OctahedralEncode(vertex.normal, packed.normal);
    OctahedralEncode(vertex.tangent, packed.tangent);
    return packed;
}

// A meshlet is a contiguous run of triangles with its own bounding sphere -
// the unit the virtualised-geometry path culls and selects detail on.
struct Meshlet {
    uint32_t VertexCount;
    uint32_t VertexOffset;
    uint32_t PrimCount;
    uint32_t PrimOffset;
    DirectX::XMFLOAT3 Center;
    float Radius;
};

// Triangles per meshlet. Also the divisor the debug view uses to colour by
// meshlet, so the shader has to agree with it.
static constexpr uint32_t kMeshletMaxPrimitives = 126;

// A cluster is a run of triangles in the index buffer plus everything the GPU
// needs to decide, on its own, whether to draw it this frame.
//
// LOD selection compares a projected error against a threshold:
//   draw when  project(GroupError, GroupBounds) <= t < project(ParentError, ParentBounds)
// Both tests use *group* bounds rather than the cluster's own, so every cluster
// in a group reaches the same verdict - which is what keeps the seam between a
// group and its neighbours closed.
struct MeshCluster {
    uint32_t IndexOffset;
    uint32_t IndexCount;

    DirectX::XMFLOAT3 Center;    // bounding sphere, object space
    float Radius;

    DirectX::XMFLOAT3 ConeAxis;  // backface cone; cutoff 1 means never cull
    float ConeCutoff;

    DirectX::XMFLOAT3 GroupCenter;
    float GroupRadius;
    float GroupError;            // error already baked into this cluster

    DirectX::XMFLOAT3 ParentCenter;
    float ParentRadius;
    float ParentError;           // error if we stepped one level coarser

    uint32_t Level;
    uint32_t Padding[3];
};
static_assert(sizeof(MeshCluster) == 96, "MeshCluster is mirrored in HLSL");

using Microsoft::WRL::ComPtr;

class Mesh {
public:
    Mesh(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const Vertex* vertices, size_t vertexCount, const uint32_t* indices, size_t indexCount);
    Mesh() = default; 
    void Draw(ID3D12GraphicsCommandList* cmdList);
    void Initialize(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

    D3D12_VERTEX_BUFFER_VIEW GetVertexView() const;
    D3D12_INDEX_BUFFER_VIEW GetIndexView() const;
    uint32_t GetIndexCount() const { return m_indexCount; }
    uint32_t GetVertexCount() const { return m_vertexCount; }

    // Frees the staging copies once the upload has been consumed. Dense meshes
    // hold hundreds of megabytes here, so this is not optional for them - but
    // the caller has to know the GPU is finished with the copy first.
    void ReleaseUploadBuffers();

    // Hands the staging copies to a caller that will hold them until the GPU
    // has finished with them. Used when the copy was recorded into a command
    // list that has not been submitted yet, so the Mesh itself cannot know when
    // it is safe to let go.
    void TakeUploadBuffers(std::vector<ComPtr<ID3D12Resource>>& outStagingBuffers);

    const std::vector<Meshlet>& GetMeshlets() const { return m_meshlets; }

    // Virtualised geometry. Uploading the cluster table is what lets the cull
    // shader pick a level of detail and reject clusters on its own, instead of
    // the renderer drawing the whole index buffer every frame.
    void UploadClusters(ID3D12Device* device,
                        ID3D12GraphicsCommandList* cmdList,
                        const std::vector<MeshCluster>& clusters,
                        uint32_t levelCount,
                        uint32_t baseTriangleCount);

    const std::vector<MeshCluster>& GetClusters() const { return m_clusters; }
    uint32_t GetClusterCount() const { return static_cast<uint32_t>(m_clusters.size()); }
    uint32_t GetClusterLevelCount() const { return m_clusterLevelCount; }
    bool HasClusters() const { return !m_clusters.empty() && m_clusterBuffer; }

    // Triangles at the finest level, which is what the mesh would cost drawn in
    // full. m_indexCount spans every level once clusters exist, so it is not
    // the number to report.
    uint32_t GetBaseTriangleCount() const {
        return m_baseTriangleCount != 0 ? m_baseTriangleCount : (m_indexCount / 3);
    }

    // Indices covering the finest level only. The cluster builder emits level 0
    // first and contiguously, so this range is the full-detail surface exactly
    // once. Anything that wants the whole mesh in one draw - the shadow depth
    // pass, a raytracing acceleration structure - has to use this rather than
    // GetIndexCount(), which spans every level stacked on top of each other.
    uint32_t GetBaseIndexCount() const { return GetBaseTriangleCount() * 3; }

    D3D12_GPU_VIRTUAL_ADDRESS GetClusterBufferAddress() const {
        return m_clusterBuffer ? m_clusterBuffer->GetGPUVirtualAddress() : 0;
    }

    // Raytracing needs the raw resources so it can transition them into
    // NON_PIXEL_SHADER_RESOURCE for acceleration-structure builds.
    ID3D12Resource* GetVertexBufferResource() const { return m_vertexBuffer.Get(); }
    ID3D12Resource* GetIndexBufferResource() const { return m_indexBuffer.Get(); }

    const DirectX::BoundingBox& GetBounds() const { return m_bounds; }

private:
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    ComPtr<ID3D12Resource> m_vertexUploadBuffer; 
    ComPtr<ID3D12Resource> m_indexUploadBuffer;  
    
    D3D12_VERTEX_BUFFER_VIEW m_vbView;
    D3D12_INDEX_BUFFER_VIEW m_ibView;
    uint32_t m_indexCount = 0;
    uint32_t m_vertexCount = 0;

    std::vector<Meshlet> m_meshlets;

    std::vector<MeshCluster> m_clusters;
    ComPtr<ID3D12Resource> m_clusterBuffer;
    ComPtr<ID3D12Resource> m_clusterUploadBuffer;
    uint32_t m_clusterLevelCount = 0;
    uint32_t m_baseTriangleCount = 0;

    DirectX::BoundingBox m_bounds;

    void GenerateMeshlets(const Vertex* vertices, uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount);
    void ComputeBounds(const Vertex* vertices, size_t vertexCount);

    ComPtr<ID3D12Resource> CreateDefaultBuffer(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, const void* initData, UINT64 byteSize, ComPtr<ID3D12Resource>& uploadBuffer);
};