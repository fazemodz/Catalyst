#pragma once
#include <windows.h>
#include <string>
#include <vector>

std::wstring BrowseForProjectFile(HWND ownerWindow);
std::wstring BrowseForProjectFolder(HWND ownerWindow);

void CreateNewProject(const std::wstring& targetFolder, const std::string& projectName);

void AddRecentProject(const std::wstring& path);
std::vector<std::wstring> GetRecentProjects();