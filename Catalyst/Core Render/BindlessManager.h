#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include "Common.h"

class BindlessManager {
public:
    void Initialize(ID3D12Device* device, UINT maxTextures);

    // srvDesc is optional. It has to be supplied for anything the default
    // description cannot express - a depth buffer, for one, is created as a
    // typeless resource and has no format an SRV can be built from directly.
    int AddTexture(ID3D12Device* device,
                   ID3D12Resource* texture,
                   const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = nullptr);

    // Only one CBV/SRV/UAV heap can be bound at a time, so anything a shader
    // must reach has to live in this heap. This hands back the address to point
    // a root descriptor table at.
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(int index) const;

    // Returns a slot to the pool so it can back a different texture later.
    // Without this the heap is a one-way allocator and every reload of an
    // asset burns a slot until the heap is exhausted.
    void ReleaseTexture(int index);

    ID3D12DescriptorHeap* GetHeap() { return m_srvHeap.Get(); }
    UINT GetCapacity() const { return m_maxTextures; }
    UINT GetUsedCount() const { return m_currentOffset - static_cast<UINT>(m_freeSlots.size()); }

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    UINT m_descriptorSize = 0;
    UINT m_currentOffset = 0;
    UINT m_maxTextures = 0;
    std::vector<UINT> m_freeSlots;
};