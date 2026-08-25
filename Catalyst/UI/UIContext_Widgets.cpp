#include "UIContext.h"
#include "UIContext_Internal.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

static float UICtx_MeasureText(FontManager& fm, const std::string& text) {
    float w = 0.0f;
    for (size_t i = 0; i < text.size();) {
        w += fm.GetGlyph(TextUtf8::NextCodepoint(text, i)).Advance;
    }
    return w;
}

bool UIContext::Button(const std::string& label, float x, float y, float width, float height,
                       uint32_t baseColor, uint32_t hoverColor, uint32_t clickColor) {
    bool isHovered = false;
    bool isClicked = false;
    const bool interactionAllowed = IsInteractionAllowed(x, y, width, height);
    const UIClipRect clipRect = m_drawList ? m_drawList->GetActiveClipRect() : UIClipRect{};

    if (m_inputManager && interactionAllowed) {
        int mx = m_inputManager->GetMouseX();
        int my = m_inputManager->GetMouseY();

        if (UICtx_PointInWidgetClip((float)mx, (float)my, x, y, width, height, clipRect)) {
            isHovered = true;
            if (m_inputManager->IsMouseButtonPressed(0)) isClicked = true;
        }
    }

    uint32_t bgColor = baseColor;
    if (isHovered)
        bgColor = (m_inputManager && m_inputManager->IsMouseButtonDown(0)) ? clickColor : hoverColor;

    if (m_drawList) {
        if ((bgColor >> 24) > 0) m_drawList->AddRectFilled(x, y, width, height, bgColor);
        if (m_fontManager && !label.empty()) {
            const float pad  = 8.0f;
            float textW = UICtx_MeasureText(*m_fontManager, label);
            // Centre when it fits; clamp to left padding when it doesn't so we
            // never start drawing before the button's left interior edge.
            float textX = x + (width - textW) * 0.5f;
            if (textX < x + pad) textX = x + pad;
            float textY = y + (height / 2.0f) + 8.0f;
            m_drawList->PushClipRect(x + 2, y + 2, width - 4, height - 4);
            m_drawList->AddText(*m_fontManager, label, textX, textY, 0xFFE0E0E0);
            m_drawList->PopClipRect();
        }
    }
    return isClicked;
}

bool UIContext::TextInput(const std::string& id, std::string& text, float x, float y, float width, float height, bool& isActive) {
    bool isHovered = false;
    const bool interactionAllowed = IsInteractionAllowed(x, y, width, height);
    const UIClipRect clipRect = m_drawList ? m_drawList->GetActiveClipRect() : UIClipRect{};

    if (m_inputManager && interactionAllowed) {
        int mx = m_inputManager->GetMouseX();
        int my = m_inputManager->GetMouseY();

        if (UICtx_PointInWidgetClip((float)mx, (float)my, x, y, width, height, clipRect)) {
            isHovered = true;
            if (m_inputManager->IsMouseButtonPressed(0)) isActive = true;
        } else if (m_inputManager->IsMouseButtonPressed(0)) {
            isActive = false;
        }

        if (isActive) {
            for (char c : m_inputManager->GetTypedCharacters()) {
                if (c == '\b') {
                    if (!text.empty()) text.pop_back();
                } else if (c >= 32 && c <= 126) {
                    text += c;
                }
            }
        }
    }

    uint32_t bgColor     = isActive ? 0xFF111111 : (isHovered ? 0xFF333333 : 0xFF2A2A2A);
    uint32_t borderColor = isActive ? 0xFFD77800 : 0xFF444444;

    if (m_drawList) {
        m_drawList->AddRectFilled(x - 2, y - 2, width + 4, height + 4, borderColor);
        m_drawList->AddRectFilled(x, y, width, height, bgColor);
        if (m_fontManager) {
            std::string displayText = text + (isActive && (GetTickCount() / 500) % 2 == 0 ? "_" : "");
            bool isEmpty = text.empty() && !isActive;
            const std::string& renderText = isEmpty ? "Project Name..." : displayText;
            const float pad = 10.0f;
            const float availW = width - pad * 2.0f;
            float textW = UICtx_MeasureText(*m_fontManager, renderText);
            // Scroll to show the end of long text (right-align when overflowing)
            float textX = textW > availW ? x + pad + availW - textW : x + pad;
            m_drawList->PushClipRect(x + 2, y + 2, width - 4, height - 4);
            m_drawList->AddText(*m_fontManager, renderText, textX, y + (height / 2.0f) + 8.0f,
                                isEmpty ? 0xFF777777 : 0xFFFFFFFF);
            m_drawList->PopClipRect();
        }
    }
    return isActive;
}

