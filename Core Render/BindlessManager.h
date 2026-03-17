#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include "Common.h"

class BindlessManager {
public:
    void Initialize(ID3D12Device* device, UINT maxTextures);
    int AddTexture(ID3D12Device* device, ID3D12Resource* texture);
    ID3D12DescriptorHeap* GetHeap() { return m_srvHeap.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    UINT m_descriptorSize;
    UINT m_currentOffset = 0;
    UINT m_maxTextures;
};