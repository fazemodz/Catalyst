#pragma once
#include <cstdint>
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

namespace TextUtf8 {
// Decodes the UTF-8 sequence starting at byteIndex and advances byteIndex past
// it. Malformed bytes decode to U+FFFD and advance by one, so callers always
// make progress no matter what a filename or log line contains.
uint32_t NextCodepoint(const std::string& text, size_t& byteIndex);

// Byte length of the UTF-8 sequence starting at byteIndex (always >= 1).
size_t SequenceLength(const std::string& text, size_t byteIndex);
}

class FontManager {
public:
    void Initialize(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, const std::string& fontPath, float fontSize);
    GlyphInfo GetGlyph(uint32_t codepoint);
    ID3D12Resource* GetTextureAtlas() { return m_textureAtlas.Get(); }
    uint32_t GetBindlessIndex() { return m_bindlessIndex; }
    void SetBindlessIndex(uint32_t index) { m_bindlessIndex = index; }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> m_textureAtlas;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_textureUploadHeap;
    std::map<uint32_t, GlyphInfo> m_glyphs;
    uint32_t m_bindlessIndex = 0;
};