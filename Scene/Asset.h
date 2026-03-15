#pragma once
#include <string>
#include <vector>

class Mesh;
class Texture;

enum class AssetType { Mesh, TextureHDR };

struct Asset {
    int id = -1;
    std::string name;
    std::string sourcePath;
    
    AssetType type = AssetType::Mesh;

    Mesh* mesh = nullptr;
    
    // PBR Textures
    Texture* albedoMap = nullptr;
    Texture* normalMap = nullptr;
    Texture* metallicMap = nullptr;
    Texture* roughnessMap = nullptr;
    Texture* aoMap = nullptr;
    
    // HDR Skybox Texture
    Texture* hdrTexture = nullptr;

    float metallicFactor = 0.0f;
    float roughnessFactor = 0.5f;
    bool enableReflections = false;

    bool useVirtualGeometry = false;
    bool debugVisualizer = false;
};