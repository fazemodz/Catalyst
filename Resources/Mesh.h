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

struct Meshlet {
    uint32_t VertexCount;
    uint32_t VertexOffset;
    uint32_t PrimCount;
    uint32_t PrimOffset;
    DirectX::XMFLOAT3 Center;
    float Radius;
};

using Microsoft::WRL::ComPtr;

class Mesh {
public:
    Mesh(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const Vertex* vertices, size_t vertexCount, const uint32_t* indices, size_t indexCount);
    Mesh() = default; 

    void Initialize(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

    D3D12_VERTEX_BUFFER_VIEW GetVertexView() const;
    D3D12_INDEX_BUFFER_VIEW GetIndexView() const;
    uint32_t GetIndexCount() const { return m_indexCount; }
    
    const std::vector<Meshlet>& GetMeshlets() const { return m_meshlets; }
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

    ComPtr<ID3D12Resource> CreateDefaultBuffer(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, const void* initData, UINT64 byteSize, ComPtr<ID3D12Resource>& uploadBuffer);
    void GenerateMeshlets(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
};