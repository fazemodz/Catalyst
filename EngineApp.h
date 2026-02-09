#pragma once
#include <windows.h>
#include "Core Render/DXRenderer.h"

class EngineApp {
public:
    void Run(HINSTANCE hInstance, int nShowCmd);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};