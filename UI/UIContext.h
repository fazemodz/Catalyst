#pragma once
#include <string>
#include <cstdint>
#include "UIDrawList.h"
#include "FontManager.h"
#include "InputManager.h"

class UIContext {
public:
    void Initialize(UIDrawList* drawList, FontManager* fontManager, InputManager* inputManager);
    
    bool Button(const std::string& label, float x, float y, float width, float height, 
                uint32_t baseColor = 0xFF444444, 
                uint32_t hoverColor = 0xFF666666, 
                uint32_t clickColor = 0xFF222222);
    
    bool TextInput(const std::string& id, std::string& text, float x, float y, float width, float height, bool& isActive);

private:
    UIDrawList* m_drawList = nullptr;
    FontManager* m_fontManager = nullptr;
    InputManager* m_inputManager = nullptr;
};