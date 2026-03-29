#include "EngineApp.h"
#include <algorithm>
#include <filesystem>
#include <vector>

DXRenderer* g_Renderer = nullptr;
InputManager* g_InputManager = nullptr;
EngineApp* EngineApp::s_instance = nullptr;

namespace fs = std::filesystem;

namespace {
constexpr wchar_t kWindowClassName[] = L"CatalystEngine";
constexpr int kViewerClientWidth = 1600;
constexpr int kViewerClientHeight = 900;

std::wstring BuildActorViewerTitle(const std::wstring& assetPath) {
    return L"Catalyst Asset Viewer - " + fs::path(assetPath).stem().wstring();
}
}

EngineApp* EngineApp::Get() {
    return s_instance;
}

bool OpenActorViewerWindow(const std::wstring& assetPath) {
    EngineApp* app = EngineApp::Get();
    return app ? app->OpenActorViewerWindow(assetPath) : false;
}

bool OpenMaterialEditorWindow(const std::wstring& assetPath) {
    EngineApp* app = EngineApp::Get();
    return app ? app->OpenMaterialEditorWindow(assetPath) : false;
}

EngineApp::WindowContext* EngineApp::CreateWindowContext(const std::wstring& title, DWORD style, int clientWidth, int clientHeight, int x, int y) {
    RECT windowRect = { 0, 0, clientWidth, clientHeight };
    AdjustWindowRect(&windowRect, style, FALSE);

    HWND hwnd = CreateWindow(
        kWindowClassName,
        title.c_str(),
        style,
        x,
        y,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        m_hInstance,
        nullptr);
    if (!hwnd) {
        return nullptr;
    }

    auto window = std::make_unique<WindowContext>();
    window->hwnd = hwnd;
    window->inputManager = std::make_unique<InputManager>();
    window->renderer = std::make_unique<DXRenderer>();

    DXRenderer* previousRenderer = g_Renderer;
    InputManager* previousInputManager = g_InputManager;
    g_Renderer = window->renderer.get();
    g_InputManager = window->inputManager.get();
    window->renderer->Initialize(hwnd, clientWidth, clientHeight);
    g_Renderer = previousRenderer;
    g_InputManager = previousInputManager;

    WindowContext* rawWindow = window.get();
    m_windows.push_back(std::move(window));
    return rawWindow;
}

EngineApp::WindowContext* EngineApp::FindWindowContext(HWND hwnd) {
    auto it = std::find_if(m_windows.begin(), m_windows.end(),
        [hwnd](const std::unique_ptr<WindowContext>& window) {
            return window->hwnd == hwnd;
        });
    return it != m_windows.end() ? it->get() : nullptr;
}

void EngineApp::DestroyWindowContext(HWND hwnd) {
    auto it = std::find_if(m_windows.begin(), m_windows.end(),
        [hwnd](const std::unique_ptr<WindowContext>& window) {
            return window->hwnd == hwnd;
        });
    if (it == m_windows.end()) {
        return;
    }

    g_Renderer = (*it)->renderer.get();
    g_InputManager = (*it)->inputManager.get();
    if ((*it)->renderer) {
        (*it)->renderer->Shutdown();
    }

    m_windows.erase(it);

    if (m_windows.empty()) {
        g_Renderer = nullptr;
        g_InputManager = nullptr;
        PostQuitMessage(0);
    } else {
        g_Renderer = m_windows.front()->renderer.get();
        g_InputManager = m_windows.front()->inputManager.get();
    }
}

bool EngineApp::OpenActorViewerWindow(const std::wstring& assetPath) {
    if (assetPath.empty()) {
        return false;
    }

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);
    const int posX = (screenW - kViewerClientWidth) / 2;
    const int posY = (screenH - kViewerClientHeight) / 2;

    WindowContext* viewerWindow = CreateWindowContext(
        BuildActorViewerTitle(assetPath),
        WS_OVERLAPPEDWINDOW,
        kViewerClientWidth,
        kViewerClientHeight,
        posX,
        posY);
    if (!viewerWindow) {
        return false;
    }

    viewerWindow->renderer->SetEngineState(EngineState::Editor);
    viewerWindow->renderer->SetStandaloneActorViewerWindow(true);
    if (!viewerWindow->renderer->OpenActorAssetViewer(assetPath)) {
        DestroyWindow(viewerWindow->hwnd);
        return false;
    }

    ShowWindow(viewerWindow->hwnd, SW_SHOW);
    UpdateWindow(viewerWindow->hwnd);
    return true;
}

