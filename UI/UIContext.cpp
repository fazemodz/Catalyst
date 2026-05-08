#include "UIContext.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include "Camera.h" 

namespace {
bool PointInRect(float px, float py, float x, float y, float width, float height) {
    return px >= x && px <= (x + width) && py >= y && py <= (y + height);
}

bool PointInWidgetClip(float px, float py, float x, float y, float width, float height, const UIClipRect& clipRect) {
    const float clipX = clipRect.Enabled ? (std::max)(x, clipRect.X) : x;
    const float clipY = clipRect.Enabled ? (std::max)(y, clipRect.Y) : y;
    const float clipRight = clipRect.Enabled ? (std::min)(x + width, clipRect.X + clipRect.Width) : (x + width);
    const float clipBottom = clipRect.Enabled ? (std::min)(y + height, clipRect.Y + clipRect.Height) : (y + height);
    return clipRight > clipX && clipBottom > clipY && PointInRect(px, py, clipX, clipY, clipRight - clipX, clipBottom - clipY);
}
}

void UIContext::Initialize(UIDrawList* drawList, FontManager* fontManager, InputManager* inputManager) {
    m_drawList = drawList;
    m_fontManager = fontManager;
    m_inputManager = inputManager;
}

void UIContext::SetModalRegion(float x, float y, float width, float height) {
    m_modalRegionActive = true;
    m_modalRegionX = x;
    m_modalRegionY = y;
    m_modalRegionWidth = width;
    m_modalRegionHeight = height;
}

void UIContext::ClearModalRegion() {
    m_modalRegionActive = false;
    m_modalRegionX = 0.0f;
    m_modalRegionY = 0.0f;
    m_modalRegionWidth = 0.0f;
    m_modalRegionHeight = 0.0f;
}

void UIContext::AddText(const std::string& text, float x, float y, uint32_t color, float wrapWidth) {
    if (m_drawList && m_fontManager && !text.empty()) {
        m_drawList->AddText(*m_fontManager, text, x, y, color, wrapWidth);
    }
}

bool UIContext::IsInteractionAllowed(float x, float y, float width, float height) const {
    if (m_drawList) {
        const UIClipRect clipRect = m_drawList->GetActiveClipRect();
        if (clipRect.Enabled) {
            const float clipRight = clipRect.X + clipRect.Width;
            const float clipBottom = clipRect.Y + clipRect.Height;
            if ((x + width) <= clipRect.X || x >= clipRight || (y + height) <= clipRect.Y || y >= clipBottom) {
                return false;
            }
        }
    }

    if (!m_modalRegionActive) {
        return true;
    }

    return x >= m_modalRegionX &&
           y >= m_modalRegionY &&
           (x + width) <= (m_modalRegionX + m_modalRegionWidth) &&
           (y + height) <= (m_modalRegionY + m_modalRegionHeight);
}

