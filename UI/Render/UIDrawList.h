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

struct UIClipRect {
    float X = 0.0f;
    float Y = 0.0f;
    float Width = 0.0f;
    float Height = 0.0f;
    bool Enabled = false;
};

struct UIDrawCommand {
    uint32_t ElementCount;
    uint32_t IndexOffset;
    uint32_t TextureID;
    UIClipRect ClipRect;
};

class UIDrawList {
public:
    std::vector<UIVertex> Vertices;
    std::vector<uint32_t> Indices;
    std::vector<UIDrawCommand> Commands;

    void Clear();
    void PushClipRect(float x, float y, float width, float height);
    void PopClipRect();
    void AddRectFilled(float x, float y, float width, float height, uint32_t color);
    
    void AddLine(float x1, float y1, float x2, float y2, float thickness, uint32_t color);
    
    void AddImage(uint32_t textureID, float x, float y, float width, float height, uint32_t color = 0xFFFFFFFF);
    void AddText(FontManager& fontManager, const std::string& text, float x, float y, uint32_t color, float wrapWidth = 0.0f);

private:
    UIClipRect GetActiveClipRect() const;
    void PushTextureBatch(uint32_t textureID);
    std::vector<UIClipRect> m_clipRectStack;
};
