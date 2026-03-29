#pragma once
#include "Asset.h"
#include "../Physics/PhysicsTypes.h"
#include <directxmath.h>

class Material;

enum class ObjectType { Mesh, Light, Skybox, PostProcessVolume };

struct PostProcessSettings {
    float exposure = 1.0f;
    DirectX::XMFLOAT3 colorTint = {1.0f, 1.0f, 1.0f};
    
    float bloomThreshold = 1.0f; 
    float bloomIntensity = 0.5f; 

    float blendRadius = 1.0f;
};

struct GameObject {
    std::string name;

    // Transform
    DirectX::XMFLOAT3 position = {0,0,0};
    DirectX::XMFLOAT3 rotation = {0,0,0};
    DirectX::XMFLOAT3 scale = {1,1,1};

    // Rendering
    DirectX::XMFLOAT4 color = {1,1,1,1};
    DirectX::XMFLOAT4 skyHorizonColor = {1,1,1,1};

    // Master Asset
    Asset* asset = nullptr;
    Material* assignedMaterial = nullptr;

    // Material Overrides
    Texture* overrideAlbedo    = nullptr;
    Texture* overrideNormal    = nullptr;
    Texture* overrideMetallic  = nullptr;
    Texture* overrideRoughness = nullptr;
    Texture* overrideAO        = nullptr;

    // Object Data
    ObjectType type = ObjectType::Mesh;
    float lightIntensity = 1.0f;

    PostProcessSettings ppSettings;
    PhysicsComponent physics;
};
