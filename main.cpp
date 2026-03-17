#include <windows.h>
#include "EngineApp.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    
    EngineApp app;
    app.Run(hInstance, nShowCmd);
    
    CoUninitialize();
    return 0;
}