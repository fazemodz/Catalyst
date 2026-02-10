#pragma once
#include "Asset.h"
#include <directxmath.h>

enum class ObjectType { Mesh, Light };

struct GameObject {
    std::string name;
    
    // Transform
    DirectX::XMFLOAT3 position = {0,0,0};
    DirectX::XMFLOAT3 rotation = {0,0,0};
    DirectX::XMFLOAT3 scale = {1,1,1};
    
    // Rendering Overrides
    DirectX::XMFLOAT4 color = {1,1,1,1};
    
    // POINTER TO MASTER ASSET
    Asset* asset = nullptr; 

    // Legacy/Light support
    ObjectType type = ObjectType::Mesh; 
    float lightIntensity = 1.0f; 
    
    // Getters that fallback to Asset data
    Mesh* GetMesh() { return asset ? asset->mesh : nullptr; }
    bool IsVirtual() { return asset ? asset->useVirtualGeometry : false; }
};