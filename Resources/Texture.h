#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <string>

using Microsoft::WRL::ComPtr;

class Texture {
public:
    void Load(const std::string& filepath, ID3D12Device* device, ID3D12CommandQueue* commandQueue);
    
    // --- NEW: Helper to create a solid color texture without loading a file ---
    void Create1x1Color(ID3D12Device* device, ID3D12CommandQueue* commandQueue, UINT colorData);

    ID3D12DescriptorHeap* GetSRVHeap() const { return m_srvHeap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle() const { return m_srvHeap->GetGPUDescriptorHandleForHeapStart(); }

private:
    ComPtr<ID3D12Resource> m_textureResource;
    ComPtr<ID3D12Resource> m_uploadHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
};