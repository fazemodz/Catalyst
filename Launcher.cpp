#include "Launcher.h"
#include <commdlg.h>
#include <shobjidl.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cwctype>
#include <regex>

namespace fs = std::filesystem;

namespace {
bool IsTextureExtension(const std::wstring& extension) {
    return extension == L".png" || extension == L".jpg" || extension == L".jpeg";
}

std::wstring EscapeJsonString(const std::wstring& value) {
    std::wstring escaped;
    escaped.reserve(value.size());

    for (wchar_t ch : value) {
        switch (ch) {
        case L'\\':
            escaped += L"\\\\";
            break;
        case L'"':
            escaped += L"\\\"";
            break;
        case L'\n':
            escaped += L"\\n";
            break;
        case L'\r':
            escaped += L"\\r";
            break;
        case L'\t':
            escaped += L"\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }

    return escaped;
}

std::wstring NormalizeProjectPathString(const std::wstring& path) {
    std::wstring normalized = path;
    std::replace(normalized.begin(), normalized.end(), L'\\', L'/');
    return normalized;
}

std::wstring BuildDefaultStartupScenePath(const std::wstring& projectFilePath) {
    const fs::path projectRoot = fs::path(projectFilePath).parent_path();
    return (projectRoot / L"Assets" / L"StartupScene.catalystmap").lexically_normal().wstring();
}

std::wstring BuildLegacyStartupScenePath(const std::wstring& projectFilePath) {
    const fs::path projectRoot = fs::path(projectFilePath).parent_path();
    return (projectRoot / L"Config" / L"StartupScene.catalystmap").lexically_normal().wstring();
}
}

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
    ofn.lpstrFilter = L"Supported Assets (*.obj;*.png;*.jpg;*.jpeg)\0*.obj;*.png;*.jpg;*.jpeg\0Wavefront OBJ (*.obj)\0*.obj\0Textures (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }
    return L"";
}

std::wstring BrowseForTextureFile(HWND ownerWindow) {
    OPENFILENAMEW ofn;
    wchar_t szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ownerWindow;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Textures (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }
    return L"";
}

std::wstring BrowseForMaterialFile(HWND ownerWindow) {
    OPENFILENAMEW ofn;
    wchar_t szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ownerWindow;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Catalyst Materials (*.catalystmat)\0*.catalystmat\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }
    return L"";
}

