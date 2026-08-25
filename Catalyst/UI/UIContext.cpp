#include "UIContext.h"
#include <algorithm>

void UIContext::Initialize(UIDrawList* drawList, FontManager* fontManager, InputManager* inputManager) {
    m_drawList     = drawList;
    m_fontManager  = fontManager;
    m_inputManager = inputManager;
}

void UIContext::SetModalRegion(float x, float y, float width, float height) {
    m_modalRegionActive = true;
    m_modalRegionX      = x;
    m_modalRegionY      = y;
    m_modalRegionWidth  = width;
    m_modalRegionHeight = height;
}

void UIContext::ClearModalRegion() {
    m_modalRegionActive = false;
    m_modalRegionX      = 0.0f;
    m_modalRegionY      = 0.0f;
    m_modalRegionWidth  = 0.0f;
    m_modalRegionHeight = 0.0f;
}

void UIContext::AddText(const std::string& text, float x, float y, uint32_t color, float wrapWidth) {
    if (m_drawList && m_fontManager && !text.empty())
        m_drawList->AddText(*m_fontManager, text, x, y, color, wrapWidth);
}

void UIContext::Image(uint32_t textureID, float x, float y, float width, float height, uint32_t color) {
    if (m_drawList)
        m_drawList->AddImage(textureID, x, y, width, height, color);
}

bool UIContext::IsInteractionAllowed(float x, float y, float width, float height) const {
    if (m_drawList) {
        const UIClipRect clipRect = m_drawList->GetActiveClipRect();
        if (clipRect.Enabled) {
            const float clipRight  = clipRect.X + clipRect.Width;
            const float clipBottom = clipRect.Y + clipRect.Height;
            if ((x + width) <= clipRect.X || x >= clipRight ||
                (y + height) <= clipRect.Y || y >= clipBottom)
                return false;
        }
    }

    if (!m_modalRegionActive)
        return true;

    return x >= m_modalRegionX &&
           y >= m_modalRegionY &&
           (x + width)  <= (m_modalRegionX + m_modalRegionWidth) &&
           (y + height) <= (m_modalRegionY + m_modalRegionHeight);
}
