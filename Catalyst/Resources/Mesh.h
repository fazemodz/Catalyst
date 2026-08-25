#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <directxmath.h>
#include <DirectXCollision.h> 

struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
    DirectX::XMFLOAT2 uv;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT3 tangent;
};

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

    const std::vector<Meshlet>& GetMeshlets() const { return m_meshlets; }

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

    std::vector<Meshlet> m_meshlets;
    DirectX::BoundingBox m_bounds;

    void GenerateMeshlets(const Vertex* vertices, uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount);

    ComPtr<ID3D12Resource> CreateDefaultBuffer(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, const void* initData, UINT64 byteSize, ComPtr<ID3D12Resource>& uploadBuffer);
};