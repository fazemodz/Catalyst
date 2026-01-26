#include "EngineApp.h"

void EngineApp::Run() {
    m_window.Initialize(1280, 720, L"Catalyst Engine");
    m_renderer.Initialize(m_window.GetHandle());
    while (m_window.IsOpen()) {
        m_window.ProcessMessages();
        m_renderer.Render();
    }
}