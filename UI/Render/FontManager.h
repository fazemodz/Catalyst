#pragma once
#include <string>
#include <vector>
#include <map>
#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>

struct GlyphInfo {
    DirectX::XMFLOAT2 UVMin;
    DirectX::XMFLOAT2 UVMax;
    DirectX::XMFLOAT2 Size;
    float Advance;
    float BearingY;
};

class FontManager {
public:
    void Initialize(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, const std::string& fontPath, float fontSize);
    GlyphInfo GetGlyph(char c);
    ID3D12Resource* GetTextureAtlas() { return m_textureAtlas.Get(); }
    uint32_t GetBindlessIndex() { return m_bindlessIndex; }
    void SetBindlessIndex(uint32_t index) { m_bindlessIndex = index; }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> m_textureAtlas;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_textureUploadHeap;
    std::map<char, GlyphInfo> m_glyphs;
    uint32_t m_bindlessIndex = 0;
};