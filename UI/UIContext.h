#pragma once
#include <string>
#include <cstdint>
#include "UIDrawList.h"
#include "FontManager.h"
#include "InputManager.h"

class Camera;

class UIContext {
public:
    void Initialize(UIDrawList* drawList, FontManager* fontManager, InputManager* inputManager);
    
    // THE FIX: Added bool& isHovered parameter
    bool TransformGizmo(DirectX::XMFLOAT3& position, Camera& camera, float viewX, float viewY, float viewW, float viewH, bool& isHovered);

    bool Button(const std::string& label, float x, float y, float width, float height, 
                uint32_t baseColor = 0xFF444444, 
                uint32_t hoverColor = 0xFF666666, 
                uint32_t clickColor = 0xFF222222);
    
    bool TextInput(const std::string& id, std::string& text, float x, float y, float width, float height, bool& isActive);
    
    bool Checkbox(const std::string& label, bool& value, float x, float y, float size = 20.0f);
    
    bool DragFloat(const std::string& label, float& value, float dragSpeed, float x, float y, float width, float height = 24.0f);

    void Image(uint32_t textureID, float x, float y, float width, float height, uint32_t color = 0xFFFFFFFF);
    
    bool ImageButton(uint32_t textureID, float x, float y, float width, float height, 
                     uint32_t baseColor = 0xFFFFFFFF, 
                     uint32_t hoverColor = 0xFFDDDDDD, 
                     uint32_t clickColor = 0xFFAAAAAA);

private:
    UIDrawList* m_drawList = nullptr;
    FontManager* m_fontManager = nullptr;
    InputManager* m_inputManager = nullptr;
    std::string m_activeSliderId;
    
    int m_activeGizmoAxis = -1;
    DirectX::XMFLOAT2 m_lastGizmoMouse = {0,0};
    DirectX::XMFLOAT2 m_lastMousePos = {0,0};
};