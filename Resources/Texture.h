#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <DirectXMath.h>
#include "../Error handler/Common.h"

using Microsoft::WRL::ComPtr;

class Texture {
public:
    // Standard Texture Loading
    void Load(const std::string& filename, ID3D12Device* device, ID3D12CommandQueue* commandQueue);
    
    // HDR Loading for Skyboxes
    void LoadHDR(const std::string& filename, ID3D12Device* device, ID3D12CommandQueue* commandQueue);
    
    // Fallback/Utility Creation
    void Create1x1Color(ID3D12Device* device, ID3D12CommandQueue* commandQueue, uint32_t color);
    
    // Accessors
    ID3D12Resource* GetResource() { return m_resource.Get(); }
    
    // Bindless Management
    void SetBindlessIndex(int index) { m_bindlessIndex = index; }
    int GetBindlessIndex() const { return m_bindlessIndex; }

    // Shutdown
    void Release() { m_resource.Reset(); }

private:
    ComPtr<ID3D12Resource> m_resource;
    int m_bindlessIndex = -1;
};