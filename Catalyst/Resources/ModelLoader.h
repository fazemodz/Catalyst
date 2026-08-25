#pragma once
#include <string>
#include <d3d12.h>
#include "PrimitiveGenerator.h"

class ModelLoader {
public:
    static MeshData LoadMeshData(const std::string& filepath);
    static MeshData LoadMeshData(const std::wstring& filepath);
    static Mesh* Load(const std::string& filepath, ID3D12Device* device, ID3D12CommandQueue* cmdQueue);
};
