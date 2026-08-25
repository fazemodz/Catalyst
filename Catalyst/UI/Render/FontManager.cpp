#include "FontManager.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include <fstream>
#include <imstb_truetype.h>
#include <stdexcept>

using namespace DirectX;

namespace {
// Codepoints baked into the atlas: ASCII plus the Latin-1 supplement, which
// covers the accented characters that show up in Windows user names and asset
// paths. 0x7F-0x9F are controls and simply bake empty.
constexpr uint32_t kFirstCodepoint = 32;
constexpr uint32_t kCodepointCount = 224; // 32..255
constexpr uint32_t kReplacementCodepoint = 0xFFFD;
}

namespace TextUtf8 {
size_t SequenceLength(const std::string& text, size_t byteIndex) {
    if (byteIndex >= text.size()) {
        return 1;
    }

    const unsigned char lead = static_cast<unsigned char>(text[byteIndex]);
    size_t length = 1;
    if ((lead & 0xE0u) == 0xC0u)      length = 2;
    else if ((lead & 0xF0u) == 0xE0u) length = 3;
    else if ((lead & 0xF8u) == 0xF0u) length = 4;

    // Truncated or non-continuation tails are treated as a single bad byte.
    if (byteIndex + length > text.size()) {
        return 1;
    }
    for (size_t i = 1; i < length; ++i) {
        if ((static_cast<unsigned char>(text[byteIndex + i]) & 0xC0u) != 0x80u) {
            return 1;
        }
    }
    return length;
}

uint32_t NextCodepoint(const std::string& text, size_t& byteIndex) {
    if (byteIndex >= text.size()) {
        byteIndex = text.size();
        return 0;
    }

    const size_t length = SequenceLength(text, byteIndex);
    const unsigned char lead = static_cast<unsigned char>(text[byteIndex]);

    if (length == 1) {
        byteIndex += 1;
        return lead < 0x80u ? lead : kReplacementCodepoint;
    }

    static const unsigned char kLeadMask[5] = {0, 0, 0x1Fu, 0x0Fu, 0x07u};
    uint32_t codepoint = lead & kLeadMask[length];
    for (size_t i = 1; i < length; ++i) {
        codepoint = (codepoint << 6) | (static_cast<unsigned char>(text[byteIndex + i]) & 0x3Fu);
    }
    byteIndex += length;
    return codepoint;
}
}

void FontManager::Initialize(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, const std::string& fontPath, float fontSize) {
    std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) throw std::runtime_error("Failed to open font file");

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> ttfBuffer(size);
    if (!file.read(reinterpret_cast<char*>(ttfBuffer.data()), size)) throw std::runtime_error("Failed to read font file");

    const int texWidth = 1024;
    const int texHeight = 1024;
    std::vector<unsigned char> tempBitmap(texWidth * texHeight);
    std::vector<stbtt_bakedchar> cdata(kCodepointCount);

    const int bakedRows = stbtt_BakeFontBitmap(ttfBuffer.data(), 0, fontSize, tempBitmap.data(), texWidth, texHeight,
                                               static_cast<int>(kFirstCodepoint), static_cast<int>(kCodepointCount), cdata.data());
    if (bakedRows <= 0) {
        throw std::runtime_error("Font atlas too small to bake the glyph range");
    }

    std::vector<uint32_t> rgbaBitmap(texWidth * texHeight);
    for (size_t i = 0; i < tempBitmap.size(); ++i) {
        uint8_t alpha = tempBitmap[i];
        rgbaBitmap[i] = (alpha << 24) | (255 << 16) | (255 << 8) | 255;
    }

    D3D12_HEAP_PROPERTIES defaultHeap = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = texWidth;
    texDesc.Height = texHeight;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_textureAtlas));

    D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };
    UINT64 uploadBufferSize;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize);
    
    D3D12_RESOURCE_DESC uploadDesc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, uploadBufferSize, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_textureUploadHeap));

    void* mappedData;
    m_textureUploadHeap->Map(0, nullptr, &mappedData);
    D3D12_SUBRESOURCE_DATA subresData = {};
    subresData.pData = rgbaBitmap.data();
    subresData.RowPitch = texWidth * 4;
    subresData.SlicePitch = subresData.RowPitch * texHeight;
    
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, nullptr);
    
    uint8_t* pDest = reinterpret_cast<uint8_t*>(mappedData);
    const uint8_t* pSrc = reinterpret_cast<const uint8_t*>(subresData.pData);
    for (UINT y = 0; y < texHeight; ++y) {
        memcpy(pDest + y * footprint.Footprint.RowPitch, pSrc + y * subresData.RowPitch, subresData.RowPitch);
    }
    m_textureUploadHeap->Unmap(0, nullptr);

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> cmdAlloc;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList;
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&cmdList));

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = m_textureAtlas.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = m_textureUploadHeap.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = footprint;

    cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_textureAtlas.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(1, &barrier);

    cmdList->Close();
    ID3D12CommandList* lists[] = { cmdList.Get() };
    cmdQueue->ExecuteCommandLists(1, lists);

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    cmdQueue->Signal(fence.Get(), 1);
    if (fence->GetCompletedValue() < 1) {
        fence->SetEventOnCompletion(1, eventHandle);
        WaitForSingleObject(eventHandle, INFINITE);
    }
    CloseHandle(eventHandle);

    for (uint32_t i = 0; i < kCodepointCount; ++i) {
        const uint32_t codepoint = kFirstCodepoint + i;
        stbtt_aligned_quad q;
        float xpos = 0, ypos = 0;
        stbtt_GetBakedQuad(cdata.data(), texWidth, texHeight, static_cast<int>(i), &xpos, &ypos, &q, 1);

        GlyphInfo gi;
        gi.UVMin = { q.s0, q.t0 };
        gi.UVMax = { q.s1, q.t1 };
        gi.Size = { q.x1 - q.x0, q.y1 - q.y0 };
        gi.Advance = xpos;
        gi.BearingY = -q.y0;

        m_glyphs[codepoint] = gi;
    }
}

GlyphInfo FontManager::GetGlyph(uint32_t codepoint) {
    const auto found = m_glyphs.find(codepoint);
    if (found != m_glyphs.end()) {
        return found->second;
    }

    // Anything outside the baked range (CJK, emoji, malformed bytes) falls back
    // to '?' rather than inserting an empty glyph into the map.
    const auto fallback = m_glyphs.find(static_cast<uint32_t>('?'));
    return fallback != m_glyphs.end() ? fallback->second : GlyphInfo{};
}