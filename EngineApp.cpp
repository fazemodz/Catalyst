#include "EngineApp.h"
#include "imgui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

DXRenderer* g_Renderer = nullptr;
InputManager* g_InputManager = nullptr;

void EngineApp::Run(HINSTANCE hInstance, int nShowCmd) {
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"CatalystEngine", NULL };
    RegisterClassEx(&wc);

    // Initial Window Title set to Catalyst Launcher
    HWND hwnd = CreateWindow(wc.lpszClassName, L"Catalyst Launcher", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, NULL, NULL, wc.hInstance, NULL);

    g_InputManager = new InputManager();
    g_Renderer = new DXRenderer();
    g_Renderer->Initialize(hwnd, 1280, 800);

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
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;

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