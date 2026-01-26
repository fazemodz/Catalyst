#include "EngineApp.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    EngineApp app(1280, 720, L"My DX12 Engine");

    if (app.Initialize(hInstance)) {
        app.Run();
    }

    return 0;
}