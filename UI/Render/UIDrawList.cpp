#include "UIDrawList.h"
#include "FontManager.h"

void UIDrawList::Clear() {
    Vertices.clear();
    Indices.clear();
    Commands.clear();
}

void UIDrawList::PushTextureBatch(uint32_t textureID) {
    if (Commands.empty() || Commands.back().TextureID != textureID) {
        UIDrawCommand cmd;
        cmd.ElementCount = 0;
        cmd.IndexOffset = (uint32_t)Indices.size();
        cmd.TextureID = textureID;
        Commands.push_back(cmd);
    }
}

void UIDrawList::AddRectFilled(float x, float y, float width, float height, uint32_t color) {
    PushTextureBatch(0); 

    uint32_t vtxIdx = (uint32_t)Vertices.size();

    Vertices.push_back({{x, y}, {0.0f, 0.0f}, color});
    Vertices.push_back({{x + width, y}, {1.0f, 0.0f}, color});
    Vertices.push_back({{x + width, y + height}, {1.0f, 1.0f}, color});
    Vertices.push_back({{x, y + height}, {0.0f, 1.0f}, color});

    Indices.push_back(vtxIdx);
    Indices.push_back(vtxIdx + 1);
    Indices.push_back(vtxIdx + 2);
    Indices.push_back(vtxIdx);
    Indices.push_back(vtxIdx + 2);
    Indices.push_back(vtxIdx + 3);

    Commands.back().ElementCount += 6;
}

void UIDrawList::AddText(FontManager& fontManager, const std::string& text, float x, float y, uint32_t color, float wrapWidth) {
    PushTextureBatch(fontManager.GetBindlessIndex());

    float startX = x;
    float cursorX = x;
    float cursorY = y;
    float lineHeight = 28.0f;

    for (char c : text) {
        if (c == '\n') {
            cursorX = startX;
            cursorY += lineHeight;
            continue;
        }

        GlyphInfo glyph = fontManager.GetGlyph(c);

    
        if (wrapWidth > 0.0f && (cursorX - startX + glyph.Advance) > wrapWidth) {
            cursorX = startX;
            cursorY += lineHeight;
            if (c == ' ') continue; 
        }

        float x0 = cursorX;
        float y0 = cursorY - glyph.BearingY;
        float x1 = x0 + glyph.Size.x;
        float y1 = y0 + glyph.Size.y;

        float u0 = glyph.UVMin.x;
        float v0 = glyph.UVMin.y;
        float u1 = glyph.UVMax.x;
        float v1 = glyph.UVMax.y;

        uint32_t vtxIdx = (uint32_t)Vertices.size();

        Vertices.push_back({{x0, y0}, {u0, v0}, color});
        Vertices.push_back({{x1, y0}, {u1, v0}, color});
        Vertices.push_back({{x1, y1}, {u1, v1}, color});
        Vertices.push_back({{x0, y1}, {u0, v1}, color});

        Indices.push_back(vtxIdx);
        Indices.push_back(vtxIdx + 1);
        Indices.push_back(vtxIdx + 2);
        Indices.push_back(vtxIdx);
        Indices.push_back(vtxIdx + 2);
        Indices.push_back(vtxIdx + 3);

        Commands.back().ElementCount += 6;
        cursorX += glyph.Advance;
    }
}