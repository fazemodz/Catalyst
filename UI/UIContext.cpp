#include "UIContext.h"
#include <windows.h>

void UIContext::Initialize(UIDrawList* drawList, FontManager* fontManager, InputManager* inputManager) {
    m_drawList = drawList;
    m_fontManager = fontManager;
    m_inputManager = inputManager;
}

bool UIContext::Button(const std::string& label, float x, float y, float width, float height, uint32_t baseColor, uint32_t hoverColor, uint32_t clickColor) {
    bool isHovered = false;
    bool isClicked = false;

    if (m_inputManager) {
        int mx = m_inputManager->GetMouseX();
        int my = m_inputManager->GetMouseY();

        if (mx >= x && mx <= x + width && my >= y && my <= y + height) {
            isHovered = true;
            if (m_inputManager->IsMouseButtonPressed(0)) isClicked = true;
        }
    }

    uint32_t bgColor = baseColor;
    if (isHovered) {
        bgColor = (m_inputManager && m_inputManager->IsMouseButtonDown(0)) ? clickColor : hoverColor;
    }

    if (m_drawList) {
        if ((bgColor >> 24) > 0) m_drawList->AddRectFilled(x, y, width, height, bgColor);
        
        if (m_fontManager && !label.empty()) {
            float textX = x + 15.0f;
            float textY = y + (height / 2.0f) + 8.0f; 
            m_drawList->AddText(*m_fontManager, label, textX, textY, 0xFFFFFFFF);
        }
    }
    return isClicked;
}

bool UIContext::TextInput(const std::string& id, std::string& text, float x, float y, float width, float height, bool& isActive) {
    bool isHovered = false;
    if (m_inputManager) {
        int mx = m_inputManager->GetMouseX();
        int my = m_inputManager->GetMouseY();
        if (mx >= x && mx <= x + width && my >= y && my <= y + height) {
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

    uint32_t bgColor = isActive ? 0xFF111111 : (isHovered ? 0xFF333333 : 0xFF2A2A2A);
    uint32_t borderColor = isActive ? 0xFFD77800 : 0xFF444444; 

    if (m_drawList) {
        m_drawList->AddRectFilled(x - 2, y - 2, width + 4, height + 4, borderColor);
        m_drawList->AddRectFilled(x, y, width, height, bgColor);
        if (m_fontManager) {
            std::string displayText = text + (isActive && (GetTickCount() / 500) % 2 == 0 ? "_" : ""); // Blinking cursor
            bool isEmpty = text.empty() && !isActive;
            m_drawList->AddText(*m_fontManager, isEmpty ? "Project Name..." : displayText, x + 10, y + (height / 2.0f) + 8.0f, isEmpty ? 0xFF777777 : 0xFFFFFFFF);
        }
    }
    return isActive;
}