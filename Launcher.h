#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct ProjectInfo {
    std::wstring Path;
    std::wstring ProjectName;
    std::wstring EngineVersion;
    std::wstring StartupScene;
};

std::wstring BrowseForProjectFile(HWND ownerWindow);
std::wstring BrowseForProjectFolder(HWND ownerWindow);

std::wstring BrowseForAssetFile(HWND ownerWindow);
std::wstring BrowseForActorAssetFile(HWND ownerWindow);
std::wstring BrowseForTextureFile(HWND ownerWindow);
std::wstring BrowseForMaterialFile(HWND ownerWindow);
std::wstring BrowseForBlueprintFile(HWND ownerWindow);
std::wstring BrowseForUIBlueprintFile(HWND ownerWindow);

bool ImportAssetToProject(const std::wstring& sourcePath, const std::wstring& projectAssetsFolder, const std::wstring& newName, std::wstring* importedPath = nullptr);

void CreateNewProject(const std::wstring& targetFolder, const std::string& projectName);

void AddRecentProject(const std::wstring& path);
void RemoveRecentProject(const std::wstring& path);
bool DeleteProject(const std::wstring& projectFilePath);
std::vector<std::wstring> GetRecentProjects();
std::vector<ProjectInfo> GetRecentProjectsInfo();
ProjectInfo ParseProjectFile(const std::wstring& path);
std::wstring ResolveProjectStartupScenePath(const std::wstring& projectFilePath);
std::wstring ResolveProjectStartupSceneSavePath(const std::wstring& projectFilePath);
bool UpdateProjectStartupScene(const std::wstring& projectFilePath, const std::wstring& startupScenePath);
