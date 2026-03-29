#include "EditorUIInternal.h"
#include "../Core Render/DXRenderer.h"
#include "../EngineApp.h"
#include "../Launcher.h" 
#include "Material.h"
#include "Mesh.h"
#include "ModelLoader.h"
#include "GameObject.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

MeshData ParseCatalystActor(const std::wstring& filepath) {
    try {
        return ModelLoader::LoadMeshData(filepath);
    } catch (...) {
        return {};
    }
}

namespace EditorUIInternal {
std::wstring StringToWide(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

std::wstring ToLowerCopy(const std::wstring& value) {
    std::wstring lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), towlower);
    return lowered;
}

bool HasExtension(const std::string& filename, const wchar_t* extension) {
    return ToLowerCopy(fs::path(StringToWide(filename)).extension().wstring()) == extension;
}

bool IsMaterialAssetName(const std::string& filename) {
    return HasExtension(filename, L".catalystmat");
}

bool IsActorAssetName(const std::string& filename) {
    return HasExtension(filename, L".catalystactor");
}

bool IsTextureAssetName(const std::string& filename) {
    const std::wstring extension = ToLowerCopy(fs::path(StringToWide(filename)).extension().wstring());
    return extension == L".png" || extension == L".jpg" || extension == L".jpeg";
}

bool IsMapAssetName(const std::string& filename) {
    return HasExtension(filename, L".catalystmap");
}

bool IsBlueprintAssetName(const std::string& filename) {
    return HasExtension(filename, L".catalystblueprint");
}

bool IsUIBlueprintAssetName(const std::string& filename) {
    return HasExtension(filename, L".catalystuiblueprint");
}

bool WriteEmptySceneMapFile(const fs::path& outputPath) {
    if (outputPath.empty()) {
        return false;
    }

    try {
        std::error_code ec;
        const fs::path parentFolder = outputPath.parent_path();
        if (!parentFolder.empty()) {
            fs::create_directories(parentFolder, ec);
        }

        std::ofstream outFile(outputPath, std::ios::binary | std::ios::trunc);
        if (!outFile.is_open()) {
            return false;
        }

        outFile << "{\n";
        outFile << "  \"type\": \"CatalystScene\",\n";
        outFile << "  \"version\": 1,\n";
        outFile << "  \"objects\": []\n";
        outFile << "}\n";
        outFile.close();
        return fs::exists(outputPath);
    } catch (...) {
        return false;
    }
}

bool WriteEmptyBlueprintFile(const fs::path& outputPath) {
    if (outputPath.empty()) {
        return false;
    }

    try {
        std::error_code ec;
        const fs::path parentFolder = outputPath.parent_path();
        if (!parentFolder.empty()) {
            fs::create_directories(parentFolder, ec);
        }

        std::ofstream outFile(outputPath, std::ios::binary | std::ios::trunc);
        if (!outFile.is_open()) {
            return false;
        }

        outFile << "{\n";
        outFile << "  \"type\": \"CatalystBlueprint\",\n";
        outFile << "  \"version\": 1,\n";
        outFile << "  \"parent\": \"Actor\",\n";
        outFile << "  \"seedDefaultGraph\": true,\n";
        outFile << "  \"nodes\": [],\n";
        outFile << "  \"links\": []\n";
        outFile << "}\n";
        outFile.close();
        return fs::exists(outputPath);
    } catch (...) {
        return false;
    }
}

bool WriteEmptyUIBlueprintFile(const fs::path& outputPath) {
    if (outputPath.empty()) {
        return false;
    }

    try {
        std::error_code ec;
        const fs::path parentFolder = outputPath.parent_path();
        if (!parentFolder.empty()) {
            fs::create_directories(parentFolder, ec);
        }

        std::ofstream outFile(outputPath, std::ios::binary | std::ios::trunc);
        if (!outFile.is_open()) {
            return false;
        }

        outFile << "{\n";
        outFile << "  \"type\": \"CatalystUIBlueprint\",\n";
        outFile << "  \"version\": 1,\n";
        outFile << "  \"parent\": \"UserWidget\",\n";
        outFile << "  \"designer\": {\n";
        outFile << "    \"root\": {\n";
        outFile << "      \"type\": \"CanvasPanel\",\n";
        outFile << "      \"name\": \"RootCanvas\",\n";
        outFile << "      \"children\": []\n";
        outFile << "    }\n";
        outFile << "  },\n";
        outFile << "  \"graph\": {\n";
        outFile << "    \"nodes\": [],\n";
        outFile << "    \"links\": []\n";
        outFile << "  }\n";
        outFile << "}\n";
        outFile.close();
        return fs::exists(outputPath);
    } catch (...) {
        return false;
    }
}

