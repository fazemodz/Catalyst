#pragma once
#include "../Error Handler/Common.h"
#include <string>
class Window {
public:
    bool Initialize(int width, int height, std::wstring title);
    void ProcessMessages();
    bool IsOpen() const;
    HWND GetHandle() const;
    ~Window();

private:
    HWND m_hwnd = nullptr;
    bool m_isOpen = true;
};