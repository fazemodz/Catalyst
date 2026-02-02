#include "Window.h"
#include <windowsx.h> // For GET_X_LPARAM macros
#include "../imgui.h" // Adjust path to where your imgui.h is

// ----------------------------------------------------------------------
// GLOBAL INPUT VARIABLES (Linked to DXRenderer)
// ----------------------------------------------------------------------
bool g_Keys[256] = { false };
bool g_RightMouseDown = false;
int g_MouseDeltaX = 0;
int g_MouseDeltaY = 0;
int g_LastMouseX = 0;
int g_LastMouseY = 0;

// ----------------------------------------------------------------------
// IMGUI FORWARD DECLARATION
// ----------------------------------------------------------------------
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 1. PASS INPUT TO IMGUI
    // If ImGui wants the mouse/keyboard, we let it handle it and return true.
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

        // --- KEYBOARD INPUT (WASD) ---
    case WM_KEYDOWN:
        if (wParam < 256) g_Keys[wParam] = true;
        if (wParam == VK_ESCAPE) PostQuitMessage(0);
        return 0;
    case WM_KEYUP:
        if (wParam < 256) g_Keys[wParam] = false;
        return 0;

        // --- MOUSE INPUT (Camera Rotation) ---
    case WM_RBUTTONDOWN:
        g_RightMouseDown = true;
        g_LastMouseX = GET_X_LPARAM(lParam);
        g_LastMouseY = GET_Y_LPARAM(lParam);
        ShowCursor(FALSE); // Hide cursor when rotating
        return 0;
    case WM_RBUTTONUP:
        g_RightMouseDown = false;
        ShowCursor(TRUE);
        return 0;
    case WM_MOUSEMOVE:
        if (g_RightMouseDown) {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            g_MouseDeltaX = x - g_LastMouseX;
            g_MouseDeltaY = y - g_LastMouseY;
            g_LastMouseX = x;
            g_LastMouseY = y;
        }
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool Window::Initialize(int width, int height, std::wstring title) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"DX12EngineClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); // Make sure we have a visible cursor
    RegisterClass(&wc);

    // Adjust window size so client area matches requested width/height
    RECT rect = { 0, 0, width, height };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = CreateWindowEx(
        0, L"DX12EngineClass", title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!m_hwnd) return false;

    ShowWindow(m_hwnd, SW_SHOW);
    return true;
}

void Window::ProcessMessages() {
    MSG msg = {};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            m_isOpen = false;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

bool Window::IsOpen() const {
    return m_isOpen;
}

HWND Window::GetHandle() const {
    return m_hwnd;
}

Window::~Window() {
    // Optional cleanup
}