bool CreateEmptySceneMap(const std::wstring& targetDirectory, std::wstring* createdPath) {
    if (targetDirectory.empty()) {
        return false;
    }

    try {
        const fs::path targetFolder(targetDirectory);
        if (!fs::exists(targetFolder) || !fs::is_directory(targetFolder)) {
            return false;
        }

        std::wstring finalName = L"NewMap.catalystmap";
        int counter = 1;
        while (fs::exists(targetFolder / finalName)) {
            finalName = L"NewMap_" + std::to_wstring(counter++) + L".catalystmap";
        }

        const fs::path outputPath = targetFolder / finalName;
        if (!WriteEmptySceneMapFile(outputPath)) {
            return false;
        }

        if (createdPath) {
            *createdPath = outputPath.wstring();
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool CreateEmptyBlueprintAsset(const std::wstring& targetDirectory, std::wstring* createdPath) {
    if (targetDirectory.empty()) {
        return false;
    }

    try {
        const fs::path targetFolder(targetDirectory);
        if (!fs::exists(targetFolder) || !fs::is_directory(targetFolder)) {
            return false;
        }

        std::wstring finalName = L"NewBlueprint.catalystblueprint";
        int counter = 1;
        while (fs::exists(targetFolder / finalName)) {
            finalName = L"NewBlueprint_" + std::to_wstring(counter++) + L".catalystblueprint";
        }

        const fs::path outputPath = targetFolder / finalName;
        if (!WriteEmptyBlueprintFile(outputPath)) {
            return false;
        }

        if (createdPath) {
            *createdPath = outputPath.wstring();
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool CreateEmptyUIBlueprintAsset(const std::wstring& targetDirectory, std::wstring* createdPath) {
    if (targetDirectory.empty()) {
        return false;
    }

    try {
        const fs::path targetFolder(targetDirectory);
        if (!fs::exists(targetFolder) || !fs::is_directory(targetFolder)) {
            return false;
        }

        std::wstring finalName = L"NewUIBlueprint.catalystuiblueprint";
        int counter = 1;
        while (fs::exists(targetFolder / finalName)) {
            finalName = L"NewUIBlueprint_" + std::to_wstring(counter++) + L".catalystuiblueprint";
        }

        const fs::path outputPath = targetFolder / finalName;
        if (!WriteEmptyUIBlueprintFile(outputPath)) {
            return false;
        }

        if (createdPath) {
            *createdPath = outputPath.wstring();
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::wstring NormalizeForCompare(const fs::path& path) {
    std::error_code ec;
    fs::path absolutePath = fs::absolute(path, ec);
    if (ec) {
        absolutePath = path;
    }
    const std::wstring normalized = absolutePath.lexically_normal().wstring();
    std::wstring lowered = normalized;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), towlower);
    return lowered;
}

bool IsPathWithinRoot(const fs::path& path, const fs::path& root) {
    const std::wstring normalizedPath = NormalizeForCompare(path);
    std::wstring normalizedRoot = NormalizeForCompare(root);
    if (normalizedRoot.empty()) {
        return false;
    }

    if (!normalizedRoot.empty() && normalizedRoot.back() != L'\\' && normalizedRoot.back() != L'/') {
        normalizedRoot += L'\\';
    }

    return normalizedPath == normalizedRoot.substr(0, normalizedRoot.size() - 1) ||
           normalizedPath.rfind(normalizedRoot, 0) == 0;
}

bool DeleteBrowserEntry(const std::wstring& targetPath, bool isFolder) {
    if (targetPath.empty()) {
        return false;
    }

    try {
        const fs::path target = fs::path(targetPath).lexically_normal();
        if (!fs::exists(target)) {
            return false;
        }

        return isFolder ? (fs::remove_all(target) > 0) : fs::remove(target);
    } catch (...) {
        return false;
    }
}

bool EnsureEmptySceneMapFile(const std::wstring& targetPath) {
    if (targetPath.empty()) {
        return false;
    }

    try {
        const fs::path outputPath = fs::path(targetPath).lexically_normal();
        if (fs::exists(outputPath)) {
            return true;
        }

        return WriteEmptySceneMapFile(outputPath);
    } catch (...) {
        return false;
    }
}

std::wstring ChooseFallbackScenePath(const std::wstring& projectFilePath, const std::wstring& deletedPath) {
    if (projectFilePath.empty()) {
        return L"";
    }

    const fs::path assetsRoot = fs::path(projectFilePath).parent_path() / L"Assets";
    fs::path candidate = assetsRoot / L"StartupScene.catalystmap";
    if (!IsPathWithinRoot(candidate, fs::path(deletedPath))) {
        return candidate.lexically_normal().wstring();
    }

    int counter = 1;
    do {
        candidate = assetsRoot / (L"RecoveredStartupScene_" + std::to_wstring(counter++) + L".catalystmap");
    } while (IsPathWithinRoot(candidate, fs::path(deletedPath)));

    return candidate.lexically_normal().wstring();
}

std::string GetBrowserItemDisplayName(const std::string& filename, bool isFolder) {
    if (isFolder) {
        return filename;
    }

    return fs::path(StringToWide(filename)).stem().string();
}

std::string TrimCopy(const std::string& value) {
    const size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }

    const size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

bool ContainsInvalidWindowsNameChars(const std::string& value) {
    static const std::string invalidChars = "<>:\"/\\|?*";
    return value.find_first_of(invalidChars) != std::string::npos;
}

bool RenameBrowserEntry(const std::wstring& sourcePath, const std::string& requestedDisplayName, bool isFolder, std::wstring* renamedPath) {
    const std::string trimmedName = TrimCopy(requestedDisplayName);
    if (trimmedName.empty() || trimmedName == "." || trimmedName == ".." || ContainsInvalidWindowsNameChars(trimmedName)) {
        return false;
    }

    try {
        const fs::path source = fs::path(sourcePath).lexically_normal();
        if (!fs::exists(source)) {
            return false;
        }

        std::wstring requestedName = StringToWide(trimmedName);
        if (!isFolder) {
            const std::wstring extension = source.extension().wstring();
            const std::wstring loweredName = ToLowerCopy(requestedName);
            const std::wstring loweredExtension = ToLowerCopy(extension);
            if (!loweredExtension.empty() &&
                loweredName.size() > loweredExtension.size() &&
                loweredName.compare(loweredName.size() - loweredExtension.size(), loweredExtension.size(), loweredExtension) == 0) {
                requestedName.erase(requestedName.size() - extension.size());
            }
        }

        fs::path requestedNamePath(requestedName);
        if (requestedName.empty() || requestedNamePath.has_parent_path()) {
            return false;
        }

        const std::wstring finalName = isFolder ? requestedName : requestedName + source.extension().wstring();
        if (finalName.empty()) {
            return false;
        }

        const fs::path target = (source.parent_path() / finalName).lexically_normal();
        if (NormalizeForCompare(target) == NormalizeForCompare(source)) {
            if (renamedPath) {
                *renamedPath = source.wstring();
            }
            return true;
        }

        if (fs::exists(target)) {
            return false;
        }

        fs::rename(source, target);
        if (renamedPath) {
            *renamedPath = target.wstring();
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::wstring RemapRenamedPath(const std::wstring& candidatePath, const std::wstring& sourcePath, const std::wstring& renamedPath) {
    if (candidatePath.empty() || sourcePath.empty() || renamedPath.empty()) {
        return candidatePath;
    }

    if (!IsPathWithinRoot(fs::path(candidatePath), fs::path(sourcePath))) {
        return candidatePath;
    }

    std::error_code ec;
    const fs::path relativePath = fs::relative(fs::path(candidatePath), fs::path(sourcePath), ec);
    if (ec || relativePath.empty() || relativePath == fs::path(L".")) {
        return fs::path(renamedPath).lexically_normal().wstring();
    }

    return (fs::path(renamedPath) / relativePath).lexically_normal().wstring();
}

bool MoveBrowserEntry(const std::wstring& sourcePath, const std::wstring& targetFolderPath, std::wstring* movedPath) {
    if (sourcePath.empty() || targetFolderPath.empty()) {
        return false;
    }

    try {
        const fs::path source = fs::path(sourcePath).lexically_normal();
        const fs::path targetFolder = fs::path(targetFolderPath).lexically_normal();
        if (!fs::exists(source) || !fs::exists(targetFolder) || !fs::is_directory(targetFolder)) {
            return false;
        }

        if (NormalizeForCompare(source.parent_path()) == NormalizeForCompare(targetFolder)) {
            return false;
        }

        if (fs::is_directory(source) && IsPathWithinRoot(targetFolder, source)) {
            return false;
        }

        const fs::path target = (targetFolder / source.filename()).lexically_normal();
        if (fs::exists(target)) {
            return false;
        }

        fs::rename(source, target);
        if (movedPath) {
            *movedPath = target.wstring();
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::string BuildMaterialLink(const std::wstring& materialPath, const std::wstring& texturePath) {
    try {
        fs::path relativePath = fs::relative(fs::path(texturePath), fs::path(materialPath).parent_path());
        return relativePath.generic_string();
    } catch (...) {
        return fs::path(texturePath).filename().generic_string();
    }
}

std::string DescribeLinkedAsset(const std::string& linkedPath) {
    if (linkedPath.empty()) {
        return "Empty";
    }

    return fs::path(linkedPath).filename().string();
}

std::string FormatFloat(float value, int precision) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), ("%." + std::to_string(precision) + "f").c_str(), value);
    return buffer;
}

std::string FitName(const std::string& value, size_t maxLength) {
    if (value.size() <= maxLength) {
        return value;
    }
    return value.substr(0, maxLength - 2) + "..";
}

float MeasureTextWidth(FontManager& fontManager, const std::string& text) {
    float width = 0.0f;
    for (char ch : text) {
        if (ch == '\n') {
            break;
        }
        width += fontManager.GetGlyph(ch).Advance;
    }
    return width;
}

std::string FitTextToWidth(FontManager& fontManager, const std::string& text, float maxWidth) {
    if (maxWidth <= 0.0f) {
        return "";
    }

    if (MeasureTextWidth(fontManager, text) <= maxWidth) {
        return text;
    }

    const std::string ellipsis = "...";
    const float ellipsisWidth = MeasureTextWidth(fontManager, ellipsis);
    if (ellipsisWidth >= maxWidth) {
        return "";
    }

    std::string fitted;
    fitted.reserve(text.size());
    float width = 0.0f;
    for (char ch : text) {
        const float charWidth = fontManager.GetGlyph(ch).Advance;
        if ((width + charWidth + ellipsisWidth) > maxWidth) {
            break;
        }
        fitted.push_back(ch);
        width += charWidth;
    }

    fitted += ellipsis;
    return fitted;
}

std::string ToDisplayString(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }

    const int requiredSize = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (requiredSize <= 1) {
        return "";
    }

    std::string converted(static_cast<size_t>(requiredSize), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, converted.data(), requiredSize, nullptr, nullptr);
    converted.pop_back();
    return converted;
}

std::string BuildBrowserPathLabel(const std::wstring& currentPath, const std::wstring& projectRoot) {
    if (currentPath.empty()) {
        return "";
    }

    const fs::path current = fs::path(currentPath).lexically_normal();
    const fs::path root = fs::path(projectRoot).lexically_normal();

    if (!projectRoot.empty()) {
        std::error_code ec;
        fs::path relativePath = fs::relative(current, root, ec);
        if (!ec && !relativePath.empty()) {
            const std::wstring rootName = root.filename().wstring();
            if (!rootName.empty()) {
                return ToDisplayString((fs::path(rootName) / relativePath).wstring());
            }
            return ToDisplayString(relativePath.wstring());
        }
    }

    return ToDisplayString(current.wstring());
}

std::wstring FindProjectRootFromAssetPath(const std::wstring& assetPath) {
    fs::path current = fs::path(assetPath).parent_path();
    while (!current.empty()) {
        if (ToLowerCopy(current.filename().wstring()) == L"assets") {
            return current.parent_path().wstring();
        }

        fs::path parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }

    return fs::path(assetPath).parent_path().wstring();
}

const char* GetMaterialPreviewMeshKey(int previewMeshIndex) {
    switch (previewMeshIndex) {
    case 1: return "Cube";
    case 2: return "Cylinder";
    default: return "Sphere";
    }
}

const char* GetMaterialPreviewMeshLabel(int previewMeshIndex) {
    switch (previewMeshIndex) {
    case 1: return "Cube";
    case 2: return "Cylinder";
    default: return "Sphere";
    }
}
}


