#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include "Common.h"

class BindlessManager {
public:
    void Initialize(ID3D12Device* device, UINT maxTextures);
    int AddTexture(ID3D12Device* device, ID3D12Resource* texture);

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