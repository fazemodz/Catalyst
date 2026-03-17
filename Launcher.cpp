#include "Launcher.h"
#include <commdlg.h>
#include <shobjidl.h>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;

std::wstring BrowseForProjectFile(HWND ownerWindow) {
    OPENFILENAMEW ofn;
    wchar_t szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ownerWindow;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Catalyst Project (*.CatalystProj)\0*.CatalystProj\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }
    return L"";
}

std::wstring BrowseForProjectFolder(HWND ownerWindow) {
    std::wstring folderPath = L"";
    IFileOpenDialog* pFileOpen;
    
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr)) {
        DWORD dwOptions;
        if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions))) pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        if (SUCCEEDED(pFileOpen->Show(ownerWindow))) {
            IShellItem* pItem;
            if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                PWSTR pszFilePath;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                    folderPath = pszFilePath;
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
    return folderPath;
}

std::vector<std::wstring> GetRecentProjects() {
    std::vector<std::wstring> projects;
    std::wifstream file(L"recent_projects.txt");
    std::wstring line;
    while(std::getline(file, line)) {
        if(!line.empty() && std::find(projects.begin(), projects.end(), line) == projects.end()) {
            projects.push_back(line);
        }
    }
    return projects;
}

void AddRecentProject(const std::wstring& path) {
    auto recents = GetRecentProjects();
    recents.erase(std::remove(recents.begin(), recents.end(), path), recents.end());
    recents.insert(recents.begin(), path);
    
    std::wofstream file(L"recent_projects.txt");
    int count = 0;
    for (const auto& p : recents) {
        if (count++ >= 10) break; 
        file << p << L"\n";
    }
}

void CreateNewProject(const std::wstring& targetFolder, const std::string& projectName) {
    std::wstring wProjectName(projectName.begin(), projectName.end());
    fs::path projectRoot = fs::path(targetFolder) / wProjectName;

    fs::create_directories(projectRoot / L"Assets");
    fs::create_directories(projectRoot / L"Scripts");
    fs::create_directories(projectRoot / L"Config");

    fs::path projectFile = projectRoot / (wProjectName + L".CatalystProj");
    
    std::wofstream outFile(projectFile);
    if (outFile.is_open()) {
        outFile << L"{\n";
        outFile << L"  \"ProjectName\": \"" << wProjectName << L"\",\n";
        outFile << L"  \"EngineVersion\": \"1.0.0\",\n";
        outFile << L"  \"StartupScene\": \"\"\n";
        outFile << L"}\n";
        outFile.close();
    }
    AddRecentProject(projectFile.wstring());
}