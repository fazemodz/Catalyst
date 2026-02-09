#pragma once
#include "Mesh.h"
#include <string>
#include <d3d12.h>

class ModelLoader {
public:
    // Loads an OBJ file and returns a ready-to-use Mesh*
    static Mesh* Load(const std::string& filepath, ID3D12Device* device);
};