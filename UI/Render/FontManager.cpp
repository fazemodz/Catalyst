#include "FontManager.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include <fstream>
#include <imstb_truetype.h>
#include <stdexcept>

using namespace DirectX;

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
    stbtt_bakedchar cdata[96]; 

    stbtt_BakeFontBitmap(ttfBuffer.data(), 0, fontSize, tempBitmap.data(), texWidth, texHeight, 32, 96, cdata);

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

    for (int i = 0; i < 96; ++i) {
        char c = static_cast<char>(32 + i);
        stbtt_aligned_quad q;
        float xpos = 0, ypos = 0;
        stbtt_GetBakedQuad(cdata, texWidth, texHeight, i, &xpos, &ypos, &q, 1);

        GlyphInfo gi;
        gi.UVMin = { q.s0, q.t0 };
        gi.UVMax = { q.s1, q.t1 };
        gi.Size = { q.x1 - q.x0, q.y1 - q.y0 };
        gi.Advance = xpos;
        gi.BearingY = -q.y0;

        m_glyphs[c] = gi;
    }
}

GlyphInfo FontManager::GetGlyph(char c) {
    if (m_glyphs.find(c) != m_glyphs.end()) {
        return m_glyphs[c];
    }
    return m_glyphs['?'];
}