bool UIContext::Checkbox(const std::string& label, bool& value, float x, float y, float size) {
    bool isHovered = false;
    bool isClicked = false;
    const bool interactionAllowed = IsInteractionAllowed(x, y, size, size);
    const UIClipRect clipRect = m_drawList ? m_drawList->GetActiveClipRect() : UIClipRect{};

    if (m_inputManager && interactionAllowed) {
        int mx = m_inputManager->GetMouseX();
        int my = m_inputManager->GetMouseY();

        if (UICtx_PointInWidgetClip((float)mx, (float)my, x, y, size, size, clipRect)) {
            isHovered = true;
            if (m_inputManager->IsMouseButtonPressed(0)) {
                value = !value;
                isClicked = true;
            }
        }
    }

    uint32_t bgColor    = isHovered ? 0xFF555555 : 0xFF333333;
    uint32_t checkColor = 0xFFE07020;

    if (m_drawList) {
        m_drawList->AddRectFilled(x, y, size, size, bgColor);
        if (value) {
            float padding = size * 0.2f;
            m_drawList->AddRectFilled(x + padding, y + padding, size - padding * 2, size - padding * 2, checkColor);
        }
        if (m_fontManager && !label.empty())
            m_drawList->AddText(*m_fontManager, label, x + size + 8.0f, y + size * 0.8f, 0xFFD0D0D0);
    }
    return isClicked;
}

bool UIContext::DragFloat(const std::string& label, float& value, float dragSpeed, float x, float y, float width, float height,
                          float labelFraction) {
    bool isHovered   = false;
    bool valueChanged = false;

    float labelWidth = width * ((labelFraction > 0.05f && labelFraction < 0.95f) ? labelFraction : 0.3f);
    float boxX       = x + labelWidth;
    float boxWidth   = width - labelWidth;
    const bool interactionAllowed = IsInteractionAllowed(boxX, y, boxWidth, height);
    const UIClipRect clipRect = m_drawList ? m_drawList->GetActiveClipRect() : UIClipRect{};

    if (m_inputManager && interactionAllowed) {
        int mx = m_inputManager->GetMouseX();
        int my = m_inputManager->GetMouseY();

        if (UICtx_PointInWidgetClip((float)mx, (float)my, boxX, y, boxWidth, height, clipRect)) {
            isHovered = true;
            if (m_inputManager->IsMouseButtonPressed(0)) {
                m_activeSliderId = label;
                m_lastMousePos = { (float)mx, (float)my };
            }
        }

        if (m_activeSliderId == label) {
            if (m_inputManager->IsMouseButtonDown(0)) {
                float dx = (float)mx - m_lastMousePos.x;
                if (dx != 0.0f) {
                    value += dx * dragSpeed;
                    valueChanged = true;
                    m_lastMousePos = { (float)mx, (float)my };
                }
            } else {
                m_activeSliderId = "";
            }
        }
    } else if (m_activeSliderId == label) {
        m_activeSliderId.clear();
    }

    if (m_drawList) {
        if (m_fontManager)
            m_drawList->AddText(*m_fontManager, label, x, y + height * 0.7f, 0xFF9A9A9A);

        uint32_t bgColor = (m_activeSliderId == label) ? 0xFF444444 : (isHovered ? 0xFF333333 : 0xFF1A1A1A);
        m_drawList->AddRectFilled(boxX, y, boxWidth, height, bgColor);

        uint32_t accentColor = 0xFF555555;
        if (label.find("X") != std::string::npos || label.find("R") != std::string::npos) accentColor = 0xFF5555FF;
        if (label.find("Y") != std::string::npos || label.find("G") != std::string::npos) accentColor = 0xFF55FF55;
        if (label.find("Z") != std::string::npos || label.find("B") != std::string::npos) accentColor = 0xFFFF5555;
        m_drawList->AddRectFilled(boxX, y, 4.0f, height, accentColor);

        if (m_fontManager) {
            char valStr[32];
            snprintf(valStr, sizeof(valStr), "%.3f", value);
            m_drawList->AddText(*m_fontManager, valStr, boxX + 15.0f, y + height * 0.7f, 0xFFE0E0E0);
        }
    }
    return valueChanged;
}

bool UIContext::ImageButton(uint32_t textureID, float x, float y, float width, float height,
                            uint32_t baseColor, uint32_t hoverColor, uint32_t clickColor) {
    bool isHovered = false;
    bool isClicked = false;
    const bool interactionAllowed = IsInteractionAllowed(x, y, width, height);
    const UIClipRect clipRect = m_drawList ? m_drawList->GetActiveClipRect() : UIClipRect{};

    if (m_inputManager && interactionAllowed) {
        int mx = m_inputManager->GetMouseX();
        int my = m_inputManager->GetMouseY();

        if (UICtx_PointInWidgetClip((float)mx, (float)my, x, y, width, height, clipRect)) {
            isHovered = true;
            if (m_inputManager->IsMouseButtonPressed(0)) isClicked = true;
        }
    }

    uint32_t color = baseColor;
    if (isHovered)
        color = (m_inputManager && m_inputManager->IsMouseButtonDown(0)) ? clickColor : hoverColor;

    if (m_drawList)
        m_drawList->AddImage(textureID, x, y, width, height, color);

    return isClicked;
}
