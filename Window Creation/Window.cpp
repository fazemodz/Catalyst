#include "Window.h"

// Input Globals
bool g_Keys[256] = { false };
bool g_RightMouseDown = false;
int g_MouseDeltaX = 0;
int g_MouseDeltaY = 0;
static POINT lastMousePos;

bool Window::Initialize(int width, int height, const wchar_t* title) {
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WindowProc, 0, 0, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"DX12WindowClass", nullptr };
    RegisterClassEx(&wc);

    m_hwnd = CreateWindow(wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr, wc.hInstance, this);
    if (!m_hwnd) return false;

    ShowWindow(m_hwnd, SW_SHOW);
    m_isOpen = true;
    return true;
}

void Window::ProcessMessages() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (msg.message == WM_QUIT) m_isOpen = false;
    }
}
LRESULT CALLBACK Window::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_KEYDOWN:
        g_Keys[wParam] = true;
        return 0;
    case WM_KEYUP:
        g_Keys[wParam] = false;
        return 0;
    case WM_RBUTTONDOWN:
        g_RightMouseDown = true;
        GetCursorPos(&lastMousePos);
        SetCapture(hwnd);
        ShowCursor(FALSE); // Hide cursor while looking around
        return 0;
    case WM_RBUTTONUP:
        g_RightMouseDown = false;
        ReleaseCapture();
        ShowCursor(TRUE);
        return 0;
    case WM_MOUSEMOVE:
        if (g_RightMouseDown) {
            POINT currentPos;
            GetCursorPos(&currentPos);
            g_MouseDeltaX = currentPos.x - lastMousePos.x;
            g_MouseDeltaY = currentPos.y - lastMousePos.y;
                
            // Keep the mouse locked in place so it doesn't hit screen edges
            SetCursorPos(lastMousePos.x, lastMousePos.y);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}