bool ImportAssetToProject(const std::wstring& sourcePath, const std::wstring& projectAssetsFolder, const std::wstring& newName, std::wstring* importedPath) {
    if (sourcePath.empty() || projectAssetsFolder.empty() || newName.empty()) return false;
    
    fs::path src(sourcePath);
    fs::path destFolder(projectAssetsFolder);
    std::wstring ext = src.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), towlower);

    const bool isActor = ext == L".obj";
    const bool isTexture = IsTextureExtension(ext);
    if (!isActor && !isTexture) {
        return false;
    }
    
    if (!fs::exists(destFolder)) {
        fs::create_directories(destFolder);
    }
    
    fs::path destFile = destFolder / newName;
    if (isActor) {
        destFile.replace_extension(L".catalystactor");
    } else {
        destFile.replace_extension(ext);
    }
    
    try {
        fs::copy_file(src, destFile, fs::copy_options::overwrite_existing);
        if (importedPath) {
            *importedPath = destFile.wstring();
        }
        return true;
    } catch (...) {
        return false;
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
    info.StartupScene = L"";

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

    std::wregex startupRegex(L"\"StartupScene\"\\s*:\\s*\"([^\"]*)\"");
    if (std::regex_search(content, match, startupRegex) && match.size() > 1) {
        info.StartupScene = match[1].str();
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

void RemoveRecentProject(const std::wstring& path) {
    auto recents = GetRecentProjects();
    recents.erase(std::remove(recents.begin(), recents.end(), path), recents.end());

    std::wofstream file(L"recent_projects.txt");
    int count = 0;
    for (const auto& recentPath : recents) {
        if (count++ >= 10) break;
        file << recentPath << L"\n";
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

std::wstring ResolveProjectStartupScenePath(const std::wstring& projectFilePath) {
    if (projectFilePath.empty()) {
        return L"";
    }

    const ProjectInfo info = ParseProjectFile(projectFilePath);
    if (!info.StartupScene.empty()) {
        fs::path startupPath(info.StartupScene);
        if (!startupPath.is_absolute()) {
            startupPath = fs::path(projectFilePath).parent_path() / startupPath;
        }
        return startupPath.lexically_normal().wstring();
    }

    return BuildDefaultStartupScenePath(projectFilePath);
}

std::wstring ResolveProjectStartupSceneSavePath(const std::wstring& projectFilePath) {
    if (projectFilePath.empty()) {
        return L"";
    }

    const ProjectInfo info = ParseProjectFile(projectFilePath);
    if (!info.StartupScene.empty()) {
        fs::path startupPath(info.StartupScene);
        if (!startupPath.is_absolute()) {
            startupPath = fs::path(projectFilePath).parent_path() / startupPath;
        }

        const std::wstring normalizedStartupPath = startupPath.lexically_normal().wstring();
        const std::wstring legacyStartupPath = BuildLegacyStartupScenePath(projectFilePath);
        if (_wcsicmp(normalizedStartupPath.c_str(), legacyStartupPath.c_str()) != 0) {
            return normalizedStartupPath;
        }
    }

    return BuildDefaultStartupScenePath(projectFilePath);
}

bool UpdateProjectStartupScene(const std::wstring& projectFilePath, const std::wstring& startupScenePath) {
    if (projectFilePath.empty() || startupScenePath.empty()) {
        return false;
    }

    const ProjectInfo info = ParseProjectFile(projectFilePath);
    const fs::path projectRoot = fs::path(projectFilePath).parent_path();
    fs::path scenePath(startupScenePath);
    std::error_code ec;
    if (!scenePath.is_absolute()) {
        scenePath = projectRoot / scenePath;
    }

    fs::path relativePath = fs::relative(scenePath, projectRoot, ec);
    const std::wstring storedPath = NormalizeProjectPathString((ec ? scenePath : relativePath).lexically_normal().wstring());

    std::wofstream outFile(projectFilePath, std::ios::trunc);
    if (!outFile.is_open()) {
        return false;
    }

    const std::wstring projectName = info.ProjectName.empty() ? fs::path(projectFilePath).stem().wstring() : info.ProjectName;
    const std::wstring engineVersion = (info.EngineVersion.empty() || info.EngineVersion == L"Unknown") ? L"1.0.0" : info.EngineVersion;

    outFile << L"{\n";
    outFile << L"  \"ProjectName\": \"" << EscapeJsonString(projectName) << L"\",\n";
    outFile << L"  \"EngineVersion\": \"" << EscapeJsonString(engineVersion) << L"\",\n";
    outFile << L"  \"StartupScene\": \"" << EscapeJsonString(storedPath) << L"\"\n";
    outFile << L"}\n";
    return true;
}

bool DeleteProject(const std::wstring& projectFilePath) {
    if (projectFilePath.empty()) {
        return false;
    }

    fs::path rawProjectFile(projectFilePath);
    std::wstring extension = rawProjectFile.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    if (extension != L".catalystproj") {
        return false;
    }

    std::error_code ec;
    fs::path normalizedProjectFile = fs::weakly_canonical(rawProjectFile, ec);
    if (ec) {
        normalizedProjectFile = rawProjectFile.lexically_normal();
    }

    fs::path projectRoot = normalizedProjectFile.parent_path();
    if (projectRoot.empty() || projectRoot.filename().empty()) {
        return false;
    }

    fs::path expectedProjectFile = projectRoot / normalizedProjectFile.filename();
    if (expectedProjectFile.lexically_normal() != normalizedProjectFile.lexically_normal()) {
        return false;
    }

    std::error_code removeError;
    const uintmax_t removedCount = fs::remove_all(projectRoot, removeError);
    if (removeError || removedCount == 0) {
        return false;
    }

    RemoveRecentProject(projectFilePath);
    return true;
}