bool EngineApp::OpenMaterialEditorWindow(const std::wstring& assetPath) {
    if (assetPath.empty()) {
        return false;
    }

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);
    const int posX = (screenW - kViewerClientWidth) / 2;
    const int posY = (screenH - kViewerClientHeight) / 2;

    WindowContext* materialWindow = CreateWindowContext(
        L"Catalyst Material Editor - " + fs::path(assetPath).stem().wstring(),
        WS_OVERLAPPEDWINDOW,
        kViewerClientWidth,
        kViewerClientHeight,
        posX,
        posY);
    if (!materialWindow) {
        return false;
    }

    materialWindow->renderer->SetEngineState(EngineState::Editor);
    materialWindow->renderer->SetStandaloneMaterialEditorWindow(true);
    if (!materialWindow->renderer->OpenMaterialAssetEditor(assetPath)) {
        DestroyWindow(materialWindow->hwnd);
        return false;
    }

    ShowWindow(materialWindow->hwnd, SW_SHOW);
    UpdateWindow(materialWindow->hwnd);
    return true;
}

void EngineApp::Run(HINSTANCE hInstance, int nShowCmd) {
    s_instance = this;
    m_hInstance = hInstance;

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, kWindowClassName, NULL };
    RegisterClassEx(&wc);

    // FIXED LAUNCHER STYLE: Prevent resizing/maximizing while in Launcher mode
    DWORD launcherStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

    // Center the Launcher on the user's screen
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int launchW = 1000;
    int launchH = 650;

    WindowContext* mainWindow = CreateWindowContext(
        L"Catalyst Launcher",
        launcherStyle,
        launchW,
        launchH,
        (screenW - launchW) / 2,
        (screenH - launchH) / 2);
    if (!mainWindow) {
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        s_instance = nullptr;
        return;
    }

    ShowWindow(mainWindow->hwnd, nShowCmd);
    UpdateWindow(mainWindow->hwnd);

    MSG msg = {};
    while (!m_windows.empty()) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            std::vector<WindowContext*> renderWindows;
            renderWindows.reserve(m_windows.size());
            for (const auto& window : m_windows) {
                renderWindows.push_back(window.get());
            }

            for (WindowContext* window : renderWindows) {
                if (FindWindowContext(window->hwnd) == window && window->inputManager) {
                    window->inputManager->Update();
                }
            }

            bool renderedAnyWindow = false;
            for (WindowContext* window : renderWindows) {
                if (FindWindowContext(window->hwnd) != window || !window->renderer || !window->inputManager) {
                    continue;
                }
                if (window->isMinimized || IsIconic(window->hwnd) || !IsWindowVisible(window->hwnd)) {
                    continue;
                }

                g_InputManager = window->inputManager.get();
                g_Renderer = window->renderer.get();
                window->renderer->Render();
                window->inputManager->EndFrame();
                renderedAnyWindow = true;
            }

            if (!renderedAnyWindow) {
                for (WindowContext* window : renderWindows) {
                    if (FindWindowContext(window->hwnd) == window && window->inputManager) {
                        window->inputManager->EndFrame();
                    }
                }
            }

            if (!renderedAnyWindow) {
                Sleep(16);
            }
        }
    }

    while (!m_windows.empty()) {
        DestroyWindow(m_windows.back()->hwnd);
    }

    g_Renderer = nullptr;
    g_InputManager = nullptr;
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    s_instance = nullptr;
}

LRESULT CALLBACK EngineApp::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    EngineApp* app = EngineApp::Get();
    WindowContext* window = app ? app->FindWindowContext(hwnd) : nullptr;

    if (window) {
        g_Renderer = window->renderer.get();
        g_InputManager = window->inputManager.get();
        if (window->inputManager) {
            window->inputManager->ProcessMessage(msg, wParam, lParam);
        }
    }

    switch (msg) {
    case WM_SIZE:
        if (window) {
            window->isMinimized = (wParam == SIZE_MINIMIZED);
        }
        if (window && window->renderer && wParam != SIZE_MINIMIZED) {
            window->renderer->OnResize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (app) {
            app->DestroyWindowContext(hwnd);
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
