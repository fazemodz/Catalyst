#pragma once
#include <windows.h>
#include <vector>

class InputManager {
public:
    void Update();
    void EndFrame();
    void ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    int GetMouseX() const { return m_mouseX; }
    int GetMouseY() const { return m_mouseY; }
    bool IsMouseButtonDown(int button) const { return m_mouseDown[button]; }
    bool IsMouseButtonPressed(int button) const { return m_mousePressed[button]; }
    int GetMouseWheelDelta() const { return m_mouseWheelDelta; }
    
    // New: Retrieves characters typed this frame
    const std::vector<char>& GetTypedCharacters() const { return m_typedChars; }

private:
    int m_mouseX = 0;
    int m_mouseY = 0;
    bool m_mouseDown[3] = { false, false, false };
    bool m_mousePrevDown[3] = { false, false, false };
    bool m_mousePressed[3] = { false, false, false };
    int m_mouseWheelDelta = 0;
    std::vector<char> m_typedChars;
};
