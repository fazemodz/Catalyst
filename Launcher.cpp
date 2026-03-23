#include "Launcher.h"
#include <commdlg.h>
#include <shobjidl.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <regex>

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

std::wstring BrowseForAssetFile(HWND ownerWindow) {
    OPENFILENAMEW ofn;
    wchar_t szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ownerWindow;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Supported Assets\0*.obj;*.fbx;*.png;*.jpg;*.wav;*.mp3\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }
    return L"";
}

void ImportAssetToProject(const std::wstring& sourcePath, const std::wstring& projectAssetsFolder, const std::wstring& newName) {
    if (sourcePath.empty() || projectAssetsFolder.empty() || newName.empty()) return;
    
    fs::path src(sourcePath);
    fs::path destFolder(projectAssetsFolder);
    
    if (!fs::exists(destFolder)) {
        fs::create_directories(destFolder);
    }
    
    fs::path destFile = destFolder / newName;
    destFile.replace_extension(L".catalystactor");
    
    try {
        fs::copy_file(src, destFile, fs::copy_options::overwrite_existing);
    } catch (...) {
        // Silently fail if file is locked or access denied
    }
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

ProjectInfo ParseProjectFile(const std::wstring& path) {
    ProjectInfo info;
    info.Path = path;
    info.ProjectName = fs::path(path).stem().wstring(); 
    info.EngineVersion = L"Unknown";

    std::wifstream file(path);
    if (!file.is_open()) return info;

    std::wstring content((std::istreambuf_iterator<wchar_t>(file)), std::istreambuf_iterator<wchar_t>());
    
    std::wsmatch match;
    std::wregex nameRegex(L"\"ProjectName\"\\s*:\\s*\"([^\"]+)\"");
    if (std::regex_search(content, match, nameRegex) && match.size() > 1) {
        info.ProjectName = match[1].str();
    }

    std::wregex versionRegex(L"\"EngineVersion\"\\s*:\\s*\"([^\"]+)\"");
    if (std::regex_search(content, match, versionRegex) && match.size() > 1) {
        info.EngineVersion = match[1].str();
    }

    return info;
}

std::vector<ProjectInfo> GetRecentProjectsInfo() {
    auto paths = GetRecentProjects();
    std::vector<ProjectInfo> infos;
    infos.reserve(paths.size());
    for (const auto& path : paths) {
        if (fs::exists(path)) { 
            infos.push_back(ParseProjectFile(path));
        }
    }
    return infos;
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