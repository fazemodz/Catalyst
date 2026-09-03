#pragma once
#include "UI/EditorSettings.h"
#include "Resources/MeshBuild.h"
#include "Resources/MeshImportOptions.h"
#include "Resources/FbxParser.h"
#include <windows.h>
#include <string>
#include <vector>

struct ProjectInfo {
    std::wstring Path;
    std::wstring ProjectName;
    std::wstring EngineVersion;
    std::wstring StartupScene;
    ProjectSettings Settings;
};

std::wstring BrowseForProjectFile(HWND ownerWindow);
std::wstring BrowseForProjectFolder(HWND ownerWindow);

std::wstring BrowseForAssetFile(HWND ownerWindow);
std::wstring BrowseForActorAssetFile(HWND ownerWindow);
std::wstring BrowseForTextureFile(HWND ownerWindow);
std::wstring BrowseForMaterialFile(HWND ownerWindow);
std::wstring BrowseForBlueprintFile(HWND ownerWindow);
std::wstring BrowseForUIBlueprintFile(HWND ownerWindow);
std::wstring BrowseForMapFile(HWND ownerWindow);

// Textures are copied as-is. Models are converted into the engine's binary
// geometry format with the import options applied, so opening them later is a
// read rather than a re-parse.
bool ImportAssetToProject(const std::wstring& sourcePath,
                          const std::wstring& projectAssetsFolder,
                          const std::wstring& newName,
                          const MeshImportOptions& meshOptions,
                          std::wstring* importedPath = nullptr,
                          CatalystImport::MeshBuildStats* outStats = nullptr,
                          std::string* outError = nullptr);

bool ImportAssetToProject(const std::wstring& sourcePath, const std::wstring& projectAssetsFolder, const std::wstring& newName, std::wstring* importedPath = nullptr);

// File extensions the model importer accepts.
bool IsImportableModelExtension(const std::wstring& extension);

struct ImportedTextureResult {
    uint32_t copied = 0;
    uint32_t missing = 0;
    std::wstring materialPath;   // the .catalystmat written, empty if none was
    std::vector<std::string> missingNames;
};

// Copies the textures a model refers to into the project beside the asset and
// writes a material that links them. Paths inside an FBX routinely point at the
// authoring machine, so each one is searched for locally rather than trusted.
ImportedTextureResult ImportModelTextures(const std::wstring& sourceModelPath,
                                          const std::wstring& importedAssetPath,
                                          const std::vector<CatalystImport::FbxTextureReference>& textures);

void CreateNewProject(const std::wstring& targetFolder, const std::string& projectName);
bool EnsureProjectCodeWorkspaceReady(const std::wstring& projectFilePath);
std::wstring ResolveProjectCodeSolutionPath(const std::wstring& projectFilePath);
std::wstring ResolveProjectGameDllPath(const std::wstring& projectFilePath, bool buildRelease);
bool OpenProjectCodeSolution(const std::wstring& projectFilePath);
bool OpenProjectCodeFile(const std::wstring& projectFilePath, const std::wstring& codeFilePath);

void AddRecentProject(const std::wstring& path);
void RemoveRecentProject(const std::wstring& path);
bool DeleteProject(const std::wstring& projectFilePath);
std::vector<std::wstring> GetRecentProjects();
std::vector<ProjectInfo> GetRecentProjectsInfo();
ProjectInfo ParseProjectFile(const std::wstring& path);
std::wstring ResolveProjectStartupScenePath(const std::wstring& projectFilePath);
std::wstring ResolveProjectStartupSceneSavePath(const std::wstring& projectFilePath);
bool UpdateProjectStartupScene(const std::wstring& projectFilePath, const std::wstring& startupScenePath);
// Rewrites the whole .CatalystProj from `info`, so every caller must start from
// a ParseProjectFile result or it will drop fields it does not know about.
bool WriteProjectFile(const std::wstring& projectFilePath, const ProjectInfo& info);
bool UpdateProjectSettings(const std::wstring& projectFilePath, const ProjectSettings& settings);

struct AppLaunchRequest {
    bool play = false;
    std::wstring projectFilePath;
    std::wstring mapPath;
};

bool BuildAdjacentPlayerLaunchRequest(AppLaunchRequest& outRequest);

void SetAppLaunchRequest(const AppLaunchRequest& request);
bool ConsumeAppLaunchRequest(AppLaunchRequest& outRequest);
