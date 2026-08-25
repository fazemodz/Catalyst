#include "UIDrawList.h"
#include "FontManager.h"
#include <cmath>

void UIDrawList::AddRectFilled(float x, float y, float width, float height, uint32_t color) {
    PushTextureBatch(0);

    uint32_t vtxIdx = (uint32_t)Vertices.size();

    Vertices.push_back({{x,         y         }, {0.0f, 0.0f}, color});
    Vertices.push_back({{x + width, y         }, {1.0f, 0.0f}, color});
    Vertices.push_back({{x + width, y + height}, {1.0f, 1.0f}, color});
    Vertices.push_back({{x,         y + height}, {0.0f, 1.0f}, color});

    Indices.push_back(vtxIdx);     Indices.push_back(vtxIdx + 1); Indices.push_back(vtxIdx + 2);
    Indices.push_back(vtxIdx);     Indices.push_back(vtxIdx + 2); Indices.push_back(vtxIdx + 3);

    Commands.back().ElementCount += 6;
}

void UIDrawList::AddLine(float x1, float y1, float x2, float y2, float thickness, uint32_t color) {
    float dx  = x2 - x1;
    float dy  = y2 - y1;
    float len = sqrtf(dx*dx + dy*dy);
    // Reject degenerate and non-finite lines before opening a draw command, so
    // no empty batch is emitted and no NaN reaches the vertex buffer.
    if (!(len > 0.0f)) return;

    PushTextureBatch(0);

    float nx = (dy / len) * (thickness * 0.5f);
    float ny = (-dx / len) * (thickness * 0.5f);

    uint32_t vtxIdx = (uint32_t)Vertices.size();

    Vertices.push_back({{x1 + nx, y1 + ny}, {0.0f, 0.0f}, color});
    Vertices.push_back({{x2 + nx, y2 + ny}, {1.0f, 0.0f}, color});
    Vertices.push_back({{x2 - nx, y2 - ny}, {1.0f, 1.0f}, color});
    Vertices.push_back({{x1 - nx, y1 - ny}, {0.0f, 1.0f}, color});

    Indices.push_back(vtxIdx);     Indices.push_back(vtxIdx + 1); Indices.push_back(vtxIdx + 2);
    Indices.push_back(vtxIdx);     Indices.push_back(vtxIdx + 2); Indices.push_back(vtxIdx + 3);

    Commands.back().ElementCount += 6;
}

void UIDrawList::AddImage(uint32_t textureID, float x, float y, float width, float height, uint32_t color) {
    PushTextureBatch(textureID);

    uint32_t vtxIdx = (uint32_t)Vertices.size();

    Vertices.push_back({{x,         y         }, {0.0f, 0.0f}, color});
    Vertices.push_back({{x + width, y         }, {1.0f, 0.0f}, color});
    Vertices.push_back({{x + width, y + height}, {1.0f, 1.0f}, color});
    Vertices.push_back({{x,         y + height}, {0.0f, 1.0f}, color});

    Indices.push_back(vtxIdx);     Indices.push_back(vtxIdx + 1); Indices.push_back(vtxIdx + 2);
    Indices.push_back(vtxIdx);     Indices.push_back(vtxIdx + 2); Indices.push_back(vtxIdx + 3);

    Commands.back().ElementCount += 6;
}

void UIDrawList::AddText(FontManager& fontManager, const std::string& text, float x, float y, uint32_t color, float wrapWidth) {
    if (text.empty()) return;

    PushTextureBatch(fontManager.GetBindlessIndex());

    float startX     = x;
    float cursorX    = x;
    float cursorY    = y;
    float lineHeight = 28.0f;

    for (size_t byteIndex = 0; byteIndex < text.size();) {
        const uint32_t c = TextUtf8::NextCodepoint(text, byteIndex);
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

        uint32_t vtxIdx = (uint32_t)Vertices.size();

        Vertices.push_back({{x0, y0}, {glyph.UVMin.x, glyph.UVMin.y}, color});
        Vertices.push_back({{x1, y0}, {glyph.UVMax.x, glyph.UVMin.y}, color});
        Vertices.push_back({{x1, y1}, {glyph.UVMax.x, glyph.UVMax.y}, color});
        Vertices.push_back({{x0, y1}, {glyph.UVMin.x, glyph.UVMax.y}, color});

        Indices.push_back(vtxIdx);     Indices.push_back(vtxIdx + 1); Indices.push_back(vtxIdx + 2);
        Indices.push_back(vtxIdx);     Indices.push_back(vtxIdx + 2); Indices.push_back(vtxIdx + 3);

        Commands.back().ElementCount += 6;
        cursorX += glyph.Advance;
    }
}
