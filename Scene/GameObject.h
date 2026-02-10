#pragma once
#include <directxmath.h>
#include <string>
#include "../Resources/Mesh.h"
#include "../Resources/Texture.h"

enum class ObjectType { Mesh, Light };

struct GameObject {
    std::string name;
    
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 rotation;
    DirectX::XMFLOAT3 scale;
    DirectX::XMFLOAT4 color;
    
    Mesh* mesh = nullptr;
    Texture* texture = nullptr;
    Texture* normalMap = nullptr;

    ObjectType type = ObjectType::Mesh; 
    float lightIntensity = 1.0f; 

    bool useVirtualGeometry = false;  // Enable the system
    bool debugVisualizer = false;     // Show the clusters (colored triangles)
};