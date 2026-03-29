#pragma once
#include <filesystem>
#include <string>

class Texture;

class Material {
public:
    std::string name;
    std::wstring sourcePath;
    std::filesystem::file_time_type lastWriteTime{};

    std::string albedoPath;
    std::string normalPath;
    std::string roughnessPath;

    Texture* albedoTexture = nullptr;
    Texture* normalTexture = nullptr;
    Texture* roughnessTexture = nullptr;

    bool LoadFromFile(const std::wstring& filepath);
    bool SaveToFile(const std::wstring& filepath) const;

    std::wstring ResolveLinkedTexturePath(const std::string& linkedPath) const;
};