bool UIContext::Button(const std::string& label, float x, float y, float width, float height, uint32_t baseColor, uint32_t hoverColor, uint32_t clickColor) {
    bool isHovered = false;
    bool isClicked = false;
    const bool interactionAllowed = IsInteractionAllowed(x, y, width, height);
    const UIClipRect clipRect = m_drawList ? m_drawList->GetActiveClipRect() : UIClipRect{};

    if (m_inputManager && interactionAllowed) {
        int mx = m_inputManager->GetMouseX();
        int my = m_inputManager->GetMouseY();

        if (PointInWidgetClip(static_cast<float>(mx), static_cast<float>(my), x, y, width, height, clipRect)) {
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
            m_drawList->AddText(*m_fontManager, label, textX, textY, 0xFFE0E0E0);
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
        if (PointInWidgetClip(static_cast<float>(mx), static_cast<float>(my), x, y, width, height, clipRect)) {
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
            std::string displayText = text + (isActive && (GetTickCount() / 500) % 2 == 0 ? "_" : ""); 
            bool isEmpty = text.empty() && !isActive;
            m_drawList->AddText(*m_fontManager, isEmpty ? "Project Name..." : displayText, x + 10, y + (height / 2.0f) + 8.0f, isEmpty ? 0xFF777777 : 0xFFFFFFFF);
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

        if (PointInWidgetClip(static_cast<float>(mx), static_cast<float>(my), x, y, size, size, clipRect)) {
            isHovered = true;
            if (m_inputManager->IsMouseButtonPressed(0)) {
                value = !value;
                isClicked = true;
            }
        }
    }

    uint32_t bgColor = isHovered ? 0xFF555555 : 0xFF333333;
    uint32_t checkColor = 0xFFE07020;
    
    if (m_drawList) {
        m_drawList->AddRectFilled(x, y, size, size, bgColor);
        if (value) {
            float padding = size * 0.2f;
            m_drawList->AddRectFilled(x + padding, y + padding, size - padding * 2, size - padding * 2, checkColor);
        }
        
        if (m_fontManager && !label.empty()) {
            m_drawList->AddText(*m_fontManager, label, x + size + 8.0f, y + size * 0.8f, 0xFFD0D0D0);
        }
    }
    
    return isClicked;
}

bool UIContext::DragFloat(const std::string& label, float& value, float dragSpeed, float x, float y, float width, float height) {
    bool isHovered = false;
    bool valueChanged = false;

    float labelWidth = width * 0.3f;
    float boxX = x + labelWidth;
    float boxWidth = width - labelWidth;
    const bool interactionAllowed = IsInteractionAllowed(boxX, y, boxWidth, height);
    const UIClipRect clipRect = m_drawList ? m_drawList->GetActiveClipRect() : UIClipRect{};

    if (m_inputManager && interactionAllowed) {
        int mx = m_inputManager->GetMouseX();
        int my = m_inputManager->GetMouseY();

        if (PointInWidgetClip(static_cast<float>(mx), static_cast<float>(my), boxX, y, boxWidth, height, clipRect)) {
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
        if (m_fontManager) {
            m_drawList->AddText(*m_fontManager, label, x, y + height * 0.7f, 0xFF9A9A9A);
        }

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

void UIContext::Image(uint32_t textureID, float x, float y, float width, float height, uint32_t color) {
    if (m_drawList) {
        m_drawList->AddImage(textureID, x, y, width, height, color);
    }
}

bool UIContext::ImageButton(uint32_t textureID, float x, float y, float width, float height, uint32_t baseColor, uint32_t hoverColor, uint32_t clickColor) {
    bool isHovered = false;
    bool isClicked = false;
    const bool interactionAllowed = IsInteractionAllowed(x, y, width, height);
    const UIClipRect clipRect = m_drawList ? m_drawList->GetActiveClipRect() : UIClipRect{};

    if (m_inputManager && interactionAllowed) {
        int mx = m_inputManager->GetMouseX();
        int my = m_inputManager->GetMouseY();

        if (PointInWidgetClip(static_cast<float>(mx), static_cast<float>(my), x, y, width, height, clipRect)) {
            isHovered = true;
            if (m_inputManager->IsMouseButtonPressed(0)) isClicked = true;
        }
    }

    uint32_t color = baseColor;
    if (isHovered) {
        color = (m_inputManager && m_inputManager->IsMouseButtonDown(0)) ? clickColor : hoverColor;
    }

    if (m_drawList) {
        m_drawList->AddImage(textureID, x, y, width, height, color);
    }

    return isClicked;
}

bool UIContext::TransformGizmo(DirectX::XMFLOAT3& position, Camera& camera, float viewX, float viewY, float viewW, float viewH, bool& isHovered) {
    isHovered = false;
    if (!m_drawList || !m_inputManager) return false;

    DirectX::XMMATRIX viewProj = camera.GetViewMatrix() * camera.GetProjectionMatrix();
    
    auto Project = [&](DirectX::XMVECTOR pos, DirectX::XMFLOAT2& outScreen) -> bool {
        DirectX::XMVECTOR ndc = DirectX::XMVector3TransformCoord(pos, viewProj);
        DirectX::XMFLOAT3 ndc3; 
        DirectX::XMStoreFloat3(&ndc3, ndc);
        outScreen.x = viewX + (ndc3.x + 1.0f) * 0.5f * viewW;
        outScreen.y = viewY + (1.0f - ndc3.y) * 0.5f * viewH;
        return ndc3.z >= 0.0f && ndc3.z <= 1.0f; 
    };

    DirectX::XMVECTOR wPos = DirectX::XMLoadFloat3(&position);
    DirectX::XMFLOAT2 cScr;
    if (!Project(wPos, cScr)) return false; 

    float dist = DirectX::XMVectorGetZ(DirectX::XMVector3Transform(wPos, camera.GetViewMatrix()));
    float gizmoScale = dist * 0.15f; 

    DirectX::XMFLOAT2 xScr, yScr, zScr;
    Project(DirectX::XMVectorAdd(wPos, DirectX::XMVectorSet(gizmoScale, 0, 0, 0)), xScr);
    Project(DirectX::XMVectorAdd(wPos, DirectX::XMVectorSet(0, gizmoScale, 0, 0)), yScr);
    Project(DirectX::XMVectorAdd(wPos, DirectX::XMVectorSet(0, 0, gizmoScale, 0)), zScr);

    int mx = m_inputManager->GetMouseX();
    int my = m_inputManager->GetMouseY();
    DirectX::XMFLOAT2 mPos = { (float)mx, (float)my };
    bool inViewport = (mx >= viewX && mx <= viewX + viewW && my >= viewY && my <= viewY + viewH);

    auto DistLine = [](DirectX::XMFLOAT2 p, DirectX::XMFLOAT2 a, DirectX::XMFLOAT2 b) {
        float l2 = (b.x - a.x)*(b.x - a.x) + (b.y - a.y)*(b.y - a.y);
        if (l2 == 0.0f) return sqrtf((p.x - a.x)*(p.x - a.x) + (p.y - a.y)*(p.y - a.y));
        float t = (std::max)(0.0f, (std::min)(1.0f, ((p.x - a.x) * (b.x - a.x) + (p.y - a.y) * (b.y - a.y)) / l2));
        DirectX::XMFLOAT2 proj = { a.x + t * (b.x - a.x), a.y + t * (b.y - a.y) };
        return sqrtf((p.x - proj.x)*(p.x - proj.x) + (p.y - proj.y)*(p.y - proj.y));
    };

    int hoveredAxis = -1;
    if (m_activeGizmoAxis == -1 && inViewport) {
        if (DistLine(mPos, cScr, xScr) < 12.0f) hoveredAxis = 0;
        else if (DistLine(mPos, cScr, yScr) < 12.0f) hoveredAxis = 1;
        else if (DistLine(mPos, cScr, zScr) < 12.0f) hoveredAxis = 2;

        if (hoveredAxis != -1 && m_inputManager->IsMouseButtonPressed(0)) {
            m_activeGizmoAxis = hoveredAxis;
            m_lastGizmoMouse = mPos;
        }
    }

    bool changed = false;
    if (m_activeGizmoAxis != -1) {
        if (!m_inputManager->IsMouseButtonDown(0)) {
            m_activeGizmoAxis = -1;
        } else {
            float dx = mPos.x - m_lastGizmoMouse.x;
            float dy = mPos.y - m_lastGizmoMouse.y;
            
            DirectX::XMFLOAT2 axisVec = {0,0};
            if (m_activeGizmoAxis == 0) axisVec = { xScr.x - cScr.x, xScr.y - cScr.y };
            if (m_activeGizmoAxis == 1) axisVec = { yScr.x - cScr.x, yScr.y - cScr.y };
            if (m_activeGizmoAxis == 2) axisVec = { zScr.x - cScr.x, zScr.y - cScr.y };
            
            float len = sqrtf(axisVec.x*axisVec.x + axisVec.y*axisVec.y);
            if (len > 0.001f) {
                axisVec.x /= len; axisVec.y /= len;
                float dragAmt = (dx * axisVec.x + dy * axisVec.y);
                float move3D = dragAmt * dist * 0.0015f; 

                if (m_activeGizmoAxis == 0) position.x += move3D;
                if (m_activeGizmoAxis == 1) position.y += move3D; 
                if (m_activeGizmoAxis == 2) position.z += move3D; 
                changed = true;
            }
            m_lastGizmoMouse = mPos;
        }
    }

   
    isHovered = (hoveredAxis != -1 || m_activeGizmoAxis != -1);

    uint32_t colX = (m_activeGizmoAxis == 0 || hoveredAxis == 0) ? 0xFF5555FF : 0xFF2222DD; 
    uint32_t colY = (m_activeGizmoAxis == 1 || hoveredAxis == 1) ? 0xFF55FF55 : 0xFF22DD22; 
    uint32_t colZ = (m_activeGizmoAxis == 2 || hoveredAxis == 2) ? 0xFFFF5555 : 0xFFDD2222; 

    m_drawList->AddLine(cScr.x, cScr.y, xScr.x, xScr.y, 4.0f, colX);
    m_drawList->AddLine(cScr.x, cScr.y, yScr.x, yScr.y, 4.0f, colY);
    m_drawList->AddLine(cScr.x, cScr.y, zScr.x, zScr.y, 4.0f, colZ);
    m_drawList->AddRectFilled(xScr.x - 5, xScr.y - 5, 10, 10, colX);
    m_drawList->AddRectFilled(yScr.x - 5, yScr.y - 5, 10, 10, colY);
    m_drawList->AddRectFilled(zScr.x - 5, zScr.y - 5, 10, 10, colZ);

    return changed;
}
