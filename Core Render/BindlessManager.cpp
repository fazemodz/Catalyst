#include "BindlessManager.h"

void BindlessManager::Initialize(ID3D12Device* device, UINT maxTextures) {
    m_maxTextures = maxTextures;
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = maxTextures;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    
    ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_srvHeap)));
    m_descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

int BindlessManager::AddTexture(ID3D12Device* device, ID3D12Resource* texture) {
    if (!texture) {
        return -1; 
    }

    if (m_currentOffset >= m_maxTextures) return -1;

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += m_currentOffset * m_descriptorSize;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texture->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    device->CreateShaderResourceView(texture, &srvDesc, handle);
    return m_currentOffset++;
}