#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct ProjectInfo {
    std::wstring Path;
    std::wstring ProjectName;
    std::wstring EngineVersion;
};

std::wstring BrowseForProjectFile(HWND ownerWindow);
std::wstring BrowseForProjectFolder(HWND ownerWindow);

std::wstring BrowseForAssetFile(HWND ownerWindow);

void ImportAssetToProject(const std::wstring& sourcePath, const std::wstring& projectAssetsFolder, const std::wstring& newName);

void CreateNewProject(const std::wstring& targetFolder, const std::string& projectName);

void AddRecentProject(const std::wstring& path);
std::vector<std::wstring> GetRecentProjects();
std::vector<ProjectInfo> GetRecentProjectsInfo();
ProjectInfo ParseProjectFile(const std::wstring& path);