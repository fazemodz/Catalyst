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

std::wstring BuildBlueprintEditorTitle(const std::wstring& assetPath) {
    return L"Catalyst Blueprint Editor - " + fs::path(assetPath).stem().wstring();
}

std::wstring BuildStandalonePlayTitle(const AppLaunchRequest& request) {
    if (!request.projectFilePath.empty()) {
        const ProjectInfo info = ParseProjectFile(request.projectFilePath);
        if (!info.ProjectName.empty()) {
            return info.ProjectName;
        }
        return fs::path(request.projectFilePath).stem().wstring();
    }

    return L"Catalyst";
}

std::wstring JoinHumanList(const std::vector<std::wstring>& items) {
    if (items.empty()) {
        return L"";
    }
    if (items.size() == 1) {
        return items.front();
    }
    if (items.size() == 2) {
        return items[0] + L" and " + items[1];
    }

    std::wstring joined;
    for (size_t index = 0; index < items.size(); ++index) {
        if (index > 0) {
            joined += (index + 1 == items.size()) ? L", and " : L", ";
        }
        joined += items[index];
    }
    return joined;
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

bool OpenBlueprintEditorWindow(const std::wstring& assetPath) {
    EngineApp* app = EngineApp::Get();
    return app ? app->OpenBlueprintEditorWindow(assetPath) : false;
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

std::wstring EngineApp::DescribeUnsavedChangesAcrossWindows() const {
    std::vector<std::wstring> summaries;
    summaries.reserve(m_windows.size());
    for (const std::unique_ptr<WindowContext>& window : m_windows) {
        if (window == nullptr || window->renderer == nullptr || !window->renderer->HasPendingUnsavedChanges()) {
            continue;
        }

        const std::wstring summary = window->renderer->GetUnsavedChangesDescription();
        if (!summary.empty()) {
            summaries.push_back(summary);
        }
    }

    return JoinHumanList(summaries);
}

void EngineApp::QueueCloseAllPrompt(WindowContext* primaryWindow) {
    if (primaryWindow == nullptr || primaryWindow->renderer == nullptr) {
        CloseAllWindows();
        return;
    }

    const std::wstring summary = DescribeUnsavedChangesAcrossWindows();
    if (summary.empty()) {
        CloseAllWindows();
        return;
    }

    ShowWindow(primaryWindow->hwnd, SW_RESTORE);
    SetForegroundWindow(primaryWindow->hwnd);
    primaryWindow->renderer->OpenClosePrompt(true, summary);
}

bool EngineApp::ResolveCloseAllWindowCommand(bool saveChanges) {
    if (saveChanges) {
        for (const std::unique_ptr<WindowContext>& window : m_windows) {
            if (window == nullptr || window->renderer == nullptr) {
                continue;
            }

            if (!window->renderer->SavePendingUnsavedChanges()) {
                for (const std::unique_ptr<WindowContext>& candidate : m_windows) {
                    if (candidate && candidate->isPrimaryWindow && candidate->renderer) {
                        candidate->renderer->SetClosePromptError(L"One or more files could not be saved. Fix that first, or discard the changes.");
                        break;
                    }
                }
                return false;
            }
        }
    }

    CloseAllWindows();
    return true;
}

void EngineApp::CloseAllWindows() {
    if (m_isClosingAllWindows) {
        return;
    }

    m_isClosingAllWindows = true;
    std::vector<HWND> windowHandles;
    windowHandles.reserve(m_windows.size());
    for (const std::unique_ptr<WindowContext>& window : m_windows) {
        if (window && window->hwnd != nullptr) {
            windowHandles.push_back(window->hwnd);
        }
    }

    for (HWND windowHandle : windowHandles) {
        if (IsWindow(windowHandle)) {
            DestroyWindow(windowHandle);
        }
    }

    m_isClosingAllWindows = false;
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
    // Render a simple preview of the blueprint in the actor viewer.
    viewerWindow->renderer->RenderBlueprintPreview(assetPath);

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

bool EngineApp::OpenBlueprintEditorWindow(const std::wstring& assetPath) {
    if (assetPath.empty()) {
        return false;
    }

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);
    const int posX = (screenW - kViewerClientWidth) / 2;
    const int posY = (screenH - kViewerClientHeight) / 2;

    WindowContext* blueprintWindow = CreateWindowContext(
        BuildBlueprintEditorTitle(assetPath),
        WS_OVERLAPPEDWINDOW,
        kViewerClientWidth,
        kViewerClientHeight,
        posX,
        posY);
    if (!blueprintWindow) {
        return false;
    }

    blueprintWindow->renderer->SetEngineState(EngineState::Editor);
    blueprintWindow->renderer->SetStandaloneBlueprintEditorWindow(true);
    if (!blueprintWindow->renderer->OpenBlueprintAssetEditor(assetPath)) {
        DestroyWindow(blueprintWindow->hwnd);
        return false;
    }

    ShowWindow(blueprintWindow->hwnd, SW_SHOW);
    UpdateWindow(blueprintWindow->hwnd);
    return true;
}

void EngineApp::Run(HINSTANCE hInstance, int nShowCmd) {
    s_instance = this;
    m_hInstance = hInstance;

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, kWindowClassName, NULL };
    RegisterClassEx(&wc);

    AppLaunchRequest startupRequest;
    const bool standalonePlayLaunch = ConsumeAppLaunchRequest(startupRequest) && startupRequest.play;
    if (standalonePlayLaunch) {
        SetAppLaunchRequest(startupRequest);
    }

    DWORD launcherStyle = standalonePlayLaunch
        ? WS_OVERLAPPEDWINDOW
        : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int launchW = standalonePlayLaunch ? screenW : 1000;
    int launchH = standalonePlayLaunch ? screenH : 650;

    WindowContext* mainWindow = CreateWindowContext(
        standalonePlayLaunch ? BuildStandalonePlayTitle(startupRequest) : L"Catalyst Launcher",
        launcherStyle,
        launchW,
        launchH,
        standalonePlayLaunch ? 0 : (screenW - launchW) / 2,
        standalonePlayLaunch ? 0 : (screenH - launchH) / 2);
    if (!mainWindow) {
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        s_instance = nullptr;
        return;
    }
    mainWindow->isPrimaryWindow = true;

    ShowWindow(mainWindow->hwnd, standalonePlayLaunch ? SW_MAXIMIZE : nShowCmd);
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
                const DXRenderer::WindowCommand windowCommand = window->renderer->ConsumePendingWindowCommand();
                window->inputManager->EndFrame();
                renderedAnyWindow = true;

                if (windowCommand == DXRenderer::WindowCommand::CloseThisWindow) {
                    if (IsWindow(window->hwnd)) {
                        DestroyWindow(window->hwnd);
                    }
                } else if (windowCommand == DXRenderer::WindowCommand::CloseAllDiscard) {
                    ResolveCloseAllWindowCommand(false);
                    break;
                } else if (windowCommand == DXRenderer::WindowCommand::CloseAllSave) {
                    ResolveCloseAllWindowCommand(true);
                    break;
                }
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
        if (app != nullptr && window != nullptr && window->isPrimaryWindow) {
            if (app->m_isClosingAllWindows) {
                app->CloseAllWindows();
            } else {
                app->QueueCloseAllPrompt(window);
            }
            return 0;
        }
        if (app == nullptr || app->m_isClosingAllWindows || window == nullptr || window->renderer == nullptr) {
            DestroyWindow(hwnd);
        } else if (!window->renderer->HasPendingUnsavedChanges()) {
            DestroyWindow(hwnd);
        } else {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            window->renderer->OpenClosePrompt(false);
        }
        return 0;
    case WM_DESTROY:
        if (app) {
            app->DestroyWindowContext(hwnd);
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
