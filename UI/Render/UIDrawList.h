#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <DirectXMath.h>

class FontManager;

struct UIVertex {
    DirectX::XMFLOAT2 Position;
    DirectX::XMFLOAT2 UV;
    uint32_t Color;
};

struct UIDrawCommand {
    uint32_t ElementCount;
    uint32_t IndexOffset;
    uint32_t TextureID;
};

class UIDrawList {
public:
    std::vector<UIVertex> Vertices;
    std::vector<uint32_t> Indices;
    std::vector<UIDrawCommand> Commands;

    void Clear();
    void AddRectFilled(float x, float y, float width, float height, uint32_t color);
    
    void AddLine(float x1, float y1, float x2, float y2, float thickness, uint32_t color);
    
    void AddImage(uint32_t textureID, float x, float y, float width, float height, uint32_t color = 0xFFFFFFFF);
    void AddText(FontManager& fontManager, const std::string& text, float x, float y, uint32_t color, float wrapWidth = 0.0f);

private:
    void PushTextureBatch(uint32_t textureID);
};