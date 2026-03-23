#include "EngineApp.h"

DXRenderer* g_Renderer = nullptr;
InputManager* g_InputManager = nullptr;

void EngineApp::Run(HINSTANCE hInstance, int nShowCmd) {
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"CatalystEngine", NULL };
    RegisterClassEx(&wc);

    // FIXED LAUNCHER STYLE: Prevent resizing/maximizing while in Launcher mode
    DWORD launcherStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

    // Center the Launcher on the user's screen
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int launchW = 1000;
    int launchH = 650;

    HWND hwnd = CreateWindow(wc.lpszClassName, L"Catalyst Launcher", launcherStyle, 
                             (screenW - launchW) / 2, (screenH - launchH) / 2, 
                             launchW, launchH, NULL, NULL, wc.hInstance, NULL);

    g_InputManager = new InputManager();
    g_Renderer = new DXRenderer();
    g_Renderer->Initialize(hwnd, launchW, launchH);

    ShowWindow(hwnd, nShowCmd);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            if (g_InputManager) {
                g_InputManager->Update();
            }
            g_Renderer->Render();
        }
    }

    g_Renderer->Shutdown();
    delete g_Renderer;
    delete g_InputManager;
    UnregisterClass(wc.lpszClassName, wc.hInstance);
}

LRESULT CALLBACK EngineApp::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_InputManager) {
        g_InputManager->ProcessMessage(msg, wParam, lParam);
    }

    switch (msg) {
    case WM_SIZE:
        if (g_Renderer && wParam != SIZE_MINIMIZED) {
            g_Renderer->OnResize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}