#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <directxmath.h>
#include <string>

using Microsoft::WRL::ComPtr;

// Define what a single point in 3D space looks like
struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
    // Later we will add Normals (lighting) and UVs (textures) here
};

class Mesh {
public:
    // Load mesh data into GPU buffers immediately
    void Initialize(ID3D12Device* device, std::vector<Vertex>& vertices, std::vector<uint16_t>& indices);
    
    // Getters for the Renderer
    D3D12_VERTEX_BUFFER_VIEW GetVertexView() const { return m_vertexBufferView; }
    D3D12_INDEX_BUFFER_VIEW GetIndexView() const { return m_indexBufferView; }
    UINT GetIndexCount() const { return m_indexCount; }

private:
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
    
    UINT m_indexCount = 0;
};