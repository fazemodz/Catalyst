#include "Mesh.h"
#include <stdexcept>

void Mesh::Initialize(ID3D12Device* device, std::vector<Vertex>& vertices, std::vector<uint16_t>& indices) {
    m_indexCount = static_cast<UINT>(indices.size());

    // 1. Create Vertex Buffer
    const UINT vBufferSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));

    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC vDesc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, vBufferSize, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &vDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertexBuffer)))) {
        throw std::runtime_error("Failed to create Mesh Vertex Buffer");
    }

    // Copy Data to GPU
    UINT8* vDataBegin;
    m_vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&vDataBegin));
    memcpy(vDataBegin, vertices.data(), vBufferSize);
    m_vertexBuffer->Unmap(0, nullptr);

    // Create View
    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(Vertex);
    m_vertexBufferView.SizeInBytes = vBufferSize;

    // 2. Create Index Buffer
    const UINT iBufferSize = static_cast<UINT>(indices.size() * sizeof(uint16_t));

    D3D12_RESOURCE_DESC iDesc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, iBufferSize, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1,0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };

    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &iDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_indexBuffer)))) {
        throw std::runtime_error("Failed to create Mesh Index Buffer");
    }

    // Copy Data
    UINT8* iDataBegin;
    m_indexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&iDataBegin));
    memcpy(iDataBegin, indices.data(), iBufferSize);
    m_indexBuffer->Unmap(0, nullptr);

    // Create View
    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
    m_indexBufferView.SizeInBytes = iBufferSize;
}