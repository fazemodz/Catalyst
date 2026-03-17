#pragma once
#include <windows.h>
#include "DXRenderer.h"
#include "InputManager.h"

class EngineApp {
public:
    void Run(HINSTANCE hInstance, int nShowCmd);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};