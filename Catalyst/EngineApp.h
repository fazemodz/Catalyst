#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <vector>
#include "DXRenderer.h"
#include "InputManager.h"

class EngineApp {
public:
    void Run(HINSTANCE hInstance, int nShowCmd);
    bool OpenActorViewerWindow(const std::wstring& assetPath);
    bool OpenMaterialEditorWindow(const std::wstring& assetPath);
    bool OpenBlueprintEditorWindow(const std::wstring& assetPath);
    bool ResolveCloseAllWindowCommand(bool saveChanges);
    static EngineApp* Get();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    struct WindowContext {
        HWND hwnd = nullptr;
        bool isPrimaryWindow = false;
        bool isMinimized = false;
        std::unique_ptr<InputManager> inputManager;
        std::unique_ptr<DXRenderer> renderer;
    };

    WindowContext* CreateWindowContext(const std::wstring& title, DWORD style, int clientWidth, int clientHeight, int x, int y);
    WindowContext* FindWindowContext(HWND hwnd);
    std::wstring DescribeUnsavedChangesAcrossWindows() const;
    void QueueCloseAllPrompt(WindowContext* primaryWindow);
    void CloseAllWindows();
    void DestroyWindowContext(HWND hwnd);

    HINSTANCE m_hInstance = nullptr;
    std::vector<std::unique_ptr<WindowContext>> m_windows;
    bool m_isClosingAllWindows = false;

    static EngineApp* s_instance;
};

bool OpenActorViewerWindow(const std::wstring& assetPath);
bool OpenMaterialEditorWindow(const std::wstring& assetPath);
bool OpenBlueprintEditorWindow(const std::wstring& assetPath);
