#pragma once

#include "EditorUI.h"
#include "Render/FontManager.h"
#include "Input/InputManager.h"

#include <filesystem>
#include <string>

extern InputManager* g_InputManager;
namespace fs = std::filesystem;

MeshData ParseCatalystActor(const std::wstring& filepath);

namespace EditorUIInternal {
std::wstring StringToWide(const std::string& value);
std::wstring ToLowerCopy(const std::wstring& value);
bool HasExtension(const std::string& filename, const wchar_t* extension);
bool IsMaterialAssetName(const std::string& filename);
bool IsActorAssetName(const std::string& filename);
bool IsTextureAssetName(const std::string& filename);
bool IsMapAssetName(const std::string& filename);
bool CreateEmptySceneMap(const std::wstring& targetDirectory, std::wstring* createdPath = nullptr);
std::wstring NormalizeForCompare(const fs::path& path);
bool IsPathWithinRoot(const fs::path& path, const fs::path& root);
bool DeleteBrowserEntry(const std::wstring& targetPath, bool isFolder);
bool EnsureEmptySceneMapFile(const std::wstring& targetPath);
std::wstring ChooseFallbackScenePath(const std::wstring& projectFilePath, const std::wstring& deletedPath);
std::string GetBrowserItemDisplayName(const std::string& filename, bool isFolder);
std::string TrimCopy(const std::string& value);
bool ContainsInvalidWindowsNameChars(const std::string& value);
bool RenameBrowserEntry(const std::wstring& sourcePath, const std::string& requestedDisplayName, bool isFolder, std::wstring* renamedPath);
std::wstring RemapRenamedPath(const std::wstring& candidatePath, const std::wstring& sourcePath, const std::wstring& renamedPath);
bool MoveBrowserEntry(const std::wstring& sourcePath, const std::wstring& targetFolderPath, std::wstring* movedPath);
std::string BuildMaterialLink(const std::wstring& materialPath, const std::wstring& texturePath);
std::string DescribeLinkedAsset(const std::string& linkedPath);
std::string FormatFloat(float value, int precision = 2);
std::string FitName(const std::string& value, size_t maxLength);
std::string FitTextToWidth(FontManager& fontManager, const std::string& text, float maxWidth);
std::string ToDisplayString(const std::wstring& value);
std::string BuildBrowserPathLabel(const std::wstring& currentPath, const std::wstring& projectRoot);
std::wstring FindProjectRootFromAssetPath(const std::wstring& assetPath);
const char* GetMaterialPreviewMeshKey(int previewMeshIndex);
const char* GetMaterialPreviewMeshLabel(int previewMeshIndex);
}
