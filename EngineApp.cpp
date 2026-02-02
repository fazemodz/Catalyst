#include "EngineApp.h"

// Define the Width and Height here so we can pass them to both Window and Renderer
const int SCREEN_WIDTH = 1280;
const int SCREEN_HEIGHT = 720;

void EngineApp::Run() {
    if (!m_window.Initialize(1280, 720, L"Catalyst - Editor")) return;

    m_renderer.Initialize(m_window.GetHandle(), 1280, 720); 
    while (m_window.IsOpen()) {
        m_window.ProcessMessages();
        m_renderer.Render();
    }
    m_renderer.Shutdown();
}