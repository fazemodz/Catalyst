#include <windows.h>
#include "EngineApp.h"
#include "Launcher.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    
    // Parse optional playable build launch args.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != nullptr) {
        AppLaunchRequest req;
        for (int i = 1; i < argc; ++i) {
            const std::wstring arg = argv[i] ? argv[i] : L"";
            if (arg == L"--play") {
                req.play = true;
            } else if (arg == L"--project" && i + 1 < argc) {
                req.projectFilePath = argv[++i];
            } else if (arg == L"--map" && i + 1 < argc) {
                req.mapPath = argv[++i];
            }
        }
        if (req.play) {
            SetAppLaunchRequest(req);
        }
        LocalFree(argv);
    }

#ifdef CATALYST_PLAYER
    AppLaunchRequest playerReq;
    if (!ConsumeAppLaunchRequest(playerReq)) {
        if (BuildAdjacentPlayerLaunchRequest(playerReq)) {
            SetAppLaunchRequest(playerReq);
        }
    } else {
        SetAppLaunchRequest(playerReq);
    }
#endif

    EngineApp app;
    app.Run(hInstance, nShowCmd);
    
    CoUninitialize();
    return 0;
}
