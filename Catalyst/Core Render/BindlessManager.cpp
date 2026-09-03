#include "BindlessManager.h"
#include <algorithm>

void BindlessManager::Initialize(ID3D12Device* device, UINT maxTextures) {
    m_maxTextures = maxTextures;
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = maxTextures;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    
    ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_srvHeap)));
    m_descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

int BindlessManager::AddTexture(ID3D12Device* device,
                                ID3D12Resource* texture,
                                const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) {
    if (!texture) {
        return -1;
    }

    UINT slot = 0;
    if (!m_freeSlots.empty()) {
        slot = m_freeSlots.back();
        m_freeSlots.pop_back();
    } else if (m_currentOffset < m_maxTextures) {
        slot = m_currentOffset++;
    } else {
        return -1;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += slot * m_descriptorSize;

    if (srvDesc != nullptr) {
        device->CreateShaderResourceView(texture, srvDesc, handle);
        return static_cast<int>(slot);
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC defaultDesc = {};
    defaultDesc.Format = texture->GetDesc().Format;
    defaultDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    defaultDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    defaultDesc.Texture2D.MipLevels = 1;

    device->CreateShaderResourceView(texture, &defaultDesc, handle);
    return static_cast<int>(slot);
}

D3D12_GPU_DESCRIPTOR_HANDLE BindlessManager::GetGpuHandle(int index) const {
    D3D12_GPU_DESCRIPTOR_HANDLE handle = {};
    if (!m_srvHeap || index < 0 || static_cast<UINT>(index) >= m_maxTextures) {
        return handle;
    }
    handle = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * m_descriptorSize;
    return handle;
}

void BindlessManager::ReleaseTexture(int index) {
    if (index < 0 || static_cast<UINT>(index) >= m_currentOffset) {
        return;
    }

    const UINT slot = static_cast<UINT>(index);
    if (std::find(m_freeSlots.begin(), m_freeSlots.end(), slot) != m_freeSlots.end()) {
        return; // already released
    }
    m_freeSlots.push_back(slot);
}