#include "InputManager.h"

void InputManager::Update() {
    for (int i = 0; i < 3; ++i) {
        m_mousePressed[i] = m_mouseDown[i] && !m_mousePrevDown[i];
        m_mousePrevDown[i] = m_mouseDown[i];
    }
}

void InputManager::EndFrame() {
    m_mouseWheelDelta = 0;
    m_typedChars.clear();
}

void InputManager::ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_MOUSEMOVE:
        m_mouseX = LOWORD(lParam);
        m_mouseY = HIWORD(lParam);
        break;
    case WM_LBUTTONDOWN: m_mouseDown[0] = true; break;
    case WM_LBUTTONUP:   m_mouseDown[0] = false; break;
    case WM_RBUTTONDOWN: m_mouseDown[1] = true; break;
    case WM_RBUTTONUP:   m_mouseDown[1] = false; break;
    case WM_MBUTTONDOWN: m_mouseDown[2] = true; break;
    case WM_MBUTTONUP:   m_mouseDown[2] = false; break;
    case WM_MOUSEWHEEL:
        m_mouseWheelDelta += GET_WHEEL_DELTA_WPARAM(wParam);
        break;
    case WM_CHAR:
        // Capture keyboard typing, including Backspace (0x08)
        if (wParam > 0 && wParam < 0x10000) {
            m_typedChars.push_back(static_cast<char>(wParam));
        }
        break;
    }
}
