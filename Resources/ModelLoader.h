#pragma once
#include <string>
#include <d3d12.h>
#include "Mesh.h"

class ModelLoader {
public:
    static Mesh* Load(const std::string& filepath, ID3D12Device* device, ID3D12CommandQueue* cmdQueue);
};