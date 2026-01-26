#pragma once
#include "./Window Creation/Window.h"
#include "./Core Render/DXRenderer.h"

class EngineApp {
public:
    void Run();
private:
    Window m_window;
    DXRenderer m_renderer;
};