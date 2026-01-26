#pragma once
#include "../Error Handler/Common.h"

class Window {
public:
    bool Initialize(int width, int height, const wchar_t* title);
    void ProcessMessages();
    bool IsOpen() const { return m_isOpen; }
    HWND GetHandle() const { return m_hwnd; }
private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    HWND m_hwnd;
    bool m_isOpen = false